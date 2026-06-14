#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/Camera2D.h"
#include <memory>

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

    // Getters / Setters
    Camera2D* GetCamera2D() const { return camera2D.get(); }
    
    bool IsMain() const { return isMain; }
    void SetMain(bool val) { isMain = val; }
    
    const Color& GetBackgroundColor() const { return backgroundColor; }
    void SetBackgroundColor(const Color& color) { backgroundColor = color; }

    float GetOrthoSize() const { return orthoSize; }
    void SetOrthoSize(float size) { orthoSize = size; }

    int GetDepth() const { return depth; }
    void SetDepth(int d) { depth = d; }

    unsigned int GetTargetId() const { return targetId; }
    void SetTargetId(unsigned int id) { targetId = id; }

    GameObject* GetFollowTarget() const { return followTarget; }
    void SetFollowTarget(GameObject* target);

    float GetSmoothing() const { return smoothing; }
    void SetSmoothing(float s) { smoothing = s; }

private:
    float orthoSize = 300.0f; // zoom size
    Color backgroundColor = Color(0.12f, 0.12f, 0.15f, 1.0f);
    int depth = 0;
    bool isMain = false;
    unsigned int targetId = 0; // serialized target GameObject ID
    GameObject* followTarget = nullptr; // runtime pointer to target
    float smoothing = 5.0f; // lerp speed

    std::unique_ptr<Camera2D> camera2D;
};
