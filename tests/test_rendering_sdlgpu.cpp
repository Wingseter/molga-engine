#include "Core/Bootstrap.h"
#include "AssetDatabaseTestAuthority.h"
#include "Common/Log.h"
#include "Common/RingBufferSink.h"
#include "Core/AssetDatabase.h"
#include "Core/PathService.h"
#include "Core/TextureManager.h"
#include "ECS/Components/MarrowRenderer.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/PointLight2D.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/ShadowOccluder2D.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/TilemapRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/GameObject.h"
#include "Rendering/Camera2D.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/FontFace.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/PostProcessPipeline.h"
#include "Rendering/PostProcessProfile2D.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/RenderPass.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Texture.h"
#include "Rendering/TextRenderer.h"
#include "Systems/Particle.h"
#include "Text/TextDiagnostic.h"
#include "doctest.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

bool Near(std::uint8_t actual, int expected, int tolerance = 3) {
    const int value = static_cast<int>(actual);
    return value >= expected - tolerance && value <= expected + tolerance;
}

std::array<std::uint8_t, 4> Pixel(
    const std::vector<std::uint8_t>& pixels, int width, int x, int y) {
    const std::size_t offset =
        static_cast<std::size_t>((y * width + x) * 4);
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2],
            pixels[offset + 3]};
}

bool Acquire(EngineHost& host, Renderer& renderer, std::string& error) {
    molga::BeginFrameResult result = host.BeginFrame();
    if (result.status != molga::FrameAcquireStatus::Acquired) {
        error = result.error.empty() ? "swapchain unavailable" : result.error;
        return false;
    }
    return renderer.BeginFrame(std::move(result.frame), &error);
}

bool RenderOutputFrame(
    EngineHost& host, Renderer& renderer, molga::GameOutputRenderer& output,
    const std::vector<std::shared_ptr<GameObject>>& objects,
    molga::RenderTarget& target, molga::PixelSize logicalSize,
    molga::GameOutputScaleMode scaleMode, molga::GameOutputResult& result,
    std::string& error) {
    if (!Acquire(host, renderer, error)) return false;
    result = output.Render(
        objects,
        {{target.Width(), target.Height()}, logicalSize, scaleMode, &target},
        renderer, ShaderManager::Get().Get("default"));
    if (!result.presented) {
        error = "game output was not presented";
        return false;
    }
    return renderer.SubmitFrame(&error);
}

bool RenderBatchFrame(EngineHost& host, Renderer& renderer,
                      molga::RenderTarget& target,
                      const std::vector<molga::Vertex2D>& vertices,
                      const molga::BatchKey& key,
                      const molga::Color4f& clear, std::string& error) {
    if (!Acquire(host, renderer, error) ||
        !renderer.BeginTarget(target, clear, molga::LoadAction::Clear,
                              &error)) {
        return false;
    }
    Shader* batch = ShaderManager::Get().Get("batch");
    if (!batch) {
        error = "batch shader is unavailable";
        return false;
    }
    Camera2D camera(static_cast<float>(target.Width()),
                    static_cast<float>(target.Height()));
    bool submitted = false;
    {
        molga::RenderPass pass(renderer, batch, &camera);
        submitted = renderer.SubmitBatch(vertices, key, nullptr, &error);
    }
    return submitted && renderer.EndTarget(&error) &&
           renderer.SubmitFrame(&error);
}

std::vector<std::uint8_t> ReadTarget(EngineHost& host,
                                     const molga::RenderTarget& target,
                                     std::string& error) {
    std::vector<std::uint8_t> pixels;
    if (!host.Graphics().ReadbackRGBA8(
            target.ColorView(),
            {0, 0, static_cast<std::uint32_t>(target.Width()),
             static_cast<std::uint32_t>(target.Height())},
            pixels, error)) {
        pixels.clear();
    }
    return pixels;
}

bool IsColor(const std::array<std::uint8_t, 4>& pixel,
             int red, int green, int blue, int tolerance = 3) {
    return Near(pixel[0], red, tolerance) && Near(pixel[1], green, tolerance) &&
           Near(pixel[2], blue, tolerance) && Near(pixel[3], 255, tolerance);
}

int LinearSrgbByte(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    const float encoded = linear <= 0.0031308f
        ? linear * 12.92f
        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(std::lround(encoded * 255.0f));
}

molga::PostProcessProfile2D MakePostProfile(const nlohmann::json& effects) {
    molga::PostProcessProfile2D profile;
    std::string error;
    if (!molga::PostProcessProfile2D::Deserialize(
            {{"schemaVersion", 1}, {"effects", effects}}, profile, &error)) {
        throw std::runtime_error(error);
    }
    return profile;
}

bool RunPostProcessFrame(
    EngineHost& host, Renderer& renderer, molga::PostProcessPipeline& pipeline,
    const molga::PostProcessProfile2D& profile, molga::RenderTarget& destination,
    const molga::Color4f& sceneClear, const molga::Color4f& destinationClear,
    molga::PixelRect destinationRect,
    molga::PostProcessExecutionResult& result, std::string& error) {
    if (!Acquire(host, renderer, error)) return false;
    if (!renderer.BeginTarget(destination, destinationClear,
                              molga::LoadAction::Clear, &error) ||
        !renderer.EndTarget(&error) ||
        !renderer.BeginTarget(pipeline.SceneTarget(), sceneClear,
                              molga::LoadAction::Clear, &error) ||
        !renderer.EndTarget(&error)) {
        return false;
    }
    molga::ColorAttachmentDescriptor attachment;
    attachment.view = destination.ColorView();
    attachment.loadAction = molga::LoadAction::Load;
    attachment.storeAction = molga::StoreAction::Store;
    result = pipeline.Execute(
        profile, renderer, attachment, molga::TextureFormat::SRGBA8,
        {destination.Width(), destination.Height()}, destinationRect);
    if (!result.success) {
        error = result.error;
        return false;
    }
    return renderer.SubmitFrame(&error);
}

int MaxRedAround(const std::vector<std::uint8_t>& pixels, int width,
                 int centerX, int centerY, int radius) {
    int maximum = 0;
    for (int y = centerY - radius; y <= centerY + radius; ++y) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            if (x == centerX && y == centerY) continue;
            maximum = std::max(maximum,
                static_cast<int>(Pixel(pixels, width, x, y)[0]));
        }
    }
    return maximum;
}

int MaxRedAnnulus(const std::vector<std::uint8_t>& pixels, int width,
                  int centerX, int centerY, int innerRadius, int outerRadius) {
    int maximum = 0;
    for (int y = centerY - outerRadius; y <= centerY + outerRadius; ++y) {
        for (int x = centerX - outerRadius; x <= centerX + outerRadius; ++x) {
            if (std::abs(x - centerX) <= innerRadius &&
                std::abs(y - centerY) <= innerRadius) {
                continue;
            }
            maximum = std::max(maximum,
                static_cast<int>(Pixel(pixels, width, x, y)[0]));
        }
    }
    return maximum;
}

std::uint16_t FloatToHalf(float value) {
    if (value == 0.0f) return 0U;
    const bool negative = value < 0.0f;
    int exponent = 0;
    const float mantissa = std::frexp(std::abs(value), &exponent) * 2.0f - 1.0f;
    int halfExponent = exponent + 14;
    int halfMantissa = static_cast<int>(std::lround(mantissa * 1024.0f));
    if (halfMantissa == 1024) {
        halfMantissa = 0;
        ++halfExponent;
    }
    if (halfExponent <= 0 || halfExponent >= 31) {
        return static_cast<std::uint16_t>((negative ? 0x8000U : 0U) |
                                          (halfExponent >= 31 ? 0x7C00U : 0U));
    }
    return static_cast<std::uint16_t>((negative ? 0x8000U : 0U) |
        (static_cast<unsigned>(halfExponent) << 10U) |
        static_cast<unsigned>(halfMantissa));
}

bool UploadHdrImage(EngineHost& host, const molga::RenderTarget& target,
                    const std::array<float, 4>& background,
                    int brightX, int brightY,
                    const std::array<float, 4>& bright,
                    std::string& error) {
    const int width = target.Width();
    const int height = target.Height();
    if (width <= 0 || height <= 0 || brightX < 0 || brightY < 0 ||
        brightX >= width || brightY >= height) {
        error = "invalid HDR upload fixture";
        return false;
    }
    std::vector<std::uint16_t> pixels(
        static_cast<std::size_t>(width * height * 4));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto& color = x == brightX && y == brightY
                ? bright : background;
            const std::size_t offset =
                static_cast<std::size_t>((y * width + x) * 4);
            for (std::size_t channel = 0; channel < 4U; ++channel) {
                pixels[offset + channel] = FloatToHalf(color[channel]);
            }
        }
    }
    return host.Graphics().UploadTextureImmediate(
        target.ColorView(),
        {0, 0, static_cast<std::uint32_t>(width),
         static_cast<std::uint32_t>(height)},
        pixels.data(), pixels.size() * sizeof(std::uint16_t),
        static_cast<std::uint32_t>(width * 8), error);
}

bool RunUploadedPostProcessFrame(
    EngineHost& host, Renderer& renderer, molga::PostProcessPipeline& pipeline,
    const molga::PostProcessProfile2D& profile, molga::RenderTarget& destination,
    molga::PostProcessExecutionResult& result, std::string& error) {
    if (!Acquire(host, renderer, error)) return false;
    if (!renderer.BeginTarget(destination, {0, 0, 0, 1},
                              molga::LoadAction::Clear, &error) ||
        !renderer.EndTarget(&error)) {
        return false;
    }
    molga::ColorAttachmentDescriptor attachment;
    attachment.view = destination.ColorView();
    attachment.loadAction = molga::LoadAction::Load;
    attachment.storeAction = molga::StoreAction::Store;
    result = pipeline.Execute(
        profile, renderer, attachment, molga::TextureFormat::SRGBA8,
        {destination.Width(), destination.Height()},
        {0, 0, destination.Width(), destination.Height()});
    if (!result.success) {
        error = result.error;
        return false;
    }
    return renderer.SubmitFrame(&error);
}

void WriteJsonFile(const std::filesystem::path& path,
                   const nlohmann::json& document) {
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        << document.dump(2) << '\n';
}

void WriteSinglePixelPpm(const std::filesystem::path& path,
                         const std::array<std::uint8_t, 3>& rgb) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "P6\n1 1\n255\n";
    file.write(reinterpret_cast<const char*>(rgb.data()),
               static_cast<std::streamsize>(rgb.size()));
}

