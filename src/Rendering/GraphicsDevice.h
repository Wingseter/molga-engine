#pragma once

#include "Platform/Window.h"
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

    std::uint32_t SwapchainWidth() const;
    std::uint32_t SwapchainHeight() const;
    const FrameTelemetry& Telemetry() const;

private:
    struct Impl;
    explicit FrameContext(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;

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

private:
    struct Impl;
    explicit GraphicsDevice(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;

    void* NativeDeviceForImGui() const;
    void* NativeTextureForImGui(TextureHandle handle) const;

    friend class FrameContext;
    friend class ::ImGuiLayer;
    friend class ::ImGuiTextureBridge;
};

std::unique_ptr<GraphicsDevice> CreateGraphicsDevice(
    void* nativeWindow, bool debugValidation, std::string& errorOut);

} // namespace molga
