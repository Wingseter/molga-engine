// ── Task 11.2 Step 2c: the retryable-shutdown child ─────────────────────────
// Task 6.2에서 이 child가 존재한 이유는 "실패한 GPU idle wait는 프로세스를
// 죽인다"였다. 그 계약은 Task 11.2에서 삭제되었다. 지금 이 child가 있는 이유는
// 다르다: EngineShutdown은 상태를 돌려주고, 막힌 상태에서는 진입점이 **돌아가지
// 않는다**. 그 "돌아가지 않음"과 "그 뒤의 소멸자가 돌지 않음"은 한 프로세스의
// 종료 순서로만 관찰되므로, 관찰하려면 실제로 끝까지 도는 프로세스가 하나
// 필요하다.
//
// marker 파일에는 네 종류가 쌓인다.
//   - stage:*      EngineShutdown 안의 단계(Bootstrap의 hook과 TextRenderer의
//                  hook이 함께 낸다). 함수가 돌아온 뒤에 남은 것을 세는 것으로는
//                  단계 사이의 순서를 구별할 수 없기 때문에 안에서 낸다.
//   - status:*     각 시도의 결과.
//   - blocked:*    막힌 시점에 무엇이 아직 살아 있는가.
//   - 나머지       종료가 완료된 뒤의 평범한 반환/소멸자 사건.
#include "Core/Bootstrap.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/FontFace.h"
#include "Rendering/GraphicsDevice.h"
#include "Rendering/Renderer.h"
#include "Rendering/TextRenderer.h"
#include "Text/TextDiagnostic.h"
#include "UI/UILayoutSnapshot.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kBadArguments = 2;
constexpr int kSetupFailed = 3;

std::vector<std::string> g_markers;
std::vector<std::string> g_stages;
std::map<std::string, int> g_stageCounts;
std::filesystem::path g_markerFile;
bool g_writeMarkers = false;

void WriteMarker(const std::string& marker) {
    g_markers.push_back(marker);
    if (!g_writeMarkers) return;
    // ofstream 하나를 열고 바로 닫는다. 이 뒤에 프로세스가 정상 소멸자를
    // 돌리지 않고 끝날 수 있으므로 줄이 버퍼에 남아 있어서는 안 된다.
    std::ofstream file(g_markerFile, std::ios::app);
    file << marker << '\n';
}

// EngineShutdown 안의 단계. 그 순서가 곧 종료 순서다.
void RecordEngineStage(const char* stage) {
    g_stages.push_back(stage);
    ++g_stageCounts[stage];
    WriteMarker(std::string("stage:") + stage);
}

// TextRenderer가 내는 두 단계를 같은 로그에 감사 이름으로 옮긴다. 여기서
// 이름을 바꾸는 것이 중요하다: 이 두 사건은 ShutdownAfterGpuIdle **안**에서
// 일어나므로, 밖에서 표시를 찍으면 순서가 아니라 표시를 찍은 순서를 재게 된다.
void RecordTextStage(const char* stage) {
    const std::string name = std::string(stage) == "atlas_cleared"
                                 ? "ReleaseGlyphAtlas"
                                 : "DestroyTextServices";
    RecordEngineStage(name.c_str());
}

int Fail(const char* reason) {
    std::fprintf(stderr, "gpu-retirement-probe: %s\n", reason);
    return kSetupFailed;
}

const char* StatusName(EngineShutdownStatus status) {
    switch (status) {
        case EngineShutdownStatus::Complete: return "Complete";
        case EngineShutdownStatus::GpuDrainFailed: return "GpuDrainFailed";
        case EngineShutdownStatus::ExternalGpuLifetime:
            return "ExternalGpuLifetime";
    }
    return "Unknown";
}

// 진단 sink와 텍스트 런타임 guard의 파괴는 종료가 완료된 다음이어야 한다.
// 그 "다음"을 관찰하려면 파괴 자체가 사건이어야 하므로, 이름을 남기는 얇은
// 소유자를 쓴다.
struct MarkerOnDestroy {
    explicit MarkerOnDestroy(std::string name) : name_(std::move(name)) {}
    MarkerOnDestroy(const MarkerOnDestroy&) = delete;
    MarkerOnDestroy& operator=(const MarkerOnDestroy&) = delete;
    ~MarkerOnDestroy() { WriteMarker(name_); }
    std::string name_;
};

} // namespace

