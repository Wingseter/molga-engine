#include "SceneSerializer.h"
#include "../ECS/GameObject.h"
#include "../ECS/Component.h"
#include "../ECS/ComponentFactory.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../ECS/Components/BoxCollider2D.h"
#include "../Scripting/ScriptManager.h"
#include "../Scripting/Script.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

nlohmann::json SceneSerializer::SerializeScene(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const std::string& sceneName) {
    json sceneJson;
    sceneJson["version"] = "1.0";
    sceneJson["name"] = sceneName;

    json objectsArray = json::array();
    for (const auto& obj : objects) {
        if (!obj) continue;
        json objJson;
        objJson["name"] = obj->GetName();
        objJson["id"] = obj->GetID();
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
        std::string name = objJson.value("name", "GameObject");
        bool active = objJson.value("active", true);
        int parentId = objJson.value("parentId", -1);

        auto obj = std::make_shared<GameObject>(name);
        if (objJson.contains("id")) {
            obj->SetID(objJson["id"].get<unsigned int>());
        }
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
