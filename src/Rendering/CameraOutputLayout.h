#pragma once

#include "ECS/Components/Camera.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Rendering/PixelSize.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class GameObject;

namespace molga {

inline constexpr std::size_t kMaxCameraOutputs = 8;

// Immutable camera state captured at a frame boundary. Keeping the conversion
// math here avoids reading a mutable Camera2D after gameplay scripts run.
struct CameraViewSnapshot {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
    float rotation = 0.0f;
    PixelSize viewportSize{};

    bool IsValid() const;
};

struct CameraOutputEntry {
    Camera* camera = nullptr;
    std::uint64_t cameraInstanceId = 0;
    unsigned int cameraObjectId = 0;
    CameraOutputRole role = CameraOutputRole::Disabled;
    int depth = 0;
    std::size_t sceneOrder = 0;
    PixelRect viewport{};
    bool renderable = false;
    CameraViewSnapshot view{};
};

struct CameraPointerMapping {
    std::uint64_t cameraInstanceId = 0;
    unsigned int cameraObjectId = 0;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float worldX = 0.0f;
    float worldY = 0.0f;
};

// Pure output selection, ordering, viewport and coordinate math. Build()
// prepares each selected Camera for its logical pixel viewport, but performs no
// GL work. Entries() is always in back-to-front composite order.
class CameraOutputLayout {
public:
    static CameraOutputLayout Build(
        const std::vector<std::shared_ptr<GameObject>>& objects,
        PixelSize logicalSize);

    PixelSize LogicalSize() const { return logicalSize_; }
    Camera* PrimaryCamera() const { return primaryCamera_; }
    const std::vector<CameraOutputEntry>& Entries() const { return entries_; }
    bool HasRenderableCamera() const;

    std::optional<CameraPointerMapping> LogicalToTopmost(
        float logicalX, float logicalY) const;
    std::optional<CameraPointerMapping> LogicalToTopmost(PixelPoint point) const {
        return LogicalToTopmost(static_cast<float>(point.x),
                                static_cast<float>(point.y));
    }

    std::optional<CameraPointerMapping> LogicalToCamera(
        std::uint64_t cameraInstanceId,
        float logicalX, float logicalY) const;
    std::optional<CameraPointerMapping> LogicalToCamera(
        std::uint64_t cameraInstanceId, PixelPoint point) const {
        return LogicalToCamera(cameraInstanceId,
                               static_cast<float>(point.x),
                               static_cast<float>(point.y));
    }
    std::optional<CameraPointerMapping> LogicalToCamera(
        const Camera& camera, float logicalX, float logicalY) const {
        return LogicalToCamera(camera.GetInstanceID(), logicalX, logicalY);
    }

private:
    PixelSize logicalSize_{};
    Camera* primaryCamera_ = nullptr;
    std::vector<CameraOutputEntry> entries_;
};

} // namespace molga
