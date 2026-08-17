#include "Rendering/GraphicsDevice.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <utility>

namespace molga {
struct ResourceHandleAccess {
    template <typename Handle>
    static std::uint32_t Index(Handle handle) { return handle.index_; }
    template <typename Handle>
    static std::uint32_t Generation(Handle handle) { return handle.generation_; }
    template <typename Handle>
    static Handle Make(std::uint32_t index, std::uint32_t generation) {
        return Handle(index, generation);
    }
};

namespace {

GraphicsDevice* currentDevice = nullptr;

struct GpuLogMonitor {
    GpuLogMonitor();
    ~GpuLogMonitor();

    SDL_LogOutputFunction previous = nullptr;
    void* previousUserdata = nullptr;
    std::atomic<std::uint32_t> errorCount{0};
};

void SDLCALL MonitorGpuLog(void* userdata, int category,
                           SDL_LogPriority priority, const char* message) {
    auto* monitor = static_cast<GpuLogMonitor*>(userdata);
    if (!monitor) return;
    if (category == SDL_LOG_CATEGORY_GPU &&
        priority >= SDL_LOG_PRIORITY_ERROR) {
        monitor->errorCount.fetch_add(1U, std::memory_order_relaxed);
    }
    if (monitor->previous &&
        (monitor->previous != MonitorGpuLog ||
         monitor->previousUserdata != monitor)) {
        monitor->previous(monitor->previousUserdata, category, priority,
                          message);
    }
}

GpuLogMonitor::GpuLogMonitor() {
    SDL_GetLogOutputFunction(&previous, &previousUserdata);
    SDL_SetLogOutputFunction(MonitorGpuLog, this);
}

GpuLogMonitor::~GpuLogMonitor() {
    SDL_LogOutputFunction current = nullptr;
    void* currentUserdata = nullptr;
    SDL_GetLogOutputFunction(&current, &currentUserdata);
    if (current == MonitorGpuLog && currentUserdata == this) {
        SDL_SetLogOutputFunction(previous, previousUserdata);
    }
}

void SetError(std::string* output, const std::string& value) {
    if (output) *output = value;
}

std::string SdlError(const char* prefix) {
    return std::string(prefix) + ": " + SDL_GetError();
}

SDL_GPUTextureFormat ToSdl(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        case TextureFormat::SRGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
        case TextureFormat::BGRA8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        case TextureFormat::SBGRA8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
        case TextureFormat::RGBA16F: return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::Depth24Stencil8:
            return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::Depth32FloatStencil8:
            return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

TextureFormat FromSdl(SDL_GPUTextureFormat format) {
    switch (format) {
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return TextureFormat::RGBA8;
        case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB: return TextureFormat::SRGBA8;
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return TextureFormat::BGRA8;
        case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB: return TextureFormat::SBGRA8;
        case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT: return TextureFormat::RGBA16F;
        case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
            return TextureFormat::Depth24Stencil8;
        case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
            return TextureFormat::Depth32FloatStencil8;
        default: return TextureFormat::RGBA8;
    }
}

std::uint32_t BytesPerPixel(TextureFormat format) {
    return format == TextureFormat::RGBA16F ? 8U : 4U;
}

SDL_GPUTextureUsageFlags ToSdl(GpuTextureUsage usage) {
    SDL_GPUTextureUsageFlags result = 0;
    if (HasUsage(usage, GpuTextureUsage::Sampler)) {
        result |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    }
    if (HasUsage(usage, GpuTextureUsage::ColorTarget)) {
        result |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    }
    if (HasUsage(usage, GpuTextureUsage::DepthStencilTarget)) {
        result |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    }
    return result;
}

SDL_GPUFilter ToSdl(TextureFilter filter) {
    return filter == TextureFilter::Nearest ? SDL_GPU_FILTER_NEAREST
                                            : SDL_GPU_FILTER_LINEAR;
}

SDL_GPUSamplerAddressMode ToSdl(TextureAddressMode mode) {
    if (mode == TextureAddressMode::Repeat) {
        return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    }
    if (mode == TextureAddressMode::MirroredRepeat) {
        return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    }
    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}

SDL_GPULoadOp ToSdl(LoadAction action) {
    switch (action) {
        case LoadAction::Load: return SDL_GPU_LOADOP_LOAD;
        case LoadAction::Clear: return SDL_GPU_LOADOP_CLEAR;
        case LoadAction::DontCare: return SDL_GPU_LOADOP_DONT_CARE;
    }
    return SDL_GPU_LOADOP_DONT_CARE;
}

SDL_GPUStoreOp ToSdl(StoreAction action) {
    return action == StoreAction::Store ? SDL_GPU_STOREOP_STORE
                                        : SDL_GPU_STOREOP_DONT_CARE;
}

SDL_GPUSampleCount ToSdlSample(std::uint8_t sampleCount) {
    switch (sampleCount) {
        case 2: return SDL_GPU_SAMPLECOUNT_2;
        case 4: return SDL_GPU_SAMPLECOUNT_4;
        case 8: return SDL_GPU_SAMPLECOUNT_8;
        default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

SDL_GPUVertexElementFormat ToVertexFormat(const std::string& format) {
    if (format == "Float") return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    if (format == "Float2") return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    if (format == "Float3") return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    if (format == "Float4") return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    if (format == "UInt") return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    if (format == "UByte4Norm") return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

std::vector<std::uint8_t> ReadBinary(const std::filesystem::path& path,
                                     std::string& errorOut) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        errorOut = "could not open shader artifact: " + path.string();
        return {};
    }
    const std::streamoff end = input.tellg();
    if (end <= 0) {
        errorOut = "shader artifact is empty: " + path.string();
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), end);
    if (!input) {
        errorOut = "could not read shader artifact: " + path.string();
        return {};
    }
    return bytes;
}

template <typename Native, typename Descriptor>
struct ResourceSlot {
    Native* native = nullptr;
    Descriptor descriptor{};
    std::uint32_t generation = 1;
};

struct PipelineResourceDescriptor {
    GraphicsPipelineDescriptor descriptor;
    PipelineKey key;
};

template <typename Handle, typename Slot>
bool IsHandleAlive(Handle handle, const std::vector<Slot>& slots) {
    const std::uint32_t generation = ResourceHandleAccess::Generation(handle);
    const std::uint32_t index = ResourceHandleAccess::Index(handle);
    return generation != 0U && index < slots.size() &&
           slots[index].native != nullptr &&
           slots[index].generation == generation;
}

template <typename Handle, typename Slot>
Handle StoreResource(std::vector<Slot>& slots,
                     std::vector<std::uint32_t>& freeSlots,
                     Slot resource) {
    if (!freeSlots.empty()) {
        const std::uint32_t index = freeSlots.back();
        freeSlots.pop_back();
        const std::uint32_t generation = slots[index].generation;
        resource.generation = generation;
        slots[index] = std::move(resource);
        return ResourceHandleAccess::Make<Handle>(index, generation);
    }
    const std::uint32_t index = static_cast<std::uint32_t>(slots.size());
    const std::uint32_t generation = resource.generation;
    slots.push_back(std::move(resource));
    return ResourceHandleAccess::Make<Handle>(index, generation);
}

template <typename Handle, typename Slot, typename Release>
void ReleaseResource(Handle& handle, std::vector<Slot>& slots,
                     std::vector<std::uint32_t>& freeSlots, Release release) {
    if (!IsHandleAlive(handle, slots)) {
        handle = {};
        return;
    }
    const std::uint32_t index = ResourceHandleAccess::Index(handle);
    Slot& slot = slots[index];
    release(slot.native);
    slot.native = nullptr;
    slot.descriptor = {};
    ++slot.generation;
    if (slot.generation == 0U) ++slot.generation;
    freeSlots.push_back(index);
    handle = {};
}

void HashByte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

template <typename Value>
void HashValue(std::uint64_t& hash, Value value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        HashByte(hash, bytes[index]);
    }
}

} // namespace

const char* TextureFormatName(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8: return "RGBA8";
        case TextureFormat::SRGBA8: return "SRGBA8";
        case TextureFormat::BGRA8: return "BGRA8";
        case TextureFormat::SBGRA8: return "SBGRA8";
        case TextureFormat::RGBA16F: return "RGBA16F";
        case TextureFormat::Depth24Stencil8: return "D24S8";
        case TextureFormat::Depth32FloatStencil8: return "D32FS8";
    }
    return "unknown";
}

