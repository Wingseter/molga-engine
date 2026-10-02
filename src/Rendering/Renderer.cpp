#include "Rendering/Renderer.h"

#include "Common/Log.h"
#include "Rendering/Camera2D.h"
#include "Rendering/GpuRetirementQueue.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/LightingPipeline2D.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Sprite.h"
#include "Rendering/Texture.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

// Task 6.2: Renderer.h의 detail 선언이 켜고 끈다. 널이면 아무 일도 없다.
molga::detail::RendererShutdownStageHook shutdownStageHook = nullptr;

void NotifyShutdownStage(const char* stage) {
    if (shutdownStageHook) shutdownStageHook(stage);
}

template <typename T>
std::vector<std::uint8_t> BytesOf(const T& value) {
    static_assert((sizeof(T) % 16U) == 0U);
    std::vector<std::uint8_t> bytes(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T));
    return bytes;
}

std::size_t NextCapacity(std::size_t required) {
    std::size_t capacity = 4096U;
    while (capacity < required && capacity <=
           std::numeric_limits<std::size_t>::max() / 2U) {
        capacity *= 2U;
    }
    return capacity >= required ? capacity : required;
}

molga::BlendState ToBlendState(BlendMode mode) {
    switch (mode) {
        case BlendMode::Opaque: return molga::BlendState::Opaque;
        case BlendMode::Alpha: return molga::BlendState::Alpha;
        case BlendMode::Additive: return molga::BlendState::Additive;
        case BlendMode::Multiply: return molga::BlendState::Multiply;
        case BlendMode::Screen: return molga::BlendState::Screen;
    }
    return molga::BlendState::Alpha;
}

struct alignas(16) BatchVertexConstants {
    mat4x4 projection;
};

struct alignas(16) BatchFragmentConstants {
    std::uint32_t useTexture = 0;
    float padding[3]{};
};

struct alignas(16) LitFragmentConstants {
    float ambientColor[4]{};
    float shadowViewport[4]{};
    float ambientIntensity = 0.0f;
    std::uint32_t lightCount = 0;
    std::uint32_t receiverLayer = 0;
    std::uint32_t useTexture = 0;
    std::uint32_t useNormalTexture = 0;
    float normalStrength = 1.0f;
    float padding[2]{};
    float lightPositionRadiusHeight[molga::kMaxPointLights2D][4]{};
    float lightColorIntensity[molga::kMaxPointLights2D][4]{};
    float lightFalloffPadding[molga::kMaxPointLights2D][4]{};
    std::uint32_t lightMetadata[molga::kMaxPointLights2D][4]{};
};

static_assert((sizeof(BatchVertexConstants) % 16U) == 0U);
static_assert((sizeof(BatchFragmentConstants) % 16U) == 0U);
static_assert((sizeof(LitFragmentConstants) % 16U) == 0U);

struct alignas(16) DefaultVertexConstants {
    mat4x4 model;
    mat4x4 projection;
    float uvRegion[4]{};
};

struct alignas(16) DefaultFragmentConstants {
    float tint[4]{};
    std::uint32_t useTexture = 0;
    float padding[3]{};
};

} // namespace

struct Renderer::Impl {
    enum class OperationType { BeginPass, EndPass, Draw, Blit };

    struct RecordedDraw {
        Shader* shader = nullptr;
        molga::BlendState blend = molga::BlendState::Alpha;
        bool depthTest = false;
        bool depthWrite = false;
        std::size_t vertexOffset = 0;
        std::size_t vertexSize = 0;
        std::uint32_t vertexCount = 0;
        std::size_t indexOffset = 0;
        std::uint32_t indexCount = 0;
        std::vector<molga::DrawTextureBinding> textures;
        std::vector<std::uint8_t> vertexUniforms;
        std::vector<std::uint8_t> fragmentUniforms;
    };

    struct RecordedBlit {
        molga::TextureView source;
        molga::PixelRectU32 sourceRect;
        molga::ColorAttachmentDescriptor destination;
        molga::PixelRectU32 destinationRect;
        molga::TextureFilter filter = molga::TextureFilter::Nearest;
    };

    struct Operation {
        OperationType type = OperationType::EndPass;
        molga::RenderPassDescriptor pass;
        molga::PixelRectU32 viewport;
        molga::PixelRectU32 scissor;
        RecordedDraw draw;
        RecordedBlit blit;
    };

    molga::GraphicsDevice* device = nullptr;
    std::optional<molga::FrameContext> frame;
    std::vector<std::uint8_t> vertexBytes;
    std::vector<std::uint32_t> indices;
    std::vector<Operation> operations;
    molga::BufferHandle vertexBuffer;
    molga::BufferHandle indexBuffer;
    std::size_t vertexCapacity = 0;
    std::size_t indexCapacity = 0;
    std::map<molga::PipelineKey, molga::PipelineHandle> pipelines;
    molga::TextureHandle whiteTexture;
    molga::TextureHandle flatNormalTexture;
    molga::TextureHandle zeroShadowTexture;
    molga::SamplerHandle linearSampler;
    molga::SamplerHandle nearestSampler;
    molga::Color4f mainClear{0.0f, 0.0f, 0.0f, 1.0f};
    bool passRecording = false;
    // Task 11.2 Step 7g. 이 renderer가 GPU idle을 한 번이라도 증명했는가.
    // 재시도가 성공한 drain을 반복하지 않게 하는 값이다.
    bool provenGpuIdle = false;
    // ── Task 11.2 Step 6c: 지금 열려 있는 패스의 뷰포트 ──────────────────────
    // ResetPassScissor가 "패스 전체"를 무엇으로 되돌릴지 아는 유일한 자리다.
    // 패스 시작에서 세우고, SetPassViewport가 갱신하며, EndTarget이 지운다.
    // 지우지 않으면 다음 패스가 열리기 전의 reset이 앞 패스의 사각형을
    // 되돌려 준다 — 그 값은 이 패스의 것이 아니다.
    molga::PixelRectU32 activePassViewport{};
    bool uploadsPrepared = false;
    bool renderEncoded = false;
    bool overlayPassOpen = false;
    bool swapchainWritten = false;
    molga::FrameTelemetry lastTelemetry{};
    molga::TextureFormat activeColorFormat = molga::TextureFormat::SRGBA8;
    bool activeHasDepth = false;
    molga::TextureFormat activeDepthFormat = molga::TextureFormat::Depth24Stencil8;
    // Task 6.2. 이 프레임이 붙든 page(정체성 -> 토큰 하나)와, 제출된 프레임들이
    // fence를 기다리며 붙들고 있는 토큰들.
    std::map<std::pair<molga::ResourceLifetimeDomain, std::uint64_t>,
             std::shared_ptr<const void>> frameRetainedPages;
    molga::GpuRetirementQueue retirement;
    // fence 없이 붙든 묶음의 수. 큐는 이것을 세지 않고(공개 선언은 계획이 준
    // 그대로다), 아래 상한 검사는 fence가 있는 쪽과 없는 쪽을 함께 보아야
    // 한다 — 자라는 것은 둘 다이기 때문이다.
    std::size_t unfencedRetainCount = 0U;
    unsigned stalledDrainDiagnostics = 0U;

