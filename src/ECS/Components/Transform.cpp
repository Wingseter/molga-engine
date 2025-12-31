#include "Transform.h"
#include "../GameObject.h"

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
