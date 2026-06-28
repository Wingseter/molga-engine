#include "SceneSnapshots.h"
#include "Editor/Editor.h"
#include "ECS/GameObject.h"
#include "ECS/Component.h"
#include "ECS/ComponentFactory.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/Script.h"
#include <unordered_set>
#include <iostream>

namespace molga {

GameObject* FindGameObjectById(unsigned int id) {
    return Editor::Get().FindObjectById(id);
}

nlohmann::json CaptureComponentSnapshot(const Component* comp) {
    nlohmann::json j;
    if (!comp) return j;
    j["type"] = comp->GetTypeName();
    j["enabled"] = comp->IsEnabled();
    comp->Serialize(j);
    return j;
}

void RestoreComponentSnapshot(GameObject* obj, const nlohmann::json& compJson) {
    if (!obj || compJson.is_null()) return;
    std::string type = compJson.value("type", "");
    if (type.empty()) return;

    Component* targetComp = nullptr;
    for (auto* c : obj->GetComponents()) {
        if (c && c->GetTypeName() == type) {
            targetComp = c;
            break;
        }
    }

    if (!targetComp) {
        auto& factory = ComponentFactory::Get();
        targetComp = factory.Create(type, obj);
        if (!targetComp) {
            auto script = ScriptManager::Get().CreateScript(type);
            if (script) {
                targetComp = obj->AddComponentRaw(script.release());
            }
        }
    }

    if (targetComp) {
        targetComp->Deserialize(compJson);
        if (compJson.contains("enabled")) {
            targetComp->SetEnabled(compJson["enabled"].get<bool>());
        }
        targetComp->ResolveAssets();
    }
}

nlohmann::json CaptureGameObjectProperties(const GameObject* obj) {
    nlohmann::json j;
    if (!obj) return j;
    j["name"] = obj->GetName();
    j["tag"] = obj->GetTag();
    j["layer"] = obj->GetLayer();
    j["active"] = obj->IsActive();
    return j;
}

void RestoreGameObjectProperties(GameObject* obj, const nlohmann::json& propJson) {
    if (!obj || propJson.is_null()) return;
    if (propJson.contains("name")) obj->SetName(propJson["name"].get<std::string>());
    if (propJson.contains("tag")) obj->SetTag(propJson["tag"].get<std::string>());
    if (propJson.contains("layer")) obj->SetLayer(propJson["layer"].get<int>());
    if (propJson.contains("active")) obj->SetActive(propJson["active"].get<bool>());
}

nlohmann::json CaptureGameObjectComponents(const GameObject* obj) {
    nlohmann::json arr = nlohmann::json::array();
    if (!obj) return arr;
    for (auto* comp : obj->GetComponents()) {
        if (!comp) continue;
        arr.push_back(CaptureComponentSnapshot(comp));
    }
    return arr;
}

void RestoreGameObjectComponents(GameObject* obj, const nlohmann::json& componentsJson) {
    if (!obj || !componentsJson.is_array()) return;

    std::unordered_set<std::string> restoredTypes;
    for (const auto& compJson : componentsJson) {
        std::string type = compJson.value("type", "");
        if (type.empty()) continue;
        restoredTypes.insert(type);

        Component* targetComp = nullptr;
        for (auto* c : obj->GetComponents()) {
            if (c && c->GetTypeName() == type) {
                targetComp = c;
                break;
            }
        }

        if (!targetComp) {
            auto& factory = ComponentFactory::Get();
            targetComp = factory.Create(type, obj);
            if (!targetComp) {
                auto script = ScriptManager::Get().CreateScript(type);
                if (script) {
                    targetComp = obj->AddComponentRaw(script.release());
                }
            }
        }

        if (targetComp) {
            targetComp->Deserialize(compJson);
            if (compJson.contains("enabled")) {
                targetComp->SetEnabled(compJson["enabled"].get<bool>());
            }
            targetComp->ResolveAssets();
        }
    }

    // Remove components that are not in the snapshot (excluding Transform)
    std::vector<Component*> currentComps = obj->GetComponents();
    for (auto* c : currentComps) {
        if (c && c->GetTypeName() != "Transform" && restoredTypes.find(c->GetTypeName()) == restoredTypes.end()) {
            obj->RemoveComponentById(c->GetRuntimeTypeID());
        }
    }
}

} // namespace molga
