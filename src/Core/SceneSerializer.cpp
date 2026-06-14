#include "SceneSerializer.h"
#include "../ECS/GameObject.h"
#include "../ECS/Component.h"
#include "../ECS/ComponentFactory.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../ECS/Components/TilemapRenderer.h"
#include "../ECS/Components/ParticleSystem.h"
#include "../ECS/Components/BoxCollider2D.h"
#include "../ECS/Components/CircleCollider2D.h"
#include "../ECS/Components/Rigidbody2D.h"
#include "../ECS/Components/AudioSource.h"
#include "../ECS/Components/AudioListener.h"
#include "../ECS/Components/Camera.h"
#include "../ECS/Components/TextRenderer2D.h"
#include "../Scripting/ScriptManager.h"
#include "../Scripting/Script.h"
#include "Core/PrefabRegistry.h"
#include "Core/PrefabUtil.h"
#include "ECS/Components/PrefabInstance.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace {
bool IsPartOfPrefabInstanceSubtree(const GameObject* obj) {
    if (!obj) return false;
    const GameObject* curr = obj->GetParent();
    while (curr) {
        if (curr->GetComponent<PrefabInstance>()) {
            return true;
        }
        curr = curr->GetParent();
    }
    return false;
}

// Is `obj` a descendant of a prefab instance that sits strictly between it and
// `root`? Such descendants belong to the nested prefab, not to the subtree being
// serialized, so they are skipped (the nested instance is stored stripped instead).
bool IsInsideNestedPrefab(const GameObject* obj, const GameObject* root) {
    const GameObject* curr = obj->GetParent();
    while (curr && curr != root) {
        if (curr->GetComponent<PrefabInstance>()) return true;
        curr = curr->GetParent();
    }
    return false;
}
}

nlohmann::json SceneSerializer::SerializeScene(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const std::string& sceneName) {
    json sceneJson;
    sceneJson["version"] = "1.0";
    sceneJson["name"] = sceneName;

    json objectsArray = json::array();
    for (const auto& obj : objects) {
        if (!obj) continue;

        if (IsPartOfPrefabInstanceSubtree(obj.get())) {
            continue;
        }

        if (auto* prefabInst = obj->GetComponent<PrefabInstance>()) {
            nlohmann::json prefabJson = PrefabRegistry::Get().GetPrefabJson(prefabInst->GetPrefabGuid());
            if (!prefabJson.is_null()) {
                auto updatedMods = PrefabUtil::GenerateModifications(obj.get(), prefabJson, prefabInst->GetIdRemap());
                prefabInst->SetModifications(updatedMods);
            }

            json strippedJson;
            json piData;
            piData["guid"] = prefabInst->GetPrefabGuid();
            piData["rootId"] = obj->GetID();
            piData["parentId"] = obj->GetParent()
                ? static_cast<int>(obj->GetParent()->GetID()) : -1;
            piData["modifications"] = prefabInst->GetModifications();
            
            strippedJson["prefabInstance"] = piData;
            objectsArray.push_back(strippedJson);
            continue;
        }

        json objJson;
        objJson["name"] = obj->GetName();
        objJson["id"] = obj->GetID();
        objJson["tag"] = obj->GetTag();
        objJson["layer"] = obj->GetLayer();
        objJson["active"] = obj->IsActive();
        objJson["parentId"] = obj->GetParent()
            ? static_cast<int>(obj->GetParent()->GetID()) : -1;

        json componentsArray = json::array();
        for (auto* comp : obj->GetComponents()) {
            if (!comp) continue;
            json compJson;
            compJson["type"] = comp->GetTypeName();
            compJson["enabled"] = comp->IsEnabled();
            comp->Serialize(compJson);
            componentsArray.push_back(compJson);
        }
        objJson["components"] = componentsArray;
        objectsArray.push_back(objJson);
    }
    sceneJson["gameObjects"] = objectsArray;
    return sceneJson;
}

bool SceneSerializer::SaveScene(const std::string& filepath,
                                 const std::vector<std::shared_ptr<GameObject>>& objects) {
    json sceneJson = SerializeScene(objects, "Untitled Scene");
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[SceneSerializer] Failed to open file for writing: " << filepath << std::endl;
        return false;
    }
    file << sceneJson.dump(2);
    file.close();
    std::cout << "[SceneSerializer] Scene saved to: " << filepath << std::endl;
    return true;
}

