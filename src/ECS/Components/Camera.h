#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/Camera2D.h"
#include "../../Rendering/PixelSize.h"
#include <cstdint>
#include <memory>
#include <string>

enum class CameraOutputRole {
    Disabled,
    Primary,
    Secondary,
};

// Top-left-origin normalized output rectangle. CameraOutputLayout converts
// these edges to logical pixels; the component only owns authored data and its
// validity contract.
struct CameraViewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;

    bool IsValid() const;
};

constexpr bool operator==(const CameraViewport& lhs,
                          const CameraViewport& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y &&
           lhs.width == rhs.width && lhs.height == rhs.height;
}

constexpr bool operator!=(const CameraViewport& lhs,
                          const CameraViewport& rhs) {
    return !(lhs == rhs);
}

class Camera : public Component {
public:
    COMPONENT_TYPE(Camera)

    Camera();
    ~Camera() override = default;

    // Component overrides
    void ResolveAssets() override;
    void Update(float dt) override;
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void OnInspectorGUI() override;

    // Configure the camera for an explicit render target. Camera components do
    // not inspect GLFW state; runtime and editor callers choose their output.
    bool PrepareForViewport(molga::PixelSize viewport);

    // Getters / Setters
    Camera2D* GetCamera2D() const { return camera2D.get(); }

    CameraOutputRole GetOutputRole() const { return outputRole; }
    void SetOutputRole(CameraOutputRole role);

    // Compatibility surface for existing single-main-camera scripts.
    bool IsMain() const { return outputRole == CameraOutputRole::Primary; }
    void SetMain(bool val) {
        SetOutputRole(val ? CameraOutputRole::Primary
                          : CameraOutputRole::Disabled);
    }

    const CameraViewport& GetViewport() const { return viewport; }
    // Invalid runtime edits are rejected without changing the authored value.
    bool SetViewport(const CameraViewport& value);
    bool SetViewport(float x, float y, float width, float height) {
        return SetViewport(CameraViewport{x, y, width, height});
    }

    std::uint32_t GetCullingMask() const { return cullingMask; }
    void SetCullingMask(std::uint32_t mask) { cullingMask = mask; }

    bool IsLightingEnabled() const { return lightingEnabled; }
    void SetLightingEnabled(bool enabled) { lightingEnabled = enabled; }

    const Color& GetAmbientColor() const { return ambientColor; }
    bool SetAmbientColor(const Color& color);

    float GetAmbientIntensity() const { return ambientIntensity; }
    bool SetAmbientIntensity(float intensity);
    
    const Color& GetBackgroundColor() const { return backgroundColor; }
    void SetBackgroundColor(const Color& color) { backgroundColor = color; }

    float GetOrthoSize() const { return orthoSize; }
    void SetOrthoSize(float size) { orthoSize = size; }

    bool IsPixelPerfect() const { return pixelPerfect; }
    void SetPixelPerfect(bool enabled) { pixelPerfect = enabled; }

    int GetPixelZoom() const { return pixelZoom; }
    void SetPixelZoom(int zoom);

    int GetDepth() const { return depth; }
    void SetDepth(int d) { depth = d; }

    unsigned int GetTargetId() const { return targetId; }
    void SetTargetId(unsigned int id) { targetId = id; }

    GameObject* GetFollowTarget() const { return followTarget; }
    void SetFollowTarget(GameObject* target);

    float GetSmoothing() const { return smoothing; }
    void SetSmoothing(float s) { smoothing = s; }

    bool IsPostProcessEnabled() const { return postProcessEnabled; }
    void SetPostProcessEnabled(bool enabled) { postProcessEnabled = enabled; }

    const std::string& GetPostProcessProfileGuid() const {
        return postProcessProfileGuid;
    }
    void SetPostProcessProfileGuid(std::string guid) {
        postProcessProfileGuid = std::move(guid);
    }

    // Returns a modern Camera component object suitable for semantic prefab
    // comparisons. Unknown keys are preserved; legacy mirrors are removed.
    static nlohmann::json CanonicalizeSerializedData(
        const nlohmann::json& serialized);

private:
    float orthoSize = 300.0f; // zoom size
    bool pixelPerfect = false;
    int pixelZoom = 1;
    Color backgroundColor = Color(0.12f, 0.12f, 0.15f, 1.0f);
    int depth = 0;
    CameraOutputRole outputRole = CameraOutputRole::Disabled;
    CameraViewport viewport{};
    std::uint32_t cullingMask = 0xFFFFFFFFu;
    bool lightingEnabled = false;
    Color ambientColor = Color::White();
    float ambientIntensity = 0.2f;
    unsigned int targetId = 0; // serialized target GameObject ID
    GameObject* followTarget = nullptr; // runtime pointer to target
    float smoothing = 5.0f; // lerp speed
    bool postProcessEnabled = false;
    std::string postProcessProfileGuid;

    std::unique_ptr<Camera2D> camera2D;
    // Runtime-only marker: the Camera2D position currently represents a
    // pixel-snapped render state rather than the follow simulation state.
    bool renderPositionSnapped_ = false;
};
