#include "Camera.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "../../Core/World.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>

#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

REGISTER_COMPONENT(Camera)

namespace {

constexpr CameraViewport kFullCameraViewport{};
constexpr std::uint32_t kAllCameraLayers = 0xFFFFFFFFu;

const char* CameraOutputRoleName(CameraOutputRole role) {
    switch (role) {
        case CameraOutputRole::Primary: return "Primary";
        case CameraOutputRole::Secondary: return "Secondary";
        case CameraOutputRole::Disabled:
        default: return "Disabled";
    }
}

CameraOutputRole ParseCameraOutputRole(const nlohmann::json& value) {
    if (value.is_string()) {
        const std::string role = value.get<std::string>();
        if (role == "Primary") return CameraOutputRole::Primary;
        if (role == "Secondary") return CameraOutputRole::Secondary;
        return CameraOutputRole::Disabled;
    }
    if (value.is_number_unsigned()) {
        const auto raw = value.get<unsigned long long>();
        if (raw == static_cast<unsigned long long>(CameraOutputRole::Primary))
            return CameraOutputRole::Primary;
        if (raw == static_cast<unsigned long long>(CameraOutputRole::Secondary))
            return CameraOutputRole::Secondary;
    } else if (value.is_number_integer()) {
        const auto raw = value.get<long long>();
        if (raw == static_cast<long long>(CameraOutputRole::Primary))
            return CameraOutputRole::Primary;
        if (raw == static_cast<long long>(CameraOutputRole::Secondary))
            return CameraOutputRole::Secondary;
    }
    return CameraOutputRole::Disabled;
}

CameraViewport ParseCameraViewport(const nlohmann::json& value) {
    if (!value.is_object()) return kFullCameraViewport;
    const auto x = value.find("x");
    const auto y = value.find("y");
    const auto width = value.find("width");
    const auto height = value.find("height");
    if (x == value.end() || y == value.end() ||
        width == value.end() || height == value.end() ||
        !x->is_number() || !y->is_number() ||
        !width->is_number() || !height->is_number()) {
        return kFullCameraViewport;
    }
    const CameraViewport parsed{x->get<float>(), y->get<float>(),
                                width->get<float>(), height->get<float>()};
    return parsed.IsValid() ? parsed : kFullCameraViewport;
}

std::uint32_t ParseCameraCullingMask(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        const auto raw = value.get<unsigned long long>();
        if (raw <= std::numeric_limits<std::uint32_t>::max())
            return static_cast<std::uint32_t>(raw);
    } else if (value.is_number_integer()) {
        const auto raw = value.get<long long>();
        if (raw >= 0 &&
            static_cast<unsigned long long>(raw) <=
                std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(raw);
        }
    }
    return kAllCameraLayers;
}

nlohmann::json SerializeCameraViewport(const CameraViewport& viewport) {
    return {{"x", viewport.x}, {"y", viewport.y},
            {"width", viewport.width}, {"height", viewport.height}};
}

bool IsFiniteColor(const Color& color) {
    return std::isfinite(color.r) && std::isfinite(color.g) &&
           std::isfinite(color.b) && std::isfinite(color.a);
}

} // namespace

bool CameraViewport::IsValid() const {
    return std::isfinite(x) && std::isfinite(y) &&
           std::isfinite(width) && std::isfinite(height) &&
           x >= 0.0f && y >= 0.0f && x <= 1.0f && y <= 1.0f &&
           width > 0.0f && height > 0.0f &&
           width <= 1.0f - x && height <= 1.0f - y;
}

Camera::Camera()
    : orthoSize(300.0f),
      pixelPerfect(false),
      pixelZoom(1),
      backgroundColor(0.12f, 0.12f, 0.15f, 1.0f),
      depth(0),
      outputRole(CameraOutputRole::Disabled),
      viewport(),
      cullingMask(kAllCameraLayers),
      lightingEnabled(false),
      ambientColor(Color::White()),
      ambientIntensity(0.2f),
      targetId(0),
      followTarget(nullptr),
      smoothing(5.0f),
      postProcessEnabled(false),
      postProcessProfileGuid(),
      camera2D(std::make_unique<Camera2D>(800.0f, 600.0f)) {
}

void Camera::SetFollowTarget(GameObject* target) {
    followTarget = target;
    targetId = target ? target->GetID() : 0;
}

void Camera::SetPixelZoom(int zoom) {
    pixelZoom = std::clamp(zoom, 1, 64);
}

void Camera::SetOutputRole(CameraOutputRole role) {
    switch (role) {
        case CameraOutputRole::Disabled:
        case CameraOutputRole::Primary:
        case CameraOutputRole::Secondary:
            outputRole = role;
            break;
        default:
            outputRole = CameraOutputRole::Disabled;
            break;
    }
}

bool Camera::SetViewport(const CameraViewport& value) {
    if (!value.IsValid()) return false;
    viewport = value;
    return true;
}

bool Camera::SetAmbientColor(const Color& color) {
    if (!IsFiniteColor(color)) return false;
    ambientColor = color;
    return true;
}

