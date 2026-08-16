#pragma once

#include <memory>
#include <string>

namespace molga {

enum class GraphicsBackend {
    OpenGL33,
    SdlGpu, // Capability path; the production Molga renderer still uses GL.
};

struct GraphicsDeviceInfo {
    GraphicsBackend backend = GraphicsBackend::OpenGL33;
    std::string driver;
    bool supportsSpirv = false;
    bool supportsMsl = false;
    bool supportsDxbc = false;
    bool supportsDxil = false;
    bool capabilityPipelineReady = false;
};

// Owns one native graphics context/device for an SDL window. Engine-facing
// code depends on this interface rather than SDL_GLContext or SDL_GPUDevice.
class GraphicsDevice {
public:
    virtual ~GraphicsDevice() = default;

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    virtual const GraphicsDeviceInfo& Info() const = 0;
    virtual bool MakeCurrent() = 0;
    virtual void Present() = 0;
    virtual void ResizeViewport(int width, int height) = 0;

    // Designated platform contract: acquire a real swapchain image and submit
    // a presentable frame. SDL_GPU also creates/binds a shader pipeline,
    // uploads a texture, and draws a sampled fullscreen quad.
    virtual bool RenderCapabilityFrame(float r, float g, float b, float a) = 0;

    virtual void* NativeContext() const = 0;
    virtual void* NativeDevice() const = 0;

protected:
    GraphicsDevice() = default;
};

std::unique_ptr<GraphicsDevice> CreateGraphicsDevice(
    GraphicsBackend backend, void* nativeWindow, bool debugValidation,
    std::string& errorOut);

} // namespace molga
