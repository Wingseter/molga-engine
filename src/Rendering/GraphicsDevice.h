#pragma once

#include "Platform/Window.h"
#include "Rendering/GpuRetirementQueue.h"
#include "Rendering/ShaderBundle.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class ImGuiLayer;
class ImGuiTextureBridge;

namespace molga {

struct ResourceHandleAccess;

enum class GraphicsBackend {
    SdlGpu,
};

enum class TextureFormat : std::uint8_t {
    RGBA8,
    SRGBA8,
    BGRA8,
    SBGRA8,
    RGBA16F,
    Depth24Stencil8,
    Depth32FloatStencil8,
};

const char* TextureFormatName(TextureFormat format);

enum class GpuTextureUsage : std::uint8_t {
    None = 0,
    Sampler = 1 << 0,
    ColorTarget = 1 << 1,
    DepthStencilTarget = 1 << 2,
};

constexpr GpuTextureUsage operator|(GpuTextureUsage left, GpuTextureUsage right) {
    return static_cast<GpuTextureUsage>(static_cast<unsigned>(left) |
                                        static_cast<unsigned>(right));
}

constexpr bool HasUsage(GpuTextureUsage value, GpuTextureUsage flag) {
    return (static_cast<unsigned>(value) & static_cast<unsigned>(flag)) != 0U;
}

enum class GpuBufferUsage : std::uint8_t {
    Vertex,
    Index,
};

enum class TextureFilter : std::uint8_t {
    Nearest,
    Linear,
};

enum class TextureAddressMode : std::uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

enum class BlendState : std::uint8_t {
    Opaque,
    Alpha,
    Additive,
    Multiply,
    Screen,
};

enum class CullMode : std::uint8_t {
    None,
    Front,
    Back,
};

enum class LoadAction : std::uint8_t {
    Load,
    Clear,
    DontCare,
};

enum class StoreAction : std::uint8_t {
    Store,
    DontCare,
};

struct Color4f {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct PixelRectU32 {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    // C++17 타깃이라 rewritten comparison도 defaulted operator<=>도 없다.
    // 필드별로 쓴다 — 오브젝트 표현 비교(memcmp)는 패딩 바이트를 함께 보므로
    // 같은 사각형이 다르게 비교될 수 있다.
    constexpr bool operator==(const PixelRectU32& other) const noexcept {
        return x == other.x && y == other.y && width == other.width &&
               height == other.height;
    }
    constexpr bool operator!=(const PixelRectU32& other) const noexcept {
        return !(*this == other);
    }
};

template <typename Tag>
class ResourceHandle {
public:
    constexpr ResourceHandle() = default;

    constexpr explicit operator bool() const noexcept {
        return generation_ != 0U;
    }
    constexpr bool operator==(const ResourceHandle& other) const noexcept {
        return index_ == other.index_ && generation_ == other.generation_;
    }
    constexpr bool operator!=(const ResourceHandle& other) const noexcept {
        return !(*this == other);
    }
    constexpr bool operator<(const ResourceHandle& other) const noexcept {
        return index_ < other.index_ ||
               (index_ == other.index_ && generation_ < other.generation_);
    }

private:
    constexpr ResourceHandle(std::uint32_t index, std::uint32_t generation)
        : index_(index), generation_(generation) {}

    std::uint32_t index_ = 0;
    std::uint32_t generation_ = 0;

    friend class GraphicsDevice;
    friend class FrameContext;
    friend struct ResourceHandleAccess;
};

using BufferHandle = ResourceHandle<struct BufferHandleTag>;
using TextureHandle = ResourceHandle<struct TextureHandleTag>;
using SamplerHandle = ResourceHandle<struct SamplerHandleTag>;
using PipelineHandle = ResourceHandle<struct PipelineHandleTag>;

struct BufferDescriptor {
    std::size_t size = 0;
    GpuBufferUsage usage = GpuBufferUsage::Vertex;
    std::string debugName;
};

struct TextureDescriptor {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t layers = 1;
    TextureFormat format = TextureFormat::RGBA8;
    GpuTextureUsage usage = GpuTextureUsage::Sampler;
    std::string debugName;
};

struct SamplerDescriptor {
    TextureFilter minFilter = TextureFilter::Linear;
    TextureFilter magFilter = TextureFilter::Linear;
    TextureAddressMode addressU = TextureAddressMode::ClampToEdge;
    TextureAddressMode addressV = TextureAddressMode::ClampToEdge;
    std::string debugName;
};

struct GraphicsPipelineDescriptor {
    const ShaderBundleEntry* shader = nullptr;
    std::filesystem::path bundleRoot;
    BlendState blend = BlendState::Alpha;
    CullMode cull = CullMode::None;
    bool depthTest = false;
    bool depthWrite = false;
    TextureFormat colorTargetFormat = TextureFormat::SRGBA8;
    TextureFormat depthStencilFormat = TextureFormat::Depth24Stencil8;
    bool hasDepthStencilTarget = false;
    std::uint8_t sampleCount = 1;
};

struct PipelineKey {
    std::uint64_t value = 0;