bool Camera::SetAmbientIntensity(float intensity) {
    if (!std::isfinite(intensity) || intensity < 0.0f) return false;
    ambientIntensity = intensity;
    return true;
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
            camera2D->SetRotation(transform->GetWorldRotation());
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
    j["outputRole"] = CameraOutputRoleName(outputRole);
    j["viewport"] = SerializeCameraViewport(viewport);
    j["cullingMask"] = cullingMask;
    j["lightingEnabled"] = lightingEnabled;
    j["ambientColor"] = {
        ambientColor.r, ambientColor.g, ambientColor.b, ambientColor.a};
    j["ambientIntensity"] = ambientIntensity;
    j["targetId"] = targetId;
    j["smoothing"] = smoothing;
    j["postProcessEnabled"] = postProcessEnabled;
    j["postProcessProfileGuid"] = postProcessProfileGuid;
}

/* static */ nlohmann::json Camera::CanonicalizeSerializedData(
    const nlohmann::json& serialized) {
    nlohmann::json canonical = serialized.is_object()
        ? serialized : nlohmann::json::object();

    CameraOutputRole role = CameraOutputRole::Disabled;
    const auto modernRole = canonical.find("outputRole");
    if (modernRole != canonical.end()) {
        role = ParseCameraOutputRole(*modernRole);
    } else {
        const auto legacyMain = canonical.find("isMain");
        if (legacyMain != canonical.end() && legacyMain->is_boolean() &&
            legacyMain->get<bool>()) {
            role = CameraOutputRole::Primary;
        }
    }
    canonical["outputRole"] = CameraOutputRoleName(role);
    canonical.erase("isMain");

    const auto serializedViewport = canonical.find("viewport");
    const CameraViewport normalizedViewport = serializedViewport != canonical.end()
        ? ParseCameraViewport(*serializedViewport) : kFullCameraViewport;
    canonical["viewport"] = SerializeCameraViewport(normalizedViewport);

    const auto serializedMask = canonical.find("cullingMask");
    canonical["cullingMask"] = serializedMask != canonical.end()
        ? ParseCameraCullingMask(*serializedMask) : kAllCameraLayers;

    // Missing Camera fields in older prefab templates represent component
    // defaults, not instance overrides.
    if (!canonical.contains("enabled")) canonical["enabled"] = true;
    if (!canonical.contains("orthoSize")) canonical["orthoSize"] = 300.0f;
    if (!canonical.contains("pixelPerfect")) canonical["pixelPerfect"] = false;
    if (!canonical.contains("pixelZoom")) canonical["pixelZoom"] = 1;
    if (!canonical.contains("backgroundColor"))
        canonical["backgroundColor"] = {0.12f, 0.12f, 0.15f, 1.0f};
    if (!canonical.contains("depth")) canonical["depth"] = 0;
    if (!canonical.contains("lightingEnabled"))
        canonical["lightingEnabled"] = false;
    if (!canonical.contains("ambientColor"))
        canonical["ambientColor"] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (!canonical.contains("ambientIntensity"))
        canonical["ambientIntensity"] = 0.2f;
    if (!canonical.contains("targetId")) canonical["targetId"] = 0u;
    if (!canonical.contains("smoothing")) canonical["smoothing"] = 5.0f;
    if (!canonical.contains("postProcessEnabled"))
        canonical["postProcessEnabled"] = false;
    if (!canonical.contains("postProcessProfileGuid"))
        canonical["postProcessProfileGuid"] = "";
    return canonical;
}

void Camera::Deserialize(const nlohmann::json& j) {
    const nlohmann::json canonical = CanonicalizeSerializedData(j);
    SetOutputRole(ParseCameraOutputRole(canonical["outputRole"]));
    viewport = ParseCameraViewport(canonical["viewport"]);
    cullingMask = ParseCameraCullingMask(canonical["cullingMask"]);
    if (canonical["lightingEnabled"].is_boolean())
        lightingEnabled = canonical["lightingEnabled"].get<bool>();
    if (canonical["ambientColor"].is_array() &&
        canonical["ambientColor"].size() >= 4U) {
        try {
            SetAmbientColor({
                canonical["ambientColor"][0].get<float>(),
                canonical["ambientColor"][1].get<float>(),
                canonical["ambientColor"][2].get<float>(),
                canonical["ambientColor"][3].get<float>()});
        } catch (...) {
            // Keep the previous valid ambient color.
        }
    }
    if (canonical["ambientIntensity"].is_number()) {
        try {
            SetAmbientIntensity(canonical["ambientIntensity"].get<float>());
        } catch (...) {
            // Keep the previous valid ambient intensity.
        }
    }

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
    if (j.contains("targetId")) targetId = j["targetId"];
    if (j.contains("smoothing")) smoothing = j["smoothing"];
    if (j.contains("postProcessEnabled"))
        postProcessEnabled = j["postProcessEnabled"];
    if (j.contains("postProcessProfileGuid"))
        postProcessProfileGuid = j["postProcessProfileGuid"];
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
    bool isMain = IsMain();
    if (ImGui::Checkbox("Is Main Camera", &isMain)) SetMain(isMain);
    ImGui::Checkbox("Post Process", &postProcessEnabled);
    if (postProcessEnabled) {
        char profile[128]{};
        std::strncpy(profile, postProcessProfileGuid.c_str(), sizeof(profile) - 1);
        if (ImGui::InputText("Post Process Profile GUID", profile, sizeof(profile))) {
            postProcessProfileGuid = profile;
        }
    }
    
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