struct GraphicsDevice::Impl {
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    GraphicsDeviceInfo info;
    std::unique_ptr<GpuLogMonitor> logMonitor;

    std::vector<ResourceSlot<SDL_GPUBuffer, BufferDescriptor>> buffers;
    std::vector<ResourceSlot<SDL_GPUTexture, TextureDescriptor>> textures;
    std::vector<ResourceSlot<SDL_GPUSampler, SamplerDescriptor>> samplers;
    std::vector<ResourceSlot<SDL_GPUGraphicsPipeline,
                             PipelineResourceDescriptor>> pipelines;
    std::vector<std::uint32_t> freeBuffers;
    std::vector<std::uint32_t> freeTextures;
    std::vector<std::uint32_t> freeSamplers;
    std::vector<std::uint32_t> freePipelines;
};

struct FrameContext::Impl {
    GraphicsDevice* owner = nullptr;
    SDL_GPUCommandBuffer* command = nullptr;
    SDL_GPUTexture* swapchain = nullptr;
    SDL_GPUCopyPass* copyPass = nullptr;
    SDL_GPURenderPass* renderPass = nullptr;
    std::vector<SDL_GPUTransferBuffer*> transfers;
    std::uint32_t swapchainWidth = 0;
    std::uint32_t swapchainHeight = 0;
    bool submitted = false;
    bool renderStarted = false;
    bool indexBound = false;
    FrameTelemetry telemetry;
};

PipelineKey MakePipelineKey(const GraphicsPipelineDescriptor& descriptor) {
    std::uint64_t hash = 1469598103934665603ULL;
    const std::uint64_t revision = descriptor.shader ? descriptor.shader->revision : 0U;
    HashValue(hash, revision);
    if (descriptor.shader) {
        HashValue(hash, descriptor.shader->vertexStride);
        for (const auto& attribute : descriptor.shader->vertexAttributes) {
            HashValue(hash, attribute.location);
            HashValue(hash, attribute.offset);
            for (unsigned char character : attribute.format) HashByte(hash, character);
            HashByte(hash, 0U);
        }
    }
    HashValue(hash, descriptor.blend);
    HashValue(hash, descriptor.cull);
    HashValue(hash, descriptor.depthTest);
    HashValue(hash, descriptor.depthWrite);
    HashValue(hash, descriptor.colorTargetFormat);
    HashValue(hash, descriptor.depthStencilFormat);
    HashValue(hash, descriptor.hasDepthStencilTarget);
    HashValue(hash, descriptor.sampleCount);
    return {hash};
}

FrameContext::FrameContext() = default;
FrameContext::FrameContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FrameContext::FrameContext(FrameContext&&) noexcept = default;
FrameContext& FrameContext::operator=(FrameContext&&) noexcept = default;

FrameContext::~FrameContext() {
    if (!impl_ || impl_->submitted || !impl_->command) return;
    EndRenderPass();
    if (impl_->copyPass) {
        SDL_EndGPUCopyPass(impl_->copyPass);
        impl_->copyPass = nullptr;
    }
    if (impl_->swapchain) {
        SDL_SubmitGPUCommandBuffer(impl_->command);
    } else {
        SDL_CancelGPUCommandBuffer(impl_->command);
    }
    for (SDL_GPUTransferBuffer* transfer : impl_->transfers) {
        SDL_ReleaseGPUTransferBuffer(impl_->owner->impl_->device, transfer);
    }
}

bool FrameContext::IsValid() const {
    return impl_ && impl_->command && !impl_->submitted;
}

