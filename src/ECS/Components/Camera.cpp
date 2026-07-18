#include "Camera.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "../../Core/World.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

REGISTER_COMPONENT(Camera)

Camera::Camera()
    : orthoSize(300.0f),
      pixelPerfect(false),
      pixelZoom(1),
      backgroundColor(0.12f, 0.12f, 0.15f, 1.0f),
      depth(0),
      isMain(false),
      targetId(0),
      followTarget(nullptr),
      smoothing(5.0f),
      camera2D(std::make_unique<Camera2D>(800.0f, 600.0f)) {
}

void Camera::SetFollowTarget(GameObject* target) {
    followTarget = target;
    targetId = target ? target->GetID() : 0;
}

void Camera::SetPixelZoom(int zoom) {
    pixelZoom = std::clamp(zoom, 1, 64);
}

void Camera::ResolveAssets() {
    if (targetId > 0 && gameObject && gameObject->GetWorld()) {
        followTarget = gameObject->GetWorld()->FindById(targetId);
    } else {
        followTarget = nullptr;
    }

    PrepareForViewport({800, 600});
}

void Camera::Update(float dt) {
    if (followTarget) {
        auto targetTransform = followTarget->GetComponent<Transform>();
        if (targetTransform) {
            // Camera2D may hold a pixel-snapped render position from the
            // previous frame. Follow interpolation must remain in the
            // unsnapped simulation state owned by the Transform.
            Transform* cameraTransform = gameObject
                ? gameObject->GetComponent<Transform>() : nullptr;
            Vector2 current = renderPositionSnapped_ && cameraTransform
                ? cameraTransform->GetWorldPosition()
                : Vector2(camera2D->GetX(), camera2D->GetY());
            Vector2 targetPos = targetTransform->GetWorldPosition();
            Vector2 next = current + (targetPos - current) * smoothing * dt;
            camera2D->SetPosition(next.x, next.y);
            renderPositionSnapped_ = false;

            if (cameraTransform) {
                cameraTransform->SetPosition(next.x, next.y);
            }
        }
    } else {
        auto transform = gameObject ? gameObject->GetComponent<Transform>() : nullptr;
        if (transform) {
            Vector2 pos = transform->GetWorldPosition();
            camera2D->SetPosition(pos.x, pos.y);
            renderPositionSnapped_ = false;
        }
    }
}

bool Camera::PrepareForViewport(molga::PixelSize viewport) {
    if (!camera2D || !viewport.IsValid()) return false;
    if (gameObject) {
        if (Transform* transform = gameObject->GetComponent<Transform>()) {
            Vector2 position = transform->GetWorldPosition();
            if (pixelPerfect) {
                const float zoom = static_cast<float>(pixelZoom);
                position.x = std::round(position.x * zoom) / zoom;
                position.y = std::round(position.y * zoom) / zoom;
                renderPositionSnapped_ = true;
            } else {
                renderPositionSnapped_ = false;
            }
            camera2D->SetPosition(position.x, position.y);
        }
    }
    camera2D->SetScreenSize(static_cast<float>(viewport.width),
                            static_cast<float>(viewport.height));
    if (pixelPerfect) {
        camera2D->SetPixelZoom(pixelZoom);
    } else {
        const float safeOrthoSize = std::max(orthoSize, 0.01f);
        camera2D->SetZoom(static_cast<float>(viewport.height) /
                          (2.0f * safeOrthoSize));
    }
    return true;
}

void Camera::Serialize(nlohmann::json& j) const {
    j["orthoSize"] = orthoSize;
    j["pixelPerfect"] = pixelPerfect;
    j["pixelZoom"] = pixelZoom;
    j["backgroundColor"] = { backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a };
    j["depth"] = depth;
    j["isMain"] = isMain;
    j["targetId"] = targetId;
    j["smoothing"] = smoothing;
}

void Camera::Deserialize(const nlohmann::json& j) {
    if (j.contains("orthoSize")) orthoSize = j["orthoSize"];
    // zoom = h / (2 * orthoSize) 계산에서 0 나눗셈을 막는다 (손상된 씬 방어).
    if (orthoSize < 0.01f) orthoSize = 0.01f;
    if (j.contains("pixelPerfect")) pixelPerfect = j["pixelPerfect"];
    if (j.contains("pixelZoom")) SetPixelZoom(j["pixelZoom"]);
    if (j.contains("backgroundColor") && j["backgroundColor"].is_array()) {
        backgroundColor.r = j["backgroundColor"][0];
        backgroundColor.g = j["backgroundColor"][1];
        backgroundColor.b = j["backgroundColor"][2];
        backgroundColor.a = j["backgroundColor"][3];
    }
    if (j.contains("depth")) depth = j["depth"];
    if (j.contains("isMain")) isMain = j["isMain"];
    if (j.contains("targetId")) targetId = j["targetId"];
    if (j.contains("smoothing")) smoothing = j["smoothing"];
}

void Camera::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    ImGui::Checkbox("Pixel Perfect", &pixelPerfect);
    if (pixelPerfect) {
        int zoom = pixelZoom;
        if (ImGui::DragInt("Pixel Zoom", &zoom, 1.0f, 1, 64)) {
            SetPixelZoom(zoom);
        }
    } else {
        ImGui::DragFloat("Ortho Size", &orthoSize, 1.0f, 1.0f, 5000.0f);
    }
    
    float col[4] = { backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a };
    if (ImGui::ColorEdit4("Clear Color", col)) {
        backgroundColor = Color(col[0], col[1], col[2], col[3]);
    }

    ImGui::DragInt("Depth", &depth, 1);
    ImGui::Checkbox("Is Main Camera", &isMain);
    
    ImGui::Separator();
    
    ImGui::Text("Follow Target:");
    ImGui::SameLine();
    std::string targetName = "None";
    if (followTarget) {
        targetName = followTarget->GetName() + " (ID: " + std::to_string(targetId) + ")";
    } else if (targetId > 0) {
        targetName = "Missing (ID: " + std::to_string(targetId) + ")";
    }
    
    ImGui::Button(targetName.c_str(), ImVec2(-1, 0));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAMEOBJECT_ID")) {
            unsigned int dragId = *(const unsigned int*)payload->Data;
            targetId = dragId;
            if (gameObject && gameObject->GetWorld()) {
                followTarget = gameObject->GetWorld()->FindById(targetId);
            }
        }
        ImGui::EndDragDropTarget();
    }

    int tempId = static_cast<int>(targetId);
    if (ImGui::InputInt("Follow Target ID", &tempId)) {
        if (tempId < 0) tempId = 0;
        targetId = static_cast<unsigned int>(tempId);
        if (gameObject && gameObject->GetWorld()) {
            followTarget = gameObject->GetWorld()->FindById(targetId);
        } else {
            followTarget = nullptr;
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        targetId = 0;
        followTarget = nullptr;
    }

    ImGui::DragFloat("Smoothing", &smoothing, 0.1f, 0.0f, 100.0f);
#endif
}