int main(int argc, char** argv) {
    bool injectIdleWaitFailure = false;
    bool holdExternalGlyphPage = false;
    bool writeShutdownMarkers = false;
    std::filesystem::path markerFile;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--inject-gpu-idle-wait-failure") {
            injectIdleWaitFailure = true;
        } else if (argument == "--hold-external-glyph-page") {
            holdExternalGlyphPage = true;
        } else if (argument == "--write-shutdown-markers") {
            writeShutdownMarkers = true;
        } else if (argument == "--marker-file") {
            if (index + 1 >= argc) return kBadArguments;
            markerFile = argv[++index];
        } else {
            std::fprintf(stderr, "gpu-retirement-probe: unknown argument %s\n",
                         argument.c_str());
            return kBadArguments;
        }
    }
    if (markerFile.empty()) return kBadArguments;
    g_markerFile = markerFile;
    g_writeMarkers = writeShutdownMarkers;
    molga::detail::SetEngineShutdownStageHookForTest(&RecordEngineStage);
    molga::detail::SetTextRendererShutdownStageHookForTest(&RecordTextStage);

    // 진단 sink는 종료보다 오래 산다. 그 파괴가 종료 완료보다 앞서면
    // EngineShutdown이 쓰는 sink가 이미 죽은 객체다.
    auto diagnosticSink = std::make_unique<molga::text::VectorTextDiagnosticSink>();
    MarkerOnDestroy textRuntimeGuardMarker("DestroyTextRuntimeGuard");

    WindowConfig config;
    config.title = "Molga GPU retirement shutdown probe";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    std::unique_ptr<EngineHost> host = EngineInit(config);
    if (!host) return Fail("could not initialize the engine host");

    auto renderer = std::make_unique<Renderer>();
    std::string error;
    if (!renderer->Init(&error)) return Fail(error.c_str());

    // 진짜 page 하나. 빈 glyph는 page를 차지하지 않으므로 cmap으로 찾은
    // 그릴 수 있는 glyph를 쓴다.
    molga::FontFace face;
    if (!face.LoadFromFile(MOLGA_GPU_RETIREMENT_PROBE_FONT, &error)) {
        return Fail(error.c_str());
    }
    const std::uint32_t glyphId = face.GlyphId(U'A');
    if (glyphId == 0U) return Fail("the probe font has no glyph for 'A'");

    // 이 renderer가 소유한 atlas 하나. host의 종료 순서는 이 TextRenderer의
    // atlas를 본다.
    auto textRenderer = std::make_unique<TextRenderer>();
    molga::GlyphAtlasKey key;
    key.fontGuid = "gpu-retirement-probe";
    key.fontRevision = "0";
    key.faceIndex = 0U;
    key.pixelSize = 24U;
    key.rasterScaleKey = 64U;
    key.renderMode = molga::GlyphRenderMode::Monochrome;
    key.glyphId = glyphId;

    molga::GlyphHandle handle;
    {
        auto scope = textRenderer->BeginGlyphCollection(1);
        handle = textRenderer->GlyphAtlas().GetGlyph(key, face, *diagnosticSink);
    }
    if (handle.proceduralTofu || handle.pageIdentity == 0U ||
        !handle.pageLifetime) {
        return Fail("the probe glyph did not land on an atlas page");
    }

    // 제출된 프레임이 그 page를 가리킨다.
    molga::BeginFrameResult acquired = host->BeginFrame();
    if (acquired.status != molga::FrameAcquireStatus::Acquired) {
        return Fail(acquired.error.empty() ? "swapchain is unavailable"
                                           : acquired.error.c_str());
    }
    if (!renderer->BeginFrame(std::move(acquired.frame), &error)) {
        return Fail(error.c_str());
    }
    renderer->RetainUntilFrameComplete(handle.pageIdentity, handle.pageLifetime);
    if (!renderer->SubmitFrame(&error)) return Fail(error.c_str());

    // 엔진이 소유한 최신 스냅샷 자리. Step 7g의 "release engine-owned
    // snapshots" 단계가 실제로 무언가를 놓는지 밖에서 보이게 한다 — 등록만
    // 하고 부르지 않는 구현은 이 marker가 없는 것으로 드러난다.
    auto engineSnapshot = std::make_shared<const molga::ui::UISnapshot>();
    host->RegisterEngineSnapshotReleaser([&engineSnapshot]() {
        if (!engineSnapshot) return;
        engineSnapshot.reset();
        WriteMarker("engine-snapshot-released");
    });
    host->RegisterGpuConsumers(renderer.get(), textRenderer.get());

    if (injectIdleWaitFailure) {
        molga::detail::SetGpuIdleWaitFailureInjectionForTest(true);
    }
    // 외부 page 토큰 하나를 계속 든다. 이 지분이 살아 있는 동안 종료는
    // ExternalGpuLifetime이어야 한다.
    std::shared_ptr<const void> externalPage;
    if (holdExternalGlyphPage) externalPage = handle.pageLifetime;
    handle.pageLifetime.reset();

    EngineShutdownStatus status = EngineShutdown(host, *diagnosticSink);
    WriteMarker(std::string("status:") + StatusName(status));
    if (status != EngineShutdownStatus::Complete) {
        // 막힌 상태의 계약: 아무것도 부수어지지 않았고, 평범한 반환도
        // 소멸자도 아직 일어나지 않았다.
        if (textRenderer->GlyphAtlas().ResidentPageCount() != 0U) {
            WriteMarker("blocked:atlas-alive");
        }
        if (host && !host->Graphics().IsDestroyed()) {
            WriteMarker("blocked:device-alive");
        }
        if (host) WriteMarker("blocked:host-alive");
        // 막힌 시점에 평범한 반환/소멸자 표식이 하나도 없다는 것을 그
        // 시점에서 확인한다. 부모가 marker 집합으로 세면 재시도가 성공한
        // 뒤의 같은 이름이 함께 잡혀 이 사실을 잴 수 없다.
        const bool anyOrdinaryMarker =
            std::find(g_markers.begin(), g_markers.end(),
                      std::string("ReturnFromEngine")) != g_markers.end() ||
            std::find(g_markers.begin(), g_markers.end(),
                      std::string("DestroyDiagnosticSink")) != g_markers.end() ||
            std::find(g_markers.begin(), g_markers.end(),
                      std::string("DestroyTextRuntimeGuard")) != g_markers.end();
        if (!anyOrdinaryMarker) WriteMarker("blocked:no-return-markers");

        // 이제 주입된 고장과 알려진 외부 소유자만 걷고 같은 host로 다시
        // 시도한다. 재시도가 완료할 때까지 이 범위를 벗어나지 않는다.
        molga::detail::SetGpuIdleWaitFailureInjectionForTest(false);
        externalPage.reset();
        status = EngineShutdown(host, *diagnosticSink);
        WriteMarker(std::string("status:") + StatusName(status));
        if (status != EngineShutdownStatus::Complete) {
            return Fail("the retried shutdown is still blocked");
        }
    }

    if (textRenderer->GlyphAtlas().ResidentPageCount() == 0U) {
        WriteMarker("atlas-destroyed");
    }
    if (!host) WriteMarker("device-destroyed");
    renderer.reset();
    textRenderer.reset();

    // 종료가 완료된 다음에야 평범한 반환과 소멸자가 온다.
    WriteMarker("ReturnFromEngine");
    diagnosticSink.reset();
    WriteMarker("DestroyDiagnosticSink");

    std::string order = "order:";
    for (std::size_t index = 0; index < g_markers.size(); ++index) {
        std::string entry = g_markers[index];
        if (entry.rfind("stage:", 0) == 0) entry = entry.substr(6);
        if (entry.rfind("status:", 0) == 0 || entry.rfind("blocked:", 0) == 0) {
            continue;
        }
        if (order.size() > 6U) order += ',';
        order += entry;
    }
    order += ",DestroyTextRuntimeGuard";
    std::printf("%s\n", order.c_str());

    std::string tail = "order-tail:";
    for (std::size_t index = 0; index < g_stages.size(); ++index) {
        if (g_stages[index] != "ReleaseGlyphAtlas") continue;
        for (std::size_t at = index; at < g_stages.size(); ++at) {
            if (at != index) tail += ',';
            tail += g_stages[at];
        }
        break;
    }
    std::printf("%s\n", tail.c_str());

    for (const auto& [stage, count] : g_stageCounts) {
        std::printf("stage-count:%s=%d\n", stage.c_str(), count);
    }
    return 0;
}
