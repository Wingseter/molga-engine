#include "Rendering/RenderSystem2D.h"
#include "Common/Log.h"
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

    // Task 6.3: 컬링/제출 루프는 RenderQueue.h에 한 벌만 있다. 명령이 가리키는
    // atlas page의 지분이 그 명령의 실제 draw 직전에만 프레임으로 넘어가는지는
    // GPU 없이 관찰되어야 하고, 여기 사본을 두면 그 관찰 대상이 사본이 된다.
    //
    // 이 호출은 던질 수 있다(Renderer::RetainUntilFrameComplete의 계약 위반은
    // std::logic_error다). 그때 batcher_.End()는 건너뛰어지고 이 프레임은
    // 버려진다 — 회복하지 않는 쪽이 맞다. 지분을 넘기지 못한 page 위에서
    // 계속 그리는 것이 이 코드가 막으려는 바로 그 상태이기 때문이다. 오늘은
    // resourceLifetime을 채우는 프로덕션 생산자가 없어 닿을 수 없고, Task
    // 8.2가 소비자를 옮길 때 이 경로의 처리를 정해야 한다.
    // Task 11.2 Steps 7a-7c: 클립 전이도 이 한 벌 안에 있다. 상태 호출이
    // 실패하면 루프가 그 자리에서 멈추고 거짓을 돌려준다 — 이전 클립 아래에서
    // 남은 명령을 계속 그리는 것이 막으려는 상태이기 때문이다. batcher는
    // 그래도 닫는다: 열어 둔 채 나가면 다음 프레임의 Begin이 앞 프레임의
    // 정점 위에 쌓인다.
    std::string clipError;
    const bool complete = SubmitVisibleCommands(
        queue.GetCommands(), cameraBounds, *renderer, batcher_, &clipError);

    batcher_.End();
    if (!complete) {
        Log::Error("RenderSystem2D",
                   "render clip state change failed; the remaining commands "
                   "of this queue were dropped rather than drawn under the "
                   "previous clip: " + clipError);
    }
}

} // namespace molga