void WritePpm(const std::filesystem::path& path, int width, int height,
              const std::vector<std::uint8_t>& rgb) {
    if (width <= 0 || height <= 0 ||
        rgb.size() != static_cast<std::size_t>(width * height * 3)) {
        throw std::runtime_error("invalid PPM fixture");
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    file.write(reinterpret_cast<const char*>(rgb.data()),
               static_cast<std::streamsize>(rgb.size()));
}

struct OutputCameraFixture {
    std::shared_ptr<GameObject> object;
    Camera* camera = nullptr;
};

OutputCameraFixture AddOutputCamera(
    std::vector<std::shared_ptr<GameObject>>& objects, const char* name,
    CameraOutputRole role, const CameraViewport& viewport, int depth,
    const Color& background) {
    auto object = std::make_shared<GameObject>(name);
    object->AddComponent<Transform>(0.0f, 0.0f);
    Camera* camera = object->AddComponent<Camera>();
    camera->SetOutputRole(role);
    if (!camera->SetViewport(viewport)) return {};
    camera->SetDepth(depth);
    camera->SetPixelPerfect(true);
    camera->SetPixelZoom(1);
    camera->SetBackgroundColor(background);
    objects.push_back(object);
    return {std::move(object), camera};
}

void AddLayerPixel(std::vector<std::shared_ptr<GameObject>>& objects,
                   const char* name, int layer, const Color& color) {
    auto object = std::make_shared<GameObject>(name);
    object->SetLayer(layer);
    object->AddComponent<Transform>(0.0f, 0.0f);
    auto* sprite = object->AddComponent<SpriteRenderer>();
    sprite->SetSize(1.0f, 1.0f);
    sprite->SetColor(color);
    objects.push_back(std::move(object));
}

// ── Task 6.3: 제출된 프레임이 붙들고 있는 glyph page 하나 ────────────────────
// 두 종료 순서를 같은 모양의 상태 위에서 재기 위한 준비. 이 픽스처가 끝나면
// 살아 있는 외부 page 지분은 정확히 하나이고, 그 하나를 들고 있는 것은
// 제출된 프레임(=renderer의 반납 큐)뿐이다.
struct SubmittedGlyphPageFixture {
    SubmittedGlyphPageFixture(EngineHost& host, Renderer& renderer,
                              TextRenderer& text) {
        std::string error;
        REQUIRE_MESSAGE(face.LoadFromFile(MOLGA_TEST_KOREAN_FONT_PATH, &error),
                        error);
        const std::uint32_t glyphId = face.GlyphId(U'가');
        REQUIRE(glyphId != 0U);

        molga::GlyphAtlasKey key;
        key.fontGuid = "shutdown-order";
        key.fontRevision = "0";
        key.faceIndex = 0U;
        key.pixelSize = 32U;
        key.rasterScaleKey = 64U;
        key.renderMode = molga::GlyphRenderMode::Monochrome;
        key.glyphId = glyphId;

        molga::GlyphHandle handle;
        {
            auto collection = text.BeginGlyphCollection(1);
            handle = text.GlyphAtlas().GetGlyph(key, face, sink);
        }
        REQUIRE_FALSE(handle.proceduralTofu);
        REQUIRE(handle.pageIdentity != 0U);
        REQUIRE(handle.pageLifetime);
        pageIdentity = handle.pageIdentity;
        REQUIRE(text.GlyphAtlas().ResidentPageCount() == 1U);

        // 텍스처는 지분을 통해서만 꺼낸다. GlyphInfo만 복사해 둔 소비자는 이
        // 질문을 할 수 없다는 것이 RetainedTexture의 내용이고, 그 계약은 진짜
        // 텍스처가 있는 곳에서만 관찰된다 — 헤드리스 캐시에서는 두 표현이
        // 똑같이 널이라 어느 쪽으로 굳혀도 단언이 움직이지 않는다.
        Texture* retained = molga::RetainedTexture(handle);
        REQUIRE(retained != nullptr);
        REQUIRE(retained->IsValid());
        REQUIRE(retained == handle.glyph.texture);

        molga::BeginFrameResult acquired = host.BeginFrame();
        REQUIRE(acquired.status == molga::FrameAcquireStatus::Acquired);
        REQUIRE_MESSAGE(renderer.BeginFrame(std::move(acquired.frame), &error),
                        error);
        renderer.RetainUntilFrameComplete(handle.pageIdentity,
                                          handle.pageLifetime);
        REQUIRE(renderer.ActiveFrameRetainedPageCount() == 1U);
        REQUIRE_MESSAGE(renderer.SubmitFrame(&error), error);

        // 이 테스트의 사본을 놓는다. 남은 외부 지분은 제출된 프레임의 것
        // 하나뿐이어야, 아래 두 케이스가 재는 것이 "종료 순서"가 된다.
        handle.pageLifetime.reset();
        REQUIRE(text.GlyphAtlas().LiveExternalPagePinCount() == 1U);

        // 반대편: 지분을 놓은 handle은 텍스처를 돌려주지 않는다. 원시 필드는
        // 여전히 그 값을 담고 있으므로, 이 둘의 차이가 곧 "GlyphInfo는 수명
        // 단위가 아니다"이다.
        CHECK(molga::RetainedTexture(handle) == nullptr);
        CHECK(handle.glyph.texture == retained);
    }

    molga::FontFace face;
    molga::text::VectorTextDiagnosticSink sink;
    std::uint64_t pageIdentity = 0U;
};

// ── Task 6.3: 거절이 조용하지 않다는 것까지가 계약이다 ──────────────────────
// 진입점 두 곳(src/main.cpp, src/runtime_main.cpp)은 어떤 테스트 바이너리도
// 컴파일하지 않으므로, 그쪽에서 순서가 뒤집혔을 때 남는 유일한 실행 시 증거가
// TextRenderer::Shutdown의 이 Log::Error다. 메시지가 존재 이유의 전부인
// 경로를 아무도 단언하지 않으면, 그 메시지는 지워져도 아무 일도 일어나지
// 않는다 — 기구가 아니라 보고가 fail-open이 된다.
class TextRendererErrorLog {
public:
    TextRendererErrorLog() : sink_(std::make_shared<Log::RingBufferSink>(16)) {
        Log::AddSink(sink_);
    }
    ~TextRendererErrorLog() { Log::RemoveSink(sink_); }
    TextRendererErrorLog(const TextRendererErrorLog&) = delete;
    TextRendererErrorLog& operator=(const TextRendererErrorLog&) = delete;

    std::size_t ErrorCount() const {
        std::size_t count = 0U;
        for (const Log::LogMessage& message : sink_->Snapshot()) {
            if (message.severity == Log::Severity::Error &&
                message.category == "TextRenderer") {
                ++count;
            }
        }
        return count;
    }

    std::string LastErrorMessage() const {
        std::string last;
        for (const Log::LogMessage& message : sink_->Snapshot()) {
            if (message.severity == Log::Severity::Error &&
                message.category == "TextRenderer") {
                last = message.message;
            }
        }
        return last;
    }

private:
    std::shared_ptr<Log::RingBufferSink> sink_;
};

// Renderer::Shutdown이 지나간 단계를 순서대로 받아 적는다.
std::vector<std::string>* g_shutdownStages = nullptr;
void RecordShutdownStage(const char* stage) {
    if (g_shutdownStages != nullptr) g_shutdownStages->push_back(stage);
}

class ShutdownStageLog {
public:
    ShutdownStageLog() {
        g_shutdownStages = &stages;
        molga::detail::SetRendererShutdownStageHookForTest(&RecordShutdownStage);
    }
    ~ShutdownStageLog() {
        molga::detail::SetRendererShutdownStageHookForTest(nullptr);
        g_shutdownStages = nullptr;
    }
    ShutdownStageLog(const ShutdownStageLog&) = delete;
    ShutdownStageLog& operator=(const ShutdownStageLog&) = delete;

    std::vector<std::string> stages;
};

} // namespace

TEST_CASE("SDL_GPU RHI rejects stale handles and invalid frame ordering") {
    WindowConfig config;
    config.title = "Molga SDL_GPU RHI contract";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    std::string error;
    molga::BufferDescriptor descriptor;
    descriptor.size = 16;
    descriptor.usage = molga::GpuBufferUsage::Vertex;
    molga::BufferHandle stale = host->Graphics().CreateBuffer(descriptor, error);
    REQUIRE(stale);
    molga::BufferHandle destroyTarget = stale;
    host->Graphics().DestroyBuffer(destroyTarget);
    CHECK_FALSE(destroyTarget);
    CHECK_FALSE(host->Graphics().IsAlive(stale));

    molga::BufferHandle live = host->Graphics().CreateBuffer(descriptor, error);
    REQUIRE(live);
    molga::BeginFrameResult acquired = host->BeginFrame();
    REQUIRE(acquired.status == molga::FrameAcquireStatus::Acquired);
    const std::array<std::uint32_t, 4> values{};
    CHECK_FALSE(acquired.frame.UploadBuffer(
        live, 8, values.data(), sizeof(values), true, &error));
    CHECK(error.find("exceeds") != std::string::npos);
    molga::RenderPassDescriptor pass;
    pass.color.swapchain = true;
    pass.color.loadAction = molga::LoadAction::Clear;
    REQUIRE(acquired.frame.BeginRenderPass(pass, &error));
    CHECK_FALSE(acquired.frame.BeginRenderPass(pass, &error));
    CHECK(error.find("nesting") != std::string::npos);
    CHECK_FALSE(acquired.frame.PushVertexUniform(
        0, values.data(), sizeof(std::uint32_t), &error));
    CHECK(error.find("16-byte") != std::string::npos);
    CHECK_FALSE(acquired.frame.UploadBuffer(
        live, 0, values.data(), sizeof(values), true, &error));
    CHECK(error.find("precede all render passes") != std::string::npos);
    acquired.frame.EndRenderPass();
    CHECK(acquired.frame.Submit(&error));
    host->Graphics().DestroyBuffer(live);
}

// ── Task 6.2: 진짜 제출 fence ───────────────────────────────────────────────
// GpuRetirementQueue의 CPU 케이스는 가짜 fence로 "신호하기 전에는 놓지 않는다"를
// 재고, 여기서는 그 경계의 반대쪽 — 실제 SDL_GPU 제출이 완료를 보고하는가 — 을
// 잰다. 이 단언이 없으면 IsSignaled()가 언제나 false인 구현도, 극성이 뒤집힌
// 구현도 CPU 스위트를 전부 통과한다.
//
// 벤더된 SDL 3.4.14의 Metal 백엔드에서 SDL_QueryGPUFence는 문서와 반대로
// "진행 중"을 돌려준다(METAL_QueryFence -> METAL_INTERNAL_IsFenceBusy). 그래서
// 이 케이스는 장치 생성 시 측정한 극성이 실제로 적용될 때만 통과한다.
TEST_CASE("SDL_GPU submission fences report completion after a device idle wait") {
    WindowConfig config;
    config.title = "Molga SDL_GPU submission fence";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);

    std::string error;
    molga::BeginFrameResult acquired = host->BeginFrame();
    REQUIRE(acquired.status == molga::FrameAcquireStatus::Acquired);
    molga::RenderPassDescriptor pass;
    pass.color.swapchain = true;
    pass.color.loadAction = molga::LoadAction::Clear;
    REQUIRE(acquired.frame.BeginRenderPass(pass, &error));
    acquired.frame.EndRenderPass();

    std::unique_ptr<molga::IGpuCompletionFence> fence;
    REQUIRE_MESSAGE(acquired.frame.SubmitAndAcquireFence(fence, &error), error);
    REQUIRE(fence);
    REQUIRE_MESSAGE(host->Graphics().WaitIdle(&error), error);
    CHECK(fence->IsSignaled());

    // 이미 제출된 프레임은 두 번째 fence를 만들지 않는다. 실패한 호출이
    // 낡은 fence를 남겨 두면 그 다음 Poll이 남의 제출을 보고 page를 놓는다.
    std::unique_ptr<molga::IGpuCompletionFence> second;
    CHECK_FALSE(acquired.frame.SubmitAndAcquireFence(second, &error));
    CHECK_FALSE(second);

    // fence 객체는 장치보다 먼저 사라져야 한다. 이 줄이 그 순서다.
    fence.reset();
}

// 위 케이스는 한쪽 방향만 잰다. 완료된 fence에 대한 CHECK(IsSignaled())는
// IsSignaled()가 통째로 `return true`인 구현도 만족시키는데, 그것이야말로 이
// 마일스톤이 막으려는 방향이다 — 제출된 순간 모든 page가 반납되고, GPU가
// 읽고 있는 텍스처가 그 밑에서 사라진다.
//
// 진짜 fence를 비행 중에 붙잡는 단언은 쓸 수 없다(GPU가 먼저 끝내면 그 단언은
// 사라진다). 대신 fail-closed 세 갈래를 완료가 증명된 진짜 fence 위에서
// 결정적으로 관찰한다: 장치가 없는 것, fence가 없는 것, 극성을 측정하지 못한
// 것. 셋 다 "아직 아니다"여야 한다.
TEST_CASE("a real submission fence answers false whenever it lacks proof") {
    WindowConfig config;
    config.title = "Molga SDL_GPU fence polarity";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);

    molga::detail::CompletionFenceProbe probe =
        molga::detail::ProbeCompletionFenceForTest(host->Graphics());
    REQUIRE(probe.valid);

    // 성공 증인. 이것이 없으면 IsSignaled()가 언제나 false인 구현이 아래 세
    // 단언을 전부 통과한다.
    CHECK(probe.completed->IsSignaled());

    // 증거가 없는 세 갈래. 이 셋이 `return true`를 죽인다.
    CHECK_FALSE(probe.withoutDevice->IsSignaled());
    CHECK_FALSE(probe.withoutFence->IsSignaled());
    CHECK_FALSE(probe.completedWithUnusablePolarity->IsSignaled());

    // 측정한 극성은 이 장치의 실제 답과 맞아야 한다. 벤더된 SDL 3.4.14의
    // Metal 백엔드에서 SDL_QueryGPUFence는 완료된 fence에 false를 돌려주므로
    // (METAL_QueryFence -> METAL_INTERNAL_IsFenceBusy), 여기서 극성은
    // ReportsBusy이고 IsSignaled()는 원값과 어긋나야 한다. 극성을 상수로
    // 박아 두면 SDL이 고쳐지는 날 이 줄이 먼저 깨진다.
    CHECK(probe.polarity != molga::FenceQueryPolarity::Unusable);
    if (probe.polarity == molga::FenceQueryPolarity::ReportsBusy) {
        CHECK_FALSE(probe.rawQueryOnCompletedFence);
        CHECK(probe.completed->IsSignaled() != probe.rawQueryOnCompletedFence);
    } else {
        CHECK(probe.rawQueryOnCompletedFence);
        CHECK(probe.completed->IsSignaled() == probe.rawQueryOnCompletedFence);
    }
}

TEST_CASE("SDL_GPU pipeline keys are deterministic and state-complete") {
    molga::ShaderBundleEntry entry;
    entry.name = "key-test";
    entry.revision = 42;
    entry.vertexStride = 8;
    entry.vertexAttributes.push_back({0, "Float2", 0});
    molga::GraphicsPipelineDescriptor descriptor;
    descriptor.shader = &entry;
    descriptor.colorTargetFormat = molga::TextureFormat::SRGBA8;
    const molga::PipelineKey first = molga::MakePipelineKey(descriptor);
    CHECK(first == molga::MakePipelineKey(descriptor));
    descriptor.blend = molga::BlendState::Additive;
    CHECK(first != molga::MakePipelineKey(descriptor));
    descriptor.blend = molga::BlendState::Alpha;
    descriptor.sampleCount = 4;
    CHECK(first != molga::MakePipelineKey(descriptor));
    descriptor.sampleCount = 1;
    ++entry.revision;
    CHECK(first != molga::MakePipelineKey(descriptor));
    --entry.revision;
    descriptor.cull = molga::CullMode::Back;
    CHECK(first != molga::MakePipelineKey(descriptor));
    descriptor.cull = molga::CullMode::None;
    descriptor.depthTest = true;
    CHECK(first != molga::MakePipelineKey(descriptor));
    descriptor.depthTest = false;
    descriptor.colorTargetFormat = molga::TextureFormat::RGBA16F;
    CHECK(first != molga::MakePipelineKey(descriptor));
}

