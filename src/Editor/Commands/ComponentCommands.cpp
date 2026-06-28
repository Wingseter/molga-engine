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
    GameObject* obj = FindGameObjectById(targetId_);
    if (!obj) return;

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

    if (comp) {
        if (!addedSnap_.is_null() && !addedSnap_.empty()) {
            comp->Deserialize(addedSnap_);
            if (addedSnap_.contains("enabled")) {
                comp->SetEnabled(addedSnap_["enabled"].get<bool>());
            }
            comp->ResolveAssets();
        } else {
            addedSnap_ = CaptureComponentSnapshot(comp);
        }
        Editor::Get().MarkSceneModified();
    }
}

void ComponentAddCommand::Undo() {
    GameObject* obj = FindGameObjectById(targetId_);
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
    GameObject* obj = FindGameObjectById(targetId_);
    if (!obj) return;

    Component* comp = nullptr;
    for (auto* c : obj->GetComponents()) {
        if (c && c->GetTypeName() == componentType_) {
            comp = c;
            break;
        }
    }

    if (comp) {
        removedSnap_ = CaptureComponentSnapshot(comp);
        obj->RemoveComponentById(comp->GetRuntimeTypeID());
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
