#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <vector>

class Component;
class GameObject;

namespace molga {

class RenderQueue;

using WorldRenderComponentVisitor = std::function<void(Component&)>;
using WorldRenderCollectOverride =
    std::function<bool(Component&, RenderQueue&)>;

// GameObject layers are authored as signed integers for legacy compatibility.
// Rendering treats every out-of-range value as layer 0 before shifting, which
// avoids undefined behavior and preserves old malformed scenes.
int NormalizeWorldRenderLayer(int layer) noexcept;
bool WorldRenderLayerMatchesMask(int layer, std::uint32_t cullingMask) noexcept;

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

// Camera-output overload. Filtering happens per object before any component on
// that object occupies a render traversal slot.
void CollectWorldRender(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    RenderQueue& queue,
    std::uint32_t cullingMask,
    const WorldRenderCollectOverride& overrideCollector = {});

} // namespace molga
