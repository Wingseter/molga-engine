#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>
#include <nlohmann/json.hpp>

class GameObject;
class World;

class PrefabRegistry {
public:
    static PrefabRegistry& Get();

    // Scans PathService::Get().AssetRoot() for all .prefab files
    void ScanAssets();

    // Instantiates a prefab into the given list using its GUID
    GameObject* Instantiate(const std::string& guid, std::vector<std::shared_ptr<GameObject>>& outObjects, std::unordered_map<unsigned int, unsigned int>& idRemap);

    // Creates or updates a prefab file with the given JSON data (subtree json) and registers it
    bool SavePrefab(const std::string& guid, const std::filesystem::path& relativePath, const nlohmann::json& subtreeJson);

    // Get info
    std::filesystem::path GetPrefabPath(const std::string& guid) const;
    std::string GetPrefabGuid(const std::filesystem::path& path) const;
    nlohmann::json GetPrefabJson(const std::string& guid) const;
    bool HasPrefab(const std::string& guid) const;

    // 등록된 모든 프리팹(guid -> 상대경로). 인스펙터 프리팹 선택 등에 사용.
    const std::unordered_map<std::string, std::filesystem::path>& GetAllPrefabs() const {
        return guidToPath_;
    }

    // Helper to generate a new GUID
    static std::string GenerateGUID();

private:
    PrefabRegistry() = default;

    // maps GUID to relative path (from asset root)
    std::unordered_map<std::string, std::filesystem::path> guidToPath_;
    // maps relative path to GUID
    std::unordered_map<std::string, std::string> pathToGuid_;
    // maps GUID to cached parsed prefab document json (not just the subtree, but the full document)
    std::unordered_map<std::string, nlohmann::json> guidToCache_;
};
