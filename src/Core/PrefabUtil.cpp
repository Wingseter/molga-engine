#include "PrefabUtil.h"
#include "Core/PrefabRegistry.h"
#include "Core/SceneSerializer.h"
#include "ECS/GameObject.h"
#include "ECS/Component.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/PrefabInstance.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/TilemapRenderer.h"
#include <iostream>
#include <limits>
#include <unordered_set>

namespace {

bool TryGetModificationTarget(const nlohmann::json& modification,
                              unsigned int& target) {
    const auto found = modification.find("target");
    if (found == modification.end()) return false;
    if (found->is_number_unsigned()) {
        const auto raw = found->get<unsigned long long>();
        if (raw > std::numeric_limits<unsigned int>::max()) return false;
        target = static_cast<unsigned int>(raw);
        return true;
    }
    if (found->is_number_integer()) {
        const auto raw = found->get<long long>();
        if (raw < 0 || static_cast<unsigned long long>(raw) >
                           std::numeric_limits<unsigned int>::max()) {
            return false;
        }
        target = static_cast<unsigned int>(raw);
        return true;
    }
    return false;
}

bool IsCameraModification(const nlohmann::json& modification,
                          const char* key) {
    const auto component = modification.find("component");
    const auto property = modification.find("key");
    return component != modification.end() && component->is_string() &&
           component->get<std::string>() == "Camera" &&
           property != modification.end() && property->is_string() &&
           property->get<std::string>() == key;
}

nlohmann::json NormalizeCameraOverrideValue(const std::string& key,
                                            const nlohmann::json& value) {
    nlohmann::json component = nlohmann::json::object();
    component[key] = value;
    return Camera::CanonicalizeSerializedData(component).at(key);
}

nlohmann::json CanonicalizeComponentSnapshot(
    const std::string& type, const nlohmann::json& snapshot) {
    if (type == "Camera") {
        return Camera::CanonicalizeSerializedData(snapshot);
    }
    if (type == "SpriteRenderer") {
        return SpriteRenderer::CanonicalizeSerializedData(snapshot);
    }
    if (type == "TilemapRenderer") {
        return TilemapRenderer::CanonicalizeSerializedData(snapshot);
    }
    return snapshot;
}

} // namespace

nlohmann::json PrefabUtil::NormalizeModifications(
    const nlohmann::json& modifications) {
    if (!modifications.is_array()) return nlohmann::json::array();

    // A modern override is authoritative even if a legacy mirror occurs later
    // in the array. Track by local target so application is order-independent.
    std::unordered_set<unsigned int> modernCameraRoleTargets;
    for (const auto& modification : modifications) {
        unsigned int target = 0;
        if (IsCameraModification(modification, "outputRole") &&
            TryGetModificationTarget(modification, target)) {
            modernCameraRoleTargets.insert(target);
        }
    }

    nlohmann::json normalized = nlohmann::json::array();
    for (const auto& modification : modifications) {
        if (!modification.is_object()) {
            normalized.push_back(modification);
            continue;
        }

        nlohmann::json canonical = modification;
        unsigned int target = 0;
        const bool hasTarget = TryGetModificationTarget(modification, target);
        if (IsCameraModification(modification, "isMain")) {
            if (hasTarget && modernCameraRoleTargets.count(target) != 0U) {
                continue;
            }
            const auto value = modification.find("value");
            const bool main = value != modification.end() &&
                              value->is_boolean() && value->get<bool>();
            canonical["key"] = "outputRole";
            canonical["value"] = main ? "Primary" : "Disabled";
        } else if (IsCameraModification(modification, "outputRole") ||
                   IsCameraModification(modification, "viewport") ||
                   IsCameraModification(modification, "cullingMask")) {
            const auto property = modification.find("key");
            const auto value = modification.find("value");
            if (property != modification.end() && value != modification.end()) {
                canonical["value"] = NormalizeCameraOverrideValue(
                    property->get<std::string>(), *value);
            }
        }
        normalized.push_back(std::move(canonical));
    }
    return normalized;
}

