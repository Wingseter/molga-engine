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

        if (cmd.geometry && cmd.batchKey.isBatchable) {
            batcher_.DrawGeometry(*cmd.geometry, cmd.batchKey);
        } else if (cmd.isBatchableSprite) {
            batcher_.DrawSprite(cmd.vertices, cmd.batchKey);
        } else {
            // Flush dynamic batch before drawing non-batchable sprites
            batcher_.Flush();

            if (cmd.fallbackRender) {
                cmd.fallbackRender(renderer);
            }
        }
    }

    batcher_.End();
}

} // namespace molga
