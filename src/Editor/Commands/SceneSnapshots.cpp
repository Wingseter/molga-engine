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

namespace {

struct ComponentIdentity {
    size_t typeId = 0;
    std::uint64_t instanceId = 0;
};

struct ObjectLease {
    GameObject* object = nullptr;
    unsigned int id = 0;
    bool editorOwned = false;
    std::shared_ptr<GameObject> hold;

    GameObject* Resolve() const {
        if (!object) return nullptr;
        if (!editorOwned) return object;
        GameObject* current = Editor::Get().FindObjectById(id);
        return current == object ? current : nullptr;
    }
};

ObjectLease HoldObject(GameObject* object) {
    ObjectLease lease;
    lease.object = object;
    if (!object) return lease;
    lease.id = object->GetID();
    lease.hold = Editor::Get().ShareObjectById(lease.id);
    lease.editorOwned = lease.hold && lease.hold.get() == object;
    if (!lease.editorOwned) lease.hold.reset();
    return lease;
}

std::vector<ComponentIdentity> SnapshotComponentIdentities(GameObject* object) {
    std::vector<ComponentIdentity> plan;
    if (!object) return plan;
    for (Component* component : object->GetComponents()) {
        if (component) {
            plan.push_back(
                {component->GetRuntimeTypeID(), component->GetInstanceID()});
        }
    }
    return plan;
}

Component* ResolveComponentIdentity(GameObject* object, size_t typeId,
                                    std::uint64_t instanceId) {
    if (!object) return nullptr;
    for (Component* component : object->GetComponents()) {
        if (component && component->GetRuntimeTypeID() == typeId &&
            component->GetInstanceID() == instanceId) {
            return component;
        }
    }
    return nullptr;
}

void RestoreResolvedComponent(const ObjectLease& lease, Component* component,
                              const nlohmann::json& snapshot) {
    GameObject* object = lease.Resolve();
    if (!object || !component) return;
    const size_t typeId = component->GetRuntimeTypeID();
    const std::uint64_t instanceId = component->GetInstanceID();

    component->Deserialize(snapshot);
    object = lease.Resolve();
    if (!object) return;
    component = ResolveComponentIdentity(object, typeId, instanceId);
    if (!component) return;

    if (snapshot.contains("enabled")) {
        component->SetEnabled(snapshot["enabled"].get<bool>());
        object = lease.Resolve();
        if (!object) return;
        component = ResolveComponentIdentity(object, typeId, instanceId);
    }
    if (component) component->ResolveAssets();
}

} // namespace

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
    const ObjectLease lease = HoldObject(obj);
    obj = lease.Resolve();
    if (!obj) return;
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

    RestoreResolvedComponent(lease, targetComp, compJson);
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
    ObjectLease lease = HoldObject(const_cast<GameObject*>(obj));
    GameObject* current = lease.Resolve();
    if (!current) return arr;
    const auto plan = SnapshotComponentIdentities(current);
    for (const auto& identity : plan) {
        current = lease.Resolve();
        if (!current) break;
        Component* component = ResolveComponentIdentity(
            current, identity.typeId, identity.instanceId);
        if (component) arr.push_back(CaptureComponentSnapshot(component));
    }
    return arr;
}

void RestoreGameObjectComponents(GameObject* obj, const nlohmann::json& componentsJson) {
    if (!obj || !componentsJson.is_array()) return;
    const ObjectLease lease = HoldObject(obj);
    obj = lease.Resolve();
    if (!obj) return;

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

        RestoreResolvedComponent(lease, targetComp, compJson);
        obj = lease.Resolve();
        if (!obj) return;
    }

    // Remove components that are not in the snapshot (excluding Transform)
    const auto removalPlan = SnapshotComponentIdentities(obj);
    for (const auto& identity : removalPlan) {
        obj = lease.Resolve();
        if (!obj) break;
        Component* component = ResolveComponentIdentity(
            obj, identity.typeId, identity.instanceId);
        if (!component) continue;
        const std::string componentType = component->GetTypeName();
        if (componentType != "Transform" &&
            restoredTypes.find(componentType) == restoredTypes.end()) {
            obj->RemoveComponentById(identity.typeId);
        }
    }
}

} // namespace molga