TEST_CASE("SDL_GPU render target preserves last-good allocation") {
    WindowConfig config;
    config.title = "Molga SDL_GPU resize contract";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);
    molga::RenderTarget target;
    std::string error;
    REQUIRE(target.Init(32, 24, &error));
    const molga::TextureView original = target.ColorView();
    CHECK_FALSE(target.Resize(0, 24, &error));
    CHECK(target.IsValid());
    CHECK(target.Width() == 32);
    CHECK(target.Height() == 24);
    CHECK(target.ColorView().texture == original.texture);
}

TEST_CASE("SDL_GPU pixel readback is top-left RGBA with bounded tolerance") {
    WindowConfig config;
    config.title = "Molga SDL_GPU pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderTarget target({molga::RenderTargetColorFormat::SRGBA8,
                                false, molga::TextureFilter::Nearest});
    REQUIRE(target.Init(64, 64, &error));

    SUBCASE("clear color") {
        REQUIRE(Acquire(*host, renderer, error));
        REQUIRE(renderer.BeginTarget(
            target, {0.2f, 0.4f, 0.8f, 1.0f}, molga::LoadAction::Clear,
            &error));
        REQUIRE(renderer.EndTarget(&error));
        REQUIRE(renderer.SubmitFrame(&error));
        std::vector<std::uint8_t> pixels;
        REQUIRE(host->Graphics().ReadbackRGBA8(
            target.ColorView(), {0, 0, 64, 64}, pixels, error));
        const auto center = Pixel(pixels, 64, 32, 32);
        INFO("center RGBA = " << static_cast<int>(center[0]) << ", "
             << static_cast<int>(center[1]) << ", "
             << static_cast<int>(center[2]) << ", "
             << static_cast<int>(center[3]));
        // SDL_GPU clear values are linear; an sRGB target encodes them before
        // the top-left RGBA readback oracle sees the stored bytes.
        CHECK(Near(center[0], 124));
        CHECK(Near(center[1], 170));
        CHECK(Near(center[2], 231));
        CHECK(Near(center[3], 255));
    }

    SUBCASE("top-left scissor and colored batch") {
        REQUIRE(Acquire(*host, renderer, error));
        REQUIRE(renderer.BeginTarget(
            target, {0.0f, 0.0f, 0.0f, 1.0f}, molga::LoadAction::Clear,
            &error));
        REQUIRE(renderer.SetPassScissor({0, 0, 16, 16}, &error));
        Shader* batch = ShaderManager::Get().Get("batch");
        REQUIRE(batch);
        Camera2D camera(64.0f, 64.0f);
        const std::vector<molga::Vertex2D> vertices{
            {0, 0, 0, 0, 1, 0, 0, 1},
            {64, 0, 1, 0, 1, 0, 0, 1},
            {64, 64, 1, 1, 1, 0, 0, 1},
            {0, 64, 0, 1, 1, 0, 0, 1}};
        molga::BatchKey key;
        key.shaderName = "batch";
        key.shaderRevision = batch->Revision();
        {
            molga::RenderPass logical(renderer, batch, &camera);
            REQUIRE(renderer.SubmitBatch(vertices, key, nullptr, &error));
        }
        REQUIRE(renderer.EndTarget(&error));
        REQUIRE(renderer.SubmitFrame(&error));
        std::vector<std::uint8_t> pixels;
        REQUIRE(host->Graphics().ReadbackRGBA8(
            target.ColorView(), {0, 0, 64, 64}, pixels, error));
        const auto topLeft = Pixel(pixels, 64, 4, 4);
        const auto bottomRight = Pixel(pixels, 64, 48, 48);
        CHECK(Near(topLeft[0], 255));
        CHECK(Near(topLeft[1], 0));
        CHECK(Near(bottomRight[0], 0));
        CHECK(Near(bottomRight[1], 0));
    }
}

TEST_CASE("SDL_GPU Scene grid shader renders stable axis pixels") {
    WindowConfig config;
    config.title = "Molga SDL_GPU Scene grid pixel oracle";
    config.width = 65;
    config.height = 65;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderTarget target({molga::RenderTargetColorFormat::SRGBA8,
                                false, molga::TextureFilter::Nearest});
    REQUIRE(target.Init(65, 65, &error));
    REQUIRE(Acquire(*host, renderer, error));
    REQUIRE(renderer.BeginTarget(target, {0, 0, 0, 1},
                                 molga::LoadAction::Clear, &error));

    Shader* grid = ShaderManager::Get().Get("grid");
    REQUIRE(grid);
    static constexpr std::array<float, 12> vertices{
        -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
    static constexpr std::array<float, 16> identity{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};
    struct alignas(16) GridConstants {
        float gridColor[4];
        float originColor[4];
        float spacing;
        float lineWidth;
        float padding[2];
    } fragment{{0.4f, 0.4f, 0.5f, 0.5f},
               {0.8f, 0.8f, 0.8f, 1.0f}, 0.5f, 1.0f, {0, 0}};
    static_assert(sizeof(GridConstants) == 48U);

    molga::DrawPacket packet;
    packet.shader = grid;
    packet.blend = molga::BlendState::Alpha;
    packet.vertexStride = sizeof(float) * 2U;
    packet.vertices.resize(sizeof(vertices));
    std::memcpy(packet.vertices.data(), vertices.data(), sizeof(vertices));
    packet.vertexUniforms.resize(sizeof(identity));
    std::memcpy(packet.vertexUniforms.data(), identity.data(),
                sizeof(identity));
    packet.fragmentUniforms.resize(sizeof(fragment));
    std::memcpy(packet.fragmentUniforms.data(), &fragment, sizeof(fragment));
    REQUIRE(renderer.Submit(packet, &error));
    REQUIRE(renderer.EndTarget(&error));
    REQUIRE(renderer.SubmitFrame(&error));

    const auto pixels = ReadTarget(*host, target, error);
    REQUIRE(pixels.size() == 65U * 65U * 4U);
    const auto xAxis = Pixel(pixels, 65, 8, 32);
    const auto yAxis = Pixel(pixels, 65, 32, 8);
    const auto background = Pixel(pixels, 65, 8, 8);
    CHECK(xAxis[0] > xAxis[1] + 50U);
    CHECK(xAxis[0] > xAxis[2] + 50U);
    CHECK(yAxis[1] > yAxis[0] + 50U);
    CHECK(yAxis[1] > yAxis[2] + 50U);
    CHECK(background[0] < 8U);
    CHECK(background[1] < 8U);
    CHECK(background[2] < 8U);
}

TEST_CASE("SDL_GPU texture origin partial upload and every blend mode are stable") {
    WindowConfig config;
    config.title = "Molga SDL_GPU texture and blend pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    std::array<std::uint8_t, 16> texels{
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255};
    Texture texture(2, 2, texels.data(), 4);
    REQUIRE(texture.IsValid());
    const molga::TextureHandle originalHandle = texture.Handle();
    const std::uint64_t originalStableId = texture.StableId();

    const std::vector<molga::Vertex2D> texturedQuad{
        {0, 0, 0, 0, 1, 1, 1, 1},
        {2, 0, 1, 0, 1, 1, 1, 1},
        {2, 2, 1, 1, 1, 1, 1, 1},
        {0, 2, 0, 1, 1, 1, 1, 1}};
    molga::BatchKey textureKey;
    textureKey.shaderName = "batch";
    textureKey.shaderRevision = ShaderManager::Get().Get("batch")->Revision();
    textureKey.texture = texture.Handle();
    textureKey.textureSampler = texture.Sampler();
    textureKey.textureStableId = texture.StableId();
    textureKey.blendMode = BlendMode::Opaque;
    molga::RenderTarget textureTarget;
    REQUIRE(textureTarget.Init(2, 2, &error));
    REQUIRE(RenderBatchFrame(*host, renderer, textureTarget, texturedQuad,
                             textureKey, {0, 0, 0, 1}, error));
    auto texturePixels = ReadTarget(*host, textureTarget, error);
    REQUIRE(texturePixels.size() == 2U * 2U * 4U);
    CHECK(IsColor(Pixel(texturePixels, 2, 0, 0), 255, 0, 0));
    CHECK(IsColor(Pixel(texturePixels, 2, 1, 0), 0, 255, 0));
    CHECK(IsColor(Pixel(texturePixels, 2, 0, 1), 0, 0, 255));
    CHECK(IsColor(Pixel(texturePixels, 2, 1, 1), 255, 255, 255));

    const std::array<std::uint8_t, 4> yellow{255, 255, 0, 255};
    REQUIRE(texture.UpdateSubData(0, 0, 1, 1, yellow.data(), 4));
    CHECK(texture.Handle() == originalHandle);
    CHECK(texture.StableId() == originalStableId);
    REQUIRE(RenderBatchFrame(*host, renderer, textureTarget, texturedQuad,
                             textureKey, {0, 0, 0, 1}, error));
    texturePixels = ReadTarget(*host, textureTarget, error);
    CHECK(IsColor(Pixel(texturePixels, 2, 0, 0), 255, 255, 0));
    CHECK(IsColor(Pixel(texturePixels, 2, 0, 1), 0, 0, 255));

    const std::vector<molga::Vertex2D> blendQuad{
        {0, 0, 0, 0, 0.5f, 0.25f, 0.0f, 0.5f},
        {1, 0, 1, 0, 0.5f, 0.25f, 0.0f, 0.5f},
        {1, 1, 1, 1, 0.5f, 0.25f, 0.0f, 0.5f},
        {0, 1, 0, 1, 0.5f, 0.25f, 0.0f, 0.5f}};
    molga::RenderTarget blendTarget;
    REQUIRE(blendTarget.Init(1, 1, &error));
    molga::BatchKey blendKey;
    blendKey.shaderName = "batch";
    blendKey.shaderRevision = ShaderManager::Get().Get("batch")->Revision();
    const molga::Color4f background{0.25f, 0.5f, 0.75f, 1.0f};
    struct BlendExpectation {
        BlendMode mode;
        std::array<float, 3> linear;
    };
    const std::array<BlendExpectation, 5> expectations{{
        {BlendMode::Opaque, {0.5f, 0.25f, 0.0f}},
        {BlendMode::Alpha, {0.375f, 0.375f, 0.375f}},
        {BlendMode::Additive, {0.5f, 0.625f, 0.75f}},
        {BlendMode::Multiply, {0.125f, 0.125f, 0.0f}},
        {BlendMode::Screen, {0.625f, 0.625f, 0.75f}},
    }};
    for (const auto& expectation : expectations) {
        blendKey.blendMode = expectation.mode;
        REQUIRE(RenderBatchFrame(*host, renderer, blendTarget, blendQuad,
                                 blendKey, background, error));
        const auto readback = ReadTarget(*host, blendTarget, error);
        REQUIRE(readback.size() == 4U);
        const auto pixel = Pixel(readback, 1, 0, 0);
        CAPTURE(static_cast<int>(expectation.mode));
        CHECK(Near(pixel[0], LinearSrgbByte(expectation.linear[0]), 5));
        CHECK(Near(pixel[1], LinearSrgbByte(expectation.linear[1]), 5));
        CHECK(Near(pixel[2], LinearSrgbByte(expectation.linear[2]), 5));
    }
}

