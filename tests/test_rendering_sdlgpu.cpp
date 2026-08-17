#include "Core/Bootstrap.h"
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
    const fs::path root = fs::temp_directory_path() /
        "molga_sdlgpu_authored_normal";
    std::error_code filesystemError;
    fs::remove_all(root, filesystemError);
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

    const fs::path root = fs::temp_directory_path() /
        "molga_sdlgpu_tilemap_particle";
    std::error_code filesystemError;
    fs::remove_all(root, filesystemError);
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

    const fs::path root = fs::temp_directory_path() /
        "molga_sdlgpu_korean_font";
    std::error_code filesystemError;
    fs::remove_all(root, filesystemError);
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