bool FrameContext::UploadBuffer(BufferHandle destination, std::size_t offset,
                                const void* data, std::size_t size, bool cycle,
                                std::string* errorOut) {
    if (!IsValid() || impl_->renderStarted || !data || size == 0U ||
        (offset % 4U) != 0U || (size % 4U) != 0U) {
        SetError(errorOut, "buffer upload must be aligned, non-empty, and precede all render passes");
        return false;
    }
    auto& slots = impl_->owner->impl_->buffers;
    if (!IsHandleAlive(destination, slots)) {
        SetError(errorOut, "buffer upload uses a stale handle");
        return false;
    }
    const auto& slot = slots[destination.index_];
    if (offset > slot.descriptor.size || size > slot.descriptor.size - offset ||
        size > std::numeric_limits<std::uint32_t>::max()) {
        SetError(errorOut, "buffer upload exceeds the destination allocation");
        return false;
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<std::uint32_t>(size);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(
        impl_->owner->impl_->device, &transferInfo);
    if (!transfer) {
        SetError(errorOut, SdlError("could not allocate buffer upload"));
        return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(impl_->owner->impl_->device,
                                            transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(impl_->owner->impl_->device, transfer);
        SetError(errorOut, SdlError("could not map buffer upload"));
        return false;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(impl_->owner->impl_->device, transfer);
    if (!impl_->copyPass) {
        impl_->copyPass = SDL_BeginGPUCopyPass(impl_->command);
        if (!impl_->copyPass) {
            SDL_ReleaseGPUTransferBuffer(impl_->owner->impl_->device, transfer);
            SetError(errorOut, SdlError("could not begin buffer upload pass"));
            return false;
        }
        ++impl_->telemetry.copyPasses;
    }
    const SDL_GPUTransferBufferLocation source{transfer, 0};
    const SDL_GPUBufferRegion target{slot.native,
                                     static_cast<std::uint32_t>(offset),
                                     static_cast<std::uint32_t>(size)};
    SDL_UploadToGPUBuffer(impl_->copyPass, &source, &target, cycle);
    impl_->transfers.push_back(transfer);
    impl_->telemetry.uploadBytes += size;
    SetError(errorOut, {});
    return true;
}

bool FrameContext::UploadTexture(TextureView destination, PixelRectU32 region,
                                 const void* data, std::size_t size,
                                 std::uint32_t bytesPerRow, bool cycle,
                                 std::string* errorOut) {
    if (!IsValid() || impl_->renderStarted || !data || size == 0U ||
        region.width == 0U || region.height == 0U) {
        SetError(errorOut, "texture upload must be non-empty and precede all render passes");
        return false;
    }
    auto& slots = impl_->owner->impl_->textures;
    if (!IsHandleAlive(destination.texture, slots)) {
        SetError(errorOut, "texture upload uses a stale handle");
        return false;
    }
    const auto& slot = slots[destination.texture.index_];
    const std::uint32_t bpp = BytesPerPixel(slot.descriptor.format);
    const std::uint64_t minimumRow = static_cast<std::uint64_t>(region.width) * bpp;
    const std::uint64_t minimumSize = static_cast<std::uint64_t>(bytesPerRow) * region.height;
    if (region.x + region.width > slot.descriptor.width ||
        region.y + region.height > slot.descriptor.height ||
        destination.layer >= slot.descriptor.layers ||
        bytesPerRow < minimumRow || size < minimumSize ||
        size > std::numeric_limits<std::uint32_t>::max()) {
        SetError(errorOut, "texture upload region or row layout is invalid");
        return false;
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<std::uint32_t>(size);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(
        impl_->owner->impl_->device, &transferInfo);
    if (!transfer) {
        SetError(errorOut, SdlError("could not allocate texture upload"));
        return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(impl_->owner->impl_->device,
                                            transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(impl_->owner->impl_->device, transfer);
        SetError(errorOut, SdlError("could not map texture upload"));
        return false;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(impl_->owner->impl_->device, transfer);
    if (!impl_->copyPass) {
        impl_->copyPass = SDL_BeginGPUCopyPass(impl_->command);
        if (!impl_->copyPass) {
            SDL_ReleaseGPUTransferBuffer(impl_->owner->impl_->device, transfer);
            SetError(errorOut, SdlError("could not begin texture upload pass"));
            return false;
        }
        ++impl_->telemetry.copyPasses;
    }
    SDL_GPUTextureTransferInfo source{};
    source.transfer_buffer = transfer;
    source.pixels_per_row = bytesPerRow / bpp;
    source.rows_per_layer = region.height;
    SDL_GPUTextureRegion target{};
    target.texture = slot.native;
    target.mip_level = destination.mipLevel;
    target.layer = destination.layer;
    target.x = region.x;
    target.y = region.y;
    target.w = region.width;
    target.h = region.height;
    target.d = 1;
    SDL_UploadToGPUTexture(impl_->copyPass, &source, &target, cycle);
    impl_->transfers.push_back(transfer);
    impl_->telemetry.uploadBytes += size;
    SetError(errorOut, {});
    return true;
}

bool FrameContext::BeginRenderPass(const RenderPassDescriptor& descriptor,
                                   std::string* errorOut) {
    if (!IsValid() || impl_->renderPass) {
        SetError(errorOut, "render pass nesting is invalid");
        return false;
    }
    if (impl_->copyPass) {
        SDL_EndGPUCopyPass(impl_->copyPass);
        impl_->copyPass = nullptr;
    }
    SDL_GPUTexture* colorTexture = impl_->swapchain;
    if (!descriptor.color.swapchain) {
        auto& slots = impl_->owner->impl_->textures;
        if (!IsHandleAlive(descriptor.color.view.texture, slots)) {
            SetError(errorOut, "render pass color attachment uses a stale handle");
            return false;
        }
        const auto& slot = slots[descriptor.color.view.texture.index_];
        if (!HasUsage(slot.descriptor.usage, GpuTextureUsage::ColorTarget)) {
            SetError(errorOut, "render pass color attachment lacks ColorTarget usage");
            return false;
        }
        colorTexture = slot.native;
    }
    if (!colorTexture) {
        SetError(errorOut, "render pass has no color attachment");
        return false;
    }
    SDL_GPUColorTargetInfo color{};
    color.texture = colorTexture;
    color.mip_level = descriptor.color.view.mipLevel;
    color.layer_or_depth_plane = descriptor.color.view.layer;
    color.clear_color = {descriptor.color.clearColor.r,
                         descriptor.color.clearColor.g,
                         descriptor.color.clearColor.b,
                         descriptor.color.clearColor.a};
    color.load_op = ToSdl(descriptor.color.loadAction);
    color.store_op = ToSdl(descriptor.color.storeAction);
    color.cycle = descriptor.color.loadAction != LoadAction::Load;

    SDL_GPUDepthStencilTargetInfo depth{};
    const SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
    if (descriptor.hasDepthStencil) {
        auto& slots = impl_->owner->impl_->textures;
        if (!IsHandleAlive(descriptor.depthStencil.view.texture, slots)) {
            SetError(errorOut, "render pass depth attachment uses a stale handle");
            return false;
        }
        const auto& slot = slots[descriptor.depthStencil.view.texture.index_];
        if (!HasUsage(slot.descriptor.usage, GpuTextureUsage::DepthStencilTarget)) {
            SetError(errorOut, "render pass depth attachment lacks DepthStencilTarget usage");
            return false;
        }
        depth.texture = slot.native;
        depth.clear_depth = descriptor.depthStencil.clearDepth;
        depth.clear_stencil = descriptor.depthStencil.clearStencil;
        depth.load_op = ToSdl(descriptor.depthStencil.depthLoadAction);
        depth.store_op = ToSdl(descriptor.depthStencil.depthStoreAction);
        depth.stencil_load_op = ToSdl(descriptor.depthStencil.stencilLoadAction);
        depth.stencil_store_op = ToSdl(descriptor.depthStencil.stencilStoreAction);
        depth.cycle = descriptor.depthStencil.depthLoadAction != LoadAction::Load ||
                      descriptor.depthStencil.stencilLoadAction != LoadAction::Load;
        depth.mip_level = static_cast<std::uint8_t>(descriptor.depthStencil.view.mipLevel);
        depth.layer = static_cast<std::uint8_t>(descriptor.depthStencil.view.layer);
        depthPointer = &depth;
    }
    impl_->renderPass = SDL_BeginGPURenderPass(impl_->command, &color, 1,
                                               depthPointer);
    if (!impl_->renderPass) {
        SetError(errorOut, SdlError("could not begin render pass"));
        return false;
    }
    impl_->renderStarted = true;
    impl_->indexBound = false;
    ++impl_->telemetry.renderPasses;
    SetError(errorOut, {});
    return true;
}

void FrameContext::EndRenderPass() {
    if (!impl_ || !impl_->renderPass) return;
    SDL_EndGPURenderPass(impl_->renderPass);
    impl_->renderPass = nullptr;
    impl_->indexBound = false;
}

bool FrameContext::SetViewport(PixelRectU32 viewport, std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass || viewport.width == 0U || viewport.height == 0U) {
        SetError(errorOut, "viewport requires an active render pass and non-zero size");
        return false;
    }
    const SDL_GPUViewport native{static_cast<float>(viewport.x),
                                 static_cast<float>(viewport.y),
                                 static_cast<float>(viewport.width),
                                 static_cast<float>(viewport.height), 0.0f, 1.0f};
    SDL_SetGPUViewport(impl_->renderPass, &native);
    SetError(errorOut, {});
    return true;
}

bool FrameContext::SetScissor(PixelRectU32 scissor, std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass || scissor.width == 0U || scissor.height == 0U) {
        SetError(errorOut, "scissor requires an active render pass and non-zero size");
        return false;
    }
    const SDL_Rect native{static_cast<int>(scissor.x), static_cast<int>(scissor.y),
                          static_cast<int>(scissor.width),
                          static_cast<int>(scissor.height)};
    SDL_SetGPUScissor(impl_->renderPass, &native);
    SetError(errorOut, {});
    return true;
}

bool FrameContext::BindPipeline(PipelineHandle pipeline, std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass) {
        SetError(errorOut, "pipeline binding requires an active render pass");
        return false;
    }
    auto& slots = impl_->owner->impl_->pipelines;
    if (!IsHandleAlive(pipeline, slots)) {
        SetError(errorOut, "pipeline binding uses a stale handle");
        return false;
    }
    SDL_BindGPUGraphicsPipeline(impl_->renderPass, slots[pipeline.index_].native);
    SetError(errorOut, {});
    return true;
}

bool FrameContext::BindVertexBuffer(std::uint32_t slot, BufferHandle buffer,
                                    std::size_t offset, std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass) {
        SetError(errorOut, "vertex binding requires an active render pass");
        return false;
    }
    auto& slots = impl_->owner->impl_->buffers;
    if (!IsHandleAlive(buffer, slots) ||
        slots[buffer.index_].descriptor.usage != GpuBufferUsage::Vertex ||
        offset >= slots[buffer.index_].descriptor.size ||
        offset > std::numeric_limits<std::uint32_t>::max()) {
        SetError(errorOut, "vertex buffer binding is invalid");
        return false;
    }
    const SDL_GPUBufferBinding binding{slots[buffer.index_].native,
                                       static_cast<std::uint32_t>(offset)};
    SDL_BindGPUVertexBuffers(impl_->renderPass, slot, &binding, 1);
    SetError(errorOut, {});
    return true;
}

bool FrameContext::BindIndexBuffer(BufferHandle buffer, std::size_t offset,
                                   std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass) {
        SetError(errorOut, "index binding requires an active render pass");
        return false;
    }
    auto& slots = impl_->owner->impl_->buffers;
    if (!IsHandleAlive(buffer, slots) ||
        slots[buffer.index_].descriptor.usage != GpuBufferUsage::Index ||
        offset >= slots[buffer.index_].descriptor.size ||
        offset > std::numeric_limits<std::uint32_t>::max()) {
        SetError(errorOut, "index buffer binding is invalid");
        return false;
    }
    const SDL_GPUBufferBinding binding{slots[buffer.index_].native,
                                       static_cast<std::uint32_t>(offset)};
    SDL_BindGPUIndexBuffer(impl_->renderPass, &binding,
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);
    impl_->indexBound = true;
    SetError(errorOut, {});
    return true;
}

bool FrameContext::BindFragmentTexture(std::uint32_t slot, TextureView texture,
                                       SamplerHandle sampler,
                                       std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass) {
        SetError(errorOut, "texture binding requires an active render pass");
        return false;
    }
    auto& textures = impl_->owner->impl_->textures;
    auto& samplers = impl_->owner->impl_->samplers;
    if (!IsHandleAlive(texture.texture, textures) ||
        !IsHandleAlive(sampler, samplers) ||
        !HasUsage(textures[texture.texture.index_].descriptor.usage,
                  GpuTextureUsage::Sampler)) {
        SetError(errorOut, "texture/sampler binding is invalid or stale");
        return false;
    }
    const SDL_GPUTextureSamplerBinding binding{
        textures[texture.texture.index_].native, samplers[sampler.index_].native};
    SDL_BindGPUFragmentSamplers(impl_->renderPass, slot, &binding, 1);
    SetError(errorOut, {});
    return true;
}