    bool operator==(const PipelineKey& other) const { return value == other.value; }
    bool operator!=(const PipelineKey& other) const { return !(*this == other); }
    bool operator<(const PipelineKey& other) const { return value < other.value; }
};

PipelineKey MakePipelineKey(const GraphicsPipelineDescriptor& descriptor);

struct TextureView {
    TextureHandle texture;
    std::uint32_t mipLevel = 0;
    std::uint32_t layer = 0;
};

struct ColorAttachmentDescriptor {
    TextureView view;
    bool swapchain = false;
    LoadAction loadAction = LoadAction::Clear;
    StoreAction storeAction = StoreAction::Store;
    Color4f clearColor{};
};

struct DepthStencilAttachmentDescriptor {
    TextureView view;
    LoadAction depthLoadAction = LoadAction::Clear;
    StoreAction depthStoreAction = StoreAction::DontCare;
    LoadAction stencilLoadAction = LoadAction::Clear;
    StoreAction stencilStoreAction = StoreAction::DontCare;
    float clearDepth = 1.0f;
    std::uint8_t clearStencil = 0;
};

struct RenderPassDescriptor {
    ColorAttachmentDescriptor color;
    bool hasDepthStencil = false;
    DepthStencilAttachmentDescriptor depthStencil;
};

struct GraphicsDeviceInfo {
    GraphicsBackend backend = GraphicsBackend::SdlGpu;
    std::string api = "sdlgpu";
    std::string driver;
    bool validationEnabled = false;
    bool supportsSpirv = false;
    bool supportsMsl = false;
    bool supportsDxbc = false;
    bool supportsDxil = false;
    bool capabilityPipelineReady = false;
    TextureFormat swapchainFormat = TextureFormat::BGRA8;
    TextureFormat depthStencilFormat = TextureFormat::Depth24Stencil8;
};

struct FrameTelemetry {
    std::uint32_t copyPasses = 0;
    std::uint32_t renderPasses = 0;
    std::uint32_t drawCalls = 0;
    std::uint64_t uploadBytes = 0;
};

class GraphicsDevice;

// ── What SDL_QueryGPUFence actually reports ─────────────────────────────────
// 문서상 계약은 "신호했으면 true"인데, 벤더된 SDL 3.4.14의 Metal 백엔드는
// 그 반대를 돌려준다:
//
//   static bool METAL_QueryFence(...) { return METAL_INTERNAL_IsFenceBusy(f); }
//   // busy == (status == Committed || status == Scheduled)
//
// 이 극성을 상수로 박으면 두 방향 모두 조용히 치명적이다. 그대로 믿으면 이
// 백엔드에서 제출 직후에 "끝났다"가 되어 GPU가 읽고 있는 page를 반납하고,
// 반대로 뒤집어 박으면 SDL이 고쳐지는 날 같은 일이 난다. 그래서 극성은
// 장치마다 한 번 측정한다(GraphicsDevice::Create의 CalibrateFenceQuery).
enum class FenceQueryPolarity : std::uint8_t {
    // 측정하지 못했다. fence로는 아무것도 반납하지 않고 idle drain에 맡긴다.
    Unusable,
    ReportsSignaled,
    ReportsBusy,
};

namespace detail {

// ── Task 6.2: 진짜 제출 fence의 "아직 아니다"를 결정적으로 관찰한다 ─────────
// 진짜 fence를 비행 중에 붙잡는 단언은 쓸 수 없다 — GPU가 먼저 끝내 버리면
// 그 단언은 실패하는 것이 아니라 사라진다. 그래서 fail-closed 쪽 세 갈래
// (장치 없음/fence 없음/극성 Unusable)를, 완료가 증명된 진짜 fence 위에서
// 결정적으로 관찰할 수 있게 열어 둔다. 이것이 없으면 IsSignaled()가 통째로
// `return true`인 구현 — 제출 즉시 page를 놓아 주는, 이 마일스톤이 막으려는
// 바로 그 방향 — 이 모든 스위트를 통과한다.
//
// completed와 completedWithUnusablePolarity는 서로 다른 SDL fence를 하나씩
// 소유한다. 같은 fence를 둘이 감싸면 소멸자가 SDL_ReleaseGPUFence를 두 번
// 부른다.
struct CompletionFenceProbe {
    bool valid = false;
    // 장치 생성 때 측정해 둔 값.
    FenceQueryPolarity polarity = FenceQueryPolarity::Unusable;
    // 완료가 증명된 fence에 대한 SDL_QueryGPUFence 원값. 측정한 극성이 이
    // 장치의 실제 답과 맞는지는 이 둘을 나란히 놓아야만 검사할 수 있다.
    bool rawQueryOnCompletedFence = false;
    std::unique_ptr<IGpuCompletionFence> completed;
    std::unique_ptr<IGpuCompletionFence> completedWithUnusablePolarity;
    std::unique_ptr<IGpuCompletionFence> withoutFence;
    std::unique_ptr<IGpuCompletionFence> withoutDevice;
};

CompletionFenceProbe ProbeCompletionFenceForTest(GraphicsDevice& device);

} // namespace detail

class FrameContext {
public:
    FrameContext();
    ~FrameContext();
    FrameContext(FrameContext&& other) noexcept;
    FrameContext& operator=(FrameContext&& other) noexcept;

