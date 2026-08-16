#include "Rendering/RenderSystem2D.h"
#include "Rendering/Renderer.h"
#include "Core/Profiling/ScopedTimer.h"
#include "Core/Profiling/ProfileScope.h"
#include "Rendering/Camera2D.h"

namespace molga {

void RenderSystem2D::Init() {
    if (!initialized_) {
        batcher_.Init();
        initialized_ = true;
    }
}

void RenderSystem2D::Shutdown() {
    if (initialized_) {
        batcher_.Shutdown();
        initialized_ = false;
    }
}

void RenderSystem2D::Render(RenderQueue& queue, Renderer* renderer, Camera2D* camera,
                            const LightingRenderContext2D* lightingContext) {
    if (!renderer) return;
    
    // Ensure initialized
    Init();

    // 1. Sort queue
    long long start = NowNanos();
    {
        MOLGA_PROFILE_SCOPE("RenderQueue.Sort", molga::ProfileCategory::Rendering);
        queue.Sort();
    }
    long long end = NowNanos();
    renderer->Stats().queueSortNanos += (end - start);

    // 2. Render commands
    batcher_.Begin(renderer, lightingContext);

    const std::optional<AABB> cameraBounds = camera
        ? std::optional<AABB>(camera->GetViewBounds()) : std::nullopt;

    for (const auto& cmd : queue.GetCommands()) {
        if (cameraBounds && cmd.worldBounds &&
            !cameraBounds->Intersects(*cmd.worldBounds)) {
            continue;
        }
        renderer->Stats().submittedCommands++;

        if (cmd.geometry) {
            if (cmd.geometryIndices) {
                batcher_.DrawIndexedGeometry(*cmd.geometry,
                                             *cmd.geometryIndices,
                                             cmd.batchKey);
                continue;
            }
            if (!cmd.batchKey.isBatchable) batcher_.Flush();
            batcher_.DrawGeometry(*cmd.geometry, cmd.batchKey);
            if (!cmd.batchKey.isBatchable) batcher_.Flush();
        } else if (cmd.isBatchableSprite) {
            if (!cmd.batchKey.isBatchable) batcher_.Flush();
            batcher_.DrawSprite(cmd.vertices, cmd.batchKey);
            if (!cmd.batchKey.isBatchable) batcher_.Flush();
        }
    }

    batcher_.End();
}

} // namespace molga