TEST_CASE("SDL_GPU IntegerFit preserves texels bars crop and UI-after-world") {
    WindowConfig config;
    config.title = "Molga SDL_GPU IntegerFit pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderSystem2D::Get().Init();
    molga::GameOutputRenderer output;

    std::vector<std::shared_ptr<GameObject>> objects;
    auto cameraObject = std::make_shared<GameObject>("Camera");
    cameraObject->AddComponent<Transform>(0.0f, 0.0f);
    Camera* camera = cameraObject->AddComponent<Camera>();
    camera->SetMain(true);
    camera->SetPixelPerfect(true);
    camera->SetPixelZoom(1);
    camera->SetBackgroundColor(Color::Black());
    objects.push_back(cameraObject);

    const auto addPixel = [&](float x, float y, const Color& color) {
        auto object = std::make_shared<GameObject>("Pixel");
        object->AddComponent<Transform>(x, y);
        auto* sprite = object->AddComponent<SpriteRenderer>();
        sprite->SetSize(1.0f, 1.0f);
        sprite->SetColor(color);
        objects.push_back(std::move(object));
    };
    addPixel(0.0f, 0.0f, Color::Red());
    addPixel(1.0f, 0.0f, Color::Green());
    addPixel(0.0f, 1.0f, Color::Blue());
    addPixel(1.0f, 1.0f, Color::White());

    molga::RenderTarget exact;
    REQUIRE(exact.Init(6, 6, &error));
    molga::GameOutputResult exactResult;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, exact, {2, 2},
                              molga::GameOutputScaleMode::IntegerFit,
                              exactResult, error));
    CHECK(exactResult.rendered);
    CHECK(exactResult.presentation.scale == 3);
    const auto exactPixels = ReadTarget(*host, exact, error);
    REQUIRE(exactPixels.size() == 6U * 6U * 4U);
    CHECK(IsColor(Pixel(exactPixels, 6, 0, 0), 255, 0, 0));
    CHECK(IsColor(Pixel(exactPixels, 6, 2, 2), 255, 0, 0));
    CHECK(IsColor(Pixel(exactPixels, 6, 3, 0), 0, 255, 0));
    CHECK(IsColor(Pixel(exactPixels, 6, 5, 2), 0, 255, 0));
    CHECK(IsColor(Pixel(exactPixels, 6, 0, 3), 0, 0, 255));
    CHECK(IsColor(Pixel(exactPixels, 6, 2, 5), 0, 0, 255));
    CHECK(IsColor(Pixel(exactPixels, 6, 3, 3), 255, 255, 255));
    CHECK(IsColor(Pixel(exactPixels, 6, 5, 5), 255, 255, 255));

    molga::RenderTarget barred;
    REQUIRE(barred.Init(8, 6, &error));
    molga::GameOutputResult barredResult;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, barred, {2, 2},
                              molga::GameOutputScaleMode::IntegerFit,
                              barredResult, error));
    CHECK(barredResult.presentation.scale == 3);
    CHECK(barredResult.presentation.contentRect.x == 1);
    const auto barredPixels = ReadTarget(*host, barred, error);
    REQUIRE(barredPixels.size() == 8U * 6U * 4U);
    CHECK(IsColor(Pixel(barredPixels, 8, 0, 0), 0, 0, 0));
    CHECK(IsColor(Pixel(barredPixels, 8, 7, 5), 0, 0, 0));
    CHECK(IsColor(Pixel(barredPixels, 8, 1, 0), 255, 0, 0));

    molga::RenderTarget cropped;
    REQUIRE(cropped.Init(1, 1, &error));
    molga::GameOutputResult croppedResult;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, cropped, {2, 2},
                              molga::GameOutputScaleMode::IntegerFit,
                              croppedResult, error));
    CHECK(croppedResult.presentation.cropped);
    CHECK(croppedResult.presentation.contentRect.x == -1);
    CHECK(croppedResult.presentation.contentRect.y == -1);
    const auto croppedPixels = ReadTarget(*host, cropped, error);
    REQUIRE(croppedPixels.size() == 4U);
    CHECK(IsColor(Pixel(croppedPixels, 1, 0, 0), 255, 255, 255));

    std::vector<std::shared_ptr<GameObject>> uiOnly;
    auto canvasObject = std::make_shared<GameObject>("Canvas");
    canvasObject->AddComponent<UICanvas>()->SetReferenceResolution({2.0f, 2.0f});
    auto* canvasRect = canvasObject->AddComponent<RectTransform>();
    canvasRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
    canvasRect->SetSizeDelta({0.0f, 0.0f});
    uiOnly.push_back(canvasObject);
    auto imageObject = std::make_shared<GameObject>("Full UI Image");
    auto* imageRect = imageObject->AddComponent<RectTransform>();
    imageRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
    imageRect->SetSizeDelta({0.0f, 0.0f});
    imageObject->AddComponent<UIImage>()->SetTint(Color::Red());
    imageObject->SetParent(canvasObject.get());
    uiOnly.push_back(imageObject);

    molga::RenderTarget uiTarget;
    REQUIRE(uiTarget.Init(2, 2, &error));
    molga::GameOutputResult uiResult;
    REQUIRE(RenderOutputFrame(*host, renderer, output, uiOnly, uiTarget, {2, 2},
                              molga::GameOutputScaleMode::Native,
                              uiResult, error));
    CHECK(uiResult.mainCamera == nullptr);
    const auto uiPixels = ReadTarget(*host, uiTarget, error);
    REQUIRE(uiPixels.size() == 2U * 2U * 4U);
    CHECK(IsColor(Pixel(uiPixels, 2, 1, 1), 255, 0, 0));

    molga::RenderSystem2D::Get().Shutdown();
}

