#include "Rendering/GraphicsDevice.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <glad/glad.h>

#include "../../external/SDL/test/testgpu/overlay.frag.dxil.h"
#include "../../external/SDL/test/testgpu/overlay.frag.msl.h"
#include "../../external/SDL/test/testgpu/overlay.frag.spv.h"
#include "../../external/SDL/test/testgpu/overlay.vert.dxil.h"
#include "../../external/SDL/test/testgpu/overlay.vert.msl.h"
#include "../../external/SDL/test/testgpu/overlay.vert.spv.h"

#include <array>
#include <cstring>
#include <memory>
#include <utility>

namespace molga {
namespace {

class OpenGLGraphicsDevice final : public GraphicsDevice {
public:
    static std::unique_ptr<OpenGLGraphicsDevice> Create(
        SDL_Window* window, std::string& errorOut) {
        SDL_GLContext context = SDL_GL_CreateContext(window);
        if (!context || !SDL_GL_MakeCurrent(window, context)) {
            errorOut = std::string("could not create OpenGL 3.3 context: ") +
                       SDL_GetError();
            if (context) SDL_GL_DestroyContext(context);
            return nullptr;
        }
        if (!gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            errorOut = "could not load OpenGL functions with GLAD";
            SDL_GL_DestroyContext(context);
            return nullptr;
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_FRAMEBUFFER_SRGB);
        return std::unique_ptr<OpenGLGraphicsDevice>(
            new OpenGLGraphicsDevice(window, context));
    }

    ~OpenGLGraphicsDevice() override {
        if (context_) SDL_GL_DestroyContext(context_);
    }

    const GraphicsDeviceInfo& Info() const override { return info_; }
    bool MakeCurrent() override {
        return window_ && context_ && SDL_GL_MakeCurrent(window_, context_);
    }
    void Present() override {
        if (window_) SDL_GL_SwapWindow(window_);
    }
    void ResizeViewport(int width, int height) override {
        glViewport(0, 0, width, height);
    }
    bool RenderCapabilityFrame(float r, float g, float b, float a) override {
        if (!MakeCurrent()) return false;
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
        Present();
        return true;
    }
    void* NativeContext() const override { return context_; }
    void* NativeDevice() const override { return nullptr; }

private:
    OpenGLGraphicsDevice(SDL_Window* window, SDL_GLContext context)
        : window_(window), context_(context) {
        info_.backend = GraphicsBackend::OpenGL33;
        info_.driver = "opengl33";
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    GraphicsDeviceInfo info_;
};

class SdlGpuGraphicsDevice final : public GraphicsDevice {
public:
    static std::unique_ptr<SdlGpuGraphicsDevice> Create(
        SDL_Window* window, bool debugValidation, std::string& errorOut) {
        // Ask SDL for every portable source/binary format we can package.
        // The selected native driver exposes the subset it accepts: SPIR-V on
        // Vulkan, MSL/metallib on Metal, and DXBC/DXIL on D3D12.
        constexpr SDL_GPUShaderFormat formats = static_cast<SDL_GPUShaderFormat>(
            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL |
            SDL_GPU_SHADERFORMAT_METALLIB | SDL_GPU_SHADERFORMAT_DXBC |
            SDL_GPU_SHADERFORMAT_DXIL);
        SDL_GPUDevice* device =
            SDL_CreateGPUDevice(formats, debugValidation, nullptr);
        if (!device) {
            errorOut = std::string("could not create SDL_GPU device: ") +
                       SDL_GetError();
            return nullptr;
        }
        if (!SDL_ClaimWindowForGPUDevice(device, window)) {
            errorOut = std::string("could not claim SDL_GPU window: ") +
                       SDL_GetError();
            SDL_DestroyGPUDevice(device);
            return nullptr;
        }
        auto result = std::unique_ptr<SdlGpuGraphicsDevice>(
            new SdlGpuGraphicsDevice(window, device));
        if (!result->CreateCapabilityPipeline(errorOut)) return nullptr;
        return result;
    }