    // ── Task 6.2: 반납이 막혔을 때의 상한 ───────────────────────────────────
    // fence 질의가 불가능한 장치(FenceQueryPolarity::Unusable)나 fence 획득이
    // 계속 실패하는 제출에서는 Poll이 아무것도 반납하지 못한다. 그대로 두면
    // 붙든 토큰이 프레임마다 늘고, atlas page의 쓰기 봉인이 영원히 풀리지
    // 않아 예산이 차는 순간부터 새 glyph가 전부 tofu가 된다. 실패 경로가
    // 무한히 자라는 것은 fail-closed가 아니다.
    //
    // 32는 정상 장치에서 결코 닿지 않는 값이다: fence가 신호하는 장치에서는
    // 비행 중인 제출이 스왑체인 깊이(2~3)를 넘지 않는다. 동시에 봉인이 풀리기
    // 까지의 최악 지연을 32 프레임으로 묶는다 — 그 사이 atlas는 봉인된 page를
    // 비켜 새 page를 열지만, 상한이 없으면 그것이 영원히 계속된다.
    static constexpr std::size_t kMaxStalledRetirements = 32U;
    static constexpr unsigned kMaxStalledDrainDiagnostics = 4U;

    std::vector<std::shared_ptr<const void>> TakeRetainedPages() {
        std::vector<std::shared_ptr<const void>> tokens;
        tokens.reserve(frameRetainedPages.size());
        for (auto& entry : frameRetainedPages) {
            tokens.push_back(std::move(entry.second));
        }
        frameRetainedPages.clear();
        return tokens;
    }

    void HoldWithoutFence(std::vector<std::shared_ptr<const void>> tokens) {
        if (tokens.empty()) return;
        retirement.RetainWithoutFence(std::move(tokens));
        ++unfencedRetainCount;
    }

    void ClearRetirementAfterProvenIdle() {
        retirement.DrainAfterGpuIdle();
        unfencedRetainCount = 0U;
    }

    // 상한에 닿으면 증거를 만들어서 푼다: 성공한 GPU idle wait는 fence 신호
    // 보다 강한 증거이므로, 그 뒤의 drain은 "제출이 끝난 뒤에만 놓는다"는
    // 계약을 깨지 않는다. 대가는 프레임 경계 하나에서의 동기 정지이고, 그것은
    // fence를 잃은 장치에서만 일어난다. idle wait마저 실패하면 아무것도 놓지
    // 않는다 — 그때는 증거가 없고, 증거가 없으면 붙들고 있는 것이 안전한
    // 쪽이다.
    void DrainBackloggedRetirements() {
        if (!device) return;
        if (retirement.PendingSubmissionCount() + unfencedRetainCount <
            kMaxStalledRetirements) {
            return;
        }
        std::string idleError;
        if (!device->WaitIdle(&idleError)) {
            Log::Error("Renderer",
                       "GPU retirement is stalled and the idle wait failed: " +
                           idleError);
            return;
        }
        ClearRetirementAfterProvenIdle();
        if (stalledDrainDiagnostics < kMaxStalledDrainDiagnostics) {
            ++stalledDrainDiagnostics;
            Log::Error("Renderer",
                       "GPU submission fences are not retiring; forced a GPU "
                       "idle wait to release retained atlas pages");
        }
    }