TEST_CASE("SDL_GPU composes split PIP cameras and camera-local culling") {
    WindowConfig config;
    config.title = "Molga SDL_GPU multi-camera pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderSystem2D::Get().Init();
    molga::GameOutputRenderer output;
    molga::RenderTarget target;
    REQUIRE(target.Init(8, 4, &error));

    std::vector<std::shared_ptr<GameObject>> objects;
    const auto base = AddOutputCamera(
        objects, "Primary Split", CameraOutputRole::Primary,
        {0.0f, 0.0f, 0.5f, 0.75f}, 0, Color::Red());
    const auto firstOverlay = AddOutputCamera(
        objects, "Secondary Split", CameraOutputRole::Secondary,
        {0.5f, 0.0f, 0.5f, 0.75f}, 0, Color::Green());
    const auto laterOverlay = AddOutputCamera(
        objects, "Later PIP", CameraOutputRole::Disabled,
        {0.25f, 0.25f, 0.5f, 0.5f}, 0, Color::Blue());
    REQUIRE(base.camera);
    REQUIRE(firstOverlay.camera);
    REQUIRE(laterOverlay.camera);

    molga::GameOutputResult split;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, target, {8, 4},
                              molga::GameOutputScaleMode::Native,
                              split, error));
    REQUIRE(split.cameraResults.size() == 2U);
    CHECK(split.mainCamera == base.camera);
    CHECK(split.cameraResults[0].cameraObjectId == base.object->GetID());
    CHECK(split.cameraResults[1].cameraObjectId == firstOverlay.object->GetID());
    const auto splitPixels = ReadTarget(*host, target, error);
    REQUIRE(splitPixels.size() == 8U * 4U * 4U);
    CHECK(IsColor(Pixel(splitPixels, 8, 1, 1), 255, 0, 0));
    CHECK(IsColor(Pixel(splitPixels, 8, 6, 1), 0, 255, 0));
    CHECK(IsColor(Pixel(splitPixels, 8, 1, 3), 0, 0, 0));
    CHECK(IsColor(Pixel(splitPixels, 8, 6, 3), 0, 0, 0));

    REQUIRE(base.camera->SetViewport({0.0f, 0.0f, 1.0f, 1.0f}));
    REQUIRE(firstOverlay.camera->SetViewport({0.25f, 0.25f, 0.5f, 0.5f}));
    firstOverlay.camera->SetDepth(5);
    laterOverlay.camera->SetOutputRole(CameraOutputRole::Secondary);
    laterOverlay.camera->SetDepth(5);
    molga::GameOutputResult tied;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, target, {8, 4},
                              molga::GameOutputScaleMode::Native,
                              tied, error));
    REQUIRE(tied.cameraResults.size() == 3U);
    CHECK(tied.cameraResults[1].cameraObjectId == firstOverlay.object->GetID());
    CHECK(tied.cameraResults[2].cameraObjectId == laterOverlay.object->GetID());
    const auto tiedPixels = ReadTarget(*host, target, error);
    CHECK(IsColor(Pixel(tiedPixels, 8, 0, 0), 255, 0, 0));
    CHECK(IsColor(Pixel(tiedPixels, 8, 3, 1), 0, 0, 255));

    firstOverlay.camera->SetDepth(6);
    molga::GameOutputResult deeper;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, target, {8, 4},
                              molga::GameOutputScaleMode::Native,
                              deeper, error));
    REQUIRE(deeper.cameraResults.size() == 3U);
    CHECK(deeper.cameraResults[2].cameraObjectId == firstOverlay.object->GetID());
    const auto deeperPixels = ReadTarget(*host, target, error);
    CHECK(IsColor(Pixel(deeperPixels, 8, 3, 1), 0, 255, 0));

    std::vector<std::shared_ptr<GameObject>> culledObjects;
    const auto layerOne = AddOutputCamera(
        culledObjects, "Layer One", CameraOutputRole::Secondary,
        {0.0f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
    const auto layerZero = AddOutputCamera(
        culledObjects, "Layer Zero", CameraOutputRole::Secondary,
        {0.5f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
    REQUIRE(layerOne.camera);
    REQUIRE(layerZero.camera);
    layerOne.camera->SetCullingMask(std::uint32_t{1} << 1U);
    layerZero.camera->SetCullingMask(std::uint32_t{1});
    AddLayerPixel(culledObjects, "Valid Layer One", 1, Color::Red());
    AddLayerPixel(culledObjects, "Invalid Layer Uses Zero", 99, Color::Green());

    molga::RenderTarget culledTarget;
    REQUIRE(culledTarget.Init(4, 2, &error));
    molga::GameOutputResult culled;
    REQUIRE(RenderOutputFrame(*host, renderer, output, culledObjects,
                              culledTarget, {4, 2},
                              molga::GameOutputScaleMode::Native,
                              culled, error));
    CHECK(culled.mainCamera == nullptr);
    REQUIRE(culled.cameraResults.size() == 2U);
    const auto culledPixels = ReadTarget(*host, culledTarget, error);
    REQUIRE(culledPixels.size() == 4U * 2U * 4U);
    CHECK(IsColor(Pixel(culledPixels, 4, 0, 0), 255, 0, 0));
    CHECK(IsColor(Pixel(culledPixels, 4, 2, 0), 0, 255, 0));
    CHECK(IsColor(Pixel(culledPixels, 4, 1, 1), 0, 0, 0));
    CHECK(IsColor(Pixel(culledPixels, 4, 3, 1), 0, 0, 0));

    std::vector<std::shared_ptr<GameObject>> fallbackObjects;
    const auto fallbackCamera = AddOutputCamera(
        fallbackObjects, "Missing PostFX Profile", CameraOutputRole::Primary,
        {0.0f, 0.0f, 0.5f, 1.0f}, 0, Color::Green());
    const auto unaffectedCamera = AddOutputCamera(
        fallbackObjects, "Unaffected Camera", CameraOutputRole::Secondary,
        {0.5f, 0.0f, 0.5f, 1.0f}, 0, Color::Blue());
    REQUIRE(fallbackCamera.camera);
    REQUIRE(unaffectedCamera.camera);
    fallbackCamera.camera->SetPostProcessEnabled(true);
    fallbackCamera.camera->SetPostProcessProfileGuid(
        "fedcba9876543210fedcba9876543210");
    molga::GameOutputResult fallback;
    REQUIRE(RenderOutputFrame(*host, renderer, output, fallbackObjects,
                              culledTarget, {4, 2},
                              molga::GameOutputScaleMode::Native,
                              fallback, error));
    REQUIRE(fallback.cameraResults.size() == 2U);
    CHECK(fallback.postProcessFallback);
    CHECK(fallback.cameraResults[0].postProcessFallback);
    CHECK_FALSE(fallback.cameraResults[0].postProcessed);
    CHECK_FALSE(fallback.cameraResults[1].postProcessFallback);
    CHECK_FALSE(fallback.cameraResults[1].postProcessed);
    const auto fallbackPixels = ReadTarget(*host, culledTarget, error);
    REQUIRE(fallbackPixels.size() == 4U * 2U * 4U);
    CHECK(IsColor(Pixel(fallbackPixels, 4, 0, 0), 0, 255, 0));
    CHECK(IsColor(Pixel(fallbackPixels, 4, 1, 1), 0, 255, 0));
    CHECK(IsColor(Pixel(fallbackPixels, 4, 2, 0), 0, 0, 255));
    CHECK(IsColor(Pixel(fallbackPixels, 4, 3, 1), 0, 0, 255));

    molga::RenderSystem2D::Get().Shutdown();
}

TEST_CASE("SDL_GPU postfx preserves ordered color rectangle and vignette pixels") {
    WindowConfig config;
    config.title = "Molga SDL_GPU postfx pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::PostProcessPipeline pipeline;

    const auto colorProfile = MakePostProfile({
        {{"type", "ColorAdjust"}, {"enabled", true},
         {"exposureEV", 1.0}, {"contrast", 0.0},
         {"saturation", 0.5}, {"tint", {1.0, 0.5, 0.25}}}
    });
    REQUIRE(pipeline.Prepare({5, 3}, colorProfile, &error));
    CHECK(pipeline.BloomMipCount() == 0U);
    molga::RenderTarget colorTarget;
    REQUIRE(colorTarget.Init(5, 3, &error));
    molga::PostProcessExecutionResult colorResult;
    REQUIRE(RunPostProcessFrame(
        *host, renderer, pipeline, colorProfile, colorTarget,
        {0.25f, 0.5f, 0.75f, 0.4f}, {0, 0, 0, 1}, {0, 0, 5, 3},
        colorResult, error));
    CHECK(colorResult.postProcessed);
    CHECK(colorResult.passes == 2);
    const auto colorPixels = ReadTarget(*host, colorTarget, error);
    REQUIRE(colorPixels.size() == 5U * 3U * 4U);
    const auto adjusted = Pixel(colorPixels, 5, 2, 1);
    CHECK(Near(adjusted[0], LinearSrgbByte(0.7149f)));
    CHECK(Near(adjusted[1], LinearSrgbByte(0.48245f)));
    CHECK(Near(adjusted[2], LinearSrgbByte(0.303725f)));
    CHECK(Near(adjusted[3], 102, 1));

    const auto resolveProfile = MakePostProfile({
        {{"type", "ColorAdjust"}, {"enabled", true},
         {"exposureEV", -1.0}}
    });
    REQUIRE(pipeline.Prepare({2, 2}, resolveProfile, &error));
    molga::RenderTarget rectangleTarget;
    REQUIRE(rectangleTarget.Init(7, 5, &error));
    molga::PostProcessExecutionResult rectangleResult;
    REQUIRE(RunPostProcessFrame(
        *host, renderer, pipeline, resolveProfile, rectangleTarget,
        {1, 0, 0, 1}, {0, 1, 0, 1}, {2, 0, 2, 2},
        rectangleResult, error));
    CHECK(rectangleResult.passes == 2);
    const auto rectanglePixels = ReadTarget(*host, rectangleTarget, error);
    REQUIRE(rectanglePixels.size() == 7U * 5U * 4U);
    const int exposedRed = LinearSrgbByte(0.5f);
    CHECK(Near(Pixel(rectanglePixels, 7, 2, 0)[0], exposedRed));
    CHECK(Near(Pixel(rectanglePixels, 7, 3, 1)[0], exposedRed));
    CHECK(IsColor(Pixel(rectanglePixels, 7, 1, 0), 0, 255, 0));
    CHECK(IsColor(Pixel(rectanglePixels, 7, 4, 1), 0, 255, 0));
    CHECK(IsColor(Pixel(rectanglePixels, 7, 2, 2), 0, 255, 0));
    CHECK(IsColor(Pixel(rectanglePixels, 7, 2, 4), 0, 255, 0));

    const auto vignetteProfile = MakePostProfile({
        {{"type", "Vignette"}, {"enabled", true}, {"intensity", 1.0},
         {"smoothness", 0.5}, {"color", {0.0, 0.0, 0.0}}}
    });
    REQUIRE(pipeline.Prepare({17, 9}, vignetteProfile, &error));
    molga::RenderTarget vignetteTarget;
    REQUIRE(vignetteTarget.Init(17, 9, &error));
    molga::PostProcessExecutionResult vignetteResult;
    REQUIRE(RunPostProcessFrame(
        *host, renderer, pipeline, vignetteProfile, vignetteTarget,
        {0.5f, 0.5f, 0.5f, 0.3f}, {0, 0, 0, 1}, {0, 0, 17, 9},
        vignetteResult, error));
    CHECK(vignetteResult.passes == 2);
    const auto vignettePixels = ReadTarget(*host, vignetteTarget, error);
    REQUIRE(vignettePixels.size() == 17U * 9U * 4U);
    const auto center = Pixel(vignettePixels, 17, 8, 4);
    const auto corner = Pixel(vignettePixels, 17, 0, 0);
    const auto side = Pixel(vignettePixels, 17, 16, 4);
    CHECK(Near(center[0], LinearSrgbByte(0.5f)));
    CHECK(corner[0] < center[0] / 2);
    CHECK(side[0] < center[0] * 3 / 5);
    CHECK(Near(center[3], 77, 1));
    CHECK(Near(corner[3], 77, 1));
    CHECK(Near(side[3], 77, 1));
}

TEST_CASE("SDL_GPU bloom preserves HDR threshold soft-knee and effect order") {
    WindowConfig config;
    config.title = "Molga SDL_GPU HDR bloom pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::PostProcessPipeline pipeline;
    const auto bloom = [](float threshold, float softKnee) {
        return nlohmann::json{
            {"type", "Bloom"}, {"enabled", true},
            {"threshold", threshold}, {"softKnee", softKnee},
            {"intensity", 1.0}, {"scatter", 0.7}};
    };
    const auto haloProfile = MakePostProfile(
        nlohmann::json::array({bloom(1.0f, 0.0f)}));
    const auto softProfile = MakePostProfile(
        nlohmann::json::array({bloom(1.0f, 1.0f)}));
    const auto bloomThenExposure = MakePostProfile(nlohmann::json::array({
        bloom(1.0f, 0.0f),
        {{"type", "ColorAdjust"}, {"exposureEV", 1.0}}
    }));
    const auto exposureThenBloom = MakePostProfile(nlohmann::json::array({
        {{"type", "ColorAdjust"}, {"exposureEV", 1.0}},
        bloom(1.0f, 0.0f)
    }));

    REQUIRE(pipeline.Prepare({32, 32}, haloProfile, &error));
    CHECK(pipeline.BloomMipCount() == 5U);
    molga::RenderTarget destination;
    REQUIRE(destination.Init(32, 32, &error));
    REQUIRE(UploadHdrImage(*host, pipeline.SceneTarget(),
                           {0, 0, 0, 0.4f}, 16, 16,
                           {8, 8, 8, 0.4f}, error));
    molga::PostProcessExecutionResult haloResult;
    REQUIRE(RunUploadedPostProcessFrame(
        *host, renderer, pipeline, haloProfile, destination,
        haloResult, error));
    CHECK(haloResult.passes == 11);
    const auto haloPixels = ReadTarget(*host, destination, error);
    REQUIRE(haloPixels.size() == 32U * 32U * 4U);
    const auto brightCenter = Pixel(haloPixels, 32, 16, 16);
    const int brightNeighbor = MaxRedAround(haloPixels, 32, 16, 16, 10);
    const auto farCorner = Pixel(haloPixels, 32, 0, 0);
    CAPTURE(brightCenter);
    CAPTURE(brightNeighbor);
    CAPTURE(farCorner);
    CHECK(brightNeighbor > 0);
    CHECK(farCorner[0] < brightNeighbor);
    CHECK(Near(brightCenter[3], 102, 1));
    CHECK(Near(Pixel(haloPixels, 32, 17, 16)[3], 102, 1));

    REQUIRE(UploadHdrImage(*host, pipeline.SceneTarget(),
                           {0, 0, 0, 0.4f}, 16, 16,
                           {0.5f, 0.5f, 0.5f, 0.4f}, error));
    REQUIRE(RunUploadedPostProcessFrame(
        *host, renderer, pipeline, haloProfile, destination,
        haloResult, error));
    const auto excludedPixels = ReadTarget(*host, destination, error);
    CHECK(MaxRedAround(excludedPixels, 32, 16, 16, 10) == 0);

    REQUIRE(UploadHdrImage(*host, pipeline.SceneTarget(),
                           {0, 0, 0, 1}, 16, 16,
                           {3, 3, 3, 1}, error));
    REQUIRE(RunUploadedPostProcessFrame(
        *host, renderer, pipeline, haloProfile, destination,
        haloResult, error));
    const int hardKneeNeighbor = MaxRedAround(
        ReadTarget(*host, destination, error), 32, 16, 16, 10);
    REQUIRE(pipeline.Prepare({32, 32}, softProfile, &error));
    REQUIRE(UploadHdrImage(*host, pipeline.SceneTarget(),
                           {0, 0, 0, 1}, 16, 16,
                           {3, 3, 3, 1}, error));
    REQUIRE(RunUploadedPostProcessFrame(
        *host, renderer, pipeline, softProfile, destination,
        haloResult, error));
    const int softKneeNeighbor = MaxRedAround(
        ReadTarget(*host, destination, error), 32, 16, 16, 10);
    CAPTURE(hardKneeNeighbor);
    CAPTURE(softKneeNeighbor);
    CHECK(softKneeNeighbor > hardKneeNeighbor);

    REQUIRE(pipeline.Prepare({32, 32}, bloomThenExposure, &error));
    REQUIRE(UploadHdrImage(*host, pipeline.SceneTarget(),
                           {0, 0, 0, 1}, 16, 16,
                           {3, 3, 3, 1}, error));
    REQUIRE(RunUploadedPostProcessFrame(
        *host, renderer, pipeline, bloomThenExposure, destination,
        haloResult, error));
    const int bloomFirstNeighbor = MaxRedAnnulus(
        ReadTarget(*host, destination, error), 32, 16, 16, 2, 10);
    REQUIRE(pipeline.Prepare({32, 32}, exposureThenBloom, &error));
    REQUIRE(UploadHdrImage(*host, pipeline.SceneTarget(),
                           {0, 0, 0, 1}, 16, 16,
                           {3, 3, 3, 1}, error));
    REQUIRE(RunUploadedPostProcessFrame(
        *host, renderer, pipeline, exposureThenBloom, destination,
        haloResult, error));
    const int exposureFirstNeighbor = MaxRedAnnulus(
        ReadTarget(*host, destination, error), 32, 16, 16, 2, 10);
    CAPTURE(bloomFirstNeighbor);
    CAPTURE(exposureFirstNeighbor);
    CHECK(exposureFirstNeighbor > bloomFirstNeighbor);

    molga::RenderTarget onePixel;
    REQUIRE(onePixel.Init(1, 1, &error));
    REQUIRE(pipeline.Prepare({1, 1}, haloProfile, &error));
    CHECK(pipeline.BloomMipCount() == 1U);
    REQUIRE(UploadHdrImage(*host, pipeline.SceneTarget(),
                           {0, 0, 0, 0}, 0, 0,
                           {2, 1, 0.5f, 0.25f}, error));
    REQUIRE(RunUploadedPostProcessFrame(
        *host, renderer, pipeline, haloProfile, onePixel,
        haloResult, error));
    const auto onePixelReadback = ReadTarget(*host, onePixel, error);
    REQUIRE(onePixelReadback.size() == 4U);
    CHECK(Near(onePixelReadback[3], 64, 1));

    error.clear();
    CHECK_FALSE(pipeline.Prepare(
        {static_cast<int>(std::numeric_limits<std::uint16_t>::max()) + 1, 1},
        haloProfile, &error));
    CHECK_FALSE(error.empty());
    CHECK((pipeline.PreparedSize() == molga::PixelSize{1, 1}));
}

TEST_CASE("SDL_GPU lighting is camera-local and hard shadows mask receivers") {
    WindowConfig config;
    config.title = "Molga SDL_GPU lighting pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderSystem2D::Get().Init();

    molga::GameOutputRenderer output;
    std::vector<std::shared_ptr<GameObject>> objects;
    const auto litCamera = AddOutputCamera(
        objects, "Lit Primary", CameraOutputRole::Primary,
        {0.0f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
    const auto unlitCamera = AddOutputCamera(
        objects, "Unlit Secondary", CameraOutputRole::Secondary,
        {0.5f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
    REQUIRE(litCamera.camera);
    REQUIRE(unlitCamera.camera);
    litCamera.camera->SetLightingEnabled(true);
    litCamera.camera->SetAmbientColor(Color::White());
    litCamera.camera->SetAmbientIntensity(0.25f);

    auto lightObject = std::make_shared<GameObject>("Point Light");
    lightObject->AddComponent<Transform>(0.5f, 0.5f);
    auto* light = lightObject->AddComponent<PointLight2D>();
    REQUIRE(light->SetColor(Color::White()));
    REQUIRE(light->SetIntensity(0.5f));
    REQUIRE(light->SetRadius(100.0f));
    REQUIRE(light->SetHeight(0.0f));
    REQUIRE(light->SetFalloff(1.0f));
    objects.push_back(lightObject);

    auto spriteObject = std::make_shared<GameObject>("White Receiver");
    spriteObject->AddComponent<Transform>(0.0f, 0.0f);
    auto* sprite = spriteObject->AddComponent<SpriteRenderer>();
    sprite->SetSize(1.0f, 1.0f);
    sprite->SetColor(Color::White());
    objects.push_back(spriteObject);

    molga::RenderTarget splitTarget;
    REQUIRE(splitTarget.Init(4, 1, &error));
    molga::GameOutputResult unlit;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, splitTarget,
                              {4, 1}, molga::GameOutputScaleMode::Native,
                              unlit, error));
    CHECK_FALSE(unlit.lightingApplied);
    CHECK(output.CachedLightingPipelineCount() == 0U);
    auto pixels = ReadTarget(*host, splitTarget, error);
    REQUIRE(pixels.size() == 4U * 4U);
    CHECK(IsColor(Pixel(pixels, 4, 0, 0), 255, 255, 255));
    CHECK(IsColor(Pixel(pixels, 4, 2, 0), 255, 255, 255));

    sprite->SetLightingMode(SpriteLightingMode2D::Lit);
    molga::GameOutputResult mixed;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, splitTarget,
                              {4, 1}, molga::GameOutputScaleMode::Native,
                              mixed, error));
    CHECK(mixed.lightingApplied);
    CHECK_FALSE(mixed.lightingFallback);
    CHECK_FALSE(mixed.shadowFallback);
    CHECK(mixed.selectedLightCount == 1);
    CHECK(mixed.lightingPasses == 1);
    REQUIRE(mixed.cameraResults.size() == 2U);
    CHECK(mixed.cameraResults[0].lightingApplied);
    CHECK_FALSE(mixed.cameraResults[1].lightingApplied);
    pixels = ReadTarget(*host, splitTarget, error);
    const int expectedLit = LinearSrgbByte(0.75f);
    CHECK(Near(Pixel(pixels, 4, 0, 0)[0], expectedLit, 5));
    CHECK(IsColor(Pixel(pixels, 4, 2, 0), 255, 255, 255));

    REQUIRE(light->SetHeight(-1.0f));
    molga::GameOutputResult below;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, splitTarget,
                              {4, 1}, molga::GameOutputScaleMode::Native,
                              below, error));
    pixels = ReadTarget(*host, splitTarget, error);
    CHECK(Near(Pixel(pixels, 4, 0, 0)[0], LinearSrgbByte(0.25f), 5));

    litCamera.camera->SetLightingEnabled(false);
    molga::GameOutputResult disabled;
    REQUIRE(RenderOutputFrame(*host, renderer, output, objects, splitTarget,
                              {4, 1}, molga::GameOutputScaleMode::Native,
                              disabled, error));
    CHECK_FALSE(disabled.lightingApplied);
    CHECK(output.CachedLightingPipelineCount() == 0U);

    std::vector<std::shared_ptr<GameObject>> shadowObjects;
    const auto shadowCamera = AddOutputCamera(
        shadowObjects, "Shadow Camera", CameraOutputRole::Primary,
        {0, 0, 1, 1}, 0, Color::Black());
    REQUIRE(shadowCamera.camera);
    shadowCamera.camera->SetLightingEnabled(true);
    shadowCamera.camera->SetAmbientIntensity(0.0f);

    auto shadowLightObject = std::make_shared<GameObject>("Shadow Light");
    shadowLightObject->AddComponent<Transform>(1.5f, 2.0f);
    auto* shadowLight = shadowLightObject->AddComponent<PointLight2D>();
    REQUIRE(shadowLight->SetIntensity(1.0f));
    REQUIRE(shadowLight->SetRadius(100.0f));
    REQUIRE(shadowLight->SetHeight(32.0f));
    REQUIRE(shadowLight->SetFalloff(1.0f));
    shadowLight->SetCastsShadows(true);
    shadowObjects.push_back(shadowLightObject);

    auto largeReceiver = std::make_shared<GameObject>("Large Receiver");
    largeReceiver->AddComponent<Transform>(0.0f, 0.0f);
    auto* largeSprite = largeReceiver->AddComponent<SpriteRenderer>();
    largeSprite->SetSize(8.0f, 4.0f);
    largeSprite->SetColor(Color::White());
    largeSprite->SetLightingMode(SpriteLightingMode2D::Lit);
    shadowObjects.push_back(largeReceiver);

    auto occluderObject = std::make_shared<GameObject>("Occluder");
    occluderObject->AddComponent<Transform>(3.0f, 2.0f);
    auto* occluder = occluderObject->AddComponent<ShadowOccluder2D>();
    REQUIRE(occluder->SetBox(Vector2::Zero(), {1.0f, 2.0f}));
    shadowObjects.push_back(occluderObject);

    molga::GameOutputRenderer shadowOutput;
    molga::RenderTarget shadowTarget;
    REQUIRE(shadowTarget.Init(8, 4, &error));
    const auto renderShadow = [&]() {
        molga::GameOutputResult result;
        REQUIRE(RenderOutputFrame(*host, renderer, shadowOutput, shadowObjects,
                                  shadowTarget, {8, 4},
                                  molga::GameOutputScaleMode::Native,
                                  result, error));
        CHECK(result.lightingApplied);
        CHECK_FALSE(result.lightingFallback);
        CHECK_FALSE(result.shadowFallback);
        CHECK(result.selectedLightCount == 1);
        CHECK(result.shadowedLightCount == 1);
        CHECK(result.shadowCasterDrawCount == 1);
        CHECK(result.lightingPasses == 1);
        CHECK(result.shadowPasses == 1);
        const auto readback = ReadTarget(*host, shadowTarget, error);
        REQUIRE(readback.size() == 8U * 4U * 4U);
        const auto visible = Pixel(readback, 8, 1, 1);
        const auto shadowed = Pixel(readback, 8, 6, 1);
        CHECK(visible[0] > 220);
        CHECK(visible[1] > 220);
        CHECK(visible[2] > 220);
        CHECK(shadowed[0] < 8);
        CHECK(shadowed[1] < 8);
        CHECK(shadowed[2] < 8);
    };
    renderShadow();
    REQUIRE(occluder->SetPolygon({
        {-0.5f, -1.0f}, {0.5f, 0.0f}, {-0.5f, 1.0f}}));
    renderShadow();

    molga::RenderSystem2D::Get().Shutdown();
}

TEST_CASE("SDL_GPU authored normals follow sprite rotation and UV flip") {
    namespace fs = std::filesystem;
    WindowConfig config;
    config.title = "Molga SDL_GPU normal-map pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    TextureManager::Get().Clear();
    // Each case scans its own subtree of the one project root the singleton
    // database's font artifact store is bound to; the project root is never
    // derived by stripping "Assets" off a scan path.
    auto& authority = test_support::AssetDatabaseTestAuthority::Get();
    std::string bindError;
    REQUIRE_MESSAGE(authority.Bind(molga::AssetDatabase::Get(), &bindError),
                    bindError);
    const fs::path root = authority.AssetsCaseRoot("authored-normal");
    std::error_code filesystemError;
    fs::create_directories(root / "Assets");
    const std::string diffuseGuid = "1234567890abcdef1234567890abcdef";
    const std::string normalGuid = "abcdef1234567890abcdef1234567890";
    const fs::path diffusePath = root / "Assets" / "diffuse.ppm";
    const fs::path normalPath = root / "Assets" / "normal.ppm";
    WriteSinglePixelPpm(diffusePath, {255, 255, 255});
    WriteSinglePixelPpm(normalPath, {255, 128, 128});
    WriteJsonFile(diffusePath.string() + ".meta", {
        {"guid", diffuseGuid}, {"importer", "TextureImporter"},
        {"importerVersion", 2},
        {"settings", {{"usage", "Color"}, {"filter", "Nearest"}}}
    });
    WriteJsonFile(normalPath.string() + ".meta", {
        {"guid", normalGuid}, {"importer", "TextureImporter"},
        {"importerVersion", 2},
        {"settings", {{"usage", "NormalMap"}, {"filter", "Nearest"}}}
    });
    molga::AssetDatabase::Get().Clear();
    molga::AssetDatabase::Get().ScanProject(root / "Assets");

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderSystem2D::Get().Init();

    std::vector<std::shared_ptr<GameObject>> objects;
    const auto camera = AddOutputCamera(
        objects, "Normal Camera", CameraOutputRole::Primary,
        {0, 0, 1, 1}, 0, Color::Black());
    REQUIRE(camera.camera);
    camera.camera->SetLightingEnabled(true);
    camera.camera->SetAmbientIntensity(0.0f);

    auto lightObject = std::make_shared<GameObject>("Right Light");
    lightObject->AddComponent<Transform>(1.5f, 0.5f);
    auto* light = lightObject->AddComponent<PointLight2D>();
    REQUIRE(light->SetIntensity(1.0f));
    REQUIRE(light->SetRadius(10.0f));
    REQUIRE(light->SetHeight(0.0f));
    REQUIRE(light->SetFalloff(1.0f));
    objects.push_back(lightObject);

    auto receiverObject = std::make_shared<GameObject>("Normal Receiver");
    auto* receiverTransform = receiverObject->AddComponent<Transform>(0, 0);
    auto* receiver = receiverObject->AddComponent<SpriteRenderer>();
    receiver->SetTextureGuid(diffuseGuid);
    receiver->SetSize(1.0f, 1.0f);
    receiver->SetLightingMode(SpriteLightingMode2D::Lit);
    receiver->SetNormalMapGuid(normalGuid);
    objects.push_back(receiverObject);

    molga::GameOutputRenderer output;
    molga::RenderTarget target;
    REQUIRE(target.Init(1, 1, &error));
    const auto renderPixel = [&]() {
        molga::GameOutputResult result;
        REQUIRE(RenderOutputFrame(*host, renderer, output, objects, target,
                                  {1, 1}, molga::GameOutputScaleMode::Native,
                                  result, error));
        REQUIRE(result.lightingApplied);
        const auto readback = ReadTarget(*host, target, error);
        REQUIRE(readback.size() == 4U);
        return Pixel(readback, 1, 0, 0);
    };

    const auto facing = renderPixel();
    CHECK(facing[0] > 220);
    CHECK(facing[1] > 220);
    CHECK(facing[2] > 220);
    receiverTransform->SetRotation(180.0f);
    const auto rotated = renderPixel();
    CHECK(rotated[0] < 8);
    CHECK(rotated[1] < 8);
    CHECK(rotated[2] < 8);
    receiverTransform->SetRotation(0.0f);
    receiver->SetFlipX(true);
    const auto flipped = renderPixel();
    CHECK(flipped[0] < 8);
    CHECK(flipped[1] < 8);
    CHECK(flipped[2] < 8);

    objects.clear();
    molga::RenderSystem2D::Get().Shutdown();
    TextureManager::Get().Clear();
    molga::AssetDatabase::Get().Clear();
    fs::remove_all(root, filesystemError);
}

TEST_CASE("SDL_GPU renders tilemap chunks and particle emitter geometry") {
    namespace fs = std::filesystem;
    WindowConfig config;
    config.title = "Molga SDL_GPU tilemap and particle pixel oracle";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderSystem2D::Get().Init();

    // Each case scans its own subtree of the one project root the singleton
    // database's font artifact store is bound to; the project root is never
    // derived by stripping "Assets" off a scan path.
    auto& authority = test_support::AssetDatabaseTestAuthority::Get();
    std::string bindError;
    REQUIRE_MESSAGE(authority.Bind(molga::AssetDatabase::Get(), &bindError),
                    bindError);
    const fs::path root = authority.AssetsCaseRoot("tilemap-particle");
    std::error_code filesystemError;
    fs::create_directories(root / "Assets");
    const fs::path tilesPath = root / "Assets" / "tiles.ppm";
    WritePpm(tilesPath, 4, 2, {
        255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 0,
        255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 0});
    WriteJsonFile(tilesPath.string() + ".meta", {
        {"guid", "2468ace02468ace02468ace02468ace0"},
        {"importer", "TextureImporter"}, {"importerVersion", 2},
        {"settings", {{"usage", "Color"}, {"filter", "Nearest"}}}
    });

    const fs::path previousAssetRoot = PathService::Get().AssetRoot();
    auto& database = molga::AssetDatabase::Get();
    TextureManager::Get().Clear();
    database.Clear();
    database.ScanProject(root / "Assets");
    PathService::Get().SetAssetRoot(root / "Assets");

    auto tileObject = std::make_shared<GameObject>("GPU Tilemap Chunk");
    tileObject->AddComponent<Transform>(0.0f, 0.0f);
    auto* tilemap = tileObject->AddComponent<TilemapRenderer>();
    REQUIRE(tilemap->Resize(2, 1));
    tilemap->tileSize = 2;
    tilemap->spriteSheetPath = "tiles.ppm";
    tilemap->SetTile(0, 0, 0);
    tilemap->SetTile(1, 0, 1);
    tilemap->ResolveAssets();

    molga::RenderQueue tileQueue;
    tilemap->CollectRender(tileQueue);
    REQUIRE(tileQueue.GetCommands().size() == 1U);
    REQUIRE(tileQueue.GetCommands().front().geometry);
    CHECK(tileQueue.GetCommands().front().geometry->size() == 8U);
    CHECK(tilemap->GetLastSubmittedChunkCount() == 1U);
    CHECK(tilemap->GetChunkRebuildCount() == 1U);

    molga::RenderTarget tileTarget({molga::RenderTargetColorFormat::SRGBA8,
                                    false, molga::TextureFilter::Nearest});
    REQUIRE(tileTarget.Init(4, 2, &error));
    REQUIRE(Acquire(*host, renderer, error));
    REQUIRE(renderer.BeginTarget(tileTarget, {0, 0, 0, 1},
                                 molga::LoadAction::Clear, &error));
    Camera2D tileCamera(4.0f, 2.0f);
    Shader* batchShader = ShaderManager::Get().Get("batch");
    REQUIRE(batchShader);
    {
        molga::RenderPass pass(renderer, batchShader, &tileCamera);
        molga::RenderSystem2D::Get().Render(
            tileQueue, &renderer, &tileCamera);
    }
    REQUIRE(renderer.EndTarget(&error));
    REQUIRE(renderer.SubmitFrame(&error));
    const auto tilePixels = ReadTarget(*host, tileTarget, error);
    REQUIRE(tilePixels.size() == 4U * 2U * 4U);
    CHECK(IsColor(Pixel(tilePixels, 4, 0, 0), 255, 0, 0));
    CHECK(IsColor(Pixel(tilePixels, 4, 1, 1), 255, 0, 0));
    CHECK(IsColor(Pixel(tilePixels, 4, 2, 0), 0, 255, 0));
    CHECK(IsColor(Pixel(tilePixels, 4, 3, 1), 0, 255, 0));

    tileQueue.Clear();
    tileObject.reset();
    TextureManager::Get().Clear();
    database.Clear();
    PathService::Get().SetAssetRoot(previousAssetRoot);
    fs::remove_all(root, filesystemError);

    std::array<std::uint8_t, 16> particleTexels{
        255, 0, 255, 255, 255, 0, 255, 255,
        255, 0, 255, 255, 255, 0, 255, 255};
    Texture particleTexture(2, 2, particleTexels.data(), 4);
    REQUIRE(particleTexture.IsValid());
    ParticleConfig particleConfig;
    particleConfig.spawnRate = 0.0f;
    particleConfig.maxParticles = 1;
    particleConfig.spawnRadius = 0.0f;
    particleConfig.minSpeed = 0.0f;
    particleConfig.maxSpeed = 0.0f;
    particleConfig.minLife = 2.0f;
    particleConfig.maxLife = 2.0f;
    particleConfig.startSize = 8.0f;
    particleConfig.endSize = 8.0f;
    particleConfig.sizeVariance = 0.0f;
    particleConfig.minRotationSpeed = 0.0f;
    particleConfig.maxRotationSpeed = 0.0f;
    particleConfig.startA = 1.0f;
    particleConfig.endA = 1.0f;
    particleConfig.seed = 17U;
    particleConfig.frameMode = ParticleFrameMode::Start;
    particleConfig.sprites = {{"fixture", ""}};

    ParticleEmitter emitter;
    emitter.SetConfig(particleConfig);
    emitter.SetPosition(8.0f, 8.0f);
    emitter.Burst(1);
    REQUIRE(emitter.GetActiveCount() == 1);
    molga::ResolvedSprite particleSprite;
    particleSprite.texture = &particleTexture;
    particleSprite.uv = {0.0f, 0.0f, 1.0f, 1.0f};
    particleSprite.pivot = {0.5f, 0.5f};
    particleSprite.nativeSize = {2.0f, 2.0f};
    particleSprite.pixelRect = {0, 0, 2, 2};
    particleSprite.valid = true;
    const auto particleBatches = emitter.BuildGeometry({particleSprite});
    REQUIRE(particleBatches.size() == 1U);
    REQUIRE(particleBatches.front().geometry);
    CHECK(particleBatches.front().QuadCount() == 1U);
    CHECK(particleBatches.front().texture == &particleTexture);
    CHECK(particleBatches.front().worldBounds.width >= 8.0f);
    CHECK(particleBatches.front().worldBounds.height >= 8.0f);

    molga::BatchKey particleKey;
    particleKey.shaderName = "batch";
    particleKey.shaderRevision = batchShader->Revision();
    particleKey.texture = particleTexture.Handle();
    particleKey.textureSampler = particleTexture.Sampler();
    particleKey.textureStableId = particleTexture.StableId();
    particleKey.blendMode = BlendMode::Alpha;
    molga::RenderTarget particleTarget({
        molga::RenderTargetColorFormat::SRGBA8, false,
        molga::TextureFilter::Nearest});
    REQUIRE(particleTarget.Init(16, 16, &error));
    REQUIRE(RenderBatchFrame(*host, renderer, particleTarget,
                             *particleBatches.front().geometry, particleKey,
                             {0, 0, 0, 1}, error));
    const auto particlePixels = ReadTarget(*host, particleTarget, error);
    REQUIRE(particlePixels.size() == 16U * 16U * 4U);
    CHECK(IsColor(Pixel(particlePixels, 16, 8, 8), 255, 0, 255));
    CHECK(IsColor(Pixel(particlePixels, 16, 0, 0), 0, 0, 0));
    std::size_t magentaPixels = 0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (IsColor(Pixel(particlePixels, 16, x, y), 255, 0, 255)) {
                ++magentaPixels;
            }
        }
    }
    CHECK(magentaPixels >= 60U);
    CHECK(magentaPixels <= 68U);

    molga::RenderSystem2D::Get().Shutdown();
}

