#include "SceneSerializer.h"
#include "../ECS/GameObject.h"
#include "../ECS/Component.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../ECS/Components/BoxCollider2D.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <functional>
#include <unordered_map>

using json = nlohmann::json;

// Component factory for deserialization
using ComponentFactory = std::function<Component*(GameObject*)>;

static std::unordered_map<std::string, ComponentFactory>& GetComponentFactories() {
    static std::unordered_map<std::string, ComponentFactory> factories = {
        {"Transform", [](GameObject* obj) { return obj->AddComponent<Transform>(); }},
        {"SpriteRenderer", [](GameObject* obj) { return obj->AddComponent<SpriteRenderer>(); }},
        {"BoxCollider2D", [](GameObject* obj) { return obj->AddComponent<BoxCollider2D>(); }}
    };
    return factories;
}

bool SceneSerializer::SaveScene(const std::string& filepath,
                                 const std::vector<std::shared_ptr<GameObject>>& objects) {
    json sceneJson;
    sceneJson["version"] = "1.0";
    sceneJson["name"] = "Untitled Scene";

    json objectsArray = json::array();

    for (const auto& obj : objects) {
        if (!obj) continue;

        json objJson;
        objJson["name"] = obj->GetName();
        objJson["id"] = obj->GetID();
        objJson["active"] = obj->IsActive();

        json componentsArray = json::array();

        // Serialize all components using the component interface
        for (const auto& comp : obj->GetComponents()) {
            if (!comp) continue;

            json compJson;
            compJson["type"] = comp->GetTypeName();
            comp->Serialize(compJson);
            componentsArray.push_back(compJson);
        }

        objJson["components"] = componentsArray;
        objectsArray.push_back(objJson);
    }

    sceneJson["gameObjects"] = objectsArray;

    // Write to file
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[SceneSerializer] Failed to open file for writing: " << filepath << std::endl;
        return false;
    }

    file << sceneJson.dump(2);  // Pretty print with 2-space indent
    file.close();

    std::cout << "[SceneSerializer] Scene saved to: " << filepath << std::endl;
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

    // Clear existing objects
    objects.clear();

    // Load GameObjects
    if (!sceneJson.contains("gameObjects")) {
        std::cerr << "[SceneSerializer] No gameObjects in scene file" << std::endl;
        return false;
    }

    auto& factories = GetComponentFactories();

    for (const auto& objJson : sceneJson["gameObjects"]) {
        std::string name = objJson.value("name", "GameObject");
        bool active = objJson.value("active", true);

        auto obj = std::make_shared<GameObject>(name);
        obj->SetActive(active);

        // Load components using factory and deserialize
        if (objJson.contains("components")) {
            for (const auto& compJson : objJson["components"]) {
                std::string type = compJson.value("type", "");

                auto factoryIt = factories.find(type);
                if (factoryIt != factories.end()) {
                    Component* comp = factoryIt->second(obj.get());
                    if (comp) {
                        comp->Deserialize(compJson);
                    }
                } else {
                    std::cerr << "[SceneSerializer] Unknown component type: " << type << std::endl;
                }
            }
        }

        objects.push_back(obj);
    }

    std::cout << "[SceneSerializer] Scene loaded from: " << filepath
              << " (" << objects.size() << " objects)" << std::endl;
    return true;
}

std::string SceneSerializer::SerializeGameObject(const GameObject* obj) {
    if (!obj) return "{}";

    json objJson;
    objJson["name"] = obj->GetName();
    objJson["id"] = obj->GetID();
    objJson["active"] = obj->IsActive();

    json componentsArray = json::array();

    // Serialize all components using the component interface
    for (const auto& comp : obj->GetComponents()) {
        if (!comp) continue;

        json compJson;
        compJson["type"] = comp->GetTypeName();
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
    obj->SetActive(objJson.value("active", true));

    auto& factories = GetComponentFactories();

    if (objJson.contains("components")) {
        for (const auto& compJson : objJson["components"]) {
            std::string type = compJson.value("type", "");

            auto factoryIt = factories.find(type);
            if (factoryIt != factories.end()) {
                Component* comp = factoryIt->second(obj.get());
                if (comp) {
                    comp->Deserialize(compJson);
                }
            } else {
                std::cerr << "[SceneSerializer] Unknown component type: " << type << std::endl;
            }
        }
    }

    return obj;
}
