#include "SceneSerializer.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../ECS/Components/BoxCollider2D.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

// Local helper functions
static void SerializeTransform(json& j, const Transform* transform) {
    j["position"] = { transform->GetX(), transform->GetY() };
    j["rotation"] = transform->GetRotation();
    j["scale"] = { transform->GetScale().x, transform->GetScale().y };
}

static void SerializeSpriteRenderer(json& j, const SpriteRenderer* sr) {
    const Color& c = sr->GetColor();
    j["color"] = { c.r, c.g, c.b, c.a };
    j["size"] = { sr->GetWidth(), sr->GetHeight() };
    j["flipX"] = sr->GetFlipX();
    j["flipY"] = sr->GetFlipY();
    j["sortingOrder"] = sr->GetSortingOrder();
    j["texturePath"] = sr->GetTexturePath();
}

static void SerializeBoxCollider2D(json& j, const BoxCollider2D* col) {
    j["size"] = { col->GetSize().x, col->GetSize().y };
    j["offset"] = { col->GetOffset().x, col->GetOffset().y };
    j["isTrigger"] = col->IsTrigger();
}

static void DeserializeTransform(const json& j, Transform* transform) {
    if (j.contains("position") && j["position"].is_array()) {
        transform->SetPosition(j["position"][0], j["position"][1]);
    }
    if (j.contains("rotation")) {
        transform->SetRotation(j["rotation"]);
    }
    if (j.contains("scale") && j["scale"].is_array()) {
        transform->SetScale(j["scale"][0], j["scale"][1]);
    }
}

static void DeserializeSpriteRenderer(const json& j, SpriteRenderer* sr) {
    if (j.contains("color") && j["color"].is_array()) {
        sr->SetColor(j["color"][0], j["color"][1], j["color"][2], j["color"][3]);
    }
    if (j.contains("size") && j["size"].is_array()) {
        sr->SetSize(j["size"][0], j["size"][1]);
    }
    if (j.contains("flipX")) {
        sr->SetFlipX(j["flipX"]);
    }
    if (j.contains("flipY")) {
        sr->SetFlipY(j["flipY"]);
    }
    if (j.contains("sortingOrder")) {
        sr->SetSortingOrder(j["sortingOrder"]);
    }
    if (j.contains("texturePath")) {
        sr->SetTexturePath(j["texturePath"]);
    }
}

static void DeserializeBoxCollider2D(const json& j, BoxCollider2D* col) {
    if (j.contains("size") && j["size"].is_array()) {
        col->SetSize(j["size"][0], j["size"][1]);
    }
    if (j.contains("offset") && j["offset"].is_array()) {
        col->SetOffset(j["offset"][0], j["offset"][1]);
    }
    if (j.contains("isTrigger")) {
        col->SetTrigger(j["isTrigger"]);
    }
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

        // Serialize Transform
        if (auto* transform = obj->GetComponent<Transform>()) {
            json compJson;
            compJson["type"] = "Transform";
            SerializeTransform(compJson, transform);
            componentsArray.push_back(compJson);
        }

        // Serialize SpriteRenderer
        if (auto* sr = obj->GetComponent<SpriteRenderer>()) {
            json compJson;
            compJson["type"] = "SpriteRenderer";
            SerializeSpriteRenderer(compJson, sr);
            componentsArray.push_back(compJson);
        }

        // Serialize BoxCollider2D
        if (auto* col = obj->GetComponent<BoxCollider2D>()) {
            json compJson;
            compJson["type"] = "BoxCollider2D";
            SerializeBoxCollider2D(compJson, col);
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

    for (const auto& objJson : sceneJson["gameObjects"]) {
        std::string name = objJson.value("name", "GameObject");
        bool active = objJson.value("active", true);

        auto obj = std::make_shared<GameObject>(name);
        obj->SetActive(active);

        // Load components
        if (objJson.contains("components")) {
            for (const auto& compJson : objJson["components"]) {
                std::string type = compJson.value("type", "");

                if (type == "Transform") {
                    auto* transform = obj->AddComponent<Transform>();
                    DeserializeTransform(compJson, transform);
                }
                else if (type == "SpriteRenderer") {
                    auto* sr = obj->AddComponent<SpriteRenderer>();
                    DeserializeSpriteRenderer(compJson, sr);
                }
                else if (type == "BoxCollider2D") {
                    auto* col = obj->AddComponent<BoxCollider2D>();
                    DeserializeBoxCollider2D(compJson, col);
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

    if (auto* transform = obj->GetComponent<Transform>()) {
        json compJson;
        compJson["type"] = "Transform";
        SerializeTransform(compJson, transform);
        componentsArray.push_back(compJson);
    }

    if (auto* sr = obj->GetComponent<SpriteRenderer>()) {
        json compJson;
        compJson["type"] = "SpriteRenderer";
        SerializeSpriteRenderer(compJson, sr);
        componentsArray.push_back(compJson);
    }

    if (auto* col = obj->GetComponent<BoxCollider2D>()) {
        json compJson;
        compJson["type"] = "BoxCollider2D";
        SerializeBoxCollider2D(compJson, col);
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

    if (objJson.contains("components")) {
        for (const auto& compJson : objJson["components"]) {
            std::string type = compJson.value("type", "");

            if (type == "Transform") {
                auto* transform = obj->AddComponent<Transform>();
                DeserializeTransform(compJson, transform);
            }
            else if (type == "SpriteRenderer") {
                auto* sr = obj->AddComponent<SpriteRenderer>();
                DeserializeSpriteRenderer(compJson, sr);
            }
            else if (type == "BoxCollider2D") {
                auto* col = obj->AddComponent<BoxCollider2D>();
                DeserializeBoxCollider2D(compJson, col);
            }
        }
    }

    return obj;
}
