#include "Rendering/WorldRenderTraversal.h"

#include "ECS/Component.h"
#include "ECS/GameObject.h"
#include "Rendering/RenderQueue.h"

namespace molga {

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
    ForEachWorldRenderComponent(objects, [&](Component& component) {
        if (overrideCollector && overrideCollector(component, queue)) return;
        component.CollectRender(queue);
    });
}

} // namespace molga