    FrameContext(const FrameContext&) = delete;
    FrameContext& operator=(const FrameContext&) = delete;

    bool IsValid() const;
    bool UploadBuffer(BufferHandle destination, std::size_t offset,
                      const void* data, std::size_t size, bool cycle = true,
                      std::string* errorOut = nullptr);
    bool UploadTexture(TextureView destination, PixelRectU32 region,
                       const void* data, std::size_t size,
                       std::uint32_t bytesPerRow, bool cycle = true,
                       std::string* errorOut = nullptr);
    bool BeginRenderPass(const RenderPassDescriptor& descriptor,
                         std::string* errorOut = nullptr);
    void EndRenderPass();
    bool SetViewport(PixelRectU32 viewport, std::string* errorOut = nullptr);
    bool SetScissor(PixelRectU32 scissor, std::string* errorOut = nullptr);
    bool BindPipeline(PipelineHandle pipeline,
                      std::string* errorOut = nullptr);
    bool BindVertexBuffer(std::uint32_t slot, BufferHandle buffer,
                          std::size_t offset = 0,
                          std::string* errorOut = nullptr);
    bool BindIndexBuffer(BufferHandle buffer, std::size_t offset = 0,
                         std::string* errorOut = nullptr);
    bool BindFragmentTexture(std::uint32_t slot, TextureView texture,
                             SamplerHandle sampler,
                             std::string* errorOut = nullptr);
    bool BindVertexTexture(std::uint32_t slot, TextureView texture,
                           SamplerHandle sampler,
                           std::string* errorOut = nullptr);
    bool PushVertexUniform(std::uint32_t slot, const void* data,
                           std::size_t size, std::string* errorOut = nullptr);
    bool PushFragmentUniform(std::uint32_t slot, const void* data,
                             std::size_t size, std::string* errorOut = nullptr);
    bool Draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0,
              std::string* errorOut = nullptr);
    bool DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0,
                     std::int32_t vertexOffset = 0,
                     std::string* errorOut = nullptr);
    bool Blit(TextureView source, PixelRectU32 sourceRect,
              const ColorAttachmentDescriptor& destination,
              PixelRectU32 destinationRect, TextureFilter filter,
              std::string* errorOut = nullptr);
    bool Submit(std::string* errorOut = nullptr);
    // Submit과 같지만 이 제출의 완료를 관찰할 수 있는 fence를 함께 돌려준다.
    // 제출된 프레임이 가리키는 자원(atlas page)을 언제 놓아도 되는지는 이
    // fence 하나로만 결정되므로, 그런 자원을 붙든 프레임은 반드시 이쪽으로
    // 제출한다.
    //
    // 돌려주는 bool은 "제출되었는가" 하나만 뜻한다(Submit과 같은 뜻이다).
    // fence를 얻지 못한 것은 제출 실패가 아니다: SDL은 백엔드로 넘기기 전에
    // 명령 버퍼를 소비 완료로 표시하므로, 그 프레임은 실제로 제출되고
    // 제시된다. 그래서 fenceOut이 비어 있는데 true인 경우가 있고, 그것은
    // "제출되지 않았다"가 아니라 "언제 끝나는지 알 수 없다"는 뜻이다 —
    // 호출자는 GpuRetirementQueue::RetainWithoutFence로 물러서야 한다.
    //
    // 이 둘을 하나의 false로 뭉치면, 실제로 화면에 나간 프레임 때문에
    // 애플리케이션이 종료된다(runtime_main의 제출 실패 경로).
    bool SubmitAndAcquireFence(std::unique_ptr<IGpuCompletionFence>& fenceOut,
                               std::string* errorOut = nullptr);