void PrefabUtil::ApplyModifications(
    GameObject* instanceRoot,
    const nlohmann::json& modifications,
    const std::unordered_map<unsigned int, unsigned int>& idRemap) {
    
    if (!instanceRoot || modifications.is_null() || !modifications.is_array()) return;
    const nlohmann::json normalized = NormalizeModifications(modifications);

    std::vector<GameObject*> subtree;
    instanceRoot->CollectSubtree(subtree);
    std::unordered_map<unsigned int, GameObject*> runtimeIdMap;
    for (auto* obj : subtree) {
        if (obj) runtimeIdMap[obj->GetID()] = obj;
    }

    for (const auto& mod : normalized) {
        if (!mod.contains("target") || !mod.contains("component") || !mod.contains("key") || !mod.contains("value")) continue;

        unsigned int targetLocalId = mod["target"].get<unsigned int>();
        std::string componentType = mod["component"].get<std::string>();
        std::string key = mod["key"].get<std::string>();
        nlohmann::json value = mod["value"];

        auto remapIt = idRemap.find(targetLocalId);
        if (remapIt == idRemap.end()) continue;

        unsigned int runtimeId = remapIt->second;
        auto runtimeObjIt = runtimeIdMap.find(runtimeId);
        if (runtimeObjIt == runtimeIdMap.end()) continue;

        GameObject* targetObj = runtimeObjIt->second;

        if (componentType == "GameObject") {
            if (key == "name") {
                targetObj->SetName(value.get<std::string>());
            } else if (key == "tag") {
                targetObj->SetTag(value.get<std::string>());
            } else if (key == "layer") {
                targetObj->SetLayer(value.get<int>());
            } else if (key == "active") {
                targetObj->SetActive(value.get<bool>());
            }
        } else {
            Component* foundComp = nullptr;
            for (auto* comp : targetObj->GetComponents()) {
                if (comp && comp->GetTypeName() == componentType) {
                    foundComp = comp;
                    break;
                }
            }

            if (foundComp) {
                nlohmann::json compJson;
                compJson["type"] = foundComp->GetTypeName();
                compJson["enabled"] = foundComp->IsEnabled();
                foundComp->Serialize(compJson);

                if (key == "enabled") {
                    compJson["enabled"] = value;
                } else {
                    compJson[key] = value;
                }

                foundComp->Deserialize(compJson);
                if (compJson.contains("enabled")) {
                    // Prefab modifications are applied while SceneSerializer is
                    // assembling a detached object graph. Lifecycle begins only
                    // after the completed graph is attached to its World.
                    foundComp->SetEnabledFromSerializedState(
                        compJson["enabled"].get<bool>());
                }
            }
        }
    }
}