bool SceneSerializer::DeserializeScene(
    const nlohmann::json& sceneJson,
    std::vector<std::shared_ptr<GameObject>>& objects) {
    objects.clear();

    if (!sceneJson.contains("gameObjects")) {
        std::cerr << "[SceneSerializer] No gameObjects in scene document" << std::endl;
        return false;
    }

    auto& factory = ComponentFactory::Get();
    struct LoadedObject { std::shared_ptr<GameObject> obj; int parentId; };
    std::vector<LoadedObject> loaded;

    for (const auto& objJson : sceneJson["gameObjects"]) {
        if (objJson.contains("prefabInstance")) {
            const auto& piData = objJson["prefabInstance"];
            std::string guid = piData.value("guid", "");
            unsigned int rootId = piData.value("rootId", 0u);
            int parentId = piData.value("parentId", -1);
            nlohmann::json modifications = piData.value("modifications", json::array());

            std::unordered_map<unsigned int, unsigned int> idRemap;
            std::vector<std::shared_ptr<GameObject>> instantiatedObjects;
            GameObject* root = PrefabRegistry::Get().Instantiate(guid, instantiatedObjects, idRemap);

            if (root) {
                unsigned int tempRootId = root->GetID();
                unsigned int localRootId = 0;
                for (const auto& [localId, tempId] : idRemap) {
                    if (tempId == tempRootId) {
                        localRootId = localId;
                        break;
                    }
                }

                root->SetID(rootId);
                idRemap[localRootId] = rootId;

                PrefabUtil::ApplyModifications(root, modifications, idRemap);

                auto* pi = root->AddComponent<PrefabInstance>();
                pi->SetPrefabGuid(guid);
                pi->SetModifications(modifications);
                pi->SetIdRemap(idRemap);

                for (auto& io : instantiatedObjects) {
                    if (io) {
                        int pId = (io.get() == root) ? parentId : -1;
                        loaded.push_back({io, pId});
                        objects.push_back(io);
                    }
                }
            }
        } else {
            std::string name = objJson.value("name", "GameObject");
            bool active = objJson.value("active", true);
            int parentId = objJson.value("parentId", -1);

            auto obj = std::make_shared<GameObject>(name);
            if (objJson.contains("id")) {
                obj->SetID(objJson["id"].get<unsigned int>());
            }
            obj->SetTag(objJson.value("tag", "Untagged"));
            obj->SetLayer(objJson.value("layer", 0));
            obj->SetActive(active);

            if (objJson.contains("components")) {
                for (const auto& compJson : objJson["components"]) {
                    std::string type = compJson.value("type", "");
                    Component* comp = factory.Create(type, obj.get());
                    if (!comp) {
                        auto script = ScriptManager::Get().CreateScript(type);
                        if (script) comp = obj->AddComponentRaw(script.release());
                    }
                    if (comp) {
                        comp->Deserialize(compJson);
                        if (compJson.contains("enabled"))
                            comp->SetEnabled(compJson["enabled"].get<bool>());
                    } else {
                        std::cerr << "[SceneSerializer] Unknown component type: " << type << std::endl;
                    }
                }
            }
            loaded.push_back({obj, parentId});
            objects.push_back(obj);
        }
    }

    std::unordered_map<unsigned int, GameObject*> idMap;
    for (auto& [obj, _] : loaded) idMap[obj->GetID()] = obj.get();
    for (auto& [obj, parentId] : loaded) {
        if (parentId >= 0) {
            auto it = idMap.find(static_cast<unsigned int>(parentId));
            if (it != idMap.end()) obj->SetParent(it->second);
        }
    }
    return true;
}

bool SceneSerializer::LoadScene(const std::string& filepath,
                                 std::vector<std::shared_ptr<GameObject>>& objects) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[SceneSerializer] Failed to open file: " << filepath << std::endl;
        return false;
    }
    json sceneJson;
    try {
        file >> sceneJson;
    } catch (const json::parse_error& e) {
        std::cerr << "[SceneSerializer] JSON parse error: " << e.what() << std::endl;
        return false;
    }
    file.close();

    bool ok = DeserializeScene(sceneJson, objects);
    if (ok) {
        std::cout << "[SceneSerializer] Scene loaded from: " << filepath
                  << " (" << objects.size() << " objects)" << std::endl;
    }
    return ok;
}

std::string SceneSerializer::SerializeGameObject(const GameObject* obj) {
    if (!obj) return "{}";

    json objJson;
    objJson["name"] = obj->GetName();
    objJson["id"] = obj->GetID();
    objJson["tag"] = obj->GetTag();
    objJson["layer"] = obj->GetLayer();
    objJson["active"] = obj->IsActive();
    objJson["parentId"] = obj->GetParent()
        ? static_cast<int>(obj->GetParent()->GetID())
        : -1;

    json componentsArray = json::array();

    // Serialize all components using the component interface
    for (auto* comp : obj->GetComponents()) {
        if (!comp) continue;

        json compJson;
        compJson["type"] = comp->GetTypeName();
        compJson["enabled"] = comp->IsEnabled();
        comp->Serialize(compJson);
        componentsArray.push_back(compJson);
    }

    objJson["components"] = componentsArray;
    return objJson.dump(2);
}

