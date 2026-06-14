#pragma once

#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>

class GameObject;

class PrefabUtil {
public:
    // Applies a list of modifications (overrides) to the instantiated hierarchy
    static void ApplyModifications(
        GameObject* instanceRoot,
        const nlohmann::json& modifications,
        const std::unordered_map<unsigned int, unsigned int>& idRemap);

    // Generates a list of modifications by diffing the current instance against its prefab template
    static nlohmann::json GenerateModifications(
        const GameObject* instanceRoot,
        const nlohmann::json& prefabJson,
        const std::unordered_map<unsigned int, unsigned int>& idRemap);

    // Applies current instance modifications to the prefab template, saving it and clearing modifications
    static bool ApplyPrefab(GameObject* instanceRoot);

    // Reverts the instance in-place by replacing the hierarchy with a clean instantiation from the prefab
    static bool RevertPrefab(GameObject* instanceRoot, std::vector<std::shared_ptr<GameObject>>& worldObjects);
};
