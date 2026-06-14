#include "Camera.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "../../Core/World.h"
#include <nlohmann/json.hpp>
#include <GLFW/glfw3.h>

#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

REGISTER_COMPONENT(Camera)

Camera::Camera()
    : orthoSize(300.0f),
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

void Camera::ResolveAssets() {
    if (targetId > 0 && gameObject && gameObject->GetWorld()) {
        followTarget = gameObject->GetWorld()->FindById(targetId);
    } else {
        followTarget = nullptr;
    }

    // Initialize/update screen size if window is available
    float w = 800.0f;
    float h = 600.0f;
    GLFWwindow* window = glfwGetCurrentContext();
    if (window) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (width > 0 && height > 0) {
            w = static_cast<float>(width);
            h = static_cast<float>(height);
        }
    }
    if (camera2D) {
        camera2D->SetScreenSize(w, h);
        camera2D->SetZoom(h / (2.0f * orthoSize));
    }
}

void Camera::Update(float dt) {
    GLFWwindow* window = glfwGetCurrentContext();
    if (window) {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0 && camera2D) {
            camera2D->SetScreenSize(static_cast<float>(w), static_cast<float>(h));
            camera2D->SetZoom(static_cast<float>(h) / (2.0f * orthoSize));
        }
    }

    if (followTarget) {
        auto targetTransform = followTarget->GetComponent<Transform>();
        if (targetTransform) {
            Vector2 current(camera2D->GetX(), camera2D->GetY());
            Vector2 targetPos = targetTransform->GetWorldPosition();
            Vector2 next = current + (targetPos - current) * smoothing * dt;
            camera2D->SetPosition(next.x, next.y);

            auto transform = gameObject->GetComponent<Transform>();
            if (transform) {
                transform->SetPosition(next.x, next.y);
            }
        }
    } else {
        auto transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 pos = transform->GetWorldPosition();
            camera2D->SetPosition(pos.x, pos.y);
        }
    }
}

void Camera::Serialize(nlohmann::json& j) const {
    j["orthoSize"] = orthoSize;
    j["backgroundColor"] = { backgroundColor.r, backgroundColor.g, backgroundColor.b, backgroundColor.a };
    j["depth"] = depth;
    j["isMain"] = isMain;
    j["targetId"] = targetId;
    j["smoothing"] = smoothing;
}

void Camera::Deserialize(const nlohmann::json& j) {
    if (j.contains("orthoSize")) orthoSize = j["orthoSize"];
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
    ImGui::DragFloat("Ortho Size", &orthoSize, 1.0f, 1.0f, 5000.0f);
    
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