bool FrameContext::BindVertexTexture(std::uint32_t slot, TextureView texture,
                                     SamplerHandle sampler,
                                     std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass) {
        SetError(errorOut, "texture binding requires an active render pass");
        return false;
    }
    auto& textures = impl_->owner->impl_->textures;
    auto& samplers = impl_->owner->impl_->samplers;
    if (!IsHandleAlive(texture.texture, textures) ||
        !IsHandleAlive(sampler, samplers) ||
        !HasUsage(textures[texture.texture.index_].descriptor.usage,
                  GpuTextureUsage::Sampler)) {
        SetError(errorOut, "texture/sampler binding is invalid or stale");
        return false;
    }
    const SDL_GPUTextureSamplerBinding binding{
        textures[texture.texture.index_].native, samplers[sampler.index_].native};
    SDL_BindGPUVertexSamplers(impl_->renderPass, slot, &binding, 1);
    SetError(errorOut, {});
    return true;
}

bool FrameContext::PushVertexUniform(std::uint32_t slot, const void* data,
                                     std::size_t size, std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass || !data || size == 0U ||
        (size % 16U) != 0U || size > std::numeric_limits<std::uint32_t>::max()) {
        SetError(errorOut, "uniform data must be 16-byte aligned and pushed in a render pass");
        return false;
    }
    SDL_PushGPUVertexUniformData(impl_->command, slot, data,
                                 static_cast<std::uint32_t>(size));
    SetError(errorOut, {});
    return true;
}

bool FrameContext::PushFragmentUniform(std::uint32_t slot, const void* data,
                                       std::size_t size, std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass || !data || size == 0U ||
        (size % 16U) != 0U || size > std::numeric_limits<std::uint32_t>::max()) {
        SetError(errorOut, "uniform data must be 16-byte aligned and pushed in a render pass");
        return false;
    }
    SDL_PushGPUFragmentUniformData(impl_->command, slot, data,
                                   static_cast<std::uint32_t>(size));
    SetError(errorOut, {});
    return true;
}