std::shared_ptr<GameObject> SceneSerializer::DeserializeGameObject(const std::string& jsonStr) {
    json objJson;
    try {
        objJson = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        std::cerr << "[SceneSerializer] JSON parse error: " << e.what() << std::endl;
        return nullptr;
    }

    std::string name = objJson.value("name", "GameObject");
    auto obj = std::make_shared<GameObject>(name);
    if (objJson.contains("id")) {
        obj->SetID(objJson["id"].get<unsigned int>());
    }
    obj->SetTag(objJson.value("tag", "Untagged"));
    obj->SetLayer(objJson.value("layer", 0));
    obj->SetActive(objJson.value("active", true));

    auto& factory = ComponentFactory::Get();

    if (objJson.contains("components")) {
        for (const auto& compJson : objJson["components"]) {
            std::string type = compJson.value("type", "");

            Component* comp = factory.Create(type, obj.get());
            if (!comp) {
                auto script = ScriptManager::Get().CreateScript(type);
                if (script) {
                    comp = obj->AddComponentRaw(script.release());
                }
            }
            if (comp) {
                comp->Deserialize(compJson);
                if (compJson.contains("enabled")) {
                    comp->SetEnabled(compJson["enabled"].get<bool>());
                }
            } else {
                std::cerr << "[SceneSerializer] Unknown component type: " << type << std::endl;
            }
        }
    }

    return obj;
}

nlohmann::json SceneSerializer::SerializeSubtree(const GameObject* root) {
    json doc;
    doc["version"] = "1.0";
    doc["name"] = root ? root->GetName() + "_Subtree" : "Subtree";

    json objectsArray = json::array();
    if (root) {
        std::vector<GameObject*> subtreeObjects;
        const_cast<GameObject*>(root)->CollectSubtree(subtreeObjects);

        for (auto* obj : subtreeObjects) {
            if (!obj) continue;

            // Descendants of a nested prefab instance belong to that prefab.
            if (obj != root && IsInsideNestedPrefab(obj, root)) {
                continue;
            }

            // A child that is itself a prefab instance is stored as a stripped
            // entry (guid + overrides). This keeps the link to the nested prefab
            // so edits to it propagate, instead of freezing a copy.
            if (obj != root) {
                if (auto* pi = obj->GetComponent<PrefabInstance>()) {
                    nlohmann::json prefabJson = PrefabRegistry::Get().GetPrefabJson(pi->GetPrefabGuid());
                    nlohmann::json mods = (!prefabJson.is_null())
                        ? PrefabUtil::GenerateModifications(obj, prefabJson, pi->GetIdRemap())
                        : pi->GetModifications();

                    json piData;
                    piData["guid"] = pi->GetPrefabGuid();
                    piData["rootId"] = obj->GetID();
                    piData["parentId"] = obj->GetParent()
                        ? static_cast<int>(obj->GetParent()->GetID()) : -1;
                    piData["modifications"] = mods;

                    json strippedJson;
                    strippedJson["prefabInstance"] = piData;
                    objectsArray.push_back(strippedJson);
                    continue;
                }
            }

            json objJson;
            objJson["name"] = obj->GetName();
            objJson["id"] = obj->GetID();
            objJson["tag"] = obj->GetTag();
            objJson["layer"] = obj->GetLayer();
            objJson["active"] = obj->IsActive();

            if (obj == root) {
                objJson["parentId"] = -1;
            } else {
                objJson["parentId"] = obj->GetParent()
                    ? static_cast<int>(obj->GetParent()->GetID()) : -1;
            }

            json componentsArray = json::array();
            for (auto* comp : obj->GetComponents()) {
                if (!comp) continue;
                // A prefab template never stores an instance link in its own
                // component list; nested instances are stored as stripped entries.
                if (comp->GetTypeName() == "PrefabInstance") continue;
                json compJson;
                compJson["type"] = comp->GetTypeName();
                compJson["enabled"] = comp->IsEnabled();
                comp->Serialize(compJson);
                componentsArray.push_back(compJson);
            }
            objJson["components"] = componentsArray;
            objectsArray.push_back(objJson);
        }
    }
    doc["gameObjects"] = objectsArray;
    return doc;
}