nlohmann::json PrefabUtil::GenerateModifications(
    const GameObject* instanceRoot,
    const nlohmann::json& prefabJson,
    const std::unordered_map<unsigned int, unsigned int>& idRemap) {

    nlohmann::json modifications = nlohmann::json::array();
    if (!instanceRoot || !prefabJson.contains("gameObjects")) return modifications;

    std::unordered_map<unsigned int, nlohmann::json> localIdMap;
    for (const auto& objJson : prefabJson["gameObjects"]) {
        if (objJson.contains("id")) {
            unsigned int localId = objJson["id"].get<unsigned int>();
            localIdMap[localId] = objJson;
        }
    }

    std::vector<GameObject*> subtree;
    const_cast<GameObject*>(instanceRoot)->CollectSubtree(subtree);
    std::unordered_map<unsigned int, GameObject*> runtimeIdMap;
    for (auto* obj : subtree) {
        if (obj) runtimeIdMap[obj->GetID()] = obj;
    }

    for (const auto& [localId, localObjJson] : localIdMap) {
        auto remapIt = idRemap.find(localId);
        if (remapIt == idRemap.end()) continue;

        unsigned int runtimeId = remapIt->second;
        auto runtimeObjIt = runtimeIdMap.find(runtimeId);
        if (runtimeObjIt == runtimeIdMap.end()) continue;

        const GameObject* targetObj = runtimeObjIt->second;

        // 1. Diff GameObject properties
        if (localObjJson.value("name", "GameObject") != targetObj->GetName()) {
            modifications.push_back({
                {"target", localId},
                {"component", "GameObject"},
                {"key", "name"},
                {"value", targetObj->GetName()}
            });
        }
        if (localObjJson.value("tag", "Untagged") != targetObj->GetTag()) {
            modifications.push_back({
                {"target", localId},
                {"component", "GameObject"},
                {"key", "tag"},
                {"value", targetObj->GetTag()}
            });
        }
        if (localObjJson.value("layer", 0) != targetObj->GetLayer()) {
            modifications.push_back({
                {"target", localId},
                {"component", "GameObject"},
                {"key", "layer"},
                {"value", targetObj->GetLayer()}
            });
        }
        if (localObjJson.value("active", true) != targetObj->IsActive()) {
            modifications.push_back({
                {"target", localId},
                {"component", "GameObject"},
                {"key", "active"},
                {"value", targetObj->IsActive()}
            });
        }

        // 2. Diff components
        std::unordered_map<std::string, nlohmann::json> prefabComps;
        if (localObjJson.contains("components")) {
            for (const auto& compJson : localObjJson["components"]) {
                std::string type = compJson.value("type", "");
                prefabComps[type] =
                    CanonicalizeComponentSnapshot(type, compJson);
            }
        }

        for (const auto* comp : targetObj->GetComponents()) {
            if (!comp) continue;
            if (comp->GetTypeName() == "PrefabInstance") continue;

            nlohmann::json runtimeCompJson;
            runtimeCompJson["type"] = comp->GetTypeName();
            runtimeCompJson["enabled"] = comp->IsEnabled();
            comp->Serialize(runtimeCompJson);
            runtimeCompJson = CanonicalizeComponentSnapshot(
                comp->GetTypeName(), runtimeCompJson);

            auto prefabCompIt = prefabComps.find(comp->GetTypeName());
            if (prefabCompIt != prefabComps.end()) {
                const auto& prefabCompJson = prefabCompIt->second;
                
                for (auto compIt = runtimeCompJson.begin(); compIt != runtimeCompJson.end(); ++compIt) {
                    std::string key = compIt.key();
                    if (key == "type") continue;

                    nlohmann::json runtimeVal = compIt.value();
                    
                    if (!prefabCompJson.contains(key) || prefabCompJson[key] != runtimeVal) {
                        modifications.push_back({
                            {"target", localId},
                            {"component", comp->GetTypeName()},
                            {"key", key},
                            {"value", runtimeVal}
                        });
                    }
                }
            } else {
                for (auto compIt = runtimeCompJson.begin(); compIt != runtimeCompJson.end(); ++compIt) {
                    std::string key = compIt.key();
                    if (key == "type") continue;
                    modifications.push_back({
                        {"target", localId},
                        {"component", comp->GetTypeName()},
                        {"key", key},
                        {"value", compIt.value()}
                    });
                }
            }
        }
    }

    return modifications;
}

GameObject* PrefabUtil::FindNearestInstanceRoot(GameObject* target) {
    for (GameObject* current = target; current; current = current->GetParent()) {
        if (current->GetComponent<PrefabInstance>()) return current;
    }
    return nullptr;
}

std::size_t PrefabUtil::RefreshNearestInstanceOverrides(
    const std::vector<GameObject*>& targets) {
    std::unordered_set<unsigned int> refreshedRootIds;
    std::size_t refreshedCount = 0;
    for (GameObject* target : targets) {
        GameObject* root = FindNearestInstanceRoot(target);
        if (!root || !refreshedRootIds.insert(root->GetID()).second) continue;

        auto* instance = root->GetComponent<PrefabInstance>();
        const nlohmann::json prefab =
            PrefabRegistry::Get().GetPrefabJson(instance->GetPrefabGuid());
        if (!prefab.is_object() || !prefab.contains("gameObjects")) continue;
        instance->SetModifications(GenerateModifications(
            root, prefab, instance->GetIdRemap()));
        ++refreshedCount;
    }
    return refreshedCount;
}

