#include "ComponentCommands.h"
#include "SceneSnapshots.h"
#include "ECS/GameObject.h"
#include "ECS/Component.h"
#include "ECS/Components/Transform.h"
#include "ECS/ComponentFactory.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/Script.h"
#include "Editor/Editor.h"
#include "Core/PrefabUtil.h"
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

void RefreshNearestPrefabOverrides(const std::vector<unsigned int>& targetIds) {
    std::vector<GameObject*> targets;
    targets.reserve(targetIds.size());
    for (unsigned int targetId : targetIds) {
        if (GameObject* target = FindGameObjectById(targetId)) {
            targets.push_back(target);
        }
    }
    PrefabUtil::RefreshNearestInstanceOverrides(targets);
}

} // namespace

std::vector<ComponentSnapshotChange> CaptureAppliedComponentChanges(
    const std::vector<ComponentSnapshotBaseline>& baselines) {
    std::vector<ComponentSnapshotChange> changes;
    changes.reserve(baselines.size());
    for (const ComponentSnapshotBaseline& baseline : baselines) {
        GameObject* object = FindGameObjectById(baseline.targetId);
        Component* component = ResolveComponentIdentity(
            object, baseline.runtimeTypeId, baseline.instanceId);
        if (!component || component->GetTypeName() != baseline.componentType) continue;

        nlohmann::json after = CaptureComponentSnapshot(component);
        // Serialize() is an extension point and may replace a component while
        // producing the snapshot. Do not attach the old snapshot to the new
        // same-type instance.
        component = ResolveComponentIdentity(
            FindGameObjectById(baseline.targetId), baseline.runtimeTypeId,
            baseline.instanceId);
        if (!component || component->GetTypeName() != baseline.componentType) continue;
        if (after != baseline.before) {
            changes.push_back({baseline.targetId, baseline.componentType,
                               baseline.before, std::move(after)});
        }
    }
    return changes;
}

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
        RefreshNearestPrefabOverrides({targetId_});
        Editor::Get().MarkSceneModified();
    }
}

void ComponentSnapshotCommand::Undo() {
    GameObject* obj = FindGameObjectById(targetId_);
    if (obj) {
        RestoreComponentSnapshot(obj, beforeSnap_);
        RefreshNearestPrefabOverrides({targetId_});
        Editor::Get().MarkSceneModified();
    }
}

// --- BatchComponentSnapshotCommand ---

BatchComponentSnapshotCommand::BatchComponentSnapshotCommand(
    std::vector<ComponentSnapshotChange> changes, bool valuesAlreadyApplied)
    : changes_(std::move(changes)), valuesAlreadyApplied_(valuesAlreadyApplied) {}

void BatchComponentSnapshotCommand::FinalizeAppliedTargets() {
    std::vector<unsigned int> changedTargets;
    changedTargets.reserve(changes_.size());
    for (const auto& change : changes_) {
        if (FindGameObjectById(change.targetId)) changedTargets.push_back(change.targetId);
    }
    if (!changedTargets.empty()) {
        RefreshNearestPrefabOverrides(changedTargets);
        Editor::Get().MarkSceneModified();
    }
}

void BatchComponentSnapshotCommand::Apply(bool after) {
    std::vector<unsigned int> changedTargets;
    changedTargets.reserve(changes_.size());
    for (const auto& change : changes_) {
        GameObject* object = FindGameObjectById(change.targetId);
        if (!object) continue;
        RestoreComponentSnapshot(object, after ? change.after : change.before);
        changedTargets.push_back(change.targetId);
    }
    if (!changedTargets.empty()) {
        RefreshNearestPrefabOverrides(changedTargets);
        Editor::Get().MarkSceneModified();
    }
}

void BatchComponentSnapshotCommand::Execute() {
    if (valuesAlreadyApplied_) {
        // Inspector gestures are previewed live. Adopting that state avoids a
        // restore/reapply cycle, which would invoke SetEnabled lifecycle hooks
        // three times for one click. Redo uses the normal Apply path.
        valuesAlreadyApplied_ = false;
        FinalizeAppliedTargets();
        return;
    }
    Apply(true);
}
void BatchComponentSnapshotCommand::Undo() { Apply(false); }

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