GameObject* SceneSerializer::DeserializeSubtreeRemapped(
    const nlohmann::json& doc,
    std::vector<std::shared_ptr<GameObject>>& outObjects,
    std::unordered_map<unsigned int, unsigned int>& idRemap) {

    if (!doc.contains("gameObjects")) {
        std::cerr << "[SceneSerializer] No gameObjects in subtree document" << std::endl;
        return nullptr;
    }

    // Guard against cyclic (self-referential) nested prefabs blowing the stack.
    static thread_local int s_nestDepth = 0;
    struct DepthScope {
        DepthScope() { ++s_nestDepth; }
        ~DepthScope() { --s_nestDepth; }
    } depthScope;
    constexpr int kMaxNestDepth = 32;
    if (s_nestDepth > kMaxNestDepth) {
        std::cerr << "[SceneSerializer] Prefab nesting too deep (possible cycle); aborting branch" << std::endl;
        return nullptr;
    }

    auto& factory = ComponentFactory::Get();
    struct LoadedObject { std::shared_ptr<GameObject> obj; int parentId; unsigned int originalId; };
    std::vector<LoadedObject> loaded;
    GameObject* rootObj = nullptr;

    for (const auto& objJson : doc["gameObjects"]) {
        // A nested prefab instance is stored stripped: recursively instantiate the
        // referenced prefab, apply its overrides, and re-attach the live link.
        if (objJson.contains("prefabInstance")) {
            const auto& piData = objJson["prefabInstance"];
            std::string nestedGuid = piData.value("guid", "");
            unsigned int localRootId = piData.value("rootId", 0u);
            int localParentId = piData.value("parentId", -1);
            nlohmann::json mods = piData.value("modifications", json::array());

            std::unordered_map<unsigned int, unsigned int> nestedRemap;
            size_t before = outObjects.size();
            GameObject* nestedRoot = PrefabRegistry::Get().Instantiate(nestedGuid, outObjects, nestedRemap);
            if (nestedRoot) {
                PrefabUtil::ApplyModifications(nestedRoot, mods, nestedRemap);

                auto* pi = nestedRoot->AddComponent<PrefabInstance>();
                pi->SetPrefabGuid(nestedGuid);
                pi->SetModifications(mods);
                pi->SetIdRemap(nestedRemap);

                // The nested instance occupies one local id in this prefab's id-space.
                idRemap[localRootId] = nestedRoot->GetID();

                // Recover the shared_ptr so the parent-linking pass can re-parent it.
                std::shared_ptr<GameObject> nestedRootPtr;
                for (size_t k = before; k < outObjects.size(); ++k) {
                    if (outObjects[k] && outObjects[k].get() == nestedRoot) {
                        nestedRootPtr = outObjects[k];
                        break;
                    }
                }
                if (nestedRootPtr) {
                    loaded.push_back({nestedRootPtr, localParentId, localRootId});
                }
            }
            continue;
        }

        std::string name = objJson.value("name", "GameObject");
        bool active = objJson.value("active", true);
        int parentId = objJson.value("parentId", -1);
        unsigned int originalId = objJson.value("id", 0u);

        auto obj = std::make_shared<GameObject>(name);
        
        // Record the localId -> newId mapping
        idRemap[originalId] = obj->GetID();

        obj->SetTag(objJson.value("tag", "Untagged"));
        obj->SetLayer(objJson.value("layer", 0));
        obj->SetActive(active);

        if (objJson.contains("components")) {
            for (const auto& compJson : objJson["components"]) {
                std::string type = compJson.value("type", "");
                Component* comp = factory.Create(type, obj.get());
                if (!comp) {
                    auto script = ScriptManager::Get().CreateScript(type);
                    if (script) comp = obj->AddComponentRaw(script.release());
                }
                if (comp) {
                    comp->Deserialize(compJson);
                    if (compJson.contains("enabled"))
                        comp->SetEnabled(compJson["enabled"].get<bool>());
                } else {
                    std::cerr << "[SceneSerializer] Unknown component type: " << type << std::endl;
                }
            }
        }
        loaded.push_back({obj, parentId, originalId});
        outObjects.push_back(obj);

        if (parentId == -1 && !rootObj) {
            rootObj = obj.get();
        }
    }

    std::unordered_map<unsigned int, GameObject*> idMap;
    for (auto& [obj, _, __] : loaded) {
        idMap[obj->GetID()] = obj.get();
    }

    for (auto& [obj, parentId, originalId] : loaded) {
        if (parentId >= 0) {
            auto remapIt = idRemap.find(static_cast<unsigned int>(parentId));
            if (remapIt != idRemap.end()) {
                unsigned int remappedParentId = remapIt->second;
                auto it = idMap.find(remappedParentId);
                if (it != idMap.end()) {
                    obj->SetParent(it->second);
                }
            }
        }
    }

    return rootObj;
}

