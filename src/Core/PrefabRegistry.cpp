#include "PrefabRegistry.h"
#include "PathService.h"
#include "SceneSerializer.h"
#include "ECS/GameObject.h"
#include "Common/Log.h"
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>

PrefabRegistry& PrefabRegistry::Get() {
    static PrefabRegistry instance;
    return instance;
}

void PrefabRegistry::ScanAssets() {
    guidToPath_.clear();
    pathToGuid_.clear();
    guidToCache_.clear();

    const std::filesystem::path assetRoot = PathService::Get().AssetRoot();
    if (assetRoot.empty() || !std::filesystem::exists(assetRoot)) {
        return;
    }

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetRoot)) {
            if (entry.is_regular_file() && entry.path().extension() == ".prefab") {
                std::ifstream file(entry.path());
                if (!file.is_open()) continue;

                nlohmann::json j;
                try {
                    file >> j;
                } catch (const std::exception&) {
                    continue; // Skip invalid JSON
                }

                if (j.contains("guid") && j.contains("gameObjects")) {
                    std::string guid = j["guid"].get<std::string>();
                    std::filesystem::path relPath = std::filesystem::relative(entry.path(), assetRoot);
                    
                    guidToPath_[guid] = relPath;
                    pathToGuid_[relPath.string()] = guid;
                    guidToCache_[guid] = j;
                }
            }
        }
    } catch (const std::exception& e) {
        Log::Error("PrefabRegistry", std::string("Error scanning prefabs: ") + e.what());
    }
}

GameObject* PrefabRegistry::Instantiate(
    const std::string& guid,
    std::vector<std::shared_ptr<GameObject>>& outObjects,
    std::unordered_map<unsigned int, unsigned int>& idRemap) {

    auto it = guidToCache_.find(guid);
    if (it == guidToCache_.end()) {
        ScanAssets(); // Re-scan
        it = guidToCache_.find(guid);
        if (it == guidToCache_.end()) {
            Log::Error("PrefabRegistry", "Prefab GUID not found: " + guid);
            return nullptr;
        }
    }

    return SceneSerializer::DeserializeSubtreeRemapped(it->second, outObjects, idRemap);
}

bool PrefabRegistry::SavePrefab(
    const std::string& guid,
    const std::filesystem::path& relativePath,
    const nlohmann::json& subtreeJson) {

    const std::filesystem::path assetRoot = PathService::Get().AssetRoot();
    std::filesystem::path fullPath = assetRoot / relativePath;

    // Ensure parent directory exists
    std::filesystem::create_directories(fullPath.parent_path());

    nlohmann::json doc;
    doc["guid"] = guid;
    doc["version"] = "1.0";
    doc["gameObjects"] = subtreeJson["gameObjects"]; // Expecting full serialized format with gameObjects array

    std::ofstream file(fullPath);
    if (!file.is_open()) {
        Log::Error("PrefabRegistry", "Failed to open prefab file for writing: " + fullPath.string());
        return false;
    }

    file << doc.dump(2);
    file.close();

    // Cache the saved prefab
    guidToPath_[guid] = relativePath;
    pathToGuid_[relativePath.string()] = guid;
    guidToCache_[guid] = doc;

    return true;
}

std::filesystem::path PrefabRegistry::GetPrefabPath(const std::string& guid) const {
    auto it = guidToPath_.find(guid);
    return it != guidToPath_.end() ? it->second : std::filesystem::path();
}

std::string PrefabRegistry::GetPrefabGuid(const std::filesystem::path& path) const {
    auto it = pathToGuid_.find(path.string());
    return it != pathToGuid_.end() ? it->second : "";
}

nlohmann::json PrefabRegistry::GetPrefabJson(const std::string& guid) const {
    auto it = guidToCache_.find(guid);
    return it != guidToCache_.end() ? it->second : nlohmann::json();
}

bool PrefabRegistry::HasPrefab(const std::string& guid) const {
    return guidToCache_.count(guid) > 0;
}

std::string PrefabRegistry::GenerateGUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

    std::stringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << dis(gen)
       << std::setw(8) << dis(gen)
       << std::setw(8) << dis(gen)
       << std::setw(8) << dis(gen);
    return ss.str();
}
