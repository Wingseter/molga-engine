#include "Editor/Commands/TransformCommand.h"
#include "Core/PrefabUtil.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"

// Editor 의존은 약하게: world_ == nullptr일 때만 사용한다.
#include "Editor/Editor.h"

namespace molga {

TransformCommand::TransformCommand(World* world, unsigned int targetId,
                                   const TransformState& before, const TransformState& after)
    : world_(world), targetId_(targetId), before_(before), after_(after) {}

TransformState TransformCommand::Capture(const Transform* tr) {
    return { tr->GetPosition(), tr->GetRotation(), tr->GetScale() };
}

Transform* TransformCommand::Resolve() const {
    GameObject* go = nullptr;
    if (world_) {
        go = world_->FindById(targetId_);
    } else {
        go = Editor::Get().FindObjectById(targetId_);
    }
    return go ? go->GetComponent<Transform>() : nullptr;
}

void TransformCommand::ApplyTo(const TransformState& s) {
    if (Transform* tr = Resolve()) {
        tr->SetPosition(s.position);
        tr->SetRotation(s.rotation);
        tr->SetScale(s.scale);
        if (!world_) {
            PrefabUtil::RefreshNearestInstanceOverrides({tr->GetGameObject()});
            Editor::Get().MarkSceneModified();
        }
    }
}

void TransformCommand::Execute() { ApplyTo(after_); }
void TransformCommand::Undo()    { ApplyTo(before_); }

MultiTransformCommand::MultiTransformCommand(
    World* world, std::vector<MultiTransformEntry> entries)
    : world_(world), entries_(std::move(entries)) {}

Transform* MultiTransformCommand::Resolve(unsigned int id) const {
    GameObject* object = world_ ? world_->FindById(id)
                                : Editor::Get().FindObjectById(id);
    return object ? object->GetComponent<Transform>() : nullptr;
}

void MultiTransformCommand::Apply(bool after) {
    bool changed = false;
    std::vector<GameObject*> changedObjects;
    changedObjects.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (Transform* transform = Resolve(entry.targetId)) {
            const TransformState& state = after ? entry.after : entry.before;
            transform->SetPosition(state.position);
            transform->SetRotation(state.rotation);
            transform->SetScale(state.scale);
            changed = true;
            changedObjects.push_back(transform->GetGameObject());
        }
    }
    if (changed && !world_) {
        PrefabUtil::RefreshNearestInstanceOverrides(changedObjects);
        Editor::Get().MarkSceneModified();
    }
}

void MultiTransformCommand::Execute() { Apply(true); }
void MultiTransformCommand::Undo() { Apply(false); }

} // namespace molga