bool FrameContext::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex,
                        std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass || vertexCount == 0U) {
        SetError(errorOut, "draw requires an active pass and vertices");
        return false;
    }
    SDL_DrawGPUPrimitives(impl_->renderPass, vertexCount, 1, firstVertex, 0);
    ++impl_->telemetry.drawCalls;
    SetError(errorOut, {});
    return true;
}

bool FrameContext::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
                               std::int32_t vertexOffset,
                               std::string* errorOut) {
    if (!IsValid() || !impl_->renderPass || !impl_->indexBound || indexCount == 0U) {
        SetError(errorOut, "indexed draw requires an active pass and index buffer");
        return false;
    }
    SDL_DrawGPUIndexedPrimitives(impl_->renderPass, indexCount, 1, firstIndex,
                                 vertexOffset, 0);
    ++impl_->telemetry.drawCalls;
    SetError(errorOut, {});
    return true;
}

bool FrameContext::Blit(TextureView source, PixelRectU32 sourceRect,
                        const ColorAttachmentDescriptor& destination,
                        PixelRectU32 destinationRect, TextureFilter filter,
                        std::string* errorOut) {
    if (!IsValid() || sourceRect.width == 0U || sourceRect.height == 0U ||
        destinationRect.width == 0U || destinationRect.height == 0U) {
        SetError(errorOut, "blit regions must be non-empty");
        return false;
    }
    EndRenderPass();
    if (impl_->copyPass) {
        SDL_EndGPUCopyPass(impl_->copyPass);
        impl_->copyPass = nullptr;
    }
    auto& textures = impl_->owner->impl_->textures;
    if (!IsHandleAlive(source.texture, textures)) {
        SetError(errorOut, "blit source uses a stale handle");
        return false;
    }
    SDL_GPUTexture* destinationTexture = impl_->swapchain;
    if (!destination.swapchain) {
        if (!IsHandleAlive(destination.view.texture, textures)) {
            SetError(errorOut, "blit destination uses a stale handle");
            return false;
        }
        destinationTexture = textures[destination.view.texture.index_].native;
    }
    if (!destinationTexture) {
        SetError(errorOut, "blit destination is unavailable");
        return false;
    }
    SDL_GPUBlitInfo info{};
    info.source = {textures[source.texture.index_].native, source.mipLevel,
                   source.layer, sourceRect.x, sourceRect.y,
                   sourceRect.width, sourceRect.height};
    info.destination = {destinationTexture, destination.view.mipLevel,
                        destination.view.layer, destinationRect.x,
                        destinationRect.y, destinationRect.width,
                        destinationRect.height};
    info.load_op = ToSdl(destination.loadAction);
    info.clear_color = {destination.clearColor.r, destination.clearColor.g,
                        destination.clearColor.b, destination.clearColor.a};
    info.flip_mode = SDL_FLIP_NONE;
    info.filter = ToSdl(filter);
    info.cycle = destination.loadAction != LoadAction::Load;
    SDL_BlitGPUTexture(impl_->command, &info);
    impl_->renderStarted = true;
    SetError(errorOut, {});
    return true;
}

bool FrameContext::Submit(std::string* errorOut) {
    if (!IsValid()) {
        SetError(errorOut, "frame was already submitted or is invalid");
        return false;
    }
    EndRenderPass();
    if (impl_->copyPass) {
        SDL_EndGPUCopyPass(impl_->copyPass);
        impl_->copyPass = nullptr;
    }
    SDL_GPUCommandBuffer* command = impl_->command;
    impl_->command = nullptr;
    impl_->submitted = true;
    const bool success = SDL_SubmitGPUCommandBuffer(command);
    for (SDL_GPUTransferBuffer* transfer : impl_->transfers) {
        SDL_ReleaseGPUTransferBuffer(impl_->owner->impl_->device, transfer);
    }
    impl_->transfers.clear();
    if (!success) {
        SetError(errorOut, SdlError("could not submit SDL_GPU frame"));
        return false;
    }
    SetError(errorOut, {});
    return true;
}

std::uint32_t FrameContext::SwapchainWidth() const {
    return impl_ ? impl_->swapchainWidth : 0U;
}

std::uint32_t FrameContext::SwapchainHeight() const {
    return impl_ ? impl_->swapchainHeight : 0U;
}

const FrameTelemetry& FrameContext::Telemetry() const {
    static const FrameTelemetry empty{};
    return impl_ ? impl_->telemetry : empty;
}

void* FrameContext::NativeCommandBufferForImGui() const {
    return impl_ ? impl_->command : nullptr;
}

void* FrameContext::NativeRenderPassForImGui() const {
    return impl_ ? impl_->renderPass : nullptr;
}

