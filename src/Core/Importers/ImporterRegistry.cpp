#include "Core/Importers/ImporterRegistry.h"

#include "Core/Importers/AudioImporter.h"
#include "Core/Importers/FontImporter.h"
#include "Core/Importers/PrefabImporter.h"
#include "Core/Importers/PostProcessProfileImporter.h"
#include "Core/Importers/TextureImporter.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace molga {
namespace {

class StructuredJsonImporter final : public IImporter {
public:
    StructuredJsonImporter(std::string name, int version,
                           std::vector<std::string> extensions,
                           std::vector<std::string> dependencyKeys)
        : name_(std::move(name)), version_(version), extensions_(std::move(extensions)),
          dependencyKeys_(std::move(dependencyKeys)) {}

    std::string Name() const override { return name_; }
    int Version() const override { return version_; }
    bool CanImport(const std::string& ext) const override {
        return std::find(extensions_.begin(), extensions_.end(), ext) != extensions_.end();
    }
    ImportResult Import(const std::string& path) const override {
        ImportResult result;
        try {
            std::ifstream file(path);
            if (!file) {
                result.error = "source not found: " + path;
                return result;
            }
            nlohmann::json document;
            file >> document;
            if (!document.is_object()) {
                result.error = "structured asset root must be an object: " + path;
                return result;
            }
            std::unordered_set<std::string> unique;
            std::function<void(const nlohmann::json&)> visit = [&](const nlohmann::json& value) {
                if (value.is_object()) {
                    for (auto it = value.begin(); it != value.end(); ++it) {
                        if (std::find(dependencyKeys_.begin(), dependencyKeys_.end(), it.key()) !=
                                dependencyKeys_.end() && it.value().is_string()) {
                            const std::string guid = it.value().get<std::string>();
                            if (!guid.empty() && unique.insert(guid).second) {
                                result.dependencies.push_back(guid);
                            }
                        }
                        visit(it.value());
                    }
                } else if (value.is_array()) {
                    for (const auto& item : value) visit(item);
                }
            };
            visit(document);
            int schemaVersion = 1;
            const auto schema = document.find("schemaVersion");
            if (schema != document.end()) {
                if (!schema->is_number_integer()) {
                    result.dependencies.clear();
                    result.metadata = nlohmann::json::object();
                    result.error = "structured asset schemaVersion must be an integer: " + path;
                    return result;
                }
                schemaVersion = schema->get<int>();
            }
            result.metadata["schemaVersion"] = schemaVersion;
            result.success = true;
        } catch (const std::exception& error) {
            result.success = false;
            result.dependencies.clear();
            result.metadata = nlohmann::json::object();
            result.error = std::string("invalid structured asset: ") + error.what();
        }
        return result;
    }

private:
    std::string name_;
    int version_ = 1;
    std::vector<std::string> extensions_;
    std::vector<std::string> dependencyKeys_;
};

} // namespace

ImporterRegistry& ImporterRegistry::Get() {
    static ImporterRegistry registry;
    registry.RegisterBuiltins();
    return registry;
}

void ImporterRegistry::Register(std::shared_ptr<IImporter> importer) {
    if (!importer || importer->Name().empty()) return;
    auto found = byName_.find(importer->Name());
    if (found != byName_.end()) {
        for (auto& existing : ordered_) {
            if (existing == found->second) {
                existing = importer;
                break;
            }
        }
    } else {
        ordered_.push_back(importer);
    }
    byName_[importer->Name()] = std::move(importer);
}

const IImporter* ImporterRegistry::FindByName(const std::string& name) const {
    const auto found = byName_.find(name);
    return found == byName_.end() ? nullptr : found->second.get();
}

const IImporter* ImporterRegistry::FindForExtension(const std::string& extension) const {
    std::string normalized = extension;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& importer : ordered_) {
        if (importer && importer->CanImport(normalized)) return importer.get();
    }
    return nullptr;
}

ImportResult ImporterRegistry::Import(const std::string& importerName,
                                      const std::string& path,
                                      const nlohmann::json& settings) const {
    try {
        if (const IImporter* importer = FindByName(importerName)) {
            return importer->Import(path, settings);
        }
        ImportResult generic;
        if (importerName != "GenericImporter") {
            generic.error = "importer is not registered: " + importerName;
            return generic;
        }
        generic.success = std::filesystem::is_regular_file(path);
        if (!generic.success) generic.error = "source not found: " + path;
        return generic;
    } catch (const std::exception& error) {
        ImportResult failed;
        failed.error = "importer '" + importerName + "' failed: " + error.what();
        return failed;
    } catch (...) {
        ImportResult failed;
        failed.error = "importer '" + importerName + "' failed with an unknown exception";
        return failed;
    }
}

void ImporterRegistry::RegisterBuiltins() {
    if (builtinsRegistered_) return;
    builtinsRegistered_ = true;
    Register(std::make_shared<TextureImporter>());
    Register(std::make_shared<AudioImporter>());
    Register(std::make_shared<PrefabImporter>());
    Register(std::make_shared<FontImporter>());
    Register(std::make_shared<PostProcessProfileImporter>());
    Register(std::make_shared<StructuredJsonImporter>(
        "AnimationClipImporter", 1, std::vector<std::string>{".animclip"},
        std::vector<std::string>{"textureGuid"}));
    Register(std::make_shared<StructuredJsonImporter>(
        "AnimatorControllerImporter", 1, std::vector<std::string>{".animator"},
        std::vector<std::string>{"clipGuid"}));
    Register(std::make_shared<StructuredJsonImporter>(
        "TileSetImporter", 1, std::vector<std::string>{".tileset"},
        std::vector<std::string>{"textureGuid"}));
}

void ImporterRegistry::ClearForTesting() {
    byName_.clear();
    ordered_.clear();
    builtinsRegistered_ = false;
}

} // namespace molga
