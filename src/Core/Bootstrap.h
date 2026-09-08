#pragma once

#include "Platform/Window.h"
#include "Rendering/GraphicsDevice.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ImGuiLayer;
class Renderer;
class TextRenderer;
namespace molga::text { class TextDiagnosticSink; }

// ── Task 11.2 Step 7f: 실패할 수 있고 재시도할 수 있는 종료 ──────────────────
// Complete만이 host를 놓는다. 두 실패는 host/장치 수명 객체를 살려 둔 채
// blocker(ReferenceInvalid) 하나를 낸다. 호출자는 실패하는 최종 종료 코드를
// 기록하고, 자기가 아는 외부 스냅샷/page 소유자만 놓은 뒤, **같은 host로**
// 다시 시도한다. 결과가 Complete가 아닌 동안에는 돌아가지도, 예외로 소유자
// 범위를 벗어나지도, 진단 sink나 텍스트 런타임 guard를 파괴하지도, host를
// 강제로 reset하지도 않는다.
enum class EngineShutdownStatus : std::uint8_t {
    Complete,
    GpuDrainFailed,
    ExternalGpuLifetime,
};

struct WindowConfig {
    std::string title = "Molga Engine";
    int width = 800;
    int height = 600;
    bool fullscreen = false;
    bool resizable = true;
    bool visible = true;
    molga::GraphicsBackend graphicsBackend = molga::GraphicsBackend::SdlGpu;
    bool graphicsValidation = false;
};

class EngineHost {
public:
    struct Impl;
    using NativeEventObserver = std::function<void(const void*)>;

    ~EngineHost();

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    void PollEvents();
    bool ShouldClose() const;
    void RequestClose();
    void SetTitle(const std::string& title);
    molga::WindowId WindowId() const;
    molga::WindowMetrics Metrics() const;
    molga::WindowPointerState Pointer() const;
    const molga::GraphicsDeviceInfo& GraphicsInfo() const;
    molga::GraphicsDevice& Graphics();
    const molga::GraphicsDevice& Graphics() const;
    molga::BeginFrameResult BeginFrame();
    bool RenderCapabilityFrame(float r, float g, float b, float a = 1.0f);
    void SetNativeEventObserver(NativeEventObserver observer);

    // ── Task 11.2 Step 7g: 종료 순서가 알아야 하는 소유자들 ─────────────────
    // Renderer와 TextRenderer는 진입점이 소유한다. host는 이름만 든다 — 값을
    // 들면 host가 두 번째 소유자가 되어 진입점의 reset이 실제로는 아무것도
    // 놓지 않게 된다. 등록하지 않으면 그 단계는 건너뛴다.
    void RegisterGpuConsumers(Renderer* renderer, TextRenderer* textRenderer);
    // 엔진이 소유한 최신 UI 스냅샷을 놓는 방법. Game View/런타임/에디터가
    // 자기 최신 결과를 들고 있는 자리를 여기 등록한다. 등록된 것이 없으면
    // 이 단계는 아무것도 놓지 않으며, 그 사실 자체가 blocker는 아니다.
    void RegisterEngineSnapshotReleaser(std::function<void()> releaser);
    // 단계 기계 자체. EngineShutdown이 부르며, Complete일 때만 host를 놓을 수
    // 있다. 멱등하다: 이미 Complete면 곧바로 Complete다.
    EngineShutdownStatus TearDownGpu(molga::text::TextDiagnosticSink& sink);

private:
    explicit EngineHost(std::unique_ptr<Impl> impl);
    void* NativeWindowHandle() const;

    std::unique_ptr<Impl> impl_;

    friend class ImGuiLayer;
    friend std::unique_ptr<EngineHost> EngineInit(const WindowConfig& config);
};

// Initialize SDL3, create the requested platform graphics device, and
// initialize the engine subsystems. The returned host owns the complete
// platform lifetime.
std::unique_ptr<EngineHost> EngineInit(const WindowConfig& config);

// ── Task 11.2 Step 7f/7g: 종료 순서는 이 함수 한 곳에만 있다 ────────────────
// Complete일 때만 host가 reset된다. 단계는 멱등하고 순서가 고정되어 있으며,
// 처음 성공한 GPU drain은 재시도를 건너 유지된다.
//
// void 오버로드는 없다. 있으면 결과를 무시하는 호출부가 하나 생기고, 그
// 호출부는 막힌 종료 위에서 그냥 진행한다.
EngineShutdownStatus EngineShutdown(std::unique_ptr<EngineHost>& host,
                                    molga::text::TextDiagnosticSink& sink);

namespace molga {
namespace detail {

// Renderer.h의 종료 단계 hook과 같은 규칙이고 같은 이유로 출하되는 빌드에
// 남는다: 관찰 대상이 프로덕션 종료 경로 그 자체이고, 단계 사이의 순서는
// 함수가 돌아온 뒤에 남은 것을 세는 것으로는 구별되지 않는다.
using EngineShutdownStageHook = void (*)(const char* stage);
void SetEngineShutdownStageHookForTest(EngineShutdownStageHook) noexcept;
// 단계를 알린다. 널 hook이면 아무 일도 하지 않는다. 공유 종료 함수
// (ShutdownRendererThenTextGpuResources)가 자기 단계를 여기로 보내므로, 순서
// 로그는 host 쪽과 renderer/텍스트 쪽이 한 벌로 남는다.
void NotifyEngineShutdownStage(const char* stage) noexcept;

} // namespace detail
} // namespace molga
