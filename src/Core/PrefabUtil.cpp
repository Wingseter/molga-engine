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

// prefab override는 저작된 값만 담는다. schemaVersion은 저작자가 고칠 수
// 없는 형식 표식이고, 컴포넌트가 그 키를 쓰기 전에 저장된 prefab 파일과
// 비교하면 손대지 않은 인스턴스마다 가짜 override가 하나씩 생긴다. 그 가짜
// override는 다음 로드에서 컴포넌트에 다시 주입되므로 무해하지도 않다.
//
// 그 대가로 남는 제약: schemaVersion으로 분기해 읽는 컴포넌트(UILabel,
// UICanvas)의 인스턴스는 자기 prefab 파일보다 새 스키마에 있다고 기록할 수
// 없다. prefab이 레거시 형식이면 인스턴스가 저작한 schema 2 전용 값들은
// 로드 때 레거시 분기가 지운다. Milestone 15의 이관이 이 자리를 다룬다.
bool IsNonOverridableComponentKey(const std::string& key) {
    return key == "type" || key == "schemaVersion";
}

// 씬 참조는 {"targetId": n} 한 모양으로만 저장된다(UIObjectRefJson). override
// 비교는 런타임 스냅샷(리매핑된 런타임 id)과 prefab 파일(prefab-local id)을
// 맞대므로, 그 사이에서 id 공간을 바꿔 주지 않으면 prefab 안쪽을 가리키는
// 참조는 손대지 않은 인스턴스에서도 언제나 달라 보인다. 그렇게 만들어진 가짜
// override에는 이번 실행에만 유효한 런타임 id가 실려 파일에 저장되고, 다음
// 로드가 그 죽은 id를 새 인스턴스에 강제해 참조를 끊는다.
//
// 매핑에 없는 id는 서브트리 바깥(다른 씬 오브젝트)을 가리키므로 손대지 않는다.
void RemapObjectRefsInPlace(
    nlohmann::json& value,
    const std::unordered_map<unsigned int, unsigned int>& remap) {
    if (value.is_object()) {
        const auto targetId = value.find("targetId");
        if (value.size() == 1 && targetId != value.end() &&
            targetId->is_number_unsigned()) {
            const auto raw = targetId->get<unsigned long long>();
            if (raw <= std::numeric_limits<unsigned int>::max()) {
                const auto found =
                    remap.find(static_cast<unsigned int>(raw));
                if (found != remap.end()) *targetId = found->second;
            }
            return;
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            RemapObjectRefsInPlace(it.value(), remap);
        }
        return;
    }
    if (value.is_array()) {
        for (auto& element : value) RemapObjectRefsInPlace(element, remap);
    }
}

std::unordered_map<unsigned int, unsigned int> InvertIdRemap(
    const std::unordered_map<unsigned int, unsigned int>& idRemap) {
    std::unordered_map<unsigned int, unsigned int> inverse;
    inverse.reserve(idRemap.size());
    for (const auto& [localId, runtimeId] : idRemap) inverse[runtimeId] = localId;
    return inverse;
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

                if (IsNonOverridableComponentKey(key)) {
                    // 옛 파일에 남아 있는 표식 override는 적용하지 않는다.
                    continue;
                }
                if (key == "enabled") {
                    compJson["enabled"] = value;
                } else {
                    // 저장된 override는 prefab id 공간에 있다. 여기서 런타임
                    // id로 돌려놓지 않으면 이 인스턴스의 참조가 prefab 파일의
                    // local id를 가리킨 채 남는다.
                    RemapObjectRefsInPlace(value, idRemap);
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
    // 런타임 스냅샷의 참조를 prefab id 공간으로 되돌리기 위한 역매핑.
    const std::unordered_map<unsigned int, unsigned int> runtimeToLocal =
        InvertIdRemap(idRemap);

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
            RemapObjectRefsInPlace(runtimeCompJson, runtimeToLocal);

            auto prefabCompIt = prefabComps.find(comp->GetTypeName());
            if (prefabCompIt != prefabComps.end()) {
                const auto& prefabCompJson = prefabCompIt->second;
                
                for (auto compIt = runtimeCompJson.begin(); compIt != runtimeCompJson.end(); ++compIt) {
                    std::string key = compIt.key();
                    if (IsNonOverridableComponentKey(key)) continue;

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
                    if (IsNonOverridableComponentKey(key)) continue;
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