    std::uint32_t SwapchainWidth() const;
    std::uint32_t SwapchainHeight() const;
    const FrameTelemetry& Telemetry() const;

private:
    struct Impl;
    explicit FrameContext(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;

    // 위 두 제출의 공통 몸통. fenceOut이 null이면 fence를 요청하지 않는다 —
    // 프레임마다 fence를 만드는 것은 공짜가 아니고, page를 붙들지 않은
    // 프레임에는 관찰할 대상이 없다.
    bool SubmitInternal(std::unique_ptr<IGpuCompletionFence>* fenceOut,
                        std::string* errorOut);

    void* NativeCommandBufferForImGui() const;
    void* NativeRenderPassForImGui() const;

    friend class GraphicsDevice;
    friend class ::ImGuiLayer;
};

enum class FrameAcquireStatus {
    Acquired,
    Unavailable,
    Fatal,
};

struct BeginFrameResult {
    FrameAcquireStatus status = FrameAcquireStatus::Fatal;
    FrameContext frame;
    std::string error;
};

class GraphicsDevice {
public:
    ~GraphicsDevice();
    GraphicsDevice(GraphicsDevice&&) = delete;
    GraphicsDevice& operator=(GraphicsDevice&&) = delete;
    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    static std::unique_ptr<GraphicsDevice> Create(
        void* nativeWindow, bool debugValidation, std::string& errorOut);
    static GraphicsDevice* Current();

    const GraphicsDeviceInfo& Info() const;

    // ── Step 5a: 이 장치의 프로세스 전역 비순환 세대 ────────────────────────
    // UIRuntimeInvalidationClock::Advance(Device)가 낸 유일한 값이며 0이 아니다.
    // 장치를 다시 만들면 반드시 다른 값이 나오므로, 옛 장치에 묶인 스냅샷과
    // 바인딩이 새 장치의 것으로 오인될 수 없다. 취득이 소진되면 장치는 아예
    // 게시되지 않는다(Create가 실패한다).
    std::uint64_t Generation() const noexcept;

    BeginFrameResult BeginFrame(WindowId windowId);

    BufferHandle CreateBuffer(const BufferDescriptor& descriptor,
                              std::string& errorOut);
    TextureHandle CreateTexture(const TextureDescriptor& descriptor,
                                std::string& errorOut);
    SamplerHandle CreateSampler(const SamplerDescriptor& descriptor,
                                std::string& errorOut);
    PipelineHandle CreatePipeline(const GraphicsPipelineDescriptor& descriptor,
                                  std::string& errorOut);

    void DestroyBuffer(BufferHandle& handle);
    void DestroyTexture(TextureHandle& handle);
    void DestroySampler(SamplerHandle& handle);
    void DestroyPipeline(PipelineHandle& handle);

    bool IsAlive(BufferHandle handle) const;
    bool IsAlive(TextureHandle handle) const;
    bool IsAlive(SamplerHandle handle) const;
    bool IsAlive(PipelineHandle handle) const;
    bool Describe(TextureHandle handle, TextureDescriptor& output) const;
    bool Describe(BufferHandle handle, BufferDescriptor& output) const;

