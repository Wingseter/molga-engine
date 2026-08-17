#include "Core/AssetDependencyValidator.h"

#include "Core/AssetDatabase.h"
#include "Core/TextureImportSettings.h"

#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace molga {
namespace {

struct ExpectedReference {
    std::string guid;
    std::string importer;
    bool requiresNormalMap = false;
};

std::string ExpectedForDependency(const std::string& ownerImporter) {
    if (ownerImporter == "AnimatorControllerImporter") return "AnimationClipImporter";
    if (ownerImporter == "AnimationClipImporter" || ownerImporter == "TileSetImporter") {
        return "TextureImporter";
    }
    return {};
}

void CollectDocumentReferences(const nlohmann::json& document,
                               std::vector<ExpectedReference>& out) {
    std::function<void(const nlohmann::json&, const std::string&)> visit;
    visit = [&](const nlohmann::json& value, const std::string& componentType) {
        if (value.is_array()) {
            for (const auto& item : value) visit(item, componentType);
            return;
        }
        if (!value.is_object()) return;

        std::string localType = componentType;
        const auto type = value.find("type");
        if (type != value.end() && type->is_string()) {
            localType = type->get<std::string>();
        }

        // Prefab instance overrides encode component properties as
        // {component, key, value}, so the referenced GUID is not stored under a
        // literal normalMapGuid JSON key. Treat it exactly like authored
        // SpriteRenderer data for package dependency validation.
        const auto overrideComponent = value.find("component");
        const auto overrideKey = value.find("key");
        const auto overrideValue = value.find("value");
        if (overrideComponent != value.end() &&
            overrideComponent->is_string() &&
            overrideComponent->get<std::string>() == "SpriteRenderer" &&
            overrideKey != value.end() && overrideKey->is_string() &&
            overrideKey->get<std::string>() == "normalMapGuid" &&
            overrideValue != value.end() && overrideValue->is_string()) {
            const std::string guid = overrideValue->get<std::string>();
            if (!guid.empty()) {
                out.push_back({guid, "TextureImporter", true});
            }
        }

        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!it.value().is_string()) {
                visit(it.value(), localType);
                continue;
            }
            const std::string guid = it.value().get<std::string>();
            std::string expected;
            bool requiresNormalMap = false;
            if (it.key() == "textureGuid" || it.key() == "fontTextureGuid") {
                expected = "TextureImporter";
            } else if (it.key() == "normalMapGuid") {
                expected = "TextureImporter";
                requiresNormalMap = true;
            } else if (it.key() == "fontGuid") {
                expected = "FontImporter";
            } else if (it.key() == "controllerGuid") {
                expected = "AnimatorControllerImporter";
            } else if (it.key() == "tileSetGuid" || it.key() == "tilesetGuid") {
                expected = "TileSetImporter";
            } else if (it.key() == "prefabGuid" ||
                       (it.key() == "guid" && (localType == "PrefabInstance" ||
                                                value.contains("modifications")))) {
                expected = "PrefabImporter";
            } else if (it.key() == "clipGuid") {
                expected = localType == "AudioSource" ? "AudioImporter"
                                                       : "AnimationClipImporter";
            } else if (it.key() == "postProcessProfileGuid") {
                expected = "PostProcessProfileImporter";
            }
            if (!expected.empty() && !guid.empty()) {
                out.push_back({guid, expected, requiresNormalMap});
            }
        }
    };
    visit(document, {});
}

class ValidationContext {
public:
    explicit ValidationContext(const AssetDatabase& database) : database_(database) {}