    void ResetFrame() {
        // EndMainPassAndSubmit을 거치지 않고 닫힌 프레임의 토큰이 여기로
        // 온다. 공개 탈출구인 Renderer::CurrentFrame()으로 FrameContext를
        // 직접 제출한 경우가 그렇고, 아직 제출되지 않은 프레임이라면
        // ~FrameContext가 swapchain 명령 버퍼를 그대로 제출한다. 어느 쪽이든
        // 명령은 GPU에 갔고 그 끝을 증명할 fence는 없으므로, 여기서 놓으면
        // 제출된 명령 밑에서 텍스처가 사라진다.
        HoldWithoutFence(TakeRetainedPages());
        frame.reset();
        vertexBytes.clear();
        indices.clear();
        operations.clear();
        passRecording = false;
        activePassViewport = molga::PixelRectU32{};
        uploadsPrepared = false;
        renderEncoded = false;
        overlayPassOpen = false;
        swapchainWritten = false;
        activeColorFormat = molga::TextureFormat::SRGBA8;
        activeHasDepth = false;
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {
    mat4x4_identity(projection_);
    mat4x4_identity(view_);
}

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(std::string* errorOut) {
    if (impl_->device) {
        if (errorOut) errorOut->clear();
        return true;
    }
    impl_->device = molga::GraphicsDevice::Current();
    if (!impl_->device) {
        if (errorOut) *errorOut = "renderer requires an active graphics device";
        return false;
    }

    std::string error;
    if (!ShaderManager::Get().Get("batch") &&
        !ShaderManager::Get().InitializeDefault(&error)) {
        if (errorOut) *errorOut = error;
        impl_->device = nullptr;
        return false;
    }

    molga::SamplerDescriptor linear;
    linear.minFilter = molga::TextureFilter::Linear;
    linear.magFilter = molga::TextureFilter::Linear;
    linear.debugName = "Renderer.LinearSampler";
    impl_->linearSampler = impl_->device->CreateSampler(linear, error);
    molga::SamplerDescriptor nearest = linear;
    nearest.minFilter = molga::TextureFilter::Nearest;
    nearest.magFilter = molga::TextureFilter::Nearest;
    nearest.debugName = "Renderer.NearestSampler";
    impl_->nearestSampler = impl_->device->CreateSampler(nearest, error);
    if (!impl_->linearSampler || !impl_->nearestSampler) {
        Shutdown();
        if (errorOut) *errorOut = error;
        return false;
    }

    auto makeFallback = [&](const char* name, std::uint32_t layers,
                            const std::array<std::uint8_t, 4>& pixel,
                            molga::TextureHandle& output) -> bool {
        molga::TextureDescriptor descriptor;
        descriptor.width = 1;
        descriptor.height = 1;
        descriptor.layers = layers;
        descriptor.format = molga::TextureFormat::RGBA8;
        descriptor.usage = molga::GpuTextureUsage::Sampler;
        descriptor.debugName = name;
        output = impl_->device->CreateTexture(descriptor, error);
        if (!output) return false;
        for (std::uint32_t layer = 0; layer < layers; ++layer) {
            if (!impl_->device->UploadTextureImmediate(
                    {output, 0, layer}, {0, 0, 1, 1}, pixel.data(),
                    pixel.size(), 4, error)) {
                return false;
            }
        }
        return true;
    };
    if (!makeFallback("Renderer.White", 1, {255, 255, 255, 255},
                      impl_->whiteTexture) ||
        !makeFallback("Renderer.FlatNormal", 1, {128, 128, 255, 255},
                      impl_->flatNormalTexture) ||
        !makeFallback("Renderer.ZeroShadow", molga::kMaxShadowLights2D,
                      {0, 0, 0, 255}, impl_->zeroShadowTexture)) {
        Shutdown();
        if (errorOut) *errorOut = error;
        return false;
    }
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::DrainSubmittedFrames(std::string* errorOut) {
    if (!impl_) return true;
    // ── Step 8a: 먼저 일을 멈추고, 그다음에 idle을 기다린다 ──────────────────
    // 열려 있던 프레임을 닫아야 그 프레임의 명령과 page 토큰이 아래 idle
    // wait가 증명하는 범위 안으로 들어온다. 이 두 줄이 끝나면 활성 프레임이
    // 없으므로 새 수집(RetainUntilFrameComplete)도 새 제출도 거절된다.
    if (impl_->frame && impl_->frame->IsValid()) {
        EndMainPassAndSubmit(nullptr);
    }
    impl_->ResetFrame();
    if (!impl_->device) {
        impl_->provenGpuIdle = true;
        if (errorOut) errorOut->clear();
        return true;
    }
    // ── Task 11.2 Step 7f: 증명하지 못하면 아무것도 부수지 않는다 ───────────
    // Task 6.2의 std::abort()는 여기 있었다. 재시도할 수 있는 종료가 생긴
    // 지금 프로세스를 죽이는 것은 fail-closed가 아니다 — 죽으면 host가
    // 붙들고 있던 외부 소유자도, 그 소유자를 놓아 줄 기회도 함께 사라진다.
    // 아직 붙들고 있는 제출 수를 오류 문자열에 남긴다: 0이 아니라는 것이
    // "teardown보다 먼저 막혔다"의 증거다.
    std::string idleError;
    if (!impl_->device->WaitIdle(&idleError)) {
        if (errorOut) {
            *errorOut = idleError + " (pending submissions: " +
                        std::to_string(impl_->retirement.PendingSubmissionCount()) +
                        ")";
        }
        return false;
    }
    impl_->provenGpuIdle = true;
    NotifyShutdownStage("idle-wait-proven");
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::HasProvenGpuIdle() const noexcept {
    return impl_ && impl_->provenGpuIdle;
}

void Renderer::ReleaseCompletedGpuLifetimes() {
    if (!impl_ || !impl_->device) return;
    // ── Step 8b: idle이 증명된 다음에만, 장치가 살아 있는 동안 ──────────────
    // 반납 큐를 먼저 비운다. 여기 남은 fence 객체는 장치를 가리키고 있으므로
    // 장치보다 먼저 사라져야 한다.
    impl_->ClearRetirementAfterProvenIdle();
    NotifyShutdownStage("retirement-drained");
}

void Renderer::DestroyDeviceResources() {
    if (!impl_) return;
    if (impl_->device) {
        for (auto& [key, pipeline] : impl_->pipelines) {
            (void)key;
            impl_->device->DestroyPipeline(pipeline);
        }
        impl_->device->DestroyBuffer(impl_->indexBuffer);
        impl_->device->DestroyBuffer(impl_->vertexBuffer);
        impl_->device->DestroySampler(impl_->nearestSampler);
        impl_->device->DestroySampler(impl_->linearSampler);
        impl_->device->DestroyTexture(impl_->zeroShadowTexture);
        impl_->device->DestroyTexture(impl_->flatNormalTexture);
        impl_->device->DestroyTexture(impl_->whiteTexture);
        NotifyShutdownStage("gpu-resources-destroyed");
    }
    impl_->pipelines.clear();
    impl_->vertexCapacity = 0;
    impl_->indexCapacity = 0;
    impl_->ResetFrame();
    impl_->device = nullptr;
    currentShader_ = nullptr;
}

void Renderer::Shutdown() {
    if (!impl_) return;
    std::string drainError;
    if (!DrainSubmittedFrames(&drainError)) {
        // 증명하지 못한 idle 위에서는 아무것도 부수지 않는다. 살아 있을지
        // 모르는 명령 버퍼 밑에서 텍스처를 파괴하는 것보다 자원을 OS가
        // 회수하게 두는 쪽이 싸다. 재시도할 수 있는 경로는 EngineShutdown이다.
        Log::Error("Renderer",
                   "GPU idle wait failed during renderer shutdown; no GPU "
                   "resource was destroyed and the device is retained: " +
                       drainError);
        return;
    }
    ReleaseCompletedGpuLifetimes();
    DestroyDeviceResources();
}

bool Renderer::BeginFrame(molga::FrameContext&& frame, std::string* errorOut) {
    if (!impl_->device && !Init(errorOut)) return false;
    if (!frame.IsValid()) {
        if (errorOut) *errorOut = "renderer received an invalid frame context";
        return false;
    }
    if (impl_->frame && impl_->frame->IsValid()) {
        if (errorOut) *errorOut = "previous renderer frame was not submitted";
        return false;
    }
    impl_->ResetFrame();
    // ── Task 11.2 Step 7g: 새 일이 시작되면 옛 증명은 무효다 ────────────────
    // provenGpuIdle은 "이 renderer가 GPU idle을 증명했다"이고, 재시도하는
    // 종료가 그 값을 보고 drain을 건너뛴다. 여기서 지우지 않으면, 증명한 뒤에
    // 프레임을 하나 더 제출한 renderer가 그 제출을 기다리지 않고 자원을
    // 부수게 된다 — 살아 있는 명령 밑에서 텍스처가 사라지는 정확히 그 상태다.
    impl_->provenGpuIdle = false;
    impl_->frame.emplace(std::move(frame));
    impl_->mainClear = {0.0f, 0.0f, 0.0f, 1.0f};
    currentShader_ = nullptr;
    logicalPass_ = molga::RenderPassState{};
    // Step 8. 프레임을 연 직후, 새 명령을 모으기 전에 정확히 한 번. 여기서
    // 돌려야 이번 프레임이 다시 채우려는 page를 앞 프레임의 이미 끝난 제출이
    // 붙들고 있지 않다. 프레임 경계가 반납의 근거는 아니다 — 반납을 결정하는
    // 것은 각 제출의 fence이고, 이것은 그 fence를 물어보는 시점일 뿐이다.
    impl_->retirement.Poll();
    // Poll이 아무것도 반납하지 못한 채 쌓이기만 하면, 여기서 상한을 걸고
    // 증명된 idle로 푼다. 정상 장치에서는 상한에 닿지 않는다.
    impl_->DrainBackloggedRetirements();
    if (errorOut) errorOut->clear();
    return true;
}

void Renderer::RetainUntilFrameComplete(
    std::uint64_t pageIdentity, std::shared_ptr<const void> pageLifetime) {
    RetainUntilFrameComplete(molga::ResourceLifetimeDomain::GlyphPage,
                             pageIdentity, std::move(pageLifetime));
}

void Renderer::RetainUntilFrameComplete(
    molga::ResourceLifetimeDomain domain, std::uint64_t pageIdentity,
    std::shared_ptr<const void> pageLifetime) {
    // 널 토큰은 붙들 것이 없다. 포화한 atlas가 돌려주는 tofu handle이 정확히
    // 그 모양(정체성 0, 토큰 없음)이므로, 그리는 쪽이 분기하지 않아도 된다.
    if (!pageLifetime) return;
    // 0은 적법한 page 정체성이 아니다. 받아 주면 정체성 없는 여러 자원이 하나의
    // 가짜 page로 뭉쳐서, 그 자리에 진짜 page 토큰이 들어갈 수 없다.
    if (pageIdentity == 0U) {
        throw std::logic_error(
            "RetainUntilFrameComplete needs a nonzero atlas page identity");
    }
    if (!HasFrame()) {
        throw std::logic_error(
            "RetainUntilFrameComplete needs an active renderer frame");
    }
    const auto key = std::make_pair(domain, pageIdentity);
    auto found = impl_->frameRetainedPages.find(key);
    if (found == impl_->frameRetainedPages.end()) {
        impl_->frameRetainedPages.emplace(key, std::move(pageLifetime));
        return;
    }
    // 같은 page를 가리키는 두 번째 명령은 아무것도 바꾸지 않는다. 다만 소유자가
    // 다르면 그것은 중복이 아니라 정체성 충돌이다: 하나의 fence에 하나의
    // 토큰만 매달리므로 나머지 하나는 어떤 fence도 반납해 주지 않는다.
    const std::shared_ptr<const void>& owner = found->second;
    const bool sameOwner = !owner.owner_before(pageLifetime) &&
                           !pageLifetime.owner_before(owner);
    if (!sameOwner) {
        throw std::logic_error(
            "one atlas page identity arrived with two different owners");
    }
}

std::size_t Renderer::ActiveFrameRetainedPageCount() const noexcept {
    return impl_->frameRetainedPages.size();
}

bool Renderer::HasFrame() const {
    return impl_->frame && impl_->frame->IsValid();
}

molga::FrameContext* Renderer::CurrentFrame() {
    return HasFrame() ? &*impl_->frame : nullptr;
}

const molga::FrameContext* Renderer::CurrentFrame() const {
    return HasFrame() ? &*impl_->frame : nullptr;
}

void Renderer::Clear(float r, float g, float b, float a) {
    impl_->mainClear = {r, g, b, a};
}

void Renderer::SetViewport(int width, int height) {
    if (width <= 0 || height <= 0 || !impl_->passRecording) return;
    SetPassViewport({0, 0, static_cast<std::uint32_t>(width),
                     static_cast<std::uint32_t>(height)}, nullptr);
}

bool Renderer::BeginTarget(molga::RenderTarget& target,
                           molga::Color4f clear, molga::LoadAction load,
                           std::string* errorOut) {
    return BeginTarget(target,
        {0, 0, static_cast<std::uint32_t>(std::max(target.Width(), 0)),
         static_cast<std::uint32_t>(std::max(target.Height(), 0))},
        clear, load, errorOut);
}

bool Renderer::BeginTarget(molga::RenderTarget& target,
                           molga::PixelRectU32 viewport,
                           molga::Color4f clear, molga::LoadAction load,
                           std::string* errorOut) {
    if (!HasFrame() || impl_->passRecording || !target.IsValid() ||
        viewport.width == 0 || viewport.height == 0) {
        if (errorOut) *errorOut = "cannot begin invalid or nested render target";
        return false;
    }
    Impl::Operation operation;
    operation.type = Impl::OperationType::BeginPass;
    operation.pass.color.view = target.ColorView();
    operation.pass.color.loadAction = load;
    operation.pass.color.storeAction = molga::StoreAction::Store;
    operation.pass.color.clearColor = clear;
    if (target.Specification().depthStencil) {
        operation.pass.hasDepthStencil = true;
        operation.pass.depthStencil.view = target.DepthStencilView();
        operation.pass.depthStencil.depthLoadAction = load;
        operation.pass.depthStencil.depthStoreAction = molga::StoreAction::Store;
        operation.pass.depthStencil.stencilLoadAction = load;
        operation.pass.depthStencil.stencilStoreAction = molga::StoreAction::Store;
    }
    operation.viewport = viewport;
    operation.scissor = viewport;
    impl_->operations.push_back(std::move(operation));
    impl_->passRecording = true;
    impl_->activePassViewport = viewport;
    impl_->activeColorFormat =
        target.Specification().colorFormat == molga::RenderTargetColorFormat::RGBA16F
            ? molga::TextureFormat::RGBA16F : molga::TextureFormat::SRGBA8;
    impl_->activeHasDepth = target.Specification().depthStencil;
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::BeginSwapchainPass(molga::LoadAction load,
                                  molga::Color4f clear,
                                  std::string* errorOut) {
    if (!HasFrame()) {
        if (errorOut) *errorOut = "no acquired frame";
        return false;
    }
    return BeginSwapchainPass(
        {0, 0, impl_->frame->SwapchainWidth(), impl_->frame->SwapchainHeight()},
        load, clear, errorOut);
}

bool Renderer::BeginTextureTarget(molga::TextureView color,
                                  molga::PixelRectU32 viewport,
                                  molga::TextureFormat format,
                                  molga::Color4f clear,
                                  molga::LoadAction load,
                                  std::string* errorOut) {
    molga::TextureDescriptor descriptor;
    if (!HasFrame() || impl_->passRecording || !color.texture ||
        viewport.width == 0 || viewport.height == 0 ||
        !impl_->device->Describe(color.texture, descriptor) ||
        !molga::HasUsage(descriptor.usage, molga::GpuTextureUsage::ColorTarget) ||
        descriptor.format != format) {
        if (errorOut) *errorOut = "cannot begin invalid texture render target";
        return false;
    }
    Impl::Operation operation;
    operation.type = Impl::OperationType::BeginPass;
    operation.pass.color.view = color;
    operation.pass.color.loadAction = load;
    operation.pass.color.storeAction = molga::StoreAction::Store;
    operation.pass.color.clearColor = clear;
    operation.viewport = viewport;
    operation.scissor = viewport;
    impl_->operations.push_back(std::move(operation));
    impl_->passRecording = true;
    impl_->activePassViewport = viewport;
    impl_->activeColorFormat = format;
    impl_->activeHasDepth = false;
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::BeginSwapchainPass(molga::PixelRectU32 viewport,
                                  molga::LoadAction load,
                                  molga::Color4f clear,
                                  std::string* errorOut) {
    if (!HasFrame() || impl_->passRecording || viewport.width == 0 ||
        viewport.height == 0) {
        if (errorOut) *errorOut = "cannot begin invalid or nested swapchain pass";
        return false;
    }
    Impl::Operation operation;
    operation.type = Impl::OperationType::BeginPass;
    operation.pass.color.swapchain = true;
    operation.pass.color.loadAction = load;
    operation.pass.color.storeAction = molga::StoreAction::Store;
    operation.pass.color.clearColor = clear;
    operation.viewport = viewport;
    operation.scissor = viewport;
    impl_->operations.push_back(std::move(operation));
    impl_->passRecording = true;
    impl_->activePassViewport = viewport;
    impl_->activeColorFormat = impl_->device->Info().swapchainFormat;
    impl_->activeHasDepth = false;
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::EndTarget(std::string* errorOut) {
    if (!impl_->passRecording) {
        if (errorOut) *errorOut = "no render target pass is active";
        return false;
    }
    Impl::Operation operation;
    operation.type = Impl::OperationType::EndPass;
    impl_->operations.push_back(std::move(operation));
    impl_->passRecording = false;
    impl_->activePassViewport = molga::PixelRectU32{};
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::SetPassViewport(molga::PixelRectU32 viewport,
                               std::string* errorOut) {
    if (!impl_->passRecording || viewport.width == 0 || viewport.height == 0) {
        if (errorOut) *errorOut = "viewport requires an active recorded pass";
        return false;
    }
    Impl::Operation operation;
    operation.type = Impl::OperationType::BeginPass; // state-only sentinel
    operation.viewport = viewport;
    operation.scissor = viewport;
    operation.pass.color.storeAction = molga::StoreAction::DontCare;
    // An empty color attachment identifies a state update during encoding.
    impl_->operations.push_back(std::move(operation));
    impl_->activePassViewport = viewport;
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::SetPassScissor(molga::PixelRectU32 scissor,
                              std::string* errorOut) {
    if (!impl_->passRecording || scissor.width == 0 || scissor.height == 0) {
        if (errorOut) *errorOut = "scissor requires an active recorded pass";
        return false;
    }
    Impl::Operation operation;
    operation.type = Impl::OperationType::BeginPass;
    operation.scissor = scissor;
    operation.pass.color.storeAction = molga::StoreAction::DontCare;
    impl_->operations.push_back(std::move(operation));
    if (errorOut) errorOut->clear();
    return true;
}

// ── Task 11.2 Step 6d: 클립을 패스 전체로 되돌린다 ──────────────────────────
// 0,0,0,0으로 되돌리지 않는다 — SetScissor는 폭 0을 거절하고, 거절되면 앞
// 명령의 클립이 그대로 남아 그 아래에서 다음 것이 그려진다. 되돌릴 값은
// 언제나 지금 열려 있는 패스의 뷰포트다.
bool Renderer::ResetPassScissor(std::string* errorOut) {
    if (!impl_->passRecording || impl_->activePassViewport.width == 0 ||
        impl_->activePassViewport.height == 0) {
        if (errorOut) *errorOut = "scissor reset requires an active pass";
        return false;
    }
    return SetPassScissor(impl_->activePassViewport, errorOut);
}

void Renderer::SetProjection(float left, float right, float bottom, float top) {
    mat4x4_ortho(projection_, left, right, bottom, top, -1.0f, 1.0f);
}

void Renderer::SetShader(Shader* shader) {
    if (!shader || shader == currentShader_) return;
    currentShader_ = shader;
    ++stats_.shaderSwitches;
}

void Renderer::Begin(Shader* shader, Camera2D* camera) {
    if (!logicalPass_.TryBegin()) {
        Log::Error("Renderer", "nested logical render pass rejected");
        return;
    }
    if (!shader) {
        Log::Error("Renderer", "logical render pass requires a shader");
        logicalPass_.TryEnd();
        return;
    }
    if (camera) {
        camera->GetProjectionMatrix(projection_);
        camera->GetViewMatrix(view_);
        mat4x4 combined;
        mat4x4_mul(combined, projection_, view_);
        mat4x4_dup(projection_, combined);
    } else {
        mat4x4_identity(view_);
    }
    SetShader(shader);
}

void Renderer::DrawSprite(Sprite* sprite) {
    if (!logicalPass_.CanDraw() || !sprite) return;
    Shader* shader = ShaderManager::Get().Get("default");
    if (!shader) shader = currentShader_;
    if (!shader) return;

    static constexpr std::array<float, 24> quad{
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
    };
    molga::DrawPacket packet;
    packet.shader = shader;
    packet.blend = molga::BlendState::Alpha;
    packet.vertexStride = sizeof(float) * 4U;
    packet.vertices.resize(sizeof(quad));
    std::memcpy(packet.vertices.data(), quad.data(), sizeof(quad));

    DefaultVertexConstants vertex{};
    sprite->GetModelMatrix(vertex.model);
    mat4x4_dup(vertex.projection, projection_);
    std::copy_n(sprite->uv, 4, vertex.uvRegion);
    packet.vertexUniforms = BytesOf(vertex);
    DefaultFragmentConstants fragment{};
    std::copy_n(sprite->color, 4, fragment.tint);
    fragment.useTexture = sprite->texture && sprite->texture->IsValid() ? 1U : 0U;
    packet.fragmentUniforms = BytesOf(fragment);
    if (fragment.useTexture != 0U) {
        packet.textures.push_back({molga::DrawTextureBinding::Stage::Fragment,
                                   0, {sprite->texture->Handle(), 0, 0},
                                   sprite->texture->Sampler()});
    }
    Submit(packet, nullptr);
    ++stats_.submittedSprites;
}

void Renderer::End() {
    if (!logicalPass_.TryEnd()) {
        Log::Error("Renderer", "logical render pass end without begin rejected");
        return;
    }
    currentShader_ = nullptr;
}

bool Renderer::IsDrawing() const { return logicalPass_.CanDraw(); }

bool Renderer::Submit(const molga::DrawPacket& packet, std::string* errorOut) {
    if (!HasFrame() || !impl_->passRecording || impl_->uploadsPrepared ||
        !packet.shader || !packet.shader->IsValid() ||
        packet.vertexStride == 0U || packet.vertices.empty() ||
        packet.vertices.size() % packet.vertexStride != 0U ||
        (!packet.vertexUniforms.empty() && packet.vertexUniforms.size() % 16U != 0U) ||
        (!packet.fragmentUniforms.empty() && packet.fragmentUniforms.size() % 16U != 0U)) {
        if (errorOut) *errorOut = "draw packet is invalid for the active frame/pass";
        return false;
    }
    Impl::Operation operation;
    operation.type = Impl::OperationType::Draw;
    operation.draw.shader = packet.shader;
    operation.draw.blend = packet.blend;
    operation.draw.depthTest = packet.depthTest;
    operation.draw.depthWrite = packet.depthWrite;
    operation.draw.vertexOffset = impl_->vertexBytes.size();
    operation.draw.vertexSize = packet.vertices.size();
    operation.draw.vertexCount = static_cast<std::uint32_t>(
        packet.vertices.size() / packet.vertexStride);
    operation.draw.indexOffset = impl_->indices.size() * sizeof(std::uint32_t);
    operation.draw.indexCount = static_cast<std::uint32_t>(packet.indices.size());
    operation.draw.textures = packet.textures;
    operation.draw.vertexUniforms = packet.vertexUniforms;
    operation.draw.fragmentUniforms = packet.fragmentUniforms;
    impl_->vertexBytes.insert(impl_->vertexBytes.end(), packet.vertices.begin(),
                              packet.vertices.end());
    impl_->indices.insert(impl_->indices.end(), packet.indices.begin(),
                          packet.indices.end());
    impl_->operations.push_back(std::move(operation));
    ++stats_.drawCalls;
    stats_.verticesUploadedBytes += packet.vertices.size();
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::SubmitBatch(
    const std::vector<molga::Vertex2D>& vertices, const molga::BatchKey& key,
    const molga::LightingRenderContext2D* lighting, std::string* errorOut) {
    if (vertices.empty() || vertices.size() % 4U != 0U) {
        if (errorOut) *errorOut = "batch geometry must contain complete quads";
        return false;
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(vertices.size() / 4U * 6U);
    for (std::uint32_t base = 0; base < vertices.size(); base += 4U) {
        indices.insert(indices.end(),
            {base, base + 2U, base + 3U, base, base + 1U, base + 2U});
    }
    return SubmitGeometry(vertices, indices, key, lighting, errorOut);
}

bool Renderer::SubmitGeometry(
    const std::vector<molga::Vertex2D>& vertices,
    const std::vector<std::uint32_t>& indices, const molga::BatchKey& key,
    const molga::LightingRenderContext2D* lighting, std::string* errorOut) {
    if (vertices.empty() || indices.empty() || indices.size() % 3U != 0U ||
        std::any_of(indices.begin(), indices.end(), [&](std::uint32_t index) {
            return index >= vertices.size();
        })) {
        if (errorOut) *errorOut = "indexed geometry is invalid";
        return false;
    }
    const bool lit = key.lit && lighting && lighting->IsUsable();
    Shader* shader = ShaderManager::Get().Get(
        lit ? "batch_lit" : (key.shaderName.empty() ? "batch" : key.shaderName));
    if (!shader || shader->VertexStride() != sizeof(molga::Vertex2D)) {
        shader = ShaderManager::Get().Get(lit ? "batch_lit" : "batch");
    }
    if (!shader) {
        if (errorOut) *errorOut = "batch shader is unavailable";
        return false;
    }

    molga::DrawPacket packet;
    packet.shader = shader;
    packet.blend = ToBlendState(key.blendMode);
    packet.vertexStride = sizeof(molga::Vertex2D);
    packet.vertices.resize(vertices.size() * sizeof(molga::Vertex2D));
    std::memcpy(packet.vertices.data(), vertices.data(), packet.vertices.size());
    packet.indices = indices;
    BatchVertexConstants vertex{};
    mat4x4_dup(vertex.projection, projection_);
    packet.vertexUniforms = BytesOf(vertex);

    const bool customMaterialShader = shader->Name() != "batch" &&
                                      shader->Name() != "batch_lit";
    if (customMaterialShader) {
        for (const auto& declared : shader->BundleEntry().textures) {
            const molga::BatchKey::ExtraTexture* supplied = nullptr;
            if (key.materialTextures) {
                const auto found = std::find_if(
                    key.materialTextures->begin(), key.materialTextures->end(),
                    [&](const molga::BatchKey::ExtraTexture& value) {
                        return value.slot == declared.slot &&
                               value.vertexStage == (declared.stage == "vertex");
                    });
                if (found != key.materialTextures->end()) supplied = &*found;
            }
            const bool arrayTexture = declared.dimension == "2DArray";
            const molga::TextureHandle texture = supplied && supplied->texture
                ? supplied->texture
                : (arrayTexture ? impl_->zeroShadowTexture : impl_->whiteTexture);
            const molga::SamplerHandle sampler = supplied && supplied->sampler
                ? supplied->sampler : impl_->linearSampler;
            packet.textures.push_back({
                declared.stage == "vertex"
                    ? molga::DrawTextureBinding::Stage::Vertex
                    : molga::DrawTextureBinding::Stage::Fragment,
                declared.slot, {texture, 0, 0}, sampler});
        }
        stats_.textureBinds += static_cast<int>(packet.textures.size());
    } else {
        const molga::TextureView diffuse = key.texture
            ? molga::TextureView{key.texture, 0, 0}
            : molga::TextureView{impl_->whiteTexture, 0, 0};
        const molga::SamplerHandle diffuseSampler = key.textureSampler
            ? key.textureSampler : impl_->linearSampler;
        packet.textures.push_back({molga::DrawTextureBinding::Stage::Fragment,
                                   0, diffuse, diffuseSampler});
        ++stats_.textureBinds;
    }

    if (customMaterialShader) {
        if (key.materialParameters) {
            packet.fragmentUniforms = *key.materialParameters;
        }
    } else if (!lit) {
        BatchFragmentConstants fragment{};
        fragment.useTexture = key.texture ? 1U : 0U;
        packet.fragmentUniforms = BytesOf(fragment);
    } else {
        LitFragmentConstants fragment{};
        const auto& frame = *lighting->frame;
        fragment.ambientColor[0] = frame.ambientColor.r;
        fragment.ambientColor[1] = frame.ambientColor.g;
        fragment.ambientColor[2] = frame.ambientColor.b;
        fragment.ambientColor[3] = frame.ambientColor.a;
        fragment.shadowViewport[0] = static_cast<float>(lighting->viewport.x);
        fragment.shadowViewport[1] = static_cast<float>(lighting->viewport.y);
        fragment.shadowViewport[2] = static_cast<float>(lighting->viewport.width);
        fragment.shadowViewport[3] = static_cast<float>(lighting->viewport.height);
        fragment.ambientIntensity = frame.ambientIntensity;
        fragment.lightCount = static_cast<std::uint32_t>(
            std::min(frame.lights.size(), molga::kMaxPointLights2D));
        fragment.receiverLayer = key.receiverLayer;
        fragment.useTexture = key.texture ? 1U : 0U;
        fragment.useNormalTexture = key.normalTexture ? 1U : 0U;
        fragment.normalStrength = key.normalStrength;
        for (std::uint32_t index = 0; index < fragment.lightCount; ++index) {
            const auto& light = frame.lights[index];
            fragment.lightPositionRadiusHeight[index][0] = light.position.x;
            fragment.lightPositionRadiusHeight[index][1] = light.position.y;
            fragment.lightPositionRadiusHeight[index][2] = light.radius;
            fragment.lightPositionRadiusHeight[index][3] = light.height;
            fragment.lightColorIntensity[index][0] = light.color.r;
            fragment.lightColorIntensity[index][1] = light.color.g;
            fragment.lightColorIntensity[index][2] = light.color.b;
            fragment.lightColorIntensity[index][3] = light.intensity;
            fragment.lightFalloffPadding[index][0] = light.falloff;
            fragment.lightMetadata[index][0] = light.affectMask;
            const bool shadowAvailable = light.shadowLayer >= 0 &&
                light.shadowLayer < static_cast<int>(molga::kMaxShadowLights2D) &&
                lighting->shadowLayerAvailable[
                    static_cast<std::size_t>(light.shadowLayer)];
            fragment.lightMetadata[index][1] = static_cast<std::uint32_t>(
                shadowAvailable ? light.shadowLayer : -1);
        }
        packet.fragmentUniforms = BytesOf(fragment);
        packet.textures.push_back({molga::DrawTextureBinding::Stage::Fragment, 1,
            {key.normalTexture ? key.normalTexture : impl_->flatNormalTexture, 0, 0},
            key.normalSampler ? key.normalSampler : impl_->linearSampler});
        packet.textures.push_back({molga::DrawTextureBinding::Stage::Fragment, 2,
            {lighting->shadowTextureArray ? lighting->shadowTextureArray
                                          : impl_->zeroShadowTexture, 0, 0},
            lighting->shadowSampler ? lighting->shadowSampler
                                    : impl_->nearestSampler});
        stats_.textureBinds += 2;
    }
    return Submit(packet, errorOut);
}

bool Renderer::SubmitFullscreen(
    Shader& shader, const std::vector<molga::DrawTextureBinding>& textures,
    const void* fragmentUniforms, std::size_t fragmentUniformSize,
    molga::BlendState blend, std::string* errorOut) {
    static constexpr std::array<float, 12> vertices{
        -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,
    };
    molga::DrawPacket packet;
    packet.shader = &shader;
    packet.blend = blend;
    packet.vertexStride = sizeof(float) * 2U;
    packet.vertices.resize(sizeof(vertices));
    std::memcpy(packet.vertices.data(), vertices.data(), sizeof(vertices));
    packet.textures = textures;
    if (fragmentUniformSize > 0U) {
        if (!fragmentUniforms || fragmentUniformSize % 16U != 0U) {
            if (errorOut) *errorOut = "fullscreen uniform block is invalid";
            return false;
        }
        packet.fragmentUniforms.resize(fragmentUniformSize);
        std::memcpy(packet.fragmentUniforms.data(), fragmentUniforms,
                    fragmentUniformSize);
    }
    stats_.textureBinds += static_cast<int>(textures.size());
    return Submit(packet, errorOut);
}

bool Renderer::Blit(molga::TextureView source,
                    molga::PixelRectU32 sourceRect,
                    const molga::ColorAttachmentDescriptor& destination,
                    molga::PixelRectU32 destinationRect,
                    molga::TextureFilter filter, std::string* errorOut) {
    if (!HasFrame() || impl_->passRecording || impl_->uploadsPrepared ||
        !source.texture || sourceRect.width == 0 || sourceRect.height == 0 ||
        destinationRect.width == 0 || destinationRect.height == 0) {
        if (errorOut) *errorOut = "blit requires a frame, closed pass, and valid regions";
        return false;
    }
    Impl::Operation operation;
    operation.type = Impl::OperationType::Blit;
    operation.blit = {source, sourceRect, destination, destinationRect, filter};
    impl_->operations.push_back(std::move(operation));
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::PrepareUploads(std::string* errorOut) {
    if (!HasFrame() || impl_->passRecording) {
        if (errorOut) *errorOut = "frame uploads require all recorded passes to be closed";
        return false;
    }
    if (impl_->uploadsPrepared) {
        if (errorOut) errorOut->clear();
        return true;
    }
    std::string error;
    const std::size_t vertexRequired = impl_->vertexBytes.size();
    const std::size_t indexRequired = impl_->indices.size() * sizeof(std::uint32_t);
    if (vertexRequired > impl_->vertexCapacity) {
        molga::BufferDescriptor descriptor;
        descriptor.size = NextCapacity(vertexRequired);
        descriptor.usage = molga::GpuBufferUsage::Vertex;
        descriptor.debugName = "Renderer.FrameVertices";
        molga::BufferHandle replacement = impl_->device->CreateBuffer(descriptor, error);
        if (!replacement) {
            if (errorOut) *errorOut = error;
            return false;
        }
        impl_->device->DestroyBuffer(impl_->vertexBuffer);
        impl_->vertexBuffer = replacement;
        impl_->vertexCapacity = descriptor.size;
    }
    if (indexRequired > impl_->indexCapacity) {
        molga::BufferDescriptor descriptor;
        descriptor.size = NextCapacity(indexRequired);
        descriptor.usage = molga::GpuBufferUsage::Index;
        descriptor.debugName = "Renderer.FrameIndices";
        molga::BufferHandle replacement = impl_->device->CreateBuffer(descriptor, error);
        if (!replacement) {
            if (errorOut) *errorOut = error;
            return false;
        }
        impl_->device->DestroyBuffer(impl_->indexBuffer);
        impl_->indexBuffer = replacement;
        impl_->indexCapacity = descriptor.size;
    }
    if (vertexRequired > 0U && !impl_->frame->UploadBuffer(
            impl_->vertexBuffer, 0, impl_->vertexBytes.data(), vertexRequired,
            true, &error)) {
        if (errorOut) *errorOut = error;
        return false;
    }
    if (indexRequired > 0U && !impl_->frame->UploadBuffer(
            impl_->indexBuffer, 0, impl_->indices.data(), indexRequired,
            true, &error)) {
        if (errorOut) *errorOut = error;
        return false;
    }
    impl_->uploadsPrepared = true;
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::EncodeRenderPasses(std::string* errorOut) {
    if (!PrepareUploads(errorOut)) return false;
    if (impl_->renderEncoded) {
        if (errorOut) errorOut->clear();
        return true;
    }
    std::string error;
    molga::TextureFormat colorFormat = impl_->device->Info().swapchainFormat;
    bool hasDepth = false;
    molga::TextureFormat depthFormat = molga::TextureFormat::Depth24Stencil8;
    for (const Impl::Operation& operation : impl_->operations) {
        switch (operation.type) {
            case Impl::OperationType::BeginPass: {
                const bool stateOnly = !operation.pass.color.swapchain &&
                    !operation.pass.color.view.texture;
                if (stateOnly) {
                    if (operation.viewport.width > 0 &&
                        !impl_->frame->SetViewport(operation.viewport, &error)) {
                        if (errorOut) *errorOut = error;
                        return false;
                    }
                    if (operation.scissor.width > 0 &&
                        !impl_->frame->SetScissor(operation.scissor, &error)) {
                        if (errorOut) *errorOut = error;
                        return false;
                    }
                    break;
                }
                if (!impl_->frame->BeginRenderPass(operation.pass, &error) ||
                    !impl_->frame->SetViewport(operation.viewport, &error) ||
                    !impl_->frame->SetScissor(operation.scissor, &error)) {
                    if (errorOut) *errorOut = error;
                    return false;
                }
                if (operation.pass.color.swapchain) {
                    impl_->swapchainWritten = true;
                }
                if (operation.pass.color.swapchain) {
                    colorFormat = impl_->device->Info().swapchainFormat;
                } else {
                    molga::TextureDescriptor described;
                    if (!impl_->device->Describe(
                            operation.pass.color.view.texture, described)) {
                        if (errorOut) *errorOut = "could not describe render target";
                        return false;
                    }
                    colorFormat = described.format;
                }
                hasDepth = operation.pass.hasDepthStencil;
                if (hasDepth) {
                    molga::TextureDescriptor described;
                    if (!impl_->device->Describe(
                            operation.pass.depthStencil.view.texture, described)) {
                        if (errorOut) *errorOut = "could not describe depth target";
                        return false;
                    }
                    depthFormat = described.format;
                }
                break;
            }
            case Impl::OperationType::EndPass:
                impl_->frame->EndRenderPass();
                break;
            case Impl::OperationType::Draw: {
                molga::GraphicsPipelineDescriptor descriptor;
                descriptor.shader = &operation.draw.shader->BundleEntry();
                descriptor.bundleRoot = operation.draw.shader->BundleRoot();
                descriptor.blend = operation.draw.blend;
                descriptor.depthTest = operation.draw.depthTest;
                descriptor.depthWrite = operation.draw.depthWrite;
                descriptor.colorTargetFormat = colorFormat;
                descriptor.hasDepthStencilTarget = hasDepth;
                descriptor.depthStencilFormat = depthFormat;
                const molga::PipelineKey key = molga::MakePipelineKey(descriptor);
                auto iterator = impl_->pipelines.find(key);
                if (iterator == impl_->pipelines.end()) {
                    molga::PipelineHandle pipeline =
                        impl_->device->CreatePipeline(descriptor, error);
                    if (!pipeline) {
                        if (errorOut) *errorOut = error;
                        return false;
                    }
                    iterator = impl_->pipelines.emplace(key, pipeline).first;
                }
                if (!impl_->frame->BindPipeline(iterator->second, &error) ||
                    !impl_->frame->BindVertexBuffer(
                        0, impl_->vertexBuffer, operation.draw.vertexOffset,
                        &error)) {
                    if (errorOut) *errorOut = error;
                    return false;
                }
                if (operation.draw.indexCount > 0U &&
                    !impl_->frame->BindIndexBuffer(
                        impl_->indexBuffer, operation.draw.indexOffset, &error)) {
                    if (errorOut) *errorOut = error;
                    return false;
                }
                for (const auto& texture : operation.draw.textures) {
                    const bool bound =
                        texture.stage == molga::DrawTextureBinding::Stage::Vertex
                        ? impl_->frame->BindVertexTexture(
                              texture.slot, texture.texture, texture.sampler,
                              &error)
                        : impl_->frame->BindFragmentTexture(
                              texture.slot, texture.texture, texture.sampler,
                              &error);
                    if (!bound) {
                        if (errorOut) *errorOut = error;
                        return false;
                    }
                }
                if (!operation.draw.vertexUniforms.empty() &&
                    !impl_->frame->PushVertexUniform(
                        0, operation.draw.vertexUniforms.data(),
                        operation.draw.vertexUniforms.size(), &error)) {
                    if (errorOut) *errorOut = error;
                    return false;
                }
                if (!operation.draw.fragmentUniforms.empty() &&
                    !impl_->frame->PushFragmentUniform(
                        0, operation.draw.fragmentUniforms.data(),
                        operation.draw.fragmentUniforms.size(), &error)) {
                    if (errorOut) *errorOut = error;
                    return false;
                }
                const bool drew = operation.draw.indexCount > 0U
                    ? impl_->frame->DrawIndexed(operation.draw.indexCount, 0, 0,
                                                &error)
                    : impl_->frame->Draw(operation.draw.vertexCount, 0, &error);
                if (!drew) {
                    if (errorOut) *errorOut = error;
                    return false;
                }
                break;
            }
            case Impl::OperationType::Blit:
                if (!impl_->frame->Blit(
                        operation.blit.source, operation.blit.sourceRect,
                        operation.blit.destination,
                        operation.blit.destinationRect,
                        operation.blit.filter, &error)) {
                    if (errorOut) *errorOut = error;
                    return false;
                }
                if (operation.blit.destination.swapchain) {
                    impl_->swapchainWritten = true;
                }
                break;
        }
    }
    impl_->frame->EndRenderPass();
    impl_->renderEncoded = true;
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::BeginMainPassForOverlay(std::string* errorOut) {
    if (!EncodeRenderPasses(errorOut)) return false;
    if (impl_->overlayPassOpen) {
        if (errorOut) errorOut->clear();
        return true;
    }
    molga::RenderPassDescriptor pass;
    pass.color.swapchain = true;
    pass.color.loadAction = impl_->swapchainWritten
        ? molga::LoadAction::Load : molga::LoadAction::Clear;
    pass.color.storeAction = molga::StoreAction::Store;
    pass.color.clearColor = impl_->mainClear;
    std::string error;
    if (!impl_->frame->BeginRenderPass(pass, &error) ||
        !impl_->frame->SetViewport(
            {0, 0, impl_->frame->SwapchainWidth(),
             impl_->frame->SwapchainHeight()}, &error)) {
        if (errorOut) *errorOut = error;
        return false;
    }
    impl_->overlayPassOpen = true;
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::EndMainPassAndSubmit(std::string* errorOut) {
    if (!HasFrame()) {
        if (errorOut) *errorOut = "no acquired frame to submit";
        return false;
    }
    if (impl_->overlayPassOpen) {
        impl_->frame->EndRenderPass();
        impl_->overlayPassOpen = false;
    }
    std::string error;
    impl_->lastTelemetry = impl_->frame->Telemetry();
    // Step 7. 이 프레임이 붙든 page 토큰 전부를, 이 제출의 fence와 함께
    // 반납 큐로 넘긴다. page를 하나도 붙들지 않은 프레임은 fence를 만들지
    // 않는다 — 관찰할 대상이 없는데 프레임마다 fence를 만들 이유가 없다.
    std::vector<std::shared_ptr<const void>> retained = impl_->TakeRetainedPages();
    bool submitted = false;
    if (retained.empty()) {
        submitted = impl_->frame->Submit(&error);
    } else {
        std::unique_ptr<molga::IGpuCompletionFence> fence;
        submitted = impl_->frame->SubmitAndAcquireFence(fence, &error);
        if (fence) {
            impl_->retirement.Enqueue(std::move(fence), std::move(retained));
        } else {
            // fence를 얻지 못했어도 명령은 이미 GPU에 갔다. 언제 끝나는지
            // 모르는 채로 토큰을 놓는 대신, 증명된 idle drain까지 붙든다.
            // 이것은 프레임 실패가 아니다 — 그 프레임은 실제로 제출되었고,
            // 실패로 보고하면 화면에 나간 프레임 때문에 애플리케이션이
            // 종료된다. 진단은 GraphicsDevice가 오류 severity로 남긴다.
            impl_->HoldWithoutFence(std::move(retained));
        }
    }
    impl_->frame.reset();
    if (!submitted) {
        if (errorOut) *errorOut = error;
        return false;
    }
    if (errorOut) errorOut->clear();
    return true;
}

bool Renderer::SubmitFrame(std::string* errorOut) {
    if (!EncodeRenderPasses(errorOut)) return false;
    return EndMainPassAndSubmit(errorOut);
}

const molga::FrameTelemetry& Renderer::LastFrameTelemetry() const {
    return impl_->lastTelemetry;
}

namespace molga {
namespace detail {

void SetRendererShutdownStageHookForTest(
    RendererShutdownStageHook hook) noexcept {
    shutdownStageHook = hook;
}

} // namespace detail
} // namespace molga