// ── Task 6.3: 프로덕션 GPU teardown 순서 ─────────────────────────────────────
// Task 6.2 Step 8b의 계약은 "idle이 증명되고 반납 큐가 비워진 다음에만
// 텍스트/atlas GPU 자원을 부순다"이다. 그 계약이 Renderer::Shutdown 안에서만
// 참이고 진입점에서 거짓이던 것이 Task 6.2가 6.3에 넘긴 선결 조건이었다.
//
// 이제 두 진입점(src/main.cpp, src/runtime_main.cpp)은
// ShutdownRendererThenTextGpuResources 하나만 부르므로, 순서는 그 함수 한
// 곳에서만 바뀔 수 있다. 아래 두 케이스가 그 한 곳을 양쪽에서 붙든다.

TEST_CASE("production shutdown releases text GPU pages only after proven idle") {
    WindowConfig config;
    config.title = "Molga SDL_GPU text teardown order";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);

    ShutdownStageLog log;
    TextRendererErrorLog errors;
    Renderer renderer;
    std::string error;
    REQUIRE_MESSAGE(renderer.Init(&error), error);
    TextRenderer text;
    SubmittedGlyphPageFixture submitted(*host, renderer, text);
    REQUIRE(errors.ErrorCount() == 0U);

    ShutdownRendererThenTextGpuResources(renderer, text);

    // renderer 쪽이 실제로 idle을 증명하고 반납 큐를 비운 다음 자기 GPU
    // 자원을 부수는 순서로 지나갔다.
    CHECK(log.stages == std::vector<std::string>{"idle-wait-proven",
                                                 "retirement-drained",
                                                 "gpu-resources-destroyed"});
    // 그리고 그 뒤에 atlas가 해제되었다. 이 해제는 제출된 프레임이 토큰을
    // 놓은 뒤에만 성립하므로(아래 반대편 케이스가 그것을 보여 준다), 0이라는
    // 값 자체가 "텍스트 teardown이 drain 뒤에 왔다"의 증거다.
    CHECK(text.GlyphAtlas().LiveExternalPagePinCount() == 0U);
    CHECK(text.GlyphAtlas().ResidentPageCount() == 0U);
    CHECK_FALSE(text.GlyphAtlas().IsPageResident(submitted.pageIdentity));
    // 올바른 순서는 아무것도 보고하지 않는다. 아래 반대편 케이스가 정확히 한
    // 건을 요구하므로, 이 0이 그 1의 짝이다.
    CHECK(errors.ErrorCount() == 0U);
}

