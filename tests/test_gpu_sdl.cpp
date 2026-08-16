#include "Core/Bootstrap.h"
#include "doctest.h"

TEST_CASE("SDL_GPU creates a native device and submits a swapchain pass") {
    WindowConfig config;
    config.title = "Molga SDL_GPU platform contract";
    config.width = 160;
    config.height = 90;
    config.visible = false;
    config.graphicsBackend = molga::GraphicsBackend::SdlGpu;
    config.graphicsValidation = true;

    auto host = EngineInit(config);
    REQUIRE(host);
    const molga::GraphicsDeviceInfo& info = host->GraphicsInfo();
    CHECK(info.backend == molga::GraphicsBackend::SdlGpu);
    CHECK_FALSE(info.driver.empty());
    CHECK(info.capabilityPipelineReady);
#if defined(__APPLE__)
    CHECK(info.driver == "metal");
    CHECK(info.supportsMsl);
#elif defined(_WIN32)
    CHECK(info.driver == "direct3d12");
    CHECK(info.supportsDxbc || info.supportsDxil);
#else
    CHECK(info.driver == "vulkan");
    CHECK(info.supportsSpirv);
#endif
    REQUIRE(host->RenderCapabilityFrame(0.04f, 0.08f, 0.16f, 1.0f));
}