bool PrefabUtil::ApplyPrefab(GameObject* instanceRoot) {
    auto* pi = instanceRoot ? instanceRoot->GetComponent<PrefabInstance>() : nullptr;
    if (!pi) return false;
    std::string guid = pi->GetPrefabGuid();
    std::filesystem::path path = PrefabRegistry::Get().GetPrefabPath(guid);
    if (path.empty()) return false;

    nlohmann::json subtree = SceneSerializer::SerializeSubtree(instanceRoot);

    std::unordered_map<unsigned int, unsigned int> inverseMap;
    for (const auto& [localId, runtimeId] : pi->GetIdRemap()) {
        inverseMap[runtimeId] = localId;
    }

    if (subtree.contains("gameObjects")) {
        for (auto& objJson : subtree["gameObjects"]) {
            // Nested prefab instances are stored stripped; their ids live under
            // "prefabInstance" and must be remapped to the template's local id-space too.
            if (objJson.contains("prefabInstance")) {
                auto& piJson = objJson["prefabInstance"];
                if (piJson.contains("rootId")) {
                    unsigned int rid = piJson["rootId"].get<unsigned int>();
                    auto it = inverseMap.find(rid);
                    if (it != inverseMap.end()) piJson["rootId"] = it->second;
                }
                if (piJson.contains("parentId") && piJson["parentId"].get<int>() >= 0) {
                    unsigned int pid = piJson["parentId"].get<unsigned int>();
                    auto pIt = inverseMap.find(pid);
                    if (pIt != inverseMap.end()) piJson["parentId"] = pIt->second;
                }
                continue;
            }

            if (objJson.contains("id")) {
                unsigned int runtimeId = objJson["id"].get<unsigned int>();
                auto it = inverseMap.find(runtimeId);
                if (it != inverseMap.end()) {
                    objJson["id"] = it->second;
                }
            }

            if (objJson.contains("parentId") && objJson["parentId"].get<int>() >= 0) {
                unsigned int runtimeParentId = objJson["parentId"].get<unsigned int>();
                auto pIt = inverseMap.find(runtimeParentId);
                if (pIt != inverseMap.end()) {
                    objJson["parentId"] = pIt->second;
                }
            }
        }
    }

    bool success = PrefabRegistry::Get().SavePrefab(guid, path, subtree);
    if (success) {
        pi->SetModifications(nlohmann::json::array());
    }
    return success;
}

bool PrefabUtil::RevertPrefab(GameObject* instanceRoot, std::vector<std::shared_ptr<GameObject>>& worldObjects) {
    auto* pi = instanceRoot ? instanceRoot->GetComponent<PrefabInstance>() : nullptr;
    if (!pi) return false;
    std::string guid = pi->GetPrefabGuid();

    std::unordered_map<unsigned int, unsigned int> idRemap;
    std::vector<std::shared_ptr<GameObject>> instantiatedObjects;
    GameObject* cleanRoot = PrefabRegistry::Get().Instantiate(guid, instantiatedObjects, idRemap);
    if (!cleanRoot) return false;

    auto* cleanPi = cleanRoot->AddComponent<PrefabInstance>();
    cleanPi->SetPrefabGuid(guid);
    cleanPi->SetIdRemap(idRemap);

    GameObject* parent = instanceRoot->GetParent();
    if (parent) {
        cleanRoot->SetParent(parent);
    }

    std::vector<GameObject*> oldSubtree;
    instanceRoot->CollectSubtree(oldSubtree);

    auto removePred = [&](const std::shared_ptr<GameObject>& o) {
        if (!o) return true;
        for (auto* oldObj : oldSubtree) {
            if (oldObj && oldObj->GetID() == o->GetID()) {
                return true;
            }
        }
        return false;
    };
    worldObjects.erase(std::remove_if(worldObjects.begin(), worldObjects.end(), removePred), worldObjects.end());

    for (auto& io : instantiatedObjects) {
        worldObjects.push_back(io);
    }

    return true;
}
