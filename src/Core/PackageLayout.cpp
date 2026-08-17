#include "Core/PackageLayout.h"
#include "Common/Sha256.h"
#include "Core/GameConfig.h"
#include "Rendering/ShaderBundle.h"
#include "Scripting/ScriptApi.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace {

namespace fs = std::filesystem;

bool NormalizeSafeRelative(const std::string& stored,
                           std::string& normalized,
                           std::string& errorOut,
                           const char* label) {
    if (stored.empty()) {
        errorOut = std::string(label) + " must not be empty";
        return false;
    }
    for (unsigned char c : stored) {
        if (c < 0x20 || c == 0x7f) {
            errorOut = std::string(label) + " contains control characters: " + stored;
            return false;
        }
    }

    const fs::path path(stored);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        errorOut = std::string(label) + " must be package-relative: " + stored;
        return false;
    }

    const fs::path clean = path.lexically_normal();
    if (clean.empty() || clean == ".") {
        errorOut = std::string(label) + " is invalid: " + stored;
        return false;
    }
    for (const auto& part : clean) {
        if (part == "..") {
            errorOut = std::string(label) + " escapes the package root: " + stored;
            return false;
        }
    }
    normalized = clean.generic_string();
    return true;
}

bool ValidatePackageFile(const fs::path& root,
                         const std::string& stored,
                         std::string& normalized,
                         std::string& errorOut,
                         const char* label) {
    if (!NormalizeSafeRelative(stored, normalized, errorOut, label)) return false;

    const fs::path candidate = root / fs::path(normalized);
    if (!fs::exists(candidate)) {
        errorOut = "Missing package entry: " + candidate.string();
        return false;
    }

    std::error_code error;
    const fs::path canonicalRoot = fs::weakly_canonical(root, error);
    if (error) {
        errorOut = "Could not canonicalize package root: " + error.message();
        return false;
    }
    const fs::path canonicalCandidate = fs::weakly_canonical(candidate, error);
    if (error) {
        errorOut = "Could not canonicalize package entry: " + candidate.string();
        return false;
    }
    const fs::path relative = fs::relative(canonicalCandidate, canonicalRoot, error);
    if (error || relative.empty() || relative.is_absolute()) {
        errorOut = std::string(label) + " is outside the package root: " + stored;
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            errorOut = std::string(label) + " is outside the package root: " + stored;
            return false;
        }
    }
    return true;
}

bool IsSha256(const std::string& value) {
    return value.size() == 64U && std::all_of(
        value.begin(), value.end(), [](unsigned char character) {
            return std::isdigit(character) ||
                   (character >= static_cast<unsigned char>('a') &&
                    character <= static_cast<unsigned char>('f'));
        });
}

} // namespace

