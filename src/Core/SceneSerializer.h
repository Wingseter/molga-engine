#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include <nlohmann/json.hpp>

class GameObject;

class SceneSerializer {
public:
    // 메모리 직렬화 (스냅샷/복제용)
    static nlohmann::json SerializeScene(
        const std::vector<std::shared_ptr<GameObject>>& objects,
        const std::string& sceneName);
    static bool DeserializeScene(
        const nlohmann::json& doc,
        std::vector<std::shared_ptr<GameObject>>& objects);

    // Save scene to JSON file
    static bool SaveScene(const std::string& filepath,
                          const std::vector<std::shared_ptr<GameObject>>& objects);

    // Load scene from JSON file
    static bool LoadScene(const std::string& filepath,
                          std::vector<std::shared_ptr<GameObject>>& objects);

    // Serialize single GameObject to JSON string
    static std::string SerializeGameObject(const GameObject* obj);

    // Deserialize single GameObject from JSON string
    static std::shared_ptr<GameObject> DeserializeGameObject(const std::string& jsonStr);

    // Subtree serialization/deserialization for Instantiate/Prefab
    static nlohmann::json SerializeSubtree(const GameObject* root);
    static GameObject* DeserializeSubtreeRemapped(
        const nlohmann::json& subtree,
        std::vector<std::shared_ptr<GameObject>>& outObjects,
        std::unordered_map<unsigned int, unsigned int>& idRemap);
};