// 반대편. 순서를 되돌리면 — 텍스트/atlas teardown이 renderer의 idle 증명과
// 반납 drain보다 먼저 오면 — atlas는 해제를 거절하고 page는 그대로 남는다.
// 이 케이스가 없으면 위 케이스의 0은 "해제가 언제 불려도 성공한다"와 구별되지
// 않으므로 순서를 아무것도 증명하지 못한다.
TEST_CASE("text GPU teardown before the renderer drain is refused, not silent") {
    WindowConfig config;
    config.title = "Molga SDL_GPU text teardown order reversed";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);

    TextRendererErrorLog errors;
    Renderer renderer;
    std::string error;
    REQUIRE_MESSAGE(renderer.Init(&error), error);
    TextRenderer text;
    SubmittedGlyphPageFixture submitted(*host, renderer, text);
    REQUIRE(errors.ErrorCount() == 0U);

    // 되돌린 순서: 제출된 프레임이 아직 page를 붙들고 있는데 텍스트가 먼저
    // 내려간다.
    text.Shutdown();
    CHECK(text.GlyphAtlas().LiveExternalPagePinCount() == 1U);
    CHECK(text.GlyphAtlas().ResidentPageCount() == 1U);
    CHECK(text.GlyphAtlas().IsPageResident(submitted.pageIdentity));
    // 거절은 조용하지 않다. 진입점 두 곳은 어떤 테스트도 컴파일하지 않으므로,
    // 그쪽에서 순서가 뒤집혔을 때 남는 유일한 실행 시 증거가 이 한 줄이다.
    CHECK(errors.ErrorCount() == 1U);
    CHECK(errors.LastErrorMessage().find("before the renderer proved idle") !=
          std::string::npos);

    // 그리고 올바른 순서를 밟으면 같은 page가 풀린다. 거절이 영구적인 고장이
    // 아니라 순서의 함수라는 것이 이 두 줄이다.
    renderer.Shutdown();
    text.Shutdown();
    CHECK(text.GlyphAtlas().LiveExternalPagePinCount() == 0U);
    CHECK(text.GlyphAtlas().ResidentPageCount() == 0U);
    // 그리고 그 성공은 두 번째 보고를 남기지 않는다.
    CHECK(errors.ErrorCount() == 1U);
}

// ── Task 6.3 Step 4: 붙듦의 유일한 프로덕션 호출 지점 ────────────────────────
// test_render_queue는 SubmitVisibleCommands를 받아 적는 대역으로 인스턴스화해
// "제출 직전"이라는 순서를 붙든다. 그런데 그 루프를 실제로 부르는 곳은
// RenderSystem2D::Render 한 줄뿐이고, 그 한 줄이 붙듦 없는 예전 인라인 루프로
// 되돌아가도 대역 쪽 단언은 하나도 움직이지 않는다 — RenderQueue →
// RenderSystem2D::Render → Renderer::RetainUntilFrameComplete 라는 이음매에
// 증인이 없기 때문이다. 이 케이스가 그 이음매를 진짜 장치 위에서 통째로 지난다.
TEST_CASE("RenderSystem2D hands a drawn text command's page to the active frame") {
    WindowConfig config;
    config.title = "Molga SDL_GPU submitted page retention";
    config.width = 64;
    config.height = 64;
    config.visible = false;
    auto host = EngineInit(config);
    REQUIRE(host);

    Renderer renderer;
    std::string error;
    REQUIRE_MESSAGE(renderer.Init(&error), error);
    molga::RenderSystem2D::Get().Init();
    TextRenderer text;

    molga::FontFace face;
    REQUIRE_MESSAGE(face.LoadFromFile(MOLGA_TEST_KOREAN_FONT_PATH, &error),
                    error);
    const std::uint32_t glyphId = face.GlyphId(U'가');
    REQUIRE(glyphId != 0U);

    molga::GlyphAtlasKey key;
    key.fontGuid = "submitted-page";
    key.fontRevision = "0";
    key.faceIndex = 0U;
    key.pixelSize = 32U;
    key.rasterScaleKey = 64U;
    key.renderMode = molga::GlyphRenderMode::Monochrome;
    key.glyphId = glyphId;

    molga::text::VectorTextDiagnosticSink sink;
    molga::GlyphHandle handle;
    {
        auto collection = text.BeginGlyphCollection(1);
        handle = text.GlyphAtlas().GetGlyph(key, face, sink);
    }
    REQUIRE_FALSE(handle.proceduralTofu);
    REQUIRE(handle.pageIdentity != 0U);
    REQUIRE(handle.pageLifetime);
    // 명령이 싣는 텍스처도 지분을 통해서만 꺼낸다. Task 8.2의 소비자가 하게
    // 될 일과 같은 모양이어야 이 케이스가 그 경로를 대신 지키는 값이 있다.
    Texture* page = molga::RetainedTexture(handle);
    REQUIRE(page != nullptr);
    REQUIRE(page->IsValid());

    auto makeCommand = [&]() {
        molga::RenderCommand command;
        command.batchKey.shaderName = "batch";
        command.batchKey.texture = page->Handle();
        command.batchKey.textureSampler = page->Sampler();
        command.batchKey.textureStableId = page->StableId();
        command.batchKey.isBatchable = true;
        command.isBatchableSprite = true;
        const float uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f},
                                {1.0f, 1.0f}, {0.0f, 1.0f}};
        const float xy[4][2] = {{-8.0f, -8.0f}, {8.0f, -8.0f},
                                {8.0f, 8.0f}, {-8.0f, 8.0f}};
        for (std::size_t corner = 0U; corner < 4U; ++corner) {
            molga::Vertex2D& vertex = command.vertices[corner];
            vertex.x = xy[corner][0];
            vertex.y = xy[corner][1];
            vertex.u = uv[corner][0];
            vertex.v = uv[corner][1];
            vertex.r = 1.0f;
            vertex.g = 1.0f;
            vertex.b = 1.0f;
            vertex.a = 1.0f;
        }
        command.resourceLifetimeIdentity = handle.pageIdentity;
        command.resourceLifetime = handle.pageLifetime;
        return command;
    };

    molga::RenderTarget target;
    REQUIRE(target.Init(64, 64, &error));
    REQUIRE(Acquire(*host, renderer, error));
    REQUIRE(renderer.BeginTarget(target, {0, 0, 0, 1},
                                 molga::LoadAction::Clear, &error));
    Camera2D camera(64.0f, 64.0f);
    Shader* batch = ShaderManager::Get().Get("batch");
    REQUIRE(batch);
    {
        molga::RenderPass pass(renderer, batch, &camera);

        // 컬링되어 그려지지 않는 명령은 아무것도 넘기지 않는다. 이 0이 없으면
        // 아래 1은 "Render가 큐를 보기만 해도 붙든다"와 구별되지 않는다.
        molga::RenderQueue culledQueue;
        molga::RenderCommand culled = makeCommand();
        culled.worldBounds = AABB(1000.0f, 1000.0f, 1.0f, 1.0f);
        culledQueue.Submit(culled);
        molga::RenderSystem2D::Get().Render(culledQueue, &renderer, &camera);
        CHECK(renderer.ActiveFrameRetainedPageCount() == 0U);

        molga::RenderQueue queue;
        queue.Submit(makeCommand());
        molga::RenderSystem2D::Get().Render(queue, &renderer, &camera);
        CHECK(renderer.ActiveFrameRetainedPageCount() == 1U);
    }
    REQUIRE(renderer.EndTarget(&error));

    // 개수만으로는 "어떤 page든 하나"와 구별되지 않는다. 이 테스트가 들고
    // 있던 사본을 놓고도 이 page의 외부 지분이 살아 있다는 것이, 프레임이
    // 붙든 것이 바로 이 page라는 증거다.
    handle.pageLifetime.reset();
    CHECK(text.GlyphAtlas().LiveExternalPagePinCount() == 1U);
    CHECK(text.GlyphAtlas().IsPageResident(handle.pageIdentity));

    REQUIRE_MESSAGE(renderer.SubmitFrame(&error), error);
    ShutdownRendererThenTextGpuResources(renderer, text);
    CHECK(text.GlyphAtlas().LiveExternalPagePinCount() == 0U);
    CHECK(text.GlyphAtlas().ResidentPageCount() == 0U);
    molga::RenderSystem2D::Get().Shutdown();
}