    ~SdlGpuGraphicsDevice() override {
        if (!device_) return;
        if (pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        if (sampler_) SDL_ReleaseGPUSampler(device_, sampler_);
        if (sampleTexture_) SDL_ReleaseGPUTexture(device_, sampleTexture_);
        if (window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
        SDL_DestroyGPUDevice(device_);
    }

    const GraphicsDeviceInfo& Info() const override { return info_; }
    bool MakeCurrent() override { return true; }
    void Present() override {}
    void ResizeViewport(int, int) override {}

    bool RenderCapabilityFrame(float r, float g, float b, float a) override {
        SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device_);
        if (!command) return false;

        SDL_GPUTexture* swapchain = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(
                command, window_, &swapchain, nullptr, nullptr)) {
            SDL_CancelGPUCommandBuffer(command);
            return false;
        }
        if (!swapchain) {
            // A minimized or otherwise non-presentable window may return no
            // image without making the acquire call fail. That is not enough
            // evidence for the designated GPU contract.
            SDL_SubmitGPUCommandBuffer(command);
            return false;
        }
        SDL_GPUColorTargetInfo target{};
        target.texture = swapchain;
        target.clear_color = {r, g, b, a};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass =
            SDL_BeginGPURenderPass(command, &target, 1, nullptr);
        if (!pass) {
            // A command buffer with an acquired swapchain cannot be
            // cancelled. Submitting it safely releases the image.
            SDL_SubmitGPUCommandBuffer(command);
            return false;
        }
        SDL_BindGPUGraphicsPipeline(pass, pipeline_);
        const SDL_GPUTextureSamplerBinding textureBinding{
            sampleTexture_, sampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        return SDL_SubmitGPUCommandBuffer(command);
    }

    void* NativeContext() const override { return nullptr; }
    void* NativeDevice() const override { return device_; }

private:
    SDL_GPUShader* CreateCapabilityShader(SDL_GPUShaderStage stage,
                                          std::string& errorOut) {
        const bool vertex = stage == SDL_GPU_SHADERSTAGE_VERTEX;
        const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device_);
        SDL_GPUShaderCreateInfo shader{};
        shader.stage = stage;
        shader.num_samplers = vertex ? 0u : 1u;
        if ((supported & SDL_GPU_SHADERFORMAT_DXIL) != 0) {
            shader.format = SDL_GPU_SHADERFORMAT_DXIL;
            shader.code = vertex ? overlay_vert_dxil : overlay_frag_dxil;
            shader.code_size = vertex ? overlay_vert_dxil_len
                                      : overlay_frag_dxil_len;
        } else if ((supported & SDL_GPU_SHADERFORMAT_MSL) != 0) {
            shader.format = SDL_GPU_SHADERFORMAT_MSL;
            shader.code = vertex ? overlay_vert_msl : overlay_frag_msl;
            shader.code_size = vertex ? overlay_vert_msl_len
                                      : overlay_frag_msl_len;
        } else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
            shader.format = SDL_GPU_SHADERFORMAT_SPIRV;
            shader.code = vertex ? overlay_vert_spv : overlay_frag_spv;
            shader.code_size = vertex ? overlay_vert_spv_len
                                      : overlay_frag_spv_len;
        } else {
            errorOut = "SDL_GPU device accepts no packaged capability shader format";
            return nullptr;
        }
        SDL_GPUShader* created = SDL_CreateGPUShader(device_, &shader);
        if (!created) {
            errorOut = std::string("could not create SDL_GPU capability shader: ") +
                       SDL_GetError();
        }
        return created;
    }

