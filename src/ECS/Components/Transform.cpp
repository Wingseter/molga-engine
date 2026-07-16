#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include <nlohmann/json.hpp>

REGISTER_COMPONENT(Transform)
#ifdef MOLGA_EDITOR
#include <imgui.h>
#include "Editor/Editor.h"
#include "Editor/Commands/TransformCommand.h"
#endif

using json = nlohmann::json;

Vector2 Transform::GetWorldPosition() const {
    Vector2 worldPos = position;

    if (gameObject && gameObject->GetParent()) {
        Transform* parentTransform = gameObject->GetParent()->GetComponent<Transform>();
        if (parentTransform) {
            Vector2 parentWorldPos = parentTransform->GetWorldPosition();
            Vector2 parentScale = parentTransform->GetWorldScale();
            float parentRot = parentTransform->GetWorldRotation();

            // Apply parent scale
            float scaledX = position.x * parentScale.x;
            float scaledY = position.y * parentScale.y;

            // Apply parent rotation
            float radians = parentRot * 3.14159265f / 180.0f;
            float cosA = std::cos(radians);
            float sinA = std::sin(radians);

            worldPos.x = scaledX * cosA - scaledY * sinA + parentWorldPos.x;
            worldPos.y = scaledX * sinA + scaledY * cosA + parentWorldPos.y;
        }
    }

    return worldPos;
}

void Transform::SetWorldPosition(const Vector2& worldPosition) {
    if (!gameObject || !gameObject->GetParent()) {
        position = worldPosition;
        return;
    }

    Transform* parentTransform = gameObject->GetParent()->GetComponent<Transform>();
    if (!parentTransform) {
        position = worldPosition;
        return;
    }

    Vector2 local = worldPosition - parentTransform->GetWorldPosition();
    const float radians = -parentTransform->GetWorldRotation() * 3.14159265f / 180.0f;
    const float cosA = std::cos(radians);
    const float sinA = std::sin(radians);
    const Vector2 unrotated(local.x * cosA - local.y * sinA,
                            local.x * sinA + local.y * cosA);
    const Vector2 parentScale = parentTransform->GetWorldScale();
    constexpr float epsilon = 1.0e-8f;
    position.x = std::abs(parentScale.x) > epsilon ? unrotated.x / parentScale.x : 0.0f;
    position.y = std::abs(parentScale.y) > epsilon ? unrotated.y / parentScale.y : 0.0f;
}

float Transform::GetWorldRotation() const {
    float worldRot = rotation;

    if (gameObject && gameObject->GetParent()) {
        Transform* parentTransform = gameObject->GetParent()->GetComponent<Transform>();
        if (parentTransform) {
            worldRot += parentTransform->GetWorldRotation();
        }
    }

    return worldRot;
}

void Transform::SetWorldRotation(float worldDegrees) {
    if (gameObject && gameObject->GetParent()) {
        if (Transform* parentTransform = gameObject->GetParent()->GetComponent<Transform>()) {
            rotation = worldDegrees - parentTransform->GetWorldRotation();
            return;
        }
    }
    rotation = worldDegrees;
}

Vector2 Transform::GetWorldScale() const {
    Vector2 worldScale = scale;

    if (gameObject && gameObject->GetParent()) {
        Transform* parentTransform = gameObject->GetParent()->GetComponent<Transform>();
        if (parentTransform) {
            Vector2 parentScale = parentTransform->GetWorldScale();
            worldScale.x *= parentScale.x;
            worldScale.y *= parentScale.y;
        }
    }

    return worldScale;
}

void Transform::Serialize(nlohmann::json& j) const {
    j["position"] = { position.x, position.y };
    j["rotation"] = rotation;
    j["scale"] = { scale.x, scale.y };
}

void Transform::Deserialize(const nlohmann::json& j) {
    if (j.contains("position") && j["position"].is_array()) {
        SetPosition(j["position"][0], j["position"][1]);
    }
    if (j.contains("rotation")) {
        SetRotation(j["rotation"]);
    }
    if (j.contains("scale") && j["scale"].is_array()) {
        SetScale(j["scale"][0], j["scale"][1]);
    }
}

void Transform::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    static molga::TransformState before;   // 활성화 순간의 스냅샷
    unsigned int ownerId = gameObject ? gameObject->GetID() : 0u;

    float pos[2] = { position.x, position.y };
    if (ImGui::DragFloat2("Position", pos, 0.5f)) { SetPosition(pos[0], pos[1]); }
    if (ImGui::IsItemActivated())           before = molga::TransformCommand::Capture(this);
    if (ImGui::IsItemDeactivatedAfterEdit())
        Editor::Get().SubmitTransformEdit(ownerId, before, molga::TransformCommand::Capture(this));

    float rot = rotation;
    if (ImGui::DragFloat("Rotation", &rot, 0.5f)) { SetRotation(rot); }
    if (ImGui::IsItemActivated())           before = molga::TransformCommand::Capture(this);
    if (ImGui::IsItemDeactivatedAfterEdit())
        Editor::Get().SubmitTransformEdit(ownerId, before, molga::TransformCommand::Capture(this));

    float scaleArr[2] = { scale.x, scale.y };
    if (ImGui::DragFloat2("Scale", scaleArr, 0.01f)) { SetScale(scaleArr[0], scaleArr[1]); }
    if (ImGui::IsItemActivated())           before = molga::TransformCommand::Capture(this);
    if (ImGui::IsItemDeactivatedAfterEdit())
        Editor::Get().SubmitTransformEdit(ownerId, before, molga::TransformCommand::Capture(this));
#endif
}
