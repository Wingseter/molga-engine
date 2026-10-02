#pragma once

#include <functional>
#include <cstdint>
#include <memory>
#include <vector>

class Component;
class GameObject;
struct WorldRenderCollectionContext;

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

// ── Task 8.2 Step 7c: 문맥 없는 순회는 존재하지 않는다 ──────────────────────
// 문맥에는 이 프레임의 renderer/텍스트 서비스/진단 sink/기본 래스터 정책이
// 들어 있다. 기본값을 가진 오버로드를 남겨 두면 텍스트를 담은 world가 문맥
// 없이 순회될 수 있고, 그때 텍스트는 아무 진단 없이 화면에서만 사라진다.
//
// An override returning true replaces the component's normal collection at
// that exact slot. Scene View uses this only for its isolated particle preview.
void CollectWorldRender(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    RenderQueue& queue,
    const WorldRenderCollectionContext& context,
    const WorldRenderCollectOverride& overrideCollector = {});

// Camera-output overload. Filtering happens per object before any component on
// that object occupies a render traversal slot.
void CollectWorldRender(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    RenderQueue& queue,
    std::uint32_t cullingMask,
    const WorldRenderCollectionContext& context,
    const WorldRenderCollectOverride& overrideCollector = {});

} // namespace molga