    bool CreateCapabilityPipeline(std::string& errorOut) {
        SDL_GPUShader* vertex = CreateCapabilityShader(
            SDL_GPU_SHADERSTAGE_VERTEX, errorOut);
        if (!vertex) return false;
        SDL_GPUShader* fragment = CreateCapabilityShader(
            SDL_GPU_SHADERSTAGE_FRAGMENT, errorOut);
        if (!fragment) {
            SDL_ReleaseGPUShader(device_, vertex);
            return false;
        }

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = vertex;
        pipelineInfo.fragment_shader = fragment;
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipelineInfo.rasterizer_state.enable_depth_clip = true;
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        SDL_ReleaseGPUShader(device_, vertex);
        SDL_ReleaseGPUShader(device_, fragment);
        if (!pipeline_) {
            errorOut = std::string("could not create SDL_GPU capability pipeline: ") +
                       SDL_GetError();
            return false;
        }

        SDL_GPUSamplerCreateInfo samplerInfo{};
        samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
        samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
        if (!sampler_) {
            errorOut = std::string("could not create SDL_GPU capability sampler: ") +
                       SDL_GetError();
            return false;
        }

        SDL_GPUTextureCreateInfo textureInfo{};
        textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
        textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        textureInfo.width = 2;
        textureInfo.height = 2;
        textureInfo.layer_count_or_depth = 1;
        textureInfo.num_levels = 1;
        textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
        sampleTexture_ = SDL_CreateGPUTexture(device_, &textureInfo);
        if (!sampleTexture_) {
            errorOut = std::string("could not create SDL_GPU capability texture: ") +
                       SDL_GetError();
            return false;
        }

        constexpr std::array<Uint8, 16> pixels{
            0xff, 0x40, 0x80, 0xff, 0x40, 0xc0, 0xff, 0xff,
            0x40, 0xff, 0x80, 0xff, 0xff, 0xd0, 0x40, 0xff};
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = static_cast<Uint32>(pixels.size());
        SDL_GPUTransferBuffer* transfer =
            SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (!transfer) {
            errorOut = std::string("could not create SDL_GPU transfer buffer: ") +
                       SDL_GetError();
            return false;
        }
        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
        if (!mapped) {
            errorOut = std::string("could not map SDL_GPU transfer buffer: ") +
                       SDL_GetError();
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            return false;
        }
        std::memcpy(mapped, pixels.data(), pixels.size());
        SDL_UnmapGPUTransferBuffer(device_, transfer);

        SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device_);
        SDL_GPUCopyPass* copy = command ? SDL_BeginGPUCopyPass(command) : nullptr;
        if (!copy) {
            errorOut = std::string("could not begin SDL_GPU upload pass: ") +
                       SDL_GetError();
            if (command) SDL_CancelGPUCommandBuffer(command);
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
            return false;
        }
        const SDL_GPUTextureTransferInfo source{transfer, 0, 0, 0};
        SDL_GPUTextureRegion destination{};
        destination.texture = sampleTexture_;
        destination.w = 2;
        destination.h = 2;
        destination.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
        SDL_EndGPUCopyPass(copy);
        const bool submitted = SDL_SubmitGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        if (!submitted) {
            errorOut = std::string("could not submit SDL_GPU texture upload: ") +
                       SDL_GetError();
            return false;
        }
        info_.capabilityPipelineReady = true;
        return true;
    }

    SdlGpuGraphicsDevice(SDL_Window* window, SDL_GPUDevice* device)
        : window_(window), device_(device) {
        info_.backend = GraphicsBackend::SdlGpu;
        const char* driver = SDL_GetGPUDeviceDriver(device_);
        info_.driver = driver ? driver : "unknown";
        const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device_);
        info_.supportsSpirv =
            (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
        info_.supportsMsl = (formats & (SDL_GPU_SHADERFORMAT_MSL |
                                       SDL_GPU_SHADERFORMAT_METALLIB)) != 0;
        info_.supportsDxbc =
            (formats & SDL_GPU_SHADERFORMAT_DXBC) != 0;
        info_.supportsDxil =
            (formats & SDL_GPU_SHADERFORMAT_DXIL) != 0;
    }

    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    SDL_GPUTexture* sampleTexture_ = nullptr;
    GraphicsDeviceInfo info_;
};

} // namespace

std::unique_ptr<GraphicsDevice> CreateGraphicsDevice(
    GraphicsBackend backend, void* nativeWindow, bool debugValidation,
    std::string& errorOut) {
    auto* window = static_cast<SDL_Window*>(nativeWindow);
    if (!window) {
        errorOut = "graphics device requires an SDL window";
        return nullptr;
    }
    if (backend == GraphicsBackend::SdlGpu) {
        return SdlGpuGraphicsDevice::Create(window, debugValidation, errorOut);
    }
    return OpenGLGraphicsDevice::Create(window, errorOut);
}

} // namespace molga
