// ── Task 6.2 Step 1d: the renderer-shutdown child ───────────────────────────
// 실패한 GPU idle wait는 프로세스를 죽인다. 그 순서를 관찰하려면 죽어도 되는
// 프로세스가 필요하므로, test_gpu_retirement는 이 child를 띄우고 종료 시그널과
// marker 파일만 읽는다. molga_text_runtime_probe와 같은 모양이다: 자기 root도
// 경로 매크로도 스스로 유도하지 않고, 부모가 넘긴 리터럴 argv만 읽는다.
//
// child가 하는 일은 하나다. 제출된 프레임이 살아 있는 atlas page 토큰을
// 붙들고 있는 상태에서 렌더러를 내린다. injection이 켜져 있으면 그 안의
// SDL_WaitForGPUIdle이 실패를 보고하고, 어떤 teardown도 일어나기 전에
// std::abort()가 나야 한다.
//
// marker 파일에는 두 종류가 쌓인다. Shutdown *안*의 단계(stage: 접두사,
// Renderer가 hook으로 알려 준다)와 Shutdown이 돌아온 *뒤*의 단계다. 앞의
// 것이 없으면 abort를 drain 뒤나 GPU 자원 파괴 루프 뒤로 옮겨도 부모가
// 차이를 볼 수 없다 — 어느 쪽이든 프로세스는 죽고 뒤의 두 marker는 남지
// 않기 때문이다.
#include "Core/Bootstrap.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/FontFace.h"
#include "Rendering/GraphicsDevice.h"
#include "Rendering/Renderer.h"
#include "Text/TextDiagnostic.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// 종료 코드. 0은 성공, 2는 argv 계약 위반, 3은 준비 실패다. 부모는 0과
// SIGABRT만 기대하므로, 그 밖의 값은 전부 "child가 재려던 것을 재지 못했다"는
// 뜻으로 읽힌다.
constexpr int kBadArguments = 2;
constexpr int kSetupFailed = 3;

std::vector<std::string> g_markers;
std::filesystem::path g_markerFile;
bool g_writeMarkers = false;

void WriteMarker(const std::string& marker) {
    g_markers.push_back(marker);
    if (!g_writeMarkers) return;
    // ofstream 하나를 열고 바로 닫는다. abort가 이 뒤에 올 수 있으므로 줄이
    // 버퍼에 남아 있어서는 안 된다.
    std::ofstream file(g_markerFile, std::ios::app);
    file << marker << '\n';
}

// Renderer::Shutdown이 단계를 지날 때마다 부른다. abort는 그 hook들보다
// 앞서야 하므로, 죽은 프로세스가 남긴 stage: 줄이 곧 "abort보다 먼저 일어난
// 일"의 전부다. 이것이 없으면 abort를 drain 뒤로, 또는 GPU 자원 파괴 루프
// 뒤로 옮겨도 부모는 차이를 볼 수 없다.
void RecordShutdownStage(const char* stage) {
    WriteMarker(std::string("stage:") + stage);
}

int Fail(const char* reason) {
    std::fprintf(stderr, "gpu-retirement-probe: %s\n", reason);
    return kSetupFailed;
}

} // namespace

int main(int argc, char** argv) {
    bool injectIdleWaitFailure = false;
    bool writeShutdownMarkers = false;
    std::filesystem::path markerFile;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--inject-gpu-idle-wait-failure") {
            injectIdleWaitFailure = true;
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
    molga::detail::SetRendererShutdownStageHookForTest(&RecordShutdownStage);

    WindowConfig config;
    config.title = "Molga GPU retirement shutdown probe";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    std::unique_ptr<EngineHost> host = EngineInit(config);
    if (!host) return Fail("could not initialize the engine host");

    {
        Renderer renderer;
        std::string error;
        if (!renderer.Init(&error)) return Fail(error.c_str());

        // 진짜 page 하나. 빈 glyph는 page를 차지하지 않으므로 cmap으로 찾은
        // 그릴 수 있는 glyph를 쓴다.
        molga::FontFace face;
        if (!face.LoadFromFile(MOLGA_GPU_RETIREMENT_PROBE_FONT, &error)) {
            return Fail(error.c_str());
        }
        const std::uint32_t glyphId = face.GlyphId(U'A');
        if (glyphId == 0U) return Fail("the probe font has no glyph for 'A'");

        molga::GlyphAtlasCache atlas;
        molga::text::VectorTextDiagnosticSink sink;
        molga::GlyphAtlasKey key;
        key.fontGuid = "gpu-retirement-probe";
        key.fontRevision = "0";
        key.faceIndex = 0U;
        key.pixelSize = 24U;
        key.rasterScaleKey = 64U;
        key.renderMode = molga::GlyphRenderMode::Monochrome;
        key.glyphId = glyphId;

        atlas.BeginFrame(1);
        molga::GlyphHandle handle = atlas.GetGlyph(key, face, sink);
        atlas.EndCollection(1);
        if (handle.proceduralTofu || handle.pageIdentity == 0U ||
            !handle.pageLifetime) {
            return Fail("the probe glyph did not land on an atlas page");
        }

        // 제출된 프레임이 그 page를 가리킨다. 이 상태로 내려가는 것이 이
        // child의 전부다.
        molga::BeginFrameResult acquired = host->BeginFrame();
        if (acquired.status != molga::FrameAcquireStatus::Acquired) {
            return Fail(acquired.error.empty() ? "swapchain is unavailable"
                                               : acquired.error.c_str());
        }
        if (!renderer.BeginFrame(std::move(acquired.frame), &error)) {
            return Fail(error.c_str());
        }
        renderer.RetainUntilFrameComplete(handle.pageIdentity,
                                          handle.pageLifetime);
        if (!renderer.SubmitFrame(&error)) return Fail(error.c_str());

        if (injectIdleWaitFailure) {
            molga::detail::SetGpuIdleWaitFailureInjectionForTest(true);
        }
        renderer.Shutdown();
        // injection이 켜져 있으면 위 호출에서 프로세스가 죽으므로 여기에
        // 도달하지 않는다.

        // Shutdown이 실제로 반납 큐를 비웠는지 여기서 증명된다: 렌더러가
        // 아직 자기 사본을 붙들고 있으면 아래 해제가 false다.
        handle.pageLifetime.reset();
        if (atlas.LiveExternalPagePinCount() != 0U) {
            return Fail("the renderer still owns a submitted page token");
        }
        if (!atlas.ReleaseAfterGpuIdle()) {
            return Fail("the atlas refused a post-idle release");
        }
        WriteMarker("atlas-destroyed");
    }

    EngineShutdown(host);
    WriteMarker("device-destroyed");

    std::string order = "order:";
    for (std::size_t index = 0; index < g_markers.size(); ++index) {
        if (index != 0U) order += ',';
        order += g_markers[index];
    }
    std::printf("%s\n", order.c_str());
    return 0;
}
