#pragma once

#include <functional>
#include <memory>
#include <vector>

class Component;
class GameObject;

namespace molga {

class RenderQueue;

using WorldRenderComponentVisitor = std::function<void(Component&)>;
using WorldRenderCollectOverride =
    std::function<bool(Component&, RenderQueue&)>;

// The shared deterministic world order: scene object vector order, then each
// object's component insertion order. Inactive objects and disabled components
// do not occupy a render slot.
void ForEachWorldRenderComponent(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const WorldRenderComponentVisitor& visitor);

// An override returning true replaces the component's normal collection at
// that exact slot. Scene View uses this only for its isolated particle preview.
void CollectWorldRender(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    RenderQueue& queue,
    const WorldRenderCollectOverride& overrideCollector = {});

} // namespace molga
