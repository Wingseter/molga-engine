#pragma once

#include "Common/Types.h"

#include <memory>
#include <vector>

class GameObject;
class World;
namespace molga { class RenderQueue; }

struct UIPointerState {
    Vector2 position;
    bool down = false;
    bool pressedThisFrame = false;
    bool releasedThisFrame = false;
    bool valid = true;
};

class UISystem {
public:
    static UISystem& Get();

    // Must run before script Update. Only the topmost button can capture a
    // press; release clicks exactly once when still inside the captured rect.
    void ProcessInput(World& world, const Vector2& viewportSize,
                      const UIPointerState& pointer);
    void ProcessInput(std::vector<std::shared_ptr<GameObject>>& objects,
                      const Vector2& viewportSize,
                      const UIPointerState& pointer);
    void CollectRender(World& world, const Vector2& viewportSize,
                       molga::RenderQueue& queue);
    void CollectRender(const std::vector<std::shared_ptr<GameObject>>& objects,
                       const Vector2& viewportSize,
                       molga::RenderQueue& queue);
    GameObject* HitTest(World& world, const Vector2& viewportSize,
                        const Vector2& point) const;
    GameObject* HitTest(const std::vector<std::shared_ptr<GameObject>>& objects,
                        const Vector2& viewportSize,
                        const Vector2& point) const;
    void ResetPointerCapture();

private:
    const void* capturedOwner_ = nullptr;
    unsigned int capturedObjectId_ = 0;
};