    bool UploadTextureImmediate(TextureView destination, PixelRectU32 region,
                                const void* data, std::size_t size,
                                std::uint32_t bytesPerRow,
                                std::string& errorOut);
    bool ReadbackRGBA8(TextureView source, PixelRectU32 region,
                       std::vector<std::uint8_t>& output,
                       std::string& errorOut);
    bool RenderCapabilityFrame(float r, float g, float b, float a,
                               std::string* errorOut = nullptr);
    bool WaitIdle(std::string* errorOut = nullptr);
    std::uint32_t ValidationErrorCount() const;

    // ── Task 11.2 Step 7g: 명시적이고 멱등한 장치 파괴 ──────────────────────
    // 소멸자가 하던 일을 이름 있는 단계로 옮긴다. 종료 순서는
    // EngineShutdown 한 곳에만 적혀 있고, 그 마지막 줄이 이것이다.
    //
    // 파괴 직전에 teardown을 **실제로 물어본다**: 이 세대에 아직 만료되지
    // 않은 텍스처 바인딩 토큰이 남아 있으면 순서가 뒤집혔다는 뜻이다.
    // 소멸자는 실패할 수 없으므로 막지는 못하지만, 아무 말 없이 지나가지도
    // 않는다 — 그 침묵이 인계받은 결함 2의 내용이었다.
    void Destroy();
    bool IsDestroyed() const noexcept { return impl_ == nullptr; }

private:
    struct Impl;
    explicit GraphicsDevice(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    // 파괴 뒤에도 답할 수 있어야 하는 값. 종료 순서가 이 세대를 여러 단계에
    // 걸쳐 쓰므로 impl_ 안에만 두면 마지막 단계에서 읽을 수 없다.
    std::uint64_t generation_ = 0;

    void* NativeDeviceForImGui() const;
    void* NativeTextureForImGui(TextureHandle handle) const;

    friend class FrameContext;
    friend class ::ImGuiLayer;
    friend class ::ImGuiTextureBridge;
    friend detail::CompletionFenceProbe detail::ProbeCompletionFenceForTest(
        GraphicsDevice&);
};

std::unique_ptr<GraphicsDevice> CreateGraphicsDevice(
    void* nativeWindow, bool debugValidation, std::string& errorOut);

namespace detail {

// ── Task 6.2: shutdown/제출 실패 주입 ────────────────────────────────────────
// 두 실패는 요구했을 때 일어나 주지 않는다. idle wait가 실패하는 장치도,
// fence 획득이 실패하는 제출도 헤드리스 시험에서는 만들 수 없지만, 둘 다
// 실패했을 때의 경로가 이 마일스톤의 계약 그 자체다(각각 종료 전 abort와
// "증거 없이 놓지 않기"). FontAtlas.h의 detail 계수기와 같은 이유로 출하되는
// 빌드에 남는다: 관찰 대상이 프로덕션 경로이므로, 테스트에만 컴파일되는
// 주입은 다른 프로그램을 재게 된다.
//
// 프로세스 전역이고 되돌릴 수 있다. idle wait 주입은 프로세스를 죽이는
// 경로를 여는 것이므로 전용 child 프로세스에서만 켠다.
void SetGpuIdleWaitFailureInjectionForTest(bool enabled) noexcept;
void SetGpuFenceAcquisitionFailureInjectionForTest(bool enabled) noexcept;

// ── Task 11.1 Step 1e: 정확한 핸들 값을 만드는 유일한 테스트 입구 ───────────
// 핸들의 두 필드(resource index, handle generation)는 private이고, 그것을 짓는
// 유일한 길은 살아 있는 장치다. 그런데 "핸들 비교는 index와 generation을 모두
// 본다"는 계약은 정확히 그 두 필드만 다른 두 값을 만들어야 시험할 수 있고,
// 헤드리스 프로세스에는 장치가 없다. 그래서 여기 하나만 둔다 — 테스트가
// molga::ResourceHandleAccess를 자기 쪽에서 다시 정의하면 ODR 위반이고, 그
// 위반은 링커가 잡아 주지 않는다.
TextureHandle MakeTextureHandleForTest(std::uint32_t index,
                                       std::uint32_t generation) noexcept;
SamplerHandle MakeSamplerHandleForTest(std::uint32_t index,
                                       std::uint32_t generation) noexcept;

} // namespace detail

} // namespace molga
