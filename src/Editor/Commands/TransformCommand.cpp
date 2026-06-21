#include "Editor/Commands/TransformCommand.h"
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
        if (!world_) Editor::Get().MarkSceneModified();
    }
}

void TransformCommand::Execute() { ApplyTo(after_); }
void TransformCommand::Undo()    { ApplyTo(before_); }

} // namespace molga