    void Visit(const std::string& guid, const std::string& expected,
               const std::string& owner,
               bool requiresNormalMap = false) {
        const AssetRecord* record = database_.Find(guid);
        if (!record) {
            const std::string message = expected == "FontImporter"
                ? "unknown font GUID '" + guid + "' referenced by " + owner
                : "missing asset '" + guid + "' referenced by " + owner;
            Add(DependencyIssueCode::Missing, owner, guid, expected, {},
                message);
            return;
        }
        if (active_.count(guid) != 0) {
            const std::string message = record->importer == "PrefabImporter"
                ? "Cyclic prefab reference (cyclic asset dependency) at '" + guid + "'"
                : "cyclic asset dependency at '" + guid + "'";
            Add(DependencyIssueCode::Cycle, owner, guid, expected, record->importer,
                message);
            return;
        }
        if (!expected.empty() && record->importer != expected) {
            Add(DependencyIssueCode::TypeMismatch, owner, guid, expected, record->importer,
                "asset type mismatch for '" + guid + "': expected " + expected +
                ", got " + record->importer);
            return;
        }
        if (record->importFailed) {
            Add(DependencyIssueCode::ImportFailed, owner, guid, expected, record->importer,
                "asset import failed for '" + guid + "': " + record->importError);
            return;
        }
        if (requiresNormalMap) {
            const TextureImportSettings settings =
                DeserializeTextureImportSettings(record->settings, true);
            if (settings.usage != TextureUsage::NormalMap) {
                Add(DependencyIssueCode::UsageMismatch, owner, guid, expected,
                    record->importer,
                    "texture usage mismatch for '" + guid +
                    "': normalMapGuid requires NormalMap usage");
                return;
            }
        }

        if (visited_.count(guid) != 0) return;
        active_.insert(guid);

        if (record->importer == "PrefabImporter") {
            const std::filesystem::path path = database_.AbsoluteSourcePath(guid);
            try {
                std::ifstream file(path);
                if (!file) throw std::runtime_error("could not open file");
                nlohmann::json document;
                file >> document;
                std::vector<ExpectedReference> references;
                CollectDocumentReferences(document, references);
                for (const ExpectedReference& reference : references) {
                    Visit(reference.guid, reference.importer, record->sourcePath,
                          reference.requiresNormalMap);
                }
            } catch (const std::exception& error) {
                Add(DependencyIssueCode::InvalidDocument, owner, guid, expected,
                    record->importer, "invalid prefab '" + record->sourcePath +
                    "': " + error.what());
            }
        } else {
            const std::string childExpected = ExpectedForDependency(record->importer);
            for (const std::string& child : record->dependencies) {
                Visit(child, childExpected, record->sourcePath);
            }
        }

        active_.erase(guid);
        visited_.insert(guid);
    }

    void ScanDocument(const std::filesystem::path& path) {
        try {
            std::ifstream file(path);
            if (!file) throw std::runtime_error("could not open file");
            nlohmann::json document;
            file >> document;
            std::vector<ExpectedReference> references;
            CollectDocumentReferences(document, references);
            for (const ExpectedReference& reference : references) {
                Visit(reference.guid, reference.importer, path.generic_string(),
                      reference.requiresNormalMap);
            }
        } catch (const std::exception& error) {
            Add(DependencyIssueCode::InvalidDocument, path.generic_string(), {}, {}, {},
                "invalid scene '" + path.generic_string() + "': " + error.what());
        }
    }

    DependencyValidationResult Take() { return std::move(result_); }

private:
    void Add(DependencyIssueCode code, const std::string& owner,
             const std::string& guid, const std::string& expected,
             const std::string& actual, const std::string& message) {
        result_.issues.push_back({code, owner, guid, expected, actual, message});
    }

    const AssetDatabase& database_;
    DependencyValidationResult result_;
    std::unordered_set<std::string> active_;
    std::unordered_set<std::string> visited_;
};

} // namespace

std::string DependencyValidationResult::Summary() const {
    std::ostringstream stream;
    for (std::size_t index = 0; index < issues.size(); ++index) {
        if (index != 0) stream << '\n';
        stream << issues[index].message;
    }
    return stream.str();
}

DependencyValidationResult AssetDependencyValidator::ValidateScenes(
    const std::vector<std::filesystem::path>& scenePaths,
    const AssetDatabase& database) {
    ValidationContext context(database);
    for (const auto& path : scenePaths) context.ScanDocument(path);
    return context.Take();
}

DependencyValidationResult AssetDependencyValidator::ValidateAssetRoots(
    const std::vector<std::string>& rootGuids,
    const AssetDatabase& database) {
    ValidationContext context(database);
    for (const auto& guid : rootGuids) context.Visit(guid, {}, "asset root");
    return context.Take();
}

} // namespace molga
