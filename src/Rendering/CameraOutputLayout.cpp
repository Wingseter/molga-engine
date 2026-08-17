#include "Rendering/CameraOutputLayout.h"

#include "Common/Constants.h"
#include "ECS/GameObject.h"
#include "Rendering/Camera2D.h"

#include <algorithm>
#include <cmath>

namespace molga {
namespace {

struct Candidate {
    Camera* camera = nullptr;
    GameObject* object = nullptr;
    std::size_t sceneOrder = 0;
    PixelRect viewport{};
};

PixelRect ToPixelRect(const CameraViewport& viewport, PixelSize logicalSize) {
    const auto edge = [](float value, int extent) {
        return static_cast<int>(std::floor(
            static_cast<double>(value) * static_cast<double>(extent)));
    };
    const int left = edge(viewport.x, logicalSize.width);
    const int top = edge(viewport.y, logicalSize.height);
    const int right = edge(viewport.x + viewport.width, logicalSize.width);
    const int bottom = edge(viewport.y + viewport.height, logicalSize.height);
    return {left, top, right - left, bottom - top};
}

bool Contains(const PixelRect& rect, float x, float y) {
    return std::isfinite(x) && std::isfinite(y) &&
           x >= static_cast<float>(rect.x) &&
           y >= static_cast<float>(rect.y) &&
           x < static_cast<float>(rect.x + rect.width) &&
           y < static_cast<float>(rect.y + rect.height);
}

CameraPointerMapping MapPointer(const CameraOutputEntry& entry,
                                float logicalX, float logicalY) {
    CameraPointerMapping result;
    result.cameraInstanceId = entry.cameraInstanceId;
    result.cameraObjectId = entry.cameraObjectId;
    result.cameraX = logicalX - static_cast<float>(entry.viewport.x);
    result.cameraY = logicalY - static_cast<float>(entry.viewport.y);

    const float halfWidth = static_cast<float>(entry.view.viewportSize.width) * 0.5f;
    const float halfHeight = static_cast<float>(entry.view.viewportSize.height) * 0.5f;
    const float localX = (result.cameraX - halfWidth) / entry.view.zoom;
    const float localY = (result.cameraY - halfHeight) / entry.view.zoom;
    const float radians = entry.view.rotation * Constants::DEG_TO_RAD;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result.worldX = entry.view.x + halfWidth +
                    cosine * localX + sine * localY;
    result.worldY = entry.view.y + halfHeight -
                    sine * localX + cosine * localY;
    return result;
}

CameraOutputEntry MakeEntry(const Candidate& candidate) {
    CameraOutputEntry entry;
    entry.camera = candidate.camera;
    entry.cameraInstanceId = candidate.camera->GetInstanceID();
    entry.cameraObjectId = candidate.object->GetID();
    entry.role = candidate.camera->GetOutputRole();
    entry.depth = candidate.camera->GetDepth();
    entry.sceneOrder = candidate.sceneOrder;
    entry.viewport = candidate.viewport;
    entry.renderable = entry.viewport.width > 0 && entry.viewport.height > 0;
    if (!entry.renderable || !candidate.camera->PrepareForViewport(
            {entry.viewport.width, entry.viewport.height})) {
        entry.renderable = false;
        return entry;
    }

    const Camera2D* camera = candidate.camera->GetCamera2D();
    if (!camera) {
        entry.renderable = false;
        return entry;
    }
    entry.view = {camera->GetX(), camera->GetY(), camera->GetZoom(),
                  camera->GetRotation(),
                  {entry.viewport.width, entry.viewport.height}};
    entry.renderable = entry.view.IsValid();
    return entry;
}

} // namespace

bool CameraViewSnapshot::IsValid() const {
    return viewportSize.IsValid() && std::isfinite(x) && std::isfinite(y) &&
           std::isfinite(zoom) && zoom > 0.0f && std::isfinite(rotation);
}

CameraOutputLayout CameraOutputLayout::Build(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    PixelSize logicalSize) {
    CameraOutputLayout result;
    result.logicalSize_ = logicalSize;
    if (!logicalSize.IsValid()) return result;

    std::optional<Candidate> primary;
    std::vector<Candidate> secondary;
    secondary.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const auto& object = objects[index];
        if (!object || !object->IsActive()) continue;
        Camera* camera = object->GetComponent<Camera>();
        if (!camera || !camera->IsEnabled()) continue;
        const CameraOutputRole role = camera->GetOutputRole();
        if (role == CameraOutputRole::Disabled) continue;

        Candidate candidate{camera, object.get(), index,
                            ToPixelRect(camera->GetViewport(), logicalSize)};
        if (role == CameraOutputRole::Primary) {
            // Strictly greater preserves the first camera in scene order on ties.
            if (!primary || camera->GetDepth() > primary->camera->GetDepth()) {
                primary = candidate;
            }
        } else if (role == CameraOutputRole::Secondary) {
            secondary.push_back(candidate);
        }
    }

    std::stable_sort(secondary.begin(), secondary.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.camera->GetDepth() != rhs.camera->GetDepth()) {
                return lhs.camera->GetDepth() > rhs.camera->GetDepth();
            }
            return lhs.sceneOrder < rhs.sceneOrder;
        });

    std::vector<Candidate> selected;
    selected.reserve(kMaxCameraOutputs);
    if (primary) {
        result.primaryCamera_ = primary->camera;
        selected.push_back(*primary);
    }
    const std::size_t remaining = kMaxCameraOutputs - selected.size();
    std::size_t selectedSecondaryCount = 0;
    for (const Candidate& candidate : secondary) {
        if (selectedSecondaryCount >= remaining) break;
        // A Secondary that collapses below one logical pixel is excluded from
        // this frame and cannot consume one of the eight output slots. The
        // chosen Primary is handled above so the compatibility representative
        // remains stable even when its authored viewport temporarily collapses.
        if (candidate.viewport.width <= 0 || candidate.viewport.height <= 0) {
            continue;
        }
        selected.push_back(candidate);
        ++selectedSecondaryCount;
    }

    result.entries_.reserve(selected.size());
    for (const Candidate& candidate : selected) {
        result.entries_.push_back(MakeEntry(candidate));
    }
    std::stable_sort(result.entries_.begin(), result.entries_.end(),
        [](const CameraOutputEntry& lhs, const CameraOutputEntry& rhs) {
            if (lhs.depth != rhs.depth) return lhs.depth < rhs.depth;
            return lhs.sceneOrder < rhs.sceneOrder;
        });
    return result;
}

bool CameraOutputLayout::HasRenderableCamera() const {
    return std::any_of(entries_.begin(), entries_.end(),
        [](const CameraOutputEntry& entry) { return entry.renderable; });
}

std::optional<CameraPointerMapping> CameraOutputLayout::LogicalToTopmost(
    float logicalX, float logicalY) const {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->renderable && Contains(it->viewport, logicalX, logicalY)) {
            return MapPointer(*it, logicalX, logicalY);
        }
    }
    return std::nullopt;
}

std::optional<CameraPointerMapping> CameraOutputLayout::LogicalToCamera(
    std::uint64_t cameraInstanceId, float logicalX, float logicalY) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(),
        [cameraInstanceId](const CameraOutputEntry& entry) {
            return entry.cameraInstanceId == cameraInstanceId;
        });
    if (found == entries_.end() || !found->renderable ||
        !Contains(found->viewport, logicalX, logicalY)) {
        return std::nullopt;
    }
    return MapPointer(*found, logicalX, logicalY);
}

} // namespace molga