// ── Task 6.3: 진입점의 종료 순서와 수집 범위를 소스 텍스트로 붙든다 ──────────
// src/main.cpp와 src/runtime_main.cpp는 molga_engine/molga_runtime만 컴파일
// 한다. 어떤 테스트 바이너리도 이 둘을 링크하지 않으므로, Task 6.2가 넘긴 바로
// 그 회귀 — renderer의 idle 증명보다 먼저 TextRenderer::Get().Shutdown()을
// 부르는 것 — 를 되돌려도 위 케이스들은 전부 통과한다. 위 케이스들이 붙드는
// 것은 공유 함수 하나이고, 그 함수를 부르는지는 붙들지 않는다.
//
// 진입점을 단위 테스트 가능하게 만드는 것은 이 태스크의 몫이 아니므로, 대신
// 그 두 파일의 텍스트에 대고 세 가지를 요구한다. 리뷰어의 diff가 조용히
// 되돌릴 수 없게 하는 것이 목적이다.
TEST_CASE("the production entry points keep one shutdown order and one collection") {
    namespace fs = std::filesystem;
    const fs::path sourceRoot(MOLGA_ENGINE_SOURCE_ROOT);
    for (const char* relative : {"src/main.cpp", "src/runtime_main.cpp"}) {
        const fs::path path = sourceRoot / relative;
        INFO("entry point " << path.string());
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        const std::string source(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        // 파일을 실제로 읽었다는 것부터 못 박는다. 경로가 어긋나면 아래
        // "없다" 단언들이 빈 문자열 위에서 공짜로 참이 된다.
        REQUIRE(source.size() > 1024U);

        auto count = [&source](const std::string& needle) {
            std::size_t total = 0U;
            for (std::size_t at = source.find(needle); at != std::string::npos;
                 at = source.find(needle, at + needle.size())) {
                ++total;
            }
            return total;
        };

        // 1. 텍스트 GPU 자원 파괴는 공유 함수를 통해서만 일어난다.
        CHECK(count("TextRenderer::Get().Shutdown()") == 0U);
        // 2. 그리고 그 공유 함수가 실제로 불린다(1의 성공 증인).
        CHECK(count("ShutdownRendererThenTextGpuResources(") >= 1U);
        // 3. 프레임 루프의 수집 범위는 정확히 하나다. 지우면 0이 되고, 둘째를
        //    열면 첫째가 아직 열려 있는 채로 std::logic_error가 난다.
        CHECK(count("BeginGlyphCollection(") == 1U);
        // 계수기 자신의 증인. 이 파일들이 확실히 담고 있는 문자열을 세지
        // 못한다면 위의 0들은 계수기 고장으로도 참이 된다.
        CHECK(count("TextRenderer") >= 1U);
    }
}

TEST_CASE("SDL_GPU Korean glyph atlas renders top-left through the batch path") {
    namespace fs = std::filesystem;
    WindowConfig config;
    config.title = "Molga SDL_GPU Korean font pixel oracle";
    config.width = 256;
    config.height = 64;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    // Each case scans its own subtree of the one project root the singleton
    // database's font artifact store is bound to; the project root is never
    // derived by stripping "Assets" off a scan path.
    auto& authority = test_support::AssetDatabaseTestAuthority::Get();
    std::string bindError;
    REQUIRE_MESSAGE(authority.Bind(molga::AssetDatabase::Get(), &bindError),
                    bindError);
    const fs::path root = authority.AssetsCaseRoot("korean-font");
    std::error_code filesystemError;
    fs::create_directories(root / "Assets" / "Fonts");
    fs::copy_file(fs::path(MOLGA_TEST_KOREAN_FONT_PATH),
                  root / "Assets" / "Fonts" / "NotoSansKR-Regular.ttf",
                  fs::copy_options::overwrite_existing);
    auto& database = molga::AssetDatabase::Get();
    database.Clear();
    database.ScanProject(root / "Assets");
    const std::string guid =
        database.GuidForSource("Fonts/NotoSansKR-Regular.ttf");
    REQUIRE_FALSE(guid.empty());

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderSystem2D::Get().Init();
    TextRenderer& text = TextRenderer::Get();
    text.Shutdown();
    REQUIRE(text.Init());
    text.InvalidateAllFonts();

    // Task 6.3 Step 8: 프레임 번호가 정해진 뒤, 텍스트가 큐에 담기기 전에
    // 이 프레임의 glyph 수집을 연다. 범위가 아래 제출과 픽셀 판독을 전부
    // 덮으므로, 명령들이 page 토큰을 다 복사하기 전에 수집이 닫히지 않는다.
    //
    // optional인 이유는 정리 순서 때문이다: 수집이 열려 있는 동안
    // TextRenderer::Shutdown은 (옳게도) atlas 해제를 거절한다.
    std::optional<TextRenderer::GlyphCollectionScope> glyphCollection(
        text.BeginGlyphCollection(1));

    molga::RenderQueue queue;
    TextDrawParams params;
    params.text = u8"한글 타이틀";
    params.fontGuid = guid;
    params.fontSizePx = 40.0f;
    params.x = 4.0f;
    params.y = 4.0f;
    params.color = Color::Red();
    text.CollectText(queue, params);
    REQUIRE(queue.GetCommands().size() == 5U);
    for (const auto& command : queue.GetCommands()) {
        CHECK(command.batchKey.textureStableId != 0U);
        CHECK(command.batchKey.texture);
        CHECK(command.batchKey.textureSampler);
    }
    CHECK(text.GetAtlasPageCount(guid, 40) >= 1U);

    molga::RenderTarget target;
    REQUIRE(target.Init(256, 64, &error));
    REQUIRE(Acquire(*host, renderer, error));
    REQUIRE(renderer.BeginTarget(target, {0, 0, 0, 1},
                                 molga::LoadAction::Clear, &error));
    Camera2D camera(256.0f, 64.0f);
    Shader* batch = ShaderManager::Get().Get("batch");
    REQUIRE(batch);
    {
        molga::RenderPass pass(renderer, batch, &camera);
        molga::RenderSystem2D::Get().Render(queue, &renderer, &camera);
    }
    REQUIRE(renderer.EndTarget(&error));
    REQUIRE(renderer.SubmitFrame(&error));

    const auto pixels = ReadTarget(*host, target, error);
    REQUIRE(pixels.size() == 256U * 64U * 4U);
    std::size_t coloredPixels = 0;
    int minX = 256;
    int minY = 64;
    int maxX = -1;
    int maxY = -1;
    int opaqueX = -1;
    int opaqueY = -1;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 256; ++x) {
            const auto pixel = Pixel(pixels, 256, x, y);
            if (pixel[0] == 0U && pixel[1] == 0U && pixel[2] == 0U) continue;
            ++coloredPixels;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
            if (opaqueX < 0 && pixel[0] >= 250U && pixel[3] >= 250U) {
                opaqueX = x;
                opaqueY = y;
            }
        }
    }
    CHECK(coloredPixels == 1243U);
    CHECK(minX == 5);
    CHECK(minY == 13);
    CHECK(maxX == 135);
    CHECK(maxY == 37);
    INFO("Korean opaque probe=" << opaqueX << "," << opaqueY);
    CHECK(opaqueX >= 0);
    CHECK(opaqueY >= 0);
    const auto koreanProbe = Pixel(pixels, 256, opaqueX, opaqueY);
    INFO("Korean probe RGBA=" << static_cast<int>(koreanProbe[0]) << ","
         << static_cast<int>(koreanProbe[1]) << ","
         << static_cast<int>(koreanProbe[2]) << ","
         << static_cast<int>(koreanProbe[3]));
    CHECK(Near(koreanProbe[0], 255, 5));
    CHECK(Near(koreanProbe[1], 0));
    CHECK(Near(koreanProbe[2], 0));
    CHECK(Near(koreanProbe[3], 255, 5));

    queue.Clear();
    glyphCollection.reset();
    text.Shutdown();
    molga::RenderSystem2D::Get().Shutdown();
    database.Clear();
    fs::remove_all(root, filesystemError);
}

#if defined(MOLGA_MARROW_SUPPORT) && defined(MOLGA_MARROW_FIXTURE_DIR)
TEST_CASE("SDL_GPU renders the pinned Marrow runtime fixture") {
    namespace fs = std::filesystem;
    const fs::path fixtureRoot = MOLGA_MARROW_FIXTURE_DIR;
    REQUIRE(fs::exists(fixtureRoot / "player_idle.mskl"));
    REQUIRE(fs::exists(fixtureRoot / "player_idle.matl"));
    REQUIRE(fs::exists(fixtureRoot / "player_fixture.png"));

    WindowConfig config;
    config.title = "Molga SDL_GPU Marrow pixel fixture";
    config.width = 256;
    config.height = 256;
    config.visible = false;
    config.graphicsValidation = true;
    auto host = EngineInit(config);
    REQUIRE(host);

    const fs::path previousAssetRoot = PathService::Get().AssetRoot();
    PathService::Get().SetAssetRoot(fixtureRoot);
    auto object = std::make_shared<GameObject>("Pinned Marrow fixture");
    auto* transform = object->AddComponent<Transform>();
    transform->SetPosition(128.0f, 128.0f);
    transform->SetScale(0.75f);
    auto* marrow = object->AddComponent<MarrowRenderer>();
    marrow->SetSkeletonPath("player_idle.mskl");
    marrow->SetAtlasPath("player_idle.matl");
    marrow->ResolveAssets(true);
    marrow->Update(0.0f);

    molga::RenderQueue queue;
    marrow->CollectRender(queue);
    REQUIRE_FALSE(queue.GetCommands().empty());
    for (const auto& command : queue.GetCommands()) {
        REQUIRE(command.geometry);
        REQUIRE(command.geometryIndices);
        CHECK_FALSE(command.geometry->empty());
        CHECK(command.geometryIndices->size() % 3U == 0U);
        CHECK(command.batchKey.textureStableId != 0U);
    }

    Renderer renderer;
    std::string error;
    REQUIRE(renderer.Init(&error));
    molga::RenderTarget target({molga::RenderTargetColorFormat::SRGBA8,
                                false, molga::TextureFilter::Nearest});
    REQUIRE(target.Init(256, 256, &error));
    REQUIRE(Acquire(*host, renderer, error));
    REQUIRE(renderer.BeginTarget(
        target, {0.0f, 0.0f, 0.0f, 1.0f}, molga::LoadAction::Clear,
        &error));
    Shader* batch = ShaderManager::Get().Get("batch");
    REQUIRE(batch);
    Camera2D camera(256.0f, 256.0f);
    {
        molga::RenderPass pass(renderer, batch, &camera);
        for (const auto& command : queue.GetCommands()) {
            REQUIRE(renderer.SubmitGeometry(
                *command.geometry, *command.geometryIndices,
                command.batchKey, nullptr, &error));
        }
    }
    REQUIRE(renderer.EndTarget(&error));
    REQUIRE(renderer.SubmitFrame(&error));

    std::vector<std::uint8_t> pixels;
    REQUIRE(host->Graphics().ReadbackRGBA8(
        target.ColorView(), {0, 0, 256, 256}, pixels, error));
    std::size_t coloredPixels = 0;
    int minX = 256;
    int minY = 256;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            const auto pixel = Pixel(pixels, 256, x, y);
            if (pixel[0] != 0U || pixel[1] != 0U || pixel[2] != 0U) {
                ++coloredPixels;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    INFO("Marrow colored pixels=" << coloredPixels << " bounds="
         << minX << "," << minY << "-" << maxX << "," << maxY);
    CHECK(coloredPixels > 11000U);
    CHECK(coloredPixels < 12000U);
    CHECK(minX == 80);
    CHECK(minY == 105);
    CHECK(maxX == 175);
    CHECK(maxY == 224);
    const auto fixedProbe = Pixel(pixels, 256, 128, 128);
    CHECK(Near(fixedProbe[0], 165));
    CHECK(Near(fixedProbe[1], 225));
    CHECK(Near(fixedProbe[2], 248));
    CHECK(Near(fixedProbe[3], 255));

    queue.Clear();
    object.reset();
    TextureManager::Get().Clear();
    PathService::Get().SetAssetRoot(previousAssetRoot);
}
#endif
