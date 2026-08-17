#include "Rendering/WorldRenderTraversal.h"

#include "ECS/Component.h"
#include "ECS/GameObject.h"
#include "Rendering/RenderQueue.h"

namespace molga {

namespace {

void CollectWorldRenderImpl(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    RenderQueue& queue,
    const std::uint32_t* cullingMask,
    const WorldRenderCollectOverride& overrideCollector) {
    for (const auto& object : objects) {
        if (!object || !object->IsActive()) continue;
        if (cullingMask &&
            !WorldRenderLayerMatchesMask(object->GetLayer(), *cullingMask)) {
            continue;
        }
        for (Component* component : object->GetComponents()) {
            if (!component || !component->IsEnabled()) continue;
            if (overrideCollector && overrideCollector(*component, queue)) continue;
            component->CollectRender(queue);
        }
    }
}

} // namespace

int NormalizeWorldRenderLayer(int layer) noexcept {
    return layer >= 0 && layer < 32 ? layer : 0;
}

bool WorldRenderLayerMatchesMask(int layer,
                                 std::uint32_t cullingMask) noexcept {
    const auto normalized = static_cast<unsigned int>(
        NormalizeWorldRenderLayer(layer));
    return (cullingMask & (std::uint32_t{1} << normalized)) != 0;
}

void ForEachWorldRenderComponent(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const WorldRenderComponentVisitor& visitor) {
    if (!visitor) return;
    for (const auto& object : objects) {
        if (!object || !object->IsActive()) continue;
        for (Component* component : object->GetComponents()) {
            if (component && component->IsEnabled()) visitor(*component);
        }
    }
}

void CollectWorldRender(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    RenderQueue& queue,
    const WorldRenderCollectOverride& overrideCollector) {
    CollectWorldRenderImpl(objects, queue, nullptr, overrideCollector);
}

void CollectWorldRender(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    RenderQueue& queue,
    std::uint32_t cullingMask,
    const WorldRenderCollectOverride& overrideCollector) {
    CollectWorldRenderImpl(objects, queue, &cullingMask, overrideCollector);
}

} // namespace molga