GraphicsDevice::GraphicsDevice(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {
    currentDevice = this;
}

GraphicsDevice::~GraphicsDevice() {
    if (!impl_) return;
    if (impl_->device) SDL_WaitForGPUIdle(impl_->device);
    for (auto& slot : impl_->pipelines) {
        if (slot.native) SDL_ReleaseGPUGraphicsPipeline(impl_->device, slot.native);
    }
    for (auto& slot : impl_->samplers) {
        if (slot.native) SDL_ReleaseGPUSampler(impl_->device, slot.native);
    }
    for (auto& slot : impl_->textures) {
        if (slot.native) SDL_ReleaseGPUTexture(impl_->device, slot.native);
    }
    for (auto& slot : impl_->buffers) {
        if (slot.native) SDL_ReleaseGPUBuffer(impl_->device, slot.native);
    }
    if (impl_->device && impl_->window) {
        SDL_ReleaseWindowFromGPUDevice(impl_->device, impl_->window);
    }
    if (impl_->device) SDL_DestroyGPUDevice(impl_->device);
    if (currentDevice == this) currentDevice = nullptr;
}

std::unique_ptr<GraphicsDevice> GraphicsDevice::Create(
    void* nativeWindow, bool debugValidation, std::string& errorOut) {
    auto* window = static_cast<SDL_Window*>(nativeWindow);
    if (!window) {
        errorOut = "graphics device requires an SDL window";
        return nullptr;
    }
    constexpr SDL_GPUShaderFormat formats = static_cast<SDL_GPUShaderFormat>(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL |
        SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL);
#if defined(__APPLE__)
    const char* requiredDriver = "metal";
#else
    const char* requiredDriver = nullptr;
#endif
    auto logMonitor = std::make_unique<GpuLogMonitor>();
    SDL_GPUDevice* device = SDL_CreateGPUDevice(formats, debugValidation,
                                                 requiredDriver);
    if (!device) {
        errorOut = SdlError("could not create SDL_GPU device");
        return nullptr;
    }
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        errorOut = SdlError("could not claim SDL_GPU window");
        SDL_DestroyGPUDevice(device);
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->window = window;
    impl->device = device;
    impl->info.validationEnabled = debugValidation;
    impl->logMonitor = std::move(logMonitor);
    const char* driver = SDL_GetGPUDeviceDriver(device);
    impl->info.driver = driver ? driver : "unknown";
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);
    impl->info.supportsSpirv = (supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
    impl->info.supportsMsl = (supported & SDL_GPU_SHADERFORMAT_MSL) != 0;
    impl->info.supportsDxbc = (supported & SDL_GPU_SHADERFORMAT_DXBC) != 0;
    impl->info.supportsDxil = (supported & SDL_GPU_SHADERFORMAT_DXIL) != 0;
    impl->info.capabilityPipelineReady = true;
    impl->info.swapchainFormat = FromSdl(
        SDL_GetGPUSwapchainTextureFormat(device, window));
    constexpr SDL_GPUTextureUsageFlags depthUsage =
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (SDL_GPUTextureSupportsFormat(
            device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D, depthUsage)) {
        impl->info.depthStencilFormat = TextureFormat::Depth24Stencil8;
    } else if (SDL_GPUTextureSupportsFormat(
                   device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                   SDL_GPU_TEXTURETYPE_2D, depthUsage)) {
        impl->info.depthStencilFormat = TextureFormat::Depth32FloatStencil8;
    } else {
        errorOut = "SDL_GPU driver has no supported depth/stencil target format";
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        return nullptr;
    }
#if defined(__APPLE__)
    if (impl->info.driver != "metal" || !impl->info.supportsMsl) {
        errorOut = "macOS production renderer requires the SDL_GPU Metal driver and MSL";
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        return nullptr;
    }
#endif
    errorOut.clear();
    return std::unique_ptr<GraphicsDevice>(new GraphicsDevice(std::move(impl)));
}

GraphicsDevice* GraphicsDevice::Current() { return currentDevice; }

const GraphicsDeviceInfo& GraphicsDevice::Info() const { return impl_->info; }

BeginFrameResult GraphicsDevice::BeginFrame(WindowId windowId) {
    BeginFrameResult result;
    SDL_Window* window = SDL_GetWindowFromID(windowId);
    if (!window || window != impl_->window) {
        result.error = "BeginFrame received an unknown window";
        return result;
    }
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (!command) {
        result.error = SdlError("could not acquire SDL_GPU command buffer");
        return result;
    }
    SDL_GPUTexture* swapchain = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command, window, &swapchain,
                                               &width, &height)) {
        SDL_CancelGPUCommandBuffer(command);
        result.error = SdlError("could not acquire SDL_GPU swapchain texture");
        return result;
    }
    if (!swapchain) {
        SDL_SubmitGPUCommandBuffer(command);
        result.status = FrameAcquireStatus::Unavailable;
        result.error.clear();
        return result;
    }
    auto frame = std::make_unique<FrameContext::Impl>();
    frame->owner = this;
    frame->command = command;
    frame->swapchain = swapchain;
    frame->swapchainWidth = width;
    frame->swapchainHeight = height;
    result.status = FrameAcquireStatus::Acquired;
    result.frame = FrameContext(std::move(frame));
    result.error.clear();
    return result;
}

BufferHandle GraphicsDevice::CreateBuffer(const BufferDescriptor& descriptor,
                                          std::string& errorOut) {
    if (descriptor.size == 0U ||
        descriptor.size > std::numeric_limits<std::uint32_t>::max()) {
        errorOut = "buffer size is invalid";
        return {};
    }
    SDL_GPUBufferCreateInfo info{};
    info.usage = descriptor.usage == GpuBufferUsage::Vertex
                     ? SDL_GPU_BUFFERUSAGE_VERTEX
                     : SDL_GPU_BUFFERUSAGE_INDEX;
    info.size = static_cast<std::uint32_t>(descriptor.size);
    SDL_GPUBuffer* native = SDL_CreateGPUBuffer(impl_->device, &info);
    if (!native) {
        errorOut = SdlError("could not create GPU buffer");
        return {};
    }
    ResourceSlot<SDL_GPUBuffer, BufferDescriptor> slot;
    slot.native = native;
    slot.descriptor = descriptor;
    errorOut.clear();
    return StoreResource<BufferHandle>(impl_->buffers, impl_->freeBuffers,
                                       std::move(slot));
}

TextureHandle GraphicsDevice::CreateTexture(const TextureDescriptor& descriptor,
                                            std::string& errorOut) {
    if (descriptor.width == 0U || descriptor.height == 0U ||
        descriptor.layers == 0U || descriptor.usage == GpuTextureUsage::None) {
        errorOut = "texture descriptor is invalid";
        return {};
    }
    SDL_GPUTextureCreateInfo info{};
    info.type = descriptor.layers > 1 ? SDL_GPU_TEXTURETYPE_2D_ARRAY
                                      : SDL_GPU_TEXTURETYPE_2D;
    info.format = ToSdl(descriptor.format);
    info.usage = ToSdl(descriptor.usage);
    info.width = descriptor.width;
    info.height = descriptor.height;
    info.layer_count_or_depth = descriptor.layers;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    if (info.format == SDL_GPU_TEXTUREFORMAT_INVALID || info.usage == 0U ||
        !SDL_GPUTextureSupportsFormat(impl_->device, info.format, info.type,
                                      info.usage)) {
        errorOut = "texture format/usage is unsupported by the active SDL_GPU driver";
        return {};
    }
    SDL_GPUTexture* native = SDL_CreateGPUTexture(impl_->device, &info);
    if (!native) {
        errorOut = SdlError("could not create GPU texture");
        return {};
    }
    ResourceSlot<SDL_GPUTexture, TextureDescriptor> slot;
    slot.native = native;
    slot.descriptor = descriptor;
    errorOut.clear();
    return StoreResource<TextureHandle>(impl_->textures, impl_->freeTextures,
                                        std::move(slot));
}

SamplerHandle GraphicsDevice::CreateSampler(const SamplerDescriptor& descriptor,
                                            std::string& errorOut) {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = ToSdl(descriptor.minFilter);
    info.mag_filter = ToSdl(descriptor.magFilter);
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = ToSdl(descriptor.addressU);
    info.address_mode_v = ToSdl(descriptor.addressV);
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.max_lod = 1.0f;
    SDL_GPUSampler* native = SDL_CreateGPUSampler(impl_->device, &info);
    if (!native) {
        errorOut = SdlError("could not create GPU sampler");
        return {};
    }
    ResourceSlot<SDL_GPUSampler, SamplerDescriptor> slot;
    slot.native = native;
    slot.descriptor = descriptor;
    errorOut.clear();
    return StoreResource<SamplerHandle>(impl_->samplers, impl_->freeSamplers,
                                        std::move(slot));
}

