#include "ComponentCommands.h"
#include "SceneSnapshots.h"
#include "ECS/GameObject.h"
#include "ECS/Component.h"
#include "ECS/Components/Transform.h"
#include "ECS/ComponentFactory.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/Script.h"
#include "Editor/Editor.h"
#include <iostream>

namespace molga {

namespace {

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

} // namespace

// --- ComponentSnapshotCommand ---

ComponentSnapshotCommand::ComponentSnapshotCommand(unsigned int targetId,
                                                 const std::string& componentType,
                                                 const nlohmann::json& beforeSnap,
                                                 const nlohmann::json& afterSnap)
    : targetId_(targetId), componentType_(componentType), beforeSnap_(beforeSnap), afterSnap_(afterSnap) {}

void ComponentSnapshotCommand::Execute() {
    GameObject* obj = FindGameObjectById(targetId_);
    if (obj) {
        RestoreComponentSnapshot(obj, afterSnap_);
        Editor::Get().MarkSceneModified();
    }
}

void ComponentSnapshotCommand::Undo() {
    GameObject* obj = FindGameObjectById(targetId_);
    if (obj) {
        RestoreComponentSnapshot(obj, beforeSnap_);
        Editor::Get().MarkSceneModified();
    }
}

// --- ComponentAddCommand ---

ComponentAddCommand::ComponentAddCommand(unsigned int targetId, const std::string& componentType)
    : targetId_(targetId), componentType_(componentType) {}

void ComponentAddCommand::Execute() {
    std::shared_ptr<GameObject> object = Editor::Get().ShareObjectById(targetId_);
    GameObject* obj = object ? object.get() : FindGameObjectById(targetId_);
    if (!obj) return;
    auto ownerStillCurrent = [&]() {
        return !object || FindGameObjectById(targetId_) == obj;
    };

    Component* comp = nullptr;
    for (auto* c : obj->GetComponents()) {
        if (c && c->GetTypeName() == componentType_) {
            comp = c;
            break;
        }
    }

    if (!comp) {
        auto& factory = ComponentFactory::Get();
        comp = factory.Create(componentType_, obj);
        if (!comp) {
            auto script = ScriptManager::Get().CreateScript(componentType_);
            if (script) {
                comp = obj->AddComponentRaw(script.release());
            }
        }
    }

    if (comp && ownerStillCurrent()) {
        if (!addedSnap_.is_null() && !addedSnap_.empty()) {
            const size_t typeId = comp->GetRuntimeTypeID();
            const std::uint64_t instanceId = comp->GetInstanceID();
            comp->Deserialize(addedSnap_);
            comp = ownerStillCurrent()
                ? ResolveComponentIdentity(obj, typeId, instanceId) : nullptr;
            if (comp && addedSnap_.contains("enabled")) {
                comp->SetEnabled(addedSnap_["enabled"].get<bool>());
                comp = ownerStillCurrent()
                    ? ResolveComponentIdentity(obj, typeId, instanceId) : nullptr;
            }
            if (comp) comp->ResolveAssets();
        } else {
            addedSnap_ = CaptureComponentSnapshot(comp);
        }
        Editor::Get().MarkSceneModified();
    }
}

void ComponentAddCommand::Undo() {
    std::shared_ptr<GameObject> object = Editor::Get().ShareObjectById(targetId_);
    GameObject* obj = object ? object.get() : FindGameObjectById(targetId_);
    if (!obj) return;

    Component* comp = nullptr;
    for (auto* c : obj->GetComponents()) {
        if (c && c->GetTypeName() == componentType_) {
            comp = c;
            break;
        }
    }

    if (comp) {
        obj->RemoveComponentById(comp->GetRuntimeTypeID());
        Editor::Get().MarkSceneModified();
    }
}

// --- ComponentRemoveCommand ---

ComponentRemoveCommand::ComponentRemoveCommand(unsigned int targetId, const std::string& componentType)
    : targetId_(targetId), componentType_(componentType) {}

void ComponentRemoveCommand::Execute() {
    std::shared_ptr<GameObject> object = Editor::Get().ShareObjectById(targetId_);
    GameObject* obj = object ? object.get() : FindGameObjectById(targetId_);
    if (!obj) return;

    Component* comp = nullptr;
    for (auto* c : obj->GetComponents()) {
        if (c && c->GetTypeName() == componentType_) {
            comp = c;
            break;
        }
    }

    if (comp) {
        const size_t typeId = comp->GetRuntimeTypeID();
        const std::uint64_t instanceId = comp->GetInstanceID();
        removedSnap_ = CaptureComponentSnapshot(comp);
        if ((!object || FindGameObjectById(targetId_) == obj) &&
            ResolveComponentIdentity(obj, typeId, instanceId)) {
            obj->RemoveComponentById(typeId);
        }
        Editor::Get().MarkSceneModified();
    }
}

void ComponentRemoveCommand::Undo() {
    GameObject* obj = FindGameObjectById(targetId_);
    if (!obj || removedSnap_.is_null() || removedSnap_.empty()) return;

    RestoreComponentSnapshot(obj, removedSnap_);
    Editor::Get().MarkSceneModified();
}

// --- CreateObjectWithComponentsCommand ---

CreateObjectWithComponentsCommand::CreateObjectWithComponentsCommand(
    const std::string& name, const std::vector<std::string>& componentTypes)
    : name_(name), componentTypes_(componentTypes) {}

void CreateObjectWithComponentsCommand::Execute() {
    if (!object_) {
        object_ = std::make_shared<GameObject>(name_);
        object_->AddComponent<Transform>();
        if (id_ != 0) {
            object_->SetID(id_);
        } else {
            id_ = object_->GetID();
        }

        auto& factory = ComponentFactory::Get();
        for (const auto& type : componentTypes_) {
            if (type == "Transform") continue;
            Component* comp = factory.Create(type, object_.get());
            if (!comp) {
                auto script = ScriptManager::Get().CreateScript(type);
                if (script) {
                    comp = object_->AddComponentRaw(script.release());
                }
            }
            if (comp) {
                comp->ResolveAssets();
            }
        }
    }
    Editor::Get().AddExistingObject(object_);
    Editor::Get().MarkSceneModified();
}

void CreateObjectWithComponentsCommand::Undo() {
    if (object_) {
        Editor::Get().RemoveObjectsByIds({ object_->GetID() });
        Editor::Get().MarkSceneModified();
    }
}

} // namespace molga