bool PackageLayout::Validate(
    const std::filesystem::path& root,
    const std::string& executableName,
    std::string& errorOut) {

    errorOut.clear();

    if (!std::filesystem::exists(root / executableName)) {
        errorOut = "Missing package entry: " + (root / executableName).string();
        return false;
    }

    std::filesystem::path configPath = root / "game.json";
    if (!std::filesystem::exists(configPath)) {
        errorOut = "Missing package entry: " + configPath.string();
        return false;
    }

    for (const auto& required : { "Assets", "Scenes", "ShaderBundle" }) {
        if (!std::filesystem::exists(root / required)) {
            errorOut = "Missing package entry: " + (root / required).string();
            return false;
        }
    }

    // Require asset catalog
    if (!std::filesystem::exists(root / "asset_catalog.json")) {
        errorOut = "Missing package entry: " + (root / "asset_catalog.json").string();
        return false;
    }

    // Require placeholder resource
    if (!std::filesystem::exists(root / "Resources" / "missing_texture.png")) {
        errorOut = "Missing package entry: " + (root / "Resources" / "missing_texture.png").string();
        return false;
    }

    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            errorOut = "Could not open game.json in package layout validation";
            return false;
        }
        nlohmann::json j;
        file >> j;

        if (!j.is_object() ||
            j.value("schemaVersion", 0) != GameConfig::CurrentSchemaVersion) {
            errorOut = "game.json must use package schema v4";
            return false;
        }
        if (!j.contains("graphics") || !j["graphics"].is_object()) {
            errorOut = "game.json is missing graphics manifest";
            return false;
        }
        const auto& graphics = j["graphics"];
        const std::string shaderManifest =
            graphics.value("shaderManifest", std::string{});
        const std::string shaderManifestSha256 =
            graphics.value("shaderManifestSha256", std::string{});
        if (graphics.value("api", std::string{}) != "sdlgpu" ||
            graphics.value("driver", std::string{}) != "metal" ||
            graphics.value("shaderFormat", std::string{}) != "msl" ||
            shaderManifest != "ShaderBundle/manifest.json" ||
            !IsSha256(shaderManifestSha256)) {
            errorOut = "graphics manifest must select SDL_GPU/Metal with MSL";
            return false;
        }
        std::string normalizedShaderManifest;
        if (!ValidatePackageFile(root, shaderManifest,
                                 normalizedShaderManifest, errorOut,
                                 "shader manifest")) {
            return false;
        }
        std::string hashError;
        const std::string actualManifestSha256 = molga::Sha256File(
            root / normalizedShaderManifest, &hashError);
        if (actualManifestSha256.empty() ||
            actualManifestSha256 != shaderManifestSha256) {
            errorOut = hashError.empty()
                ? "shader manifest SHA-256 mismatch"
                : hashError;
            return false;
        }
        molga::ShaderBundleManifest bundle;
        if (!molga::ShaderBundleManifest::Load(
                root / normalizedShaderManifest, bundle, errorOut) ||
            !bundle.Validate(root / "ShaderBundle", false, errorOut)) {
            return false;
        }
        std::error_code scanError;
        for (fs::recursive_directory_iterator iterator(root / "ShaderBundle",
                                                        scanError), end;
             !scanError && iterator != end; iterator.increment(scanError)) {
            if (!iterator->is_regular_file(scanError)) continue;
            const fs::path relative = fs::relative(
                iterator->path(), root / "ShaderBundle", scanError);
            if (scanError) break;
            if (relative == "manifest.json") continue;
            if (relative.extension() != ".msl") {
                errorOut = "forbidden non-MSL shader package entry: " +
                           relative.generic_string();
                return false;
            }
        }
        if (scanError) {
            errorOut = "could not inspect packaged shader bundle: " +
                       scanError.message();
            return false;
        }
        for (fs::recursive_directory_iterator iterator(root, scanError), end;
             !scanError && iterator != end; iterator.increment(scanError)) {
            if (!iterator->is_regular_file(scanError)) continue;
            std::string extension = iterator->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
            if (extension == ".hlsl" || extension == ".spv" ||
                extension == ".dxil" || extension == ".vert" ||
                extension == ".frag") {
                errorOut = "forbidden shader authoring/compiler payload: " +
                           fs::relative(iterator->path(), root).generic_string();
                return false;
            }
            std::string filename = iterator->path().filename().string();
            std::transform(filename.begin(), filename.end(), filename.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
            if (filename == "molga_shaderc" || filename == "molga_shaderc.exe") {
                errorOut = "shader compiler must not be included in a game package";
                return false;
            }
        }
        if (scanError) {
            errorOut = "could not inspect package shader exclusions: " +
                       scanError.message();
            return false;
        }

        std::string mainScene = j.value("mainScene", "Scenes/main.json");
        std::string normalizedMainScene;
        if (!ValidatePackageFile(root, mainScene, normalizedMainScene, errorOut, "mainScene")) {
            return false;
        }

        std::vector<std::string> scenes;
        if (j.contains("scenes") && j["scenes"].is_array()) {
            for (const auto& scene : j["scenes"]) {
                if (scene.is_string()) {
                    scenes.push_back(scene.get<std::string>());
                }
            }
        } else {
            scenes.push_back(mainScene);
        }

        for (const auto& scene : scenes) {
            std::string normalized;
            if (!ValidatePackageFile(root, scene, normalized, errorOut, "scene path")) return false;
        }

        if (j.contains("sceneCatalog")) {
            if (!j["sceneCatalog"].is_array()) {
                errorOut = "sceneCatalog must be an array";
                return false;
            }

            std::unordered_set<std::string> ids;
            std::unordered_set<std::string> packagePaths;
            std::unordered_map<std::string, std::string> catalog;
            for (const auto& entry : j["sceneCatalog"]) {
                if (!entry.is_object() || !entry.contains("id") ||
                    !entry["id"].is_string() || !entry.contains("packagePath") ||
                    !entry["packagePath"].is_string()) {
                    errorOut = "sceneCatalog entries require string id and packagePath fields";
                    return false;
                }

                std::string id;
                const std::string storedId = entry["id"].get<std::string>();
                if (!NormalizeSafeRelative(storedId, id, errorOut, "scene id")) return false;

                std::string packagePath;
                const std::string storedPackagePath = entry["packagePath"].get<std::string>();
                if (!ValidatePackageFile(root, storedPackagePath, packagePath,
                                         errorOut, "scene packagePath")) return false;

                if (!ids.insert(id).second) {
                    errorOut = "Duplicate scene catalog id: " + id;
                    return false;
                }
                if (!packagePaths.insert(packagePath).second) {
                    errorOut = "Duplicate scene catalog packagePath: " + packagePath;
                    return false;
                }
                catalog.emplace(id, packagePath);
            }

            std::string startupId;
            if (!j.contains("startupSceneId") || !j["startupSceneId"].is_string() ||
                !NormalizeSafeRelative(j["startupSceneId"].get<std::string>(), startupId,
                                       errorOut, "startupSceneId")) {
                if (errorOut.empty()) errorOut = "startupSceneId is required with sceneCatalog";
                return false;
            }
            const auto startup = catalog.find(startupId);
            if (startup == catalog.end()) {
                errorOut = "startupSceneId is not present in sceneCatalog: " + startupId;
                return false;
            }
            if (startup->second != normalizedMainScene) {
                errorOut = "mainScene does not match startupSceneId's packagePath";
                return false;
            }
        }

        if (j.contains("scripts")) {
            auto& s = j["scripts"];
            bool enabled = s.value("enabled", false);
            std::string library = s.value("library", "");
            if (enabled && !library.empty()) {
                if (s.value("apiVersion", 0) != molga::ScriptApiVersion) {
                    errorOut = "script package must be rebuilt for Script API v3";
                    return false;
                }
                std::string normalizedLibrary;
                if (!NormalizeSafeRelative(library, normalizedLibrary, errorOut,
                                           "script library")) return false;
                if (!std::filesystem::exists(root / normalizedLibrary)) {
                    errorOut = "Script library is enabled in game.json but missing from package: " +
                               (root / normalizedLibrary).string();
                    return false;
                }
                if (!ValidatePackageFile(root, library, normalizedLibrary, errorOut,
                                         "script library")) return false;
            }
        }
    } catch (...) {
        errorOut = "Failed to parse game.json in package layout validation";
        return false;
    }

    errorOut.clear();
    return true;
}

std::string PackageLayout::ExecutableNameFor(const std::string& gameName) {
#ifdef _WIN32
    return gameName + ".exe";
#else
    return gameName;
#endif
}