PipelineHandle GraphicsDevice::CreatePipeline(
    const GraphicsPipelineDescriptor& descriptor, std::string& errorOut) {
    if (!descriptor.shader || descriptor.bundleRoot.empty()) {
        errorOut = "pipeline requires a shader bundle entry and root";
        return {};
    }
    const char* formatName = nullptr;
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    if (impl_->info.supportsMsl) {
        formatName = "msl";
        format = SDL_GPU_SHADERFORMAT_MSL;
    } else if (impl_->info.supportsDxil) {
        formatName = "dxil";
        format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (impl_->info.supportsSpirv) {
        formatName = "spirv";
        format = SDL_GPU_SHADERFORMAT_SPIRV;
    }
    if (!formatName) {
        errorOut = "active SDL_GPU driver accepts no bundled shader format";
        return {};
    }

    auto createShader = [&](const ShaderStageRecord& stage,
                            SDL_GPUShaderStage nativeStage) -> SDL_GPUShader* {
        const auto artifactIterator = stage.artifacts.find(formatName);
        if (artifactIterator == stage.artifacts.end()) {
            errorOut = "shader bundle lacks the active backend artifact";
            return nullptr;
        }
        const ShaderArtifactRecord& artifact = artifactIterator->second;
        std::vector<std::uint8_t> bytes = ReadBinary(
            descriptor.bundleRoot / artifact.path, errorOut);
        if (bytes.empty()) return nullptr;
        SDL_GPUShaderCreateInfo info{};
        info.code_size = bytes.size();
        info.code = bytes.data();
        info.entrypoint = artifact.entryPoint.c_str();
        info.format = format;
        info.stage = nativeStage;
        info.num_samplers = stage.resources.samplers;
        info.num_storage_textures = stage.resources.storageTextures;
        info.num_storage_buffers = stage.resources.storageBuffers;
        info.num_uniform_buffers = stage.resources.uniformBuffers;
        SDL_GPUShader* shader = SDL_CreateGPUShader(impl_->device, &info);
        if (!shader) errorOut = SdlError("could not create bundled GPU shader");
        return shader;
    };

    SDL_GPUShader* vertex = createShader(descriptor.shader->vertex,
                                         SDL_GPU_SHADERSTAGE_VERTEX);
    if (!vertex) return {};
    SDL_GPUShader* fragment = createShader(descriptor.shader->fragment,
                                           SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!fragment) {
        SDL_ReleaseGPUShader(impl_->device, vertex);
        return {};
    }

    std::vector<SDL_GPUVertexAttribute> attributes;
    attributes.reserve(descriptor.shader->vertexAttributes.size());
    for (const auto& attribute : descriptor.shader->vertexAttributes) {
        const SDL_GPUVertexElementFormat element = ToVertexFormat(attribute.format);
        if (element == SDL_GPU_VERTEXELEMENTFORMAT_INVALID) {
            SDL_ReleaseGPUShader(impl_->device, vertex);
            SDL_ReleaseGPUShader(impl_->device, fragment);
            errorOut = "unsupported vertex format in shader bundle: " +
                       attribute.format;
            return {};
        }
        attributes.push_back({attribute.location, 0, element, attribute.offset});
    }
    const SDL_GPUVertexBufferDescription vertexBuffer{
        0, descriptor.shader->vertexStride, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    SDL_GPUColorTargetDescription color{};
    color.format = ToSdl(descriptor.colorTargetFormat);
    auto& blend = color.blend_state;
    blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                             SDL_GPU_COLORCOMPONENT_G |
                             SDL_GPU_COLORCOMPONENT_B |
                             SDL_GPU_COLORCOMPONENT_A;
    blend.enable_color_write_mask = true;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    switch (descriptor.blend) {
        case BlendState::Opaque:
            blend.enable_blend = false;
            break;
        case BlendState::Alpha:
            blend.enable_blend = true;
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case BlendState::Additive:
            blend.enable_blend = true;
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            break;
        case BlendState::Multiply:
            blend.enable_blend = true;
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_DST_ALPHA;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            break;
        case BlendState::Screen:
            blend.enable_blend = true;
            blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
            blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            break;
    }

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.vertex_input_state.vertex_buffer_descriptions = &vertexBuffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes.data();
    info.vertex_input_state.num_vertex_attributes =
        static_cast<std::uint32_t>(attributes.size());
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = descriptor.cull == CullMode::Front
                                         ? SDL_GPU_CULLMODE_FRONT
                                         : descriptor.cull == CullMode::Back
                                               ? SDL_GPU_CULLMODE_BACK
                                               : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = ToSdlSample(descriptor.sampleCount);
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.depth_stencil_state.enable_depth_test = descriptor.depthTest;
    info.depth_stencil_state.enable_depth_write = descriptor.depthWrite;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = descriptor.hasDepthStencilTarget;
    info.target_info.depth_stencil_format = ToSdl(descriptor.depthStencilFormat);
    SDL_GPUGraphicsPipeline* native = SDL_CreateGPUGraphicsPipeline(
        impl_->device, &info);
    SDL_ReleaseGPUShader(impl_->device, vertex);
    SDL_ReleaseGPUShader(impl_->device, fragment);
    if (!native) {
        errorOut = SdlError("could not create GPU graphics pipeline");
        return {};
    }
    ResourceSlot<SDL_GPUGraphicsPipeline, PipelineResourceDescriptor> slot;
    slot.native = native;
    slot.descriptor.descriptor = descriptor;
    slot.descriptor.descriptor.shader = nullptr;
    slot.descriptor.key = MakePipelineKey(descriptor);
    errorOut.clear();
    return StoreResource<PipelineHandle>(impl_->pipelines, impl_->freePipelines,
                                         std::move(slot));
}

void GraphicsDevice::DestroyBuffer(BufferHandle& handle) {
    ReleaseResource(handle, impl_->buffers, impl_->freeBuffers,
                    [&](SDL_GPUBuffer* native) {
                        SDL_ReleaseGPUBuffer(impl_->device, native);
                    });
}

void GraphicsDevice::DestroyTexture(TextureHandle& handle) {
    ReleaseResource(handle, impl_->textures, impl_->freeTextures,
                    [&](SDL_GPUTexture* native) {
                        SDL_ReleaseGPUTexture(impl_->device, native);
                    });
}

void GraphicsDevice::DestroySampler(SamplerHandle& handle) {
    ReleaseResource(handle, impl_->samplers, impl_->freeSamplers,
                    [&](SDL_GPUSampler* native) {
                        SDL_ReleaseGPUSampler(impl_->device, native);
                    });
}

void GraphicsDevice::DestroyPipeline(PipelineHandle& handle) {
    ReleaseResource(handle, impl_->pipelines, impl_->freePipelines,
                    [&](SDL_GPUGraphicsPipeline* native) {
                        SDL_ReleaseGPUGraphicsPipeline(impl_->device, native);
                    });
}

bool GraphicsDevice::IsAlive(BufferHandle handle) const {
    return IsHandleAlive(handle, impl_->buffers);
}
bool GraphicsDevice::IsAlive(TextureHandle handle) const {
    return IsHandleAlive(handle, impl_->textures);
}
bool GraphicsDevice::IsAlive(SamplerHandle handle) const {
    return IsHandleAlive(handle, impl_->samplers);
}
bool GraphicsDevice::IsAlive(PipelineHandle handle) const {
    return IsHandleAlive(handle, impl_->pipelines);
}

bool GraphicsDevice::Describe(TextureHandle handle,
                              TextureDescriptor& output) const {
    if (!IsAlive(handle)) return false;
    output = impl_->textures[handle.index_].descriptor;
    return true;
}

bool GraphicsDevice::Describe(BufferHandle handle,
                              BufferDescriptor& output) const {
    if (!IsAlive(handle)) return false;
    output = impl_->buffers[handle.index_].descriptor;
    return true;
}

bool GraphicsDevice::UploadTextureImmediate(TextureView destination,
                                            PixelRectU32 region,
                                            const void* data, std::size_t size,
                                            std::uint32_t bytesPerRow,
                                            std::string& errorOut) {
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (!command) {
        errorOut = SdlError("could not acquire immediate upload command buffer");
        return false;
    }
    auto frame = std::make_unique<FrameContext::Impl>();
    frame->owner = this;
    frame->command = command;
    FrameContext context(std::move(frame));
    if (!context.UploadTexture(destination, region, data, size, bytesPerRow,
                               false, &errorOut)) {
        return false;
    }
    return context.Submit(&errorOut);
}

bool GraphicsDevice::ReadbackRGBA8(TextureView source, PixelRectU32 region,
                                   std::vector<std::uint8_t>& output,
                                   std::string& errorOut) {
    if (!IsAlive(source.texture)) {
        errorOut = "readback uses a stale texture handle";
        return false;
    }
    const TextureDescriptor& descriptor =
        impl_->textures[source.texture.index_].descriptor;
    if (descriptor.format == TextureFormat::RGBA16F ||
        descriptor.format == TextureFormat::Depth24Stencil8 ||
        descriptor.format == TextureFormat::Depth32FloatStencil8 ||
        region.width == 0U || region.height == 0U ||
        region.x + region.width > descriptor.width ||
        region.y + region.height > descriptor.height) {
        errorOut = "RGBA8 readback format or region is invalid";
        return false;
    }
    const std::uint64_t byteCount64 =
        static_cast<std::uint64_t>(region.width) * region.height * 4U;
    if (byteCount64 > std::numeric_limits<std::uint32_t>::max()) {
        errorOut = "RGBA8 readback is too large";
        return false;
    }
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transferInfo.size = static_cast<std::uint32_t>(byteCount64);
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(
        impl_->device, &transferInfo);
    if (!transfer) {
        errorOut = SdlError("could not create readback transfer buffer");
        return false;
    }
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(impl_->device);
    SDL_GPUCopyPass* copy = command ? SDL_BeginGPUCopyPass(command) : nullptr;
    if (!copy) {
        if (command) SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        errorOut = SdlError("could not begin readback copy pass");
        return false;
    }
    SDL_GPUTextureRegion nativeRegion{};
    nativeRegion.texture = impl_->textures[source.texture.index_].native;
    nativeRegion.mip_level = source.mipLevel;
    nativeRegion.layer = source.layer;
    nativeRegion.x = region.x;
    nativeRegion.y = region.y;
    nativeRegion.w = region.width;
    nativeRegion.h = region.height;
    nativeRegion.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = transfer;
    destination.pixels_per_row = region.width;
    destination.rows_per_layer = region.height;
    SDL_DownloadFromGPUTexture(copy, &nativeRegion, &destination);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    if (!fence) {
        SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        errorOut = SdlError("could not submit readback command buffer");
        return false;
    }
    SDL_GPUFence* fences[] = {fence};
    const bool waited = SDL_WaitForGPUFences(impl_->device, true, fences, 1);
    SDL_ReleaseGPUFence(impl_->device, fence);
    if (!waited) {
        SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        errorOut = SdlError("could not wait for readback fence");
        return false;
    }
    void* mapped = SDL_MapGPUTransferBuffer(impl_->device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        errorOut = SdlError("could not map readback transfer buffer");
        return false;
    }
    output.resize(static_cast<std::size_t>(byteCount64));
    std::memcpy(output.data(), mapped, output.size());
    SDL_UnmapGPUTransferBuffer(impl_->device, transfer);
    SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
    if (descriptor.format == TextureFormat::BGRA8 ||
        descriptor.format == TextureFormat::SBGRA8) {
        for (std::size_t offset = 0; offset < output.size(); offset += 4U) {
            std::swap(output[offset], output[offset + 2U]);
        }
    }
    errorOut.clear();
    return true;
}

bool GraphicsDevice::RenderCapabilityFrame(float r, float g, float b, float a,
                                           std::string* errorOut) {
    BeginFrameResult acquired = BeginFrame(SDL_GetWindowID(impl_->window));
    if (acquired.status != FrameAcquireStatus::Acquired) {
        SetError(errorOut, acquired.status == FrameAcquireStatus::Unavailable
                               ? "swapchain is unavailable"
                               : acquired.error);
        return false;
    }
    RenderPassDescriptor pass;
    pass.color.swapchain = true;
    pass.color.loadAction = LoadAction::Clear;
    pass.color.storeAction = StoreAction::Store;
    pass.color.clearColor = {r, g, b, a};
    std::string error;
    if (!acquired.frame.BeginRenderPass(pass, &error)) {
        SetError(errorOut, error);
        return false;
    }
    acquired.frame.EndRenderPass();
    if (!acquired.frame.Submit(&error)) {
        SetError(errorOut, error);
        return false;
    }
    SetError(errorOut, {});
    return true;
}

bool GraphicsDevice::WaitIdle(std::string* errorOut) {
    if (SDL_WaitForGPUIdle(impl_->device)) {
        SetError(errorOut, {});
        return true;
    }
    SetError(errorOut, SdlError("could not wait for SDL_GPU idle"));
    return false;
}

std::uint32_t GraphicsDevice::ValidationErrorCount() const {
    return impl_ && impl_->logMonitor
        ? impl_->logMonitor->errorCount.load(std::memory_order_relaxed)
        : 0U;
}

void* GraphicsDevice::NativeDeviceForImGui() const { return impl_->device; }

void* GraphicsDevice::NativeTextureForImGui(TextureHandle handle) const {
    return IsAlive(handle) ? impl_->textures[handle.index_].native : nullptr;
}

std::unique_ptr<GraphicsDevice> CreateGraphicsDevice(
    void* nativeWindow, bool debugValidation, std::string& errorOut) {
    return GraphicsDevice::Create(nativeWindow, debugValidation, errorOut);
}

} // namespace molga
