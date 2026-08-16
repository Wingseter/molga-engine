#pragma once

#include "Platform/Window.h"
#include "Rendering/GraphicsDevice.h"

#include <functional>
#include <memory>
#include <string>

class ImGuiLayer;

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

// Caller must release all renderer resources before resetting the host. The
// host waits for GPU idle, releases resources/device, destroys the window, and
// finally shuts SDL down.
void EngineShutdown(std::unique_ptr<EngineHost>& host);
