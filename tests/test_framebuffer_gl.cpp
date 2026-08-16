#include "Core/AssetDatabase.h"
#include "Core/Bootstrap.h"
#include "Core/ProjectSettings.h"
#include "Core/TextureManager.h"
#include "Rendering/Camera2D.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/LightingFrame2D.h"
#include "Rendering/LightingPipeline2D.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Rendering/PostProcessPipeline.h"
#include "Rendering/PostProcessProfile2D.h"
#include "Rendering/PostProcessProfileResolver.h"
#include "Rendering/ShaderManager.h"
#include "ECS/Component.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/PointLight2D.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/ShadowOccluder2D.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/GameObject.h"
#include "doctest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glad/glad.h>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

class SharedGlContext {
public:
    static SharedGlContext& Get() {
        static SharedGlContext context;
        return context;
    }

    bool Ready() const { return ready_; }
    void MakeCurrent() const {
        if (host_) host_->MakeContextCurrent();
    }

private:
    SharedGlContext() {
        WindowConfig config;
        config.title = "framebuffer-test";
        config.width = 64;
        config.height = 64;
        config.visible = false;
        host_ = EngineInit(config);
        ready_ = host_ && host_->MakeContextCurrent();
    }

    std::unique_ptr<EngineHost> host_;
    bool ready_ = false;
};

struct ProjectSettingsScope {
    ProjectSettingsScope() : before(ProjectSettings::Get().Serialize()) {}
    ~ProjectSettingsScope() { ProjectSettings::Get().Deserialize(before); }

    nlohmann::json before;
};

class ThrowingRenderComponent final : public Component {
public:
    COMPONENT_TYPE(ThrowingRenderComponent)
    void CollectRender(molga::RenderQueue&) override {
        throw std::runtime_error("intentional render collection failure");
    }
};

std::array<unsigned char, 4> ReadTopLeftPixel(GLuint framebuffer,
                                              int framebufferHeight,
                                              int x, int topY) {
    std::array<unsigned char, 4> pixel{};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(x, framebufferHeight - 1 - topY, 1, 1,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    return pixel;
}

bool IsColor(const std::array<unsigned char, 4>& pixel,
             unsigned char red, unsigned char green, unsigned char blue) {
    return pixel[0] == red && pixel[1] == green && pixel[2] == blue &&
           pixel[3] == 255;
}

bool NearByte(unsigned char actual, int expected, int tolerance = 3) {
    return std::abs(static_cast<int>(actual) - expected) <= tolerance;
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

void ClearHdr(Framebuffer& framebuffer,
              const std::array<float, 4>& color) {
    ScopedFramebufferBinding binding(framebuffer);
    glDisable(GL_SCISSOR_TEST);
    glClearBufferfv(GL_COLOR, 0, color.data());
}

void WriteHdrPixel(Framebuffer& framebuffer, int x, int topY,
                   const std::array<float, 4>& color) {
    ScopedFramebufferBinding binding(framebuffer);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, framebuffer.Height() - 1 - topY, 1, 1);
    glClearBufferfv(GL_COLOR, 0, color.data());
}

void WriteJsonFile(const std::filesystem::path& path,
                   const nlohmann::json& document) {
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        << document.dump(2) << '\n';
}

void WriteSinglePixelPpm(const std::filesystem::path& path,
                         const std::array<unsigned char, 3>& rgb) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "P6\n1 1\n255\n";
    file.write(reinterpret_cast<const char*>(rgb.data()),
               static_cast<std::streamsize>(rgb.size()));
}

int MaxRedAround(GLuint framebuffer, int framebufferHeight,
                 int centerX, int centerTopY, int radius) {
    int maximum = 0;
    for (int y = centerTopY - radius; y <= centerTopY + radius; ++y) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            if (x == centerX && y == centerTopY) continue;
            maximum = std::max(maximum, static_cast<int>(
                ReadTopLeftPixel(framebuffer, framebufferHeight, x, y)[0]));
        }
    }
    return maximum;
}

struct TestOutputCamera {
    std::shared_ptr<GameObject> object;
    Camera* camera = nullptr;
};

TestOutputCamera AddOutputCamera(
    std::vector<std::shared_ptr<GameObject>>& objects, const char* name,
    CameraOutputRole role, const CameraViewport& viewport, int depth,
    const Color& background) {
    auto object = std::make_shared<GameObject>(name);
    object->AddComponent<Transform>(0.0f, 0.0f);
    Camera* camera = object->AddComponent<Camera>();
    camera->SetOutputRole(role);
    if (!camera->SetViewport(viewport)) {
        throw std::runtime_error("invalid test camera viewport");
    }
    camera->SetDepth(depth);
    camera->SetPixelPerfect(true);
    camera->SetPixelZoom(1);
    camera->SetBackgroundColor(background);
    objects.push_back(object);
    return {std::move(object), camera};
}

std::shared_ptr<GameObject> AddLayerPixel(
    std::vector<std::shared_ptr<GameObject>>& objects, const char* name,
    int layer, const Color& color) {
    auto object = std::make_shared<GameObject>(name);
    object->SetLayer(layer);
    object->AddComponent<Transform>(0.0f, 0.0f);
    SpriteRenderer* sprite = object->AddComponent<SpriteRenderer>();
    sprite->SetSize(1.0f, 1.0f);
    sprite->SetColor(color);
    objects.push_back(object);
    return object;
}

const molga::CameraOutputResult* FindCameraResult(
    const molga::GameOutputResult& output, unsigned int objectId) {
    const auto found = std::find_if(
        output.cameraResults.begin(), output.cameraResults.end(),
        [objectId](const molga::CameraOutputResult& camera) {
            return camera.cameraObjectId == objectId;
        });
    return found == output.cameraResults.end() ? nullptr : &*found;
}

void CheckGlState(GLuint expectedDraw, GLuint expectedRead,
                  const std::array<GLint, 4>& expectedViewport,
                  const std::array<GLint, 4>& expectedScissor,
                  GLboolean expectedScissorEnabled,
                  GLboolean expectedSrgbEnabled) {
    GLint draw = 0;
    GLint read = 0;
    std::array<GLint, 4> viewport{};
    std::array<GLint, 4> scissor{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    glGetIntegerv(GL_SCISSOR_BOX, scissor.data());
    CHECK(draw == static_cast<GLint>(expectedDraw));
    CHECK(read == static_cast<GLint>(expectedRead));
    CHECK(viewport == expectedViewport);
    CHECK(scissor == expectedScissor);
    CHECK(glIsEnabled(GL_SCISSOR_TEST) == expectedScissorEnabled);
    CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB) == expectedSrgbEnabled);
}

} // namespace

TEST_CASE("Framebuffer bind restores framebuffer viewport scissor and sRGB state") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();

    {
        GLuint previous = 0;
        glGenFramebuffers(1, &previous);
        glBindFramebuffer(GL_FRAMEBUFFER, previous);
        glViewport(3, 4, 31, 29);
        glEnable(GL_SCISSOR_TEST);
        glScissor(5, 6, 23, 19);
        glDisable(GL_FRAMEBUFFER_SRGB);

        Framebuffer framebuffer;
        REQUIRE(framebuffer.Init(32, 24));

        GLint bound = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        CHECK(bound == static_cast<GLint>(previous));

        framebuffer.Bind();
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        CHECK(viewport[0] == 0);
        CHECK(viewport[1] == 0);
        CHECK(viewport[2] == 32);
        CHECK(viewport[3] == 24);
        CHECK(glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE);
        CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE);

        framebuffer.Unbind();
        GLint restoredViewport[4]{};
        GLint restoredScissor[4]{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        glGetIntegerv(GL_VIEWPORT, restoredViewport);
        glGetIntegerv(GL_SCISSOR_BOX, restoredScissor);
        CHECK(bound == static_cast<GLint>(previous));
        CHECK(restoredViewport[0] == 3);
        CHECK(restoredViewport[1] == 4);
        CHECK(restoredViewport[2] == 31);
        CHECK(restoredViewport[3] == 29);
        CHECK(restoredScissor[0] == 5);
        CHECK(restoredScissor[1] == 6);
        CHECK(restoredScissor[2] == 23);
        CHECK(restoredScissor[3] == 19);
        CHECK(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
        CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_FALSE);

        GLint maximum = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
        const GLuint textureBefore = framebuffer.ColorTexture();
        CHECK_FALSE(framebuffer.Resize(maximum + 1, 1));
        CHECK(framebuffer.Width() == 32);
        CHECK(framebuffer.Height() == 24);
        CHECK(framebuffer.ColorTexture() == textureBefore);

        // Scene View and Game View bind their own FBOs back-to-back. Each
        // scoped pass must return to the editor's exact draw/read state, even
        // when rendering unwinds through an engine-component exception.
        Framebuffer sceneFramebuffer;
        Framebuffer gameFramebuffer;
        REQUIRE(sceneFramebuffer.Init(20, 18));
        REQUIRE(gameFramebuffer.Init(40, 30));
        for (Framebuffer* target : {&sceneFramebuffer, &gameFramebuffer}) {
            try {
                ScopedFramebufferBinding scoped(*target);
                glViewport(0, 0, target->Width(), target->Height());
                throw 17;
            } catch (int value) {
                CHECK(value == 17);
            }
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
            glGetIntegerv(GL_VIEWPORT, restoredViewport);
            glGetIntegerv(GL_SCISSOR_BOX, restoredScissor);
            CHECK(bound == static_cast<GLint>(previous));
            CHECK(restoredViewport[0] == 3);
            CHECK(restoredViewport[1] == 4);
            CHECK(restoredViewport[2] == 31);
            CHECK(restoredViewport[3] == 29);
            CHECK(restoredScissor[0] == 5);
            CHECK(restoredScissor[1] == 6);
            CHECK(restoredScissor[2] == 23);
            CHECK(restoredScissor[3] == 19);
            CHECK(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
            CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_FALSE);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &previous);
    }

}

TEST_CASE("Framebuffer supports HDR nearest color without depth-stencil") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();

    const FramebufferSpecification specification{
        FramebufferColorFormat::RGBA16F, false,
        FramebufferTextureFilter::Nearest};
    Framebuffer framebuffer(specification);
    REQUIRE(framebuffer.Init(7, 5));
    CHECK(framebuffer.Specification() == specification);

    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glBindTexture(GL_TEXTURE_2D, framebuffer.ColorTexture());
    GLint internalFormat = 0;
    GLint minFilter = 0;
    GLint magFilter = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                             &internalFormat);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magFilter);
    CHECK(internalFormat == GL_RGBA16F);
    CHECK(minFilter == GL_NEAREST);
    CHECK(magFilter == GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));

    GLint previousDraw = 0;
    GLint previousRead = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.Id());
    GLint depthAttachmentType = GL_RENDERBUFFER;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthAttachmentType);
    CHECK(depthAttachmentType == GL_NONE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDraw));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousRead));

    glEnable(GL_FRAMEBUFFER_SRGB);
    framebuffer.Bind();
    CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_FALSE);
    framebuffer.Unbind();
    CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB) == GL_TRUE);
}

TEST_CASE("IntegerFit presents nearest pixels, black bars and crop while restoring GL state") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();

    {
        const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
        Shader* defaultShader = ShaderManager::Get().Load(
            "default", (shaderRoot / "default.vert").string(),
            (shaderRoot / "default.frag").string());
        REQUIRE(defaultShader != nullptr);
        REQUIRE(ShaderManager::Get().Load(
                    "batch", (shaderRoot / "batch.vert").string(),
                    (shaderRoot / "batch.frag").string()) != nullptr);

        Renderer renderer;
        renderer.Init();
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

        Framebuffer target;
        REQUIRE(target.Init(6, 6));
        target.Bind();
        GLuint independentRead = 0;
        glGenFramebuffers(1, &independentRead);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, independentRead);
        const std::array<GLint, 4> viewport{2, 3, 4, 2};
        const std::array<GLint, 4> scissor{1, 2, 3, 4};
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glEnable(GL_CULL_FACE);
        glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
                            GL_ZERO, GL_ONE);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_MAX);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        glStencilMaskSeparate(GL_FRONT, 0x12u);
        glStencilMaskSeparate(GL_BACK, 0x34u);
        glClearColor(0.2f, 0.3f, 0.4f, 0.5f);
        glClearDepth(0.25);
        glClearStencil(7);

        const auto result = output.Render(
            objects,
            {{6, 6}, {2, 2}, molga::GameOutputScaleMode::IntegerFit},
            renderer, defaultShader);
        REQUIRE(result.presented);
        REQUIRE(result.rendered);
        CHECK(result.presentation.scale == 3);
        CHECK((output.LogicalFramebufferSize() == molga::PixelSize{2, 2}));
        CheckGlState(target.Id(), independentRead, viewport, scissor,
                     GL_TRUE, GL_FALSE);
        CHECK(glIsEnabled(GL_BLEND) == GL_TRUE);
        CHECK(glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
        CHECK(glIsEnabled(GL_STENCIL_TEST) == GL_TRUE);
        CHECK(glIsEnabled(GL_CULL_FACE) == GL_TRUE);
        GLint integerState = 0;
        glGetIntegerv(GL_BLEND_SRC_RGB, &integerState);
        CHECK(integerState == GL_DST_ALPHA);
        glGetIntegerv(GL_BLEND_DST_RGB, &integerState);
        CHECK(integerState == GL_ONE_MINUS_DST_ALPHA);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &integerState);
        CHECK(integerState == GL_ZERO);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &integerState);
        CHECK(integerState == GL_ONE);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &integerState);
        CHECK(integerState == GL_FUNC_REVERSE_SUBTRACT);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &integerState);
        CHECK(integerState == GL_MAX);
        GLboolean depthWrite = GL_TRUE;
        std::array<GLboolean, 4> colorWrite{};
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorWrite.data());
        CHECK(depthWrite == GL_FALSE);
        CHECK((colorWrite == std::array<GLboolean, 4>{
            GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE}));
        glGetIntegerv(GL_STENCIL_WRITEMASK, &integerState);
        CHECK(integerState == 0x12);
        glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &integerState);
        CHECK(integerState == 0x34);
        std::array<GLfloat, 4> clearColor{};
        GLdouble clearDepth = 1.0;
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor.data());
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
        CHECK(clearColor[0] == doctest::Approx(0.2f));
        CHECK(clearColor[1] == doctest::Approx(0.3f));
        CHECK(clearColor[2] == doctest::Approx(0.4f));
        CHECK(clearColor[3] == doctest::Approx(0.5f));
        CHECK(clearDepth == doctest::Approx(0.25));
        glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &integerState);
        CHECK(integerState == 7);

        // The remaining pixel probes author fresh frames; return to their
        // ordinary writable state after proving the output scope restoration.
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMaskSeparate(GL_FRONT_AND_BACK, ~GLuint{0});

        // Every source texel expands to one exact 3x3 block; no interpolation
        // is permitted at the color boundaries.
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 6, 0, 0), 255, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 6, 2, 2), 255, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 6, 3, 0), 0, 255, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 6, 5, 2), 0, 255, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 6, 0, 3), 0, 0, 255));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 6, 2, 5), 0, 0, 255));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 6, 3, 3), 255, 255, 255));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 6, 5, 5), 255, 255, 255));

        glBindFramebuffer(GL_READ_FRAMEBUFFER, independentRead);
        target.Unbind();
        glDeleteFramebuffers(1, &independentRead);

        // A non-multiple target is cleared to black outside the centered
        // integer content rectangle.
        Framebuffer barredTarget;
        REQUIRE(barredTarget.Init(8, 6));
        barredTarget.Bind();
        const auto barred = output.Render(
            objects,
            {{8, 6}, {2, 2}, molga::GameOutputScaleMode::IntegerFit},
            renderer, defaultShader);
        REQUIRE(barred.presented);
        CHECK(barred.presentation.scale == 3);
        CHECK(barred.presentation.contentRect.x == 1);
        CHECK(IsColor(ReadTopLeftPixel(barredTarget.Id(), 6, 0, 0), 0, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(barredTarget.Id(), 6, 7, 5), 0, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(barredTarget.Id(), 6, 1, 0), 255, 0, 0));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, barredTarget.Id());
        barredTarget.Unbind();

        // At 1x crop the signed top-left rectangle and GL bottom-left blit must
        // select the same bottom-right logical pixel as input mapping does.
        Framebuffer cropTarget;
        REQUIRE(cropTarget.Init(1, 1));
        cropTarget.Bind();
        const auto cropped = output.Render(
            objects,
            {{1, 1}, {2, 2}, molga::GameOutputScaleMode::IntegerFit},
            renderer, defaultShader);
        REQUIRE(cropped.presented);
        REQUIRE(cropped.presentation.cropped);
        CHECK(cropped.presentation.contentRect.x == -1);
        CHECK(cropped.presentation.contentRect.y == -1);
        CHECK(IsColor(ReadTopLeftPixel(cropTarget.Id(), 1, 0, 0),
                      255, 255, 255));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, cropTarget.Id());
        cropTarget.Unbind();

        // Screen-space UI is an independent final pass. A camera-less title
        // or error menu must still render over the fallback clear.
        std::vector<std::shared_ptr<GameObject>> uiOnlyObjects;
        auto canvasObject = std::make_shared<GameObject>("Canvas");
        auto* canvas = canvasObject->AddComponent<UICanvas>();
        canvas->SetReferenceResolution({2.0f, 2.0f});
        auto* canvasRect = canvasObject->AddComponent<RectTransform>();
        canvasRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        canvasRect->SetSizeDelta({0.0f, 0.0f});
        uiOnlyObjects.push_back(canvasObject);

        auto imageObject = std::make_shared<GameObject>("Full UI Image");
        auto* imageRect = imageObject->AddComponent<RectTransform>();
        imageRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        imageRect->SetSizeDelta({0.0f, 0.0f});
        imageObject->AddComponent<UIImage>()->SetTint(Color::Red());
        imageObject->SetParent(canvasObject.get());
        uiOnlyObjects.push_back(imageObject);

        Framebuffer uiOnlyTarget;
        REQUIRE(uiOnlyTarget.Init(2, 2));
        uiOnlyTarget.Bind();
        const auto uiOnly = output.Render(
            uiOnlyObjects,
            {{2, 2}, {2, 2}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(uiOnly.presented);
        CHECK(uiOnly.mainCamera == nullptr);
        CHECK(IsColor(ReadTopLeftPixel(uiOnlyTarget.Id(), 2, 1, 1),
                      255, 0, 0));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, uiOnlyTarget.Id());
        uiOnlyTarget.Unbind();

        // Exercise the complete queue-to-pixel path with deliberately reversed
        // submission order. The layer stack, order-in-layer, and YAxis key must
        // each win over the later-submitted sprite at its probe pixel.
        {
            ProjectSettingsScope projectSettings;
            ProjectSettings::Get().sortingLayers = {
                "Background", "Default", "Foreground"};

            std::vector<std::shared_ptr<GameObject>> sortedObjects;
            auto sortedCameraObject = std::make_shared<GameObject>("Sort Camera");
            sortedCameraObject->AddComponent<Transform>(0.0f, 0.0f);
            Camera* sortedCamera = sortedCameraObject->AddComponent<Camera>();
            sortedCamera->SetMain(true);
            sortedCamera->SetPixelPerfect(true);
            sortedCamera->SetPixelZoom(1);
            sortedCamera->SetBackgroundColor(Color::Black());
            sortedObjects.push_back(sortedCameraObject);

            const auto addSortedSprite = [&sortedObjects](
                    const char* name, float x, float y, float width,
                    float height, const Color& color, const char* layer,
                    int order, molga::SortMode2D mode) {
                auto object = std::make_shared<GameObject>(name);
                object->AddComponent<Transform>(x, y);
                auto* sprite = object->AddComponent<SpriteRenderer>();
                sprite->SetSize(width, height);
                sprite->SetColor(color);
                sprite->SetSortingLayer(layer);
                sprite->SetSortingOrder(order);
                sprite->SetSortMode(mode);
                sortedObjects.push_back(std::move(object));
            };

            // Layer probe: front-to-back submission must still finish blue.
            addSortedSprite("Foreground", 0.0f, 0.0f, 1.0f, 1.0f,
                            Color::Blue(), "Foreground", 0,
                            molga::SortMode2D::Fixed);
            addSortedSprite("Default", 0.0f, 0.0f, 1.0f, 1.0f,
                            Color::Green(), "Default", 0,
                            molga::SortMode2D::Fixed);
            addSortedSprite("Background", 0.0f, 0.0f, 1.0f, 1.0f,
                            Color::Red(), "Background", 0,
                            molga::SortMode2D::Fixed);

            // Order probe: the high order is submitted before the low order.
            addSortedSprite("High Order", 1.0f, 0.0f, 1.0f, 1.0f,
                            Color::White(), "Foreground", 5,
                            molga::SortMode2D::Fixed);
            addSortedSprite("Low Order", 1.0f, 0.0f, 1.0f, 1.0f,
                            Color::Red(), "Foreground", -1,
                            molga::SortMode2D::Fixed);

            // Y probe: both tall sprites cover (2, 1), but the larger world Y
            // is submitted first and must be drawn last in Molga's +Y-down world.
            addSortedSprite("Large Y", 2.0f, 0.5f, 1.0f, 2.0f,
                            Color::Green(), "Foreground", 7,
                            molga::SortMode2D::YAxis);
            addSortedSprite("Small Y", 2.0f, 0.0f, 1.0f, 2.0f,
                            Color::Red(), "Foreground", 7,
                            molga::SortMode2D::YAxis);

            Framebuffer sortedTarget;
            REQUIRE(sortedTarget.Init(3, 2));
            sortedTarget.Bind();
            const auto sorted = output.Render(
                sortedObjects,
                {{3, 2}, {3, 2}, molga::GameOutputScaleMode::Native},
                renderer, defaultShader);
            REQUIRE(sorted.presented);
            REQUIRE(sorted.rendered);
            CHECK(IsColor(ReadTopLeftPixel(sortedTarget.Id(), 2, 0, 0),
                          0, 0, 255));
            CHECK(IsColor(ReadTopLeftPixel(sortedTarget.Id(), 2, 1, 0),
                          255, 255, 255));
            CHECK(IsColor(ReadTopLeftPixel(sortedTarget.Id(), 2, 2, 1),
                          0, 255, 0));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, sortedTarget.Id());
            sortedTarget.Unbind();
        }

        // Allocation failure and an exception from collection both restore
        // the exact entry state.
        target.Bind();
        glGenFramebuffers(1, &independentRead);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, independentRead);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        glDisable(GL_FRAMEBUFFER_SRGB);
        GLint maximum = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
        const auto failed = output.Render(
            objects,
            {{6, 6}, {maximum + 1, 1},
             molga::GameOutputScaleMode::IntegerFit},
            renderer, defaultShader);
        CHECK(failed.allocationFailed);
        CheckGlState(target.Id(), independentRead, viewport, scissor,
                     GL_TRUE, GL_FALSE);

        auto throwingObject = std::make_shared<GameObject>("Throwing");
        throwingObject->AddComponent<ThrowingRenderComponent>();
        auto throwingObjects = objects;
        throwingObjects.push_back(std::move(throwingObject));
        CHECK_THROWS_AS(
            output.Render(
                throwingObjects,
                {{6, 6}, {2, 2}, molga::GameOutputScaleMode::IntegerFit},
                renderer, defaultShader),
            std::runtime_error);
        CheckGlState(target.Id(), independentRead, viewport, scissor,
                     GL_TRUE, GL_FALSE);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, independentRead);
        target.Unbind();
        glDeleteFramebuffers(1, &independentRead);

        molga::RenderSystem2D::Get().Shutdown();
        ShaderManager::Get().Shutdown();
    }

}

TEST_CASE("Game output composes split and PIP cameras with deterministic viewport clears") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(defaultShader->IsValid());
    REQUIRE(ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string()) != nullptr);
    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        molga::GameOutputRenderer output;
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

        Framebuffer target;
        REQUIRE(target.Init(8, 4));
        target.Bind();
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        const auto split = output.Render(
            objects,
            {{8, 4}, {8, 4}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(split.presented);
        REQUIRE(split.rendered);
        CHECK(split.mainCamera == base.camera);
        REQUIRE(split.cameraResults.size() == 2);
        CHECK(split.cameraResults[0].cameraObjectId == base.object->GetID());
        CHECK(split.cameraResults[1].cameraObjectId ==
              firstOverlay.object->GetID());
        CHECK(split.cameraResults[0].viewport.x == 0);
        CHECK(split.cameraResults[0].viewport.y == 0);
        CHECK(split.cameraResults[0].viewport.width == 4);
        CHECK(split.cameraResults[0].viewport.height == 3);
        CHECK(split.cameraResults[1].viewport.x == 4);
        CHECK(split.cameraResults[1].viewport.width == 4);
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 4, 1, 1), 255, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 4, 6, 1), 0, 255, 0));
        // The whole logical target is cleared once, so uncovered authored space
        // cannot retain the magenta sentinel from before the frame.
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 4, 1, 3), 0, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 4, 6, 3), 0, 0, 0));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();

        // Reuse the same cameras as a full-screen base plus overlapping PIP.
        // Equal-depth cameras composite in scene order, so the later camera is
        // topmost and its background clear replaces everything below its rect.
        REQUIRE(base.camera->SetViewport({0.0f, 0.0f, 1.0f, 1.0f}));
        REQUIRE(firstOverlay.camera->SetViewport(
            {0.25f, 0.25f, 0.5f, 0.5f}));
        firstOverlay.camera->SetDepth(5);
        laterOverlay.camera->SetOutputRole(CameraOutputRole::Secondary);
        laterOverlay.camera->SetDepth(5);
        target.Bind();
        const auto tiedPip = output.Render(
            objects,
            {{8, 4}, {8, 4}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(tiedPip.cameraResults.size() == 3);
        CHECK(tiedPip.cameraResults[1].cameraObjectId ==
              firstOverlay.object->GetID());
        CHECK(tiedPip.cameraResults[2].cameraObjectId ==
              laterOverlay.object->GetID());
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 4, 0, 0), 255, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 4, 3, 1), 0, 0, 255));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();

        // Depth outranks scene order: the earlier green camera now composites
        // last and owns the overlap.
        firstOverlay.camera->SetDepth(6);
        target.Bind();
        const auto deeperPip = output.Render(
            objects,
            {{8, 4}, {8, 4}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(deeperPip.cameraResults.size() == 3);
        CHECK(deeperPip.cameraResults[1].cameraObjectId ==
              laterOverlay.object->GetID());
        CHECK(deeperPip.cameraResults[2].cameraObjectId ==
              firstOverlay.object->GetID());
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 4, 3, 1), 0, 255, 0));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Secondary-only cameras apply culling masks and map invalid layers to zero") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(defaultShader->IsValid());
    REQUIRE(ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string()) != nullptr);

    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        std::vector<std::shared_ptr<GameObject>> objects;
        const auto layerOneCamera = AddOutputCamera(
            objects, "Layer One", CameraOutputRole::Secondary,
            {0.0f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
        layerOneCamera.camera->SetCullingMask(std::uint32_t{1} << 1);
        const auto layerZeroCamera = AddOutputCamera(
            objects, "Layer Zero", CameraOutputRole::Secondary,
            {0.5f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
        layerZeroCamera.camera->SetCullingMask(std::uint32_t{1});

        AddLayerPixel(objects, "Valid Layer One", 1, Color::Red());
        AddLayerPixel(objects, "Invalid Layer Uses Zero", 99, Color::Green());

        molga::GameOutputRenderer output;
        Framebuffer target;
        REQUIRE(target.Init(4, 2));
        target.Bind();
        const auto result = output.Render(
            objects,
            {{4, 2}, {4, 2}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(result.presented);
        REQUIRE(result.rendered);
        CHECK(result.mainCamera == nullptr);
        REQUIRE(result.cameraResults.size() == 2);
        CHECK(result.cameraResults[0].outputRole ==
              CameraOutputRole::Secondary);
        CHECK(result.cameraResults[1].outputRole ==
              CameraOutputRole::Secondary);
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 2, 0, 0), 255, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 2, 2, 0), 0, 255, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 2, 1, 1), 0, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 2, 3, 1), 0, 0, 0));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Color adjustment uses ordered linear math and restores fullscreen GL state") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* stateShader = ShaderManager::Get().Load(
        "postfx-state-sentinel", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(stateShader != nullptr);
    REQUIRE(stateShader->IsValid());

    const molga::PostProcessProfile2D profile = MakePostProfile({
        {{"type", "ColorAdjust"}, {"enabled", true},
         {"exposureEV", 1.0}, {"contrast", 0.0},
         {"saturation", 0.5}, {"tint", {1.0, 0.5, 0.25}}}
    });
    Framebuffer destination;
    Framebuffer entryDraw;
    REQUIRE(destination.Init(5, 3));
    REQUIRE(entryDraw.Init(3, 2));

    GLuint entryRead = 0;
    GLuint sentinelVao = 0;
    GLuint sentinelBuffer = 0;
    GLuint sentinelTextures[2]{};
    glGenFramebuffers(1, &entryRead);
    glGenVertexArrays(1, &sentinelVao);
    glGenBuffers(1, &sentinelBuffer);
    glGenTextures(2, sentinelTextures);

    {
        molga::PostProcessPipeline pipeline;
        std::string error;
        REQUIRE(pipeline.Prepare({5, 3}, profile, &error));
        CHECK(pipeline.BloomMipCount() == 0);
        ClearHdr(pipeline.SceneTarget(), {0.25f, 0.5f, 0.75f, 0.4f});

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, entryDraw.Id());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, entryRead);
        const std::array<GLint, 4> viewport{2, 1, 3, 1};
        const std::array<GLint, 4> scissor{1, 0, 2, 2};
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ZERO);
        glDepthMask(GL_FALSE);
        glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        stateShader->Use();
        glBindVertexArray(sentinelVao);
        glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sentinelTextures[0]);
        const unsigned char sentinelPixel[4] = {0, 0, 0, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, sentinelPixel);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sentinelTextures[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, sentinelPixel);
        glActiveTexture(GL_TEXTURE3);

        const auto result = pipeline.Execute(profile, destination.Id(), {5, 3});
        REQUIRE(result.success);
        CHECK(result.postProcessed);
        CHECK(result.passes == 2); // Color Adjust + final resolve.
        CheckGlState(entryDraw.Id(), entryRead, viewport, scissor,
                     GL_TRUE, GL_FALSE);
        CHECK(glIsEnabled(GL_BLEND) == GL_TRUE);
        CHECK(glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
        CHECK(glIsEnabled(GL_CULL_FACE) == GL_TRUE);
        GLint value = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &value);
        CHECK(value == static_cast<GLint>(stateShader->GetID()));
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &value);
        CHECK(value == static_cast<GLint>(sentinelVao));
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &value);
        CHECK(value == static_cast<GLint>(sentinelBuffer));
        glGetIntegerv(GL_ACTIVE_TEXTURE, &value);
        CHECK(value == GL_TEXTURE3);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &value);
        CHECK(value == static_cast<GLint>(sentinelTextures[0]));
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &value);
        CHECK(value == static_cast<GLint>(sentinelTextures[1]));
        glActiveTexture(GL_TEXTURE3);
        GLboolean depthMask = GL_TRUE;
        std::array<GLboolean, 4> colorMask{};
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask.data());
        CHECK(depthMask == GL_FALSE);
        CHECK((colorMask == std::array<GLboolean, 4>{
            GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE}));

        // exposure -> contrast -> saturation -> tint in linear RGB.
        // [.25, .5, .75] becomes approximately
        // [.7149, .48245, .303725] before the sRGB resolve.
        const auto pixel = ReadTopLeftPixel(destination.Id(), 3, 2, 1);
        CHECK(NearByte(pixel[0], LinearSrgbByte(0.7149f)));
        CHECK(NearByte(pixel[1], LinearSrgbByte(0.48245f)));
        CHECK(NearByte(pixel[2], LinearSrgbByte(0.303725f)));
        CHECK(NearByte(pixel[3], 102, 1));
    }

    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDeleteTextures(2, sentinelTextures);
    glDeleteBuffers(1, &sentinelBuffer);
    glDeleteVertexArrays(1, &sentinelVao);
    glDeleteFramebuffers(1, &entryRead);
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Post-process resolve writes only its top-left destination rectangle") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const molga::PostProcessProfile2D profile = MakePostProfile({
        {{"type", "ColorAdjust"}, {"enabled", true},
         {"exposureEV", -1.0}}
    });
    Framebuffer destination;
    REQUIRE(destination.Init(7, 5));
    {
        ScopedFramebufferBinding binding(destination);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    {
        molga::PostProcessPipeline pipeline;
        std::string error;
        REQUIRE(pipeline.Prepare({2, 2}, profile, &error));
        ClearHdr(pipeline.SceneTarget(), {1.0f, 0.0f, 0.0f, 1.0f});

        const auto result = pipeline.Execute(
            profile, destination.Id(), {7, 5}, {2, 0, 2, 2});
        REQUIRE(result.success);
        CHECK(result.postProcessed);
        CHECK(result.passes == 2);

        const int exposedRed = LinearSrgbByte(0.5f);
        const auto topLeft = ReadTopLeftPixel(destination.Id(), 5, 2, 0);
        const auto bottomRight = ReadTopLeftPixel(destination.Id(), 5, 3, 1);
        CHECK(NearByte(topLeft[0], exposedRed));
        CHECK(topLeft[1] == 0);
        CHECK(topLeft[2] == 0);
        CHECK(NearByte(bottomRight[0], exposedRed));
        CHECK(bottomRight[1] == 0);
        CHECK(bottomRight[2] == 0);
        CHECK(IsColor(ReadTopLeftPixel(destination.Id(), 5, 1, 0),
                      0, 255, 0));
        CHECK(IsColor(ReadTopLeftPixel(destination.Id(), 5, 4, 1),
                      0, 255, 0));
        CHECK(IsColor(ReadTopLeftPixel(destination.Id(), 5, 2, 2),
                      0, 255, 0));
        CHECK(IsColor(ReadTopLeftPixel(destination.Id(), 5, 2, 4),
                      0, 255, 0));

        // Rect dimensions must match the prepared camera-sized pipeline.
        const auto invalid = pipeline.Execute(
            profile, destination.Id(), {7, 5}, {0, 0, 1, 2});
        CHECK_FALSE(invalid.success);
        CHECK_FALSE(invalid.error.empty());
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Post-process resolve reports an incomplete destination and restores state") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const molga::PostProcessProfile2D profile = MakePostProfile({
        {{"type", "ColorAdjust"}, {"enabled", true},
         {"exposureEV", -1.0}}
    });
    Framebuffer entryDraw;
    REQUIRE(entryDraw.Init(2, 2));
    GLuint incompleteDestination = 0;
    GLuint entryRead = 0;
    glGenFramebuffers(1, &incompleteDestination);
    glGenFramebuffers(1, &entryRead);

    {
        molga::PostProcessPipeline pipeline;
        std::string error;
        REQUIRE(pipeline.Prepare({2, 2}, profile, &error));
        ClearHdr(pipeline.SceneTarget(), {1.0f, 0.0f, 0.0f, 1.0f});

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, entryDraw.Id());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, entryRead);
        const std::array<GLint, 4> viewport{1, 0, 1, 2};
        const std::array<GLint, 4> scissor{0, 1, 2, 1};
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        glEnable(GL_SCISSOR_TEST);
        glDisable(GL_FRAMEBUFFER_SRGB);

        const auto result = pipeline.Execute(
            profile, incompleteDestination, {2, 2});
        CHECK_FALSE(result.success);
        CHECK_FALSE(result.postProcessed);
        CHECK_FALSE(result.error.empty());
        CheckGlState(entryDraw.Id(), entryRead, viewport, scissor,
                     GL_TRUE, GL_FALSE);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDeleteFramebuffers(1, &entryRead);
    glDeleteFramebuffers(1, &incompleteDestination);
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Vignette darkens aspect-aware corners and preserves alpha") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const molga::PostProcessProfile2D profile = MakePostProfile({
        {{"type", "Vignette"}, {"enabled", true}, {"intensity", 1.0},
         {"smoothness", 0.5}, {"color", {0.0, 0.0, 0.0}}}
    });
    Framebuffer destination;
    REQUIRE(destination.Init(17, 9));
    {
        molga::PostProcessPipeline pipeline;
        std::string error;
        REQUIRE(pipeline.Prepare({17, 9}, profile, &error));
        ClearHdr(pipeline.SceneTarget(), {0.5f, 0.5f, 0.5f, 0.3f});
        const auto result = pipeline.Execute(profile, destination.Id(), {17, 9});
        REQUIRE(result.success);
        CHECK(result.passes == 2);

        const auto center = ReadTopLeftPixel(destination.Id(), 9, 8, 4);
        const auto corner = ReadTopLeftPixel(destination.Id(), 9, 0, 0);
        const auto side = ReadTopLeftPixel(destination.Id(), 9, 16, 4);
        CHECK(NearByte(center[0], LinearSrgbByte(0.5f)));
        CHECK(corner[0] < center[0] / 2);
        CHECK(side[0] < center[0] * 3 / 5);
        CHECK(NearByte(center[3], 77, 1));
        CHECK(NearByte(corner[3], 77, 1));
        CHECK(NearByte(side[3], 77, 1));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Bloom creates HDR halos, honors threshold and soft knee, and is ordered") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const auto bloom = [](float threshold, float softKnee) {
        return nlohmann::json{
            {"type", "Bloom"}, {"enabled", true},
            {"threshold", threshold}, {"softKnee", softKnee},
            {"intensity", 1.0}, {"scatter", 0.7}};
    };
    const molga::PostProcessProfile2D haloProfile =
        MakePostProfile(nlohmann::json::array({bloom(1.0f, 0.0f)}));
    const molga::PostProcessProfile2D softProfile =
        MakePostProfile(nlohmann::json::array({bloom(1.0f, 1.0f)}));
    const molga::PostProcessProfile2D bloomThenExposure = MakePostProfile(
        nlohmann::json::array({
        bloom(1.0f, 0.0f),
        {{"type", "ColorAdjust"}, {"exposureEV", 1.0}}
    }));
    const molga::PostProcessProfile2D exposureThenBloom = MakePostProfile(
        nlohmann::json::array({
        {{"type", "ColorAdjust"}, {"exposureEV", 1.0}},
        bloom(1.0f, 0.0f)
    }));

    Framebuffer destination;
    REQUIRE(destination.Init(32, 32));
    {
        molga::PostProcessPipeline pipeline;
        std::string error;
        REQUIRE(pipeline.Prepare({32, 32}, haloProfile, &error));
        CHECK(pipeline.BloomMipCount() == 5);
        ClearHdr(pipeline.SceneTarget(), {0.0f, 0.0f, 0.0f, 0.4f});
        WriteHdrPixel(pipeline.SceneTarget(), 16, 16,
                      {8.0f, 8.0f, 8.0f, 0.4f});
        const auto haloResult =
            pipeline.Execute(haloProfile, destination.Id(), {32, 32});
        REQUIRE(haloResult.success);
        CHECK(haloResult.passes == 11);
        const auto brightCenter =
            ReadTopLeftPixel(destination.Id(), 32, 16, 16);
        const int brightNeighbor =
            MaxRedAround(destination.Id(), 32, 16, 16, 10);
        const auto farCorner =
            ReadTopLeftPixel(destination.Id(), 32, 0, 0);
        CAPTURE(brightCenter);
        CAPTURE(brightNeighbor);
        CAPTURE(farCorner);
        CHECK(brightNeighbor > 0);
        CHECK(farCorner[0] < brightNeighbor);
        CHECK(NearByte(brightCenter[3], 102, 1));
        CHECK(NearByte(ReadTopLeftPixel(
            destination.Id(), 32, 17, 16)[3], 102, 1));

        // Below-threshold input remains only at the source texel.
        ClearHdr(pipeline.SceneTarget(), {0.0f, 0.0f, 0.0f, 0.4f});
        WriteHdrPixel(pipeline.SceneTarget(), 16, 16,
                      {0.5f, 0.5f, 0.5f, 0.4f});
        REQUIRE(pipeline.Execute(haloProfile, destination.Id(), {32, 32}).success);
        const int excludedNeighbor =
            MaxRedAround(destination.Id(), 32, 16, 16, 10);
        CHECK(excludedNeighbor == 0);

        // Soft knee admits energy just below the hard prefilter boundary.
        ClearHdr(pipeline.SceneTarget(), {0.0f, 0.0f, 0.0f, 1.0f});
        WriteHdrPixel(pipeline.SceneTarget(), 16, 16,
                      {3.0f, 3.0f, 3.0f, 1.0f});
        REQUIRE(pipeline.Execute(haloProfile, destination.Id(), {32, 32}).success);
        const int hardKneeNeighbor =
            MaxRedAround(destination.Id(), 32, 16, 16, 10);
        REQUIRE(pipeline.Prepare({32, 32}, softProfile, &error));
        ClearHdr(pipeline.SceneTarget(), {0.0f, 0.0f, 0.0f, 1.0f});
        WriteHdrPixel(pipeline.SceneTarget(), 16, 16,
                      {3.0f, 3.0f, 3.0f, 1.0f});
        REQUIRE(pipeline.Execute(softProfile, destination.Id(), {32, 32}).success);
        const int softKneeNeighbor =
            MaxRedAround(destination.Id(), 32, 16, 16, 10);
        CAPTURE(hardKneeNeighbor);
        CAPTURE(softKneeNeighbor);
        CHECK(softKneeNeighbor > hardKneeNeighbor);

        // With ordered execution, exposure before Bloom crosses the threshold;
        // exposure after Bloom cannot retroactively create a halo.
        REQUIRE(pipeline.Prepare({32, 32}, bloomThenExposure, &error));
        ClearHdr(pipeline.SceneTarget(), {0.0f, 0.0f, 0.0f, 1.0f});
        WriteHdrPixel(pipeline.SceneTarget(), 16, 16,
                      {3.0f, 3.0f, 3.0f, 1.0f});
        REQUIRE(pipeline.Execute(
            bloomThenExposure, destination.Id(), {32, 32}).success);
        const int bloomFirstNeighbor =
            MaxRedAround(destination.Id(), 32, 16, 16, 10);

        REQUIRE(pipeline.Prepare({32, 32}, exposureThenBloom, &error));
        ClearHdr(pipeline.SceneTarget(), {0.0f, 0.0f, 0.0f, 1.0f});
        WriteHdrPixel(pipeline.SceneTarget(), 16, 16,
                      {3.0f, 3.0f, 3.0f, 1.0f});
        REQUIRE(pipeline.Execute(
            exposureThenBloom, destination.Id(), {32, 32}).success);
        const int exposureFirstNeighbor =
            MaxRedAround(destination.Id(), 32, 16, 16, 10);
        CAPTURE(bloomFirstNeighbor);
        CAPTURE(exposureFirstNeighbor);
        CHECK(exposureFirstNeighbor > bloomFirstNeighbor);

        // The adaptive chain also remains valid at the minimum output size.
        Framebuffer onePixelDestination;
        REQUIRE(onePixelDestination.Init(1, 1));
        REQUIRE(pipeline.Prepare({1, 1}, haloProfile, &error));
        CHECK(pipeline.BloomMipCount() == 1);
        ClearHdr(pipeline.SceneTarget(), {2.0f, 1.0f, 0.5f, 0.25f});
        const auto onePixel = pipeline.Execute(
            haloProfile, onePixelDestination.Id(), {1, 1});
        CHECK(onePixel.success);
        CHECK(onePixel.postProcessed);
        CHECK(NearByte(ReadTopLeftPixel(
            onePixelDestination.Id(), 1, 0, 0)[3], 64, 1));

        GLint maximumTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
        error.clear();
        CHECK_FALSE(pipeline.Prepare(
            {maximumTextureSize + 1, 1}, haloProfile, &error));
        CHECK_FALSE(error.empty());
        CHECK((pipeline.PreparedSize() == molga::PixelSize{1, 1}));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Game output bypasses neutral profiles and keeps UI outside post-processing") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "molga_postfx_game_output_gl";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Assets");
    const std::string guid = "abcdefabcdefabcdefabcdefabcdefab";
    const std::filesystem::path source = root / "Assets" / "game.postfx";
    WriteJsonFile(source, {
        {"schemaVersion", 1},
        {"effects", {{{"type", "ColorAdjust"}, {"exposureEV", -1.0}}}}
    });
    WriteJsonFile(source.string() + ".meta", {
        {"guid", guid}, {"importer", "PostProcessProfileImporter"},
        {"importerVersion", 1}
    });
    molga::AssetDatabase::Get().Clear();
    molga::AssetDatabase::Get().ScanProject(root / "Assets");
    molga::PostProcessProfileResolver::Get().ClearForTesting();
    REQUIRE(molga::AssetDatabase::Get().Find(guid) != nullptr);

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(defaultShader->IsValid());
    REQUIRE(ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string()) != nullptr);
    REQUIRE(ShaderManager::Get().Load(
        "batch_lit", (shaderRoot / "batch_lit.vert").string(),
        (shaderRoot / "batch_lit.frag").string()) != nullptr);

    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        molga::GameOutputRenderer output;
        std::vector<std::shared_ptr<GameObject>> objects;
        auto cameraObject = std::make_shared<GameObject>("Post Camera");
        cameraObject->AddComponent<Transform>(0.0f, 0.0f);
        Camera* camera = cameraObject->AddComponent<Camera>();
        camera->SetMain(true);
        camera->SetPixelPerfect(true);
        camera->SetPixelZoom(1);
        camera->SetBackgroundColor(Color::White());
        camera->SetPostProcessEnabled(true);
        camera->SetPostProcessProfileGuid(guid);
        camera->SetLightingEnabled(true);
        camera->SetAmbientIntensity(0.5f);
        objects.push_back(cameraObject);

        auto worldObject = std::make_shared<GameObject>("Lit World");
        worldObject->AddComponent<Transform>(0.0f, 0.0f);
        SpriteRenderer* worldSprite =
            worldObject->AddComponent<SpriteRenderer>();
        worldSprite->SetSize(2.0f, 2.0f);
        worldSprite->SetColor(Color::Green());
        worldSprite->SetLightingMode(SpriteLightingMode2D::Lit);
        objects.push_back(worldObject);

        auto canvasObject = std::make_shared<GameObject>("Canvas");
        canvasObject->AddComponent<UICanvas>()->SetReferenceResolution(
            {2.0f, 2.0f});
        auto* canvasRect = canvasObject->AddComponent<RectTransform>();
        canvasRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        canvasRect->SetSizeDelta({0.0f, 0.0f});
        objects.push_back(canvasObject);

        auto imageObject = std::make_shared<GameObject>("Unaffected UI");
        auto* imageRect = imageObject->AddComponent<RectTransform>();
        imageRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        imageRect->SetSizeDelta({0.0f, 0.0f});
        imageObject->AddComponent<UIImage>()->SetTint(Color::Red());
        imageObject->SetParent(canvasObject.get());
        objects.push_back(imageObject);

        Framebuffer integerTarget;
        REQUIRE(integerTarget.Init(8, 6));
        integerTarget.Bind();
        const auto active = output.Render(
            objects,
            {{8, 6}, {2, 2}, molga::GameOutputScaleMode::IntegerFit},
            renderer, defaultShader);
        REQUIRE(active.presented);
        REQUIRE(active.rendered);
        CHECK(active.mainCamera == camera);
        CHECK(active.postProcessed);
        CHECK_FALSE(active.postProcessFallback);
        CHECK(active.postProcessPasses == 2);
        CHECK(active.lightingApplied);
        CHECK_FALSE(active.lightingFallback);
        CHECK(active.selectedLightCount == 0);
        CHECK(active.lightingPasses == 1);
        CHECK(renderer.Stats().postProcessPasses == 2);
        CHECK(active.presentation.contentRect.x == 1);
        // Exposure would turn world red into half intensity. UI is rendered
        // after resolve, so it remains exact red and is still nearest-scaled.
        CHECK(IsColor(ReadTopLeftPixel(integerTarget.Id(), 6, 1, 0),
                      255, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(integerTarget.Id(), 6, 6, 5),
                      255, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(integerTarget.Id(), 6, 0, 0),
                      0, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(integerTarget.Id(), 6, 7, 5),
                      0, 0, 0));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, integerTarget.Id());
        integerTarget.Unbind();

        // A mathematically neutral profile takes the original direct path.
        WriteJsonFile(source, {
            {"schemaVersion", 1},
            {"effects", {{{"type", "ColorAdjust"}, {"enabled", true}}}}
        });
        REQUIRE(molga::AssetDatabase::Get().TryReimport(guid));
        Framebuffer neutralTarget;
        REQUIRE(neutralTarget.Init(3, 2));
        neutralTarget.Bind();
        const auto neutral = output.Render(
            objects,
            {{3, 2}, {3, 2}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        CHECK(neutral.presented);
        CHECK_FALSE(neutral.postProcessed);
        CHECK_FALSE(neutral.postProcessFallback);
        CHECK(neutral.postProcessPasses == 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, neutralTarget.Id());
        neutralTarget.Unbind();

        // Re-enable a simple effect and resize through the 1x1 edge case.
        WriteJsonFile(source, {
            {"schemaVersion", 1},
            {"effects", {{{"type", "ColorAdjust"}, {"exposureEV", -1.0}}}}
        });
        REQUIRE(molga::AssetDatabase::Get().TryReimport(guid));
        Framebuffer onePixelTarget;
        REQUIRE(onePixelTarget.Init(1, 1));
        onePixelTarget.Bind();
        const auto onePixel = output.Render(
            objects,
            {{1, 1}, {1, 1}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        CHECK(onePixel.presented);
        CHECK(onePixel.postProcessed);
        CHECK_FALSE(onePixel.postProcessFallback);
        CHECK(IsColor(ReadTopLeftPixel(onePixelTarget.Id(), 1, 0, 0),
                      255, 0, 0));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, onePixelTarget.Id());
        onePixelTarget.Unbind();
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    molga::PostProcessProfileResolver::Get().ClearForTesting();
    molga::AssetDatabase::Get().Clear();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("Game output isolates camera PostFX results and cleans unused pipelines") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "molga_multi_camera_postfx_gl";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Assets");
    const std::string guid = "1234567890abcdef1234567890abcdef";
    const std::filesystem::path source = root / "Assets" / "camera.postfx";
    WriteJsonFile(source, {
        {"schemaVersion", 1},
        {"effects", {{{"type", "ColorAdjust"}, {"exposureEV", -1.0}}}}
    });
    WriteJsonFile(source.string() + ".meta", {
        {"guid", guid}, {"importer", "PostProcessProfileImporter"},
        {"importerVersion", 1}
    });
    molga::AssetDatabase::Get().Clear();
    molga::AssetDatabase::Get().ScanProject(root / "Assets");
    molga::PostProcessProfileResolver::Get().ClearForTesting();
    REQUIRE(molga::AssetDatabase::Get().Find(guid) != nullptr);

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(defaultShader->IsValid());
    REQUIRE(ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string()) != nullptr);

    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        molga::GameOutputRenderer output;
        std::vector<std::shared_ptr<GameObject>> objects;
        const auto wideCamera = AddOutputCamera(
            objects, "Wide FX Camera", CameraOutputRole::Primary,
            {0.0f, 0.0f, 0.75f, 1.0f}, 0, Color::White());
        const auto narrowCamera = AddOutputCamera(
            objects, "Narrow FX Camera", CameraOutputRole::Secondary,
            {0.75f, 0.0f, 0.25f, 1.0f}, 0, Color::White());
        for (Camera* camera : {wideCamera.camera, narrowCamera.camera}) {
            camera->SetPostProcessEnabled(true);
            camera->SetPostProcessProfileGuid(guid);
        }

        Framebuffer target;
        REQUIRE(target.Init(8, 2));
        target.Bind();
        const auto bothActive = output.Render(
            objects,
            {{8, 2}, {8, 2}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(bothActive.presented);
        REQUIRE(bothActive.rendered);
        CHECK(bothActive.mainCamera == wideCamera.camera);
        CHECK(bothActive.postProcessed);
        CHECK_FALSE(bothActive.postProcessFallback);
        CHECK(bothActive.postProcessPasses == 4);
        REQUIRE(bothActive.cameraResults.size() == 2);
        const auto* wideResult = FindCameraResult(
            bothActive, wideCamera.object->GetID());
        const auto* narrowResult = FindCameraResult(
            bothActive, narrowCamera.object->GetID());
        REQUIRE(wideResult != nullptr);
        REQUIRE(narrowResult != nullptr);
        CHECK(wideResult->viewport.width == 6);
        CHECK(narrowResult->viewport.width == 2);
        CHECK(wideResult->postProcessed);
        CHECK(narrowResult->postProcessed);
        CHECK_FALSE(wideResult->postProcessFallback);
        CHECK_FALSE(narrowResult->postProcessFallback);
        CHECK(wideResult->postProcessPasses == 2);
        CHECK(narrowResult->postProcessPasses == 2);
        CHECK(output.CachedPostProcessPipelineCount() == 2);
        const int halfWhite = LinearSrgbByte(0.5f);
        const auto widePixel = ReadTopLeftPixel(target.Id(), 2, 0, 0);
        const auto narrowPixel = ReadTopLeftPixel(target.Id(), 2, 7, 0);
        CHECK(NearByte(widePixel[0], halfWhite));
        CHECK(NearByte(widePixel[1], halfWhite));
        CHECK(NearByte(widePixel[2], halfWhite));
        CHECK(NearByte(narrowPixel[0], halfWhite));
        CHECK(NearByte(narrowPixel[1], halfWhite));
        CHECK(NearByte(narrowPixel[2], halfWhite));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();

        // Camera masks do not apply to the one global UI pass, and the UI pass
        // happens after both independent HDR resolves.
        wideCamera.camera->SetCullingMask(0);
        narrowCamera.camera->SetCullingMask(0);
        auto canvasObject = std::make_shared<GameObject>("Global Canvas");
        canvasObject->SetLayer(31);
        canvasObject->AddComponent<UICanvas>()->SetReferenceResolution(
            {8.0f, 2.0f});
        auto* canvasRect = canvasObject->AddComponent<RectTransform>();
        canvasRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        canvasRect->SetSizeDelta({0.0f, 0.0f});
        objects.push_back(canvasObject);

        auto imageObject = std::make_shared<GameObject>("Global UI Image");
        imageObject->SetLayer(31);
        auto* imageRect = imageObject->AddComponent<RectTransform>();
        imageRect->SetAnchors({0.0f, 0.0f}, {1.0f, 1.0f});
        imageRect->SetSizeDelta({0.0f, 0.0f});
        imageObject->AddComponent<UIImage>()->SetTint(Color::Red());
        imageObject->SetParent(canvasObject.get());
        objects.push_back(imageObject);

        target.Bind();
        const auto withUi = output.Render(
            objects,
            {{8, 2}, {8, 2}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        CHECK(withUi.postProcessPasses == 4);
        CHECK(output.CachedPostProcessPipelineCount() == 2);
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 2, 0, 0), 255, 0, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 2, 7, 1), 255, 0, 0));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();

        // Removing one selected camera evicts only its instance-keyed pipeline.
        narrowCamera.camera->SetOutputRole(CameraOutputRole::Disabled);
        target.Bind();
        const auto oneActive = output.Render(
            objects,
            {{8, 2}, {8, 2}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(oneActive.cameraResults.size() == 1);
        CHECK(oneActive.cameraResults[0].cameraObjectId ==
              wideCamera.object->GetID());
        CHECK(oneActive.cameraResults[0].postProcessed);
        CHECK(oneActive.postProcessPasses == 2);
        CHECK(output.CachedPostProcessPipelineCount() == 1);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();

        // A selected camera that stops using PostFX also releases its cache.
        wideCamera.camera->SetPostProcessEnabled(false);
        target.Bind();
        const auto direct = output.Render(
            objects,
            {{8, 2}, {8, 2}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(direct.cameraResults.size() == 1);
        CHECK_FALSE(direct.cameraResults[0].postProcessed);
        CHECK_FALSE(direct.cameraResults[0].postProcessFallback);
        CHECK(direct.cameraResults[0].postProcessPasses == 0);
        CHECK_FALSE(direct.postProcessed);
        CHECK(direct.postProcessPasses == 0);
        CHECK(output.CachedPostProcessPipelineCount() == 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    molga::PostProcessProfileResolver::Get().ClearForTesting();
    molga::AssetDatabase::Get().Clear();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("Post-process shader failure falls back to direct world rendering") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "molga_postfx_fallback_gl";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Assets");
    const std::string guid = "fedcbafedcbafedcbafedcbafedcbafe";
    const std::filesystem::path source = root / "Assets" / "fallback.postfx";
    WriteJsonFile(source, {
        {"schemaVersion", 1},
        {"effects", {{{"type", "ColorAdjust"}, {"exposureEV", 1.0}}}}
    });
    WriteJsonFile(source.string() + ".meta", {
        {"guid", guid}, {"importer", "PostProcessProfileImporter"},
        {"importerVersion", 1}
    });
    molga::AssetDatabase::Get().Clear();
    molga::AssetDatabase::Get().ScanProject(root / "Assets");
    molga::PostProcessProfileResolver::Get().ClearForTesting();

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string()) != nullptr);
    Shader* poisonedResolve = ShaderManager::Get().Load(
        "postfx.resolve", "/missing/postfx.vert", "/missing/postfx.frag");
    REQUIRE(poisonedResolve != nullptr);
    REQUIRE_FALSE(poisonedResolve->IsValid());

    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        std::vector<std::shared_ptr<GameObject>> objects;
        const auto fallbackCamera = AddOutputCamera(
            objects, "Fallback Camera", CameraOutputRole::Primary,
            {0.0f, 0.0f, 0.5f, 1.0f}, 0, Color::Green());
        fallbackCamera.camera->SetPostProcessEnabled(true);
        fallbackCamera.camera->SetPostProcessProfileGuid(guid);
        const auto directCamera = AddOutputCamera(
            objects, "Unaffected Direct Camera", CameraOutputRole::Secondary,
            {0.5f, 0.0f, 0.5f, 1.0f}, 0, Color::Blue());

        molga::GameOutputRenderer output;
        Framebuffer target;
        REQUIRE(target.Init(4, 2));
        target.Bind();
        const auto result = output.Render(
            objects,
            {{4, 2}, {4, 2}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        CHECK(result.presented);
        CHECK(result.rendered);
        CHECK(result.mainCamera == fallbackCamera.camera);
        CHECK_FALSE(result.postProcessed);
        CHECK(result.postProcessFallback);
        CHECK(result.postProcessPasses == 0);
        REQUIRE(result.cameraResults.size() == 2);
        const auto* fallbackResult = FindCameraResult(
            result, fallbackCamera.object->GetID());
        const auto* directResult = FindCameraResult(
            result, directCamera.object->GetID());
        REQUIRE(fallbackResult != nullptr);
        REQUIRE(directResult != nullptr);
        CHECK(fallbackResult->rendered);
        CHECK_FALSE(fallbackResult->postProcessed);
        CHECK(fallbackResult->postProcessFallback);
        CHECK(fallbackResult->postProcessPasses == 0);
        CHECK(directResult->rendered);
        CHECK_FALSE(directResult->postProcessed);
        CHECK_FALSE(directResult->postProcessFallback);
        CHECK(directResult->postProcessPasses == 0);
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 2, 1, 1),
                      0, 255, 0));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 2, 3, 1),
                      0, 0, 255));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    molga::PostProcessProfileResolver::Get().ClearForTesting();
    molga::AssetDatabase::Get().Clear();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("Lighting remains opt-in and is independent across split cameras") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(defaultShader->IsValid());
    Shader* batchShader = ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string());
    REQUIRE(batchShader != nullptr);
    REQUIRE(batchShader->IsValid());
    Shader* litShader = ShaderManager::Get().Load(
        "batch_lit", (shaderRoot / "batch_lit.vert").string(),
        (shaderRoot / "batch_lit.frag").string());
    REQUIRE(litShader != nullptr);
    REQUIRE(litShader->IsValid());

    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        molga::GameOutputRenderer output;
        std::vector<std::shared_ptr<GameObject>> objects;
        const auto litCamera = AddOutputCamera(
            objects, "Lit Primary", CameraOutputRole::Primary,
            {0.0f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
        const auto legacyCamera = AddOutputCamera(
            objects, "Legacy Secondary", CameraOutputRole::Secondary,
            {0.5f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
        litCamera.camera->SetLightingEnabled(true);
        litCamera.camera->SetAmbientColor(Color::White());
        litCamera.camera->SetAmbientIntensity(0.25f);

        auto lightObject = std::make_shared<GameObject>("Point Light");
        lightObject->AddComponent<Transform>(0.5f, 0.5f);
        PointLight2D* light = lightObject->AddComponent<PointLight2D>();
        REQUIRE(light->SetColor(Color::White()));
        REQUIRE(light->SetIntensity(0.5f));
        REQUIRE(light->SetRadius(100.0f));
        // A zero-height light exactly at the sampled receiver position must
        // use the shader's deterministic +Z direction fallback.
        REQUIRE(light->SetHeight(0.0f));
        REQUIRE(light->SetFalloff(1.0f));
        objects.push_back(lightObject);

        auto spriteObject = std::make_shared<GameObject>("White Receiver");
        spriteObject->AddComponent<Transform>(0.0f, 0.0f);
        SpriteRenderer* sprite =
            spriteObject->AddComponent<SpriteRenderer>();
        sprite->SetSize(1.0f, 1.0f);
        sprite->SetColor(Color::White());
        objects.push_back(spriteObject);

        Framebuffer target;
        REQUIRE(target.Init(4, 1));

        // A Camera may opt in, but the default Unlit receiver must still use
        // the exact legacy shader path without allocating a lighting cache.
        target.Bind();
        const auto unlit = output.Render(
            objects,
            {{4, 1}, {4, 1}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(unlit.presented);
        REQUIRE(unlit.rendered);
        CHECK_FALSE(unlit.lightingApplied);
        CHECK_FALSE(unlit.lightingFallback);
        CHECK(unlit.selectedLightCount == 0);
        CHECK(unlit.lightingPasses == 0);
        CHECK(output.CachedLightingPipelineCount() == 0);
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 1, 0, 0),
                      255, 255, 255));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 1, 2, 0),
                      255, 255, 255));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();

        // The Primary receives ambient + one centered flat-normal point light,
        // while the Secondary remains byte-identical Unlit.
        sprite->SetLightingMode(SpriteLightingMode2D::Lit);
        target.Bind();
        const auto mixed = output.Render(
            objects,
            {{4, 1}, {4, 1}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(mixed.presented);
        REQUIRE(mixed.rendered);
        CHECK(mixed.lightingApplied);
        CHECK_FALSE(mixed.lightingFallback);
        CHECK_FALSE(mixed.shadowFallback);
        CHECK(mixed.selectedLightCount == 1);
        CHECK(mixed.shadowedLightCount == 0);
        CHECK(mixed.lightingPasses == 1);
        CHECK(mixed.shadowPasses == 0);
        REQUIRE(mixed.cameraResults.size() == 2);
        const auto* primaryResult =
            FindCameraResult(mixed, litCamera.object->GetID());
        const auto* secondaryResult =
            FindCameraResult(mixed, legacyCamera.object->GetID());
        REQUIRE(primaryResult != nullptr);
        REQUIRE(secondaryResult != nullptr);
        CHECK(primaryResult->lightingApplied);
        CHECK(primaryResult->selectedLightCount == 1);
        CHECK(primaryResult->lightingPasses == 1);
        CHECK_FALSE(secondaryResult->lightingApplied);
        CHECK(secondaryResult->selectedLightCount == 0);
        CHECK(secondaryResult->lightingPasses == 0);
        CHECK(output.CachedLightingPipelineCount() == 1);

        const int expectedLit = LinearSrgbByte(0.75f);
        const auto litPixel = ReadTopLeftPixel(target.Id(), 1, 0, 0);
        CHECK(NearByte(litPixel[0], expectedLit, 5));
        CHECK(NearByte(litPixel[1], expectedLit, 5));
        CHECK(NearByte(litPixel[2], expectedLit, 5));
        CHECK(litPixel[3] == 255);
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 1, 2, 0),
                      255, 255, 255));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();

        const auto renderPrimaryPixel = [&]() {
            target.Bind();
            const auto frame = output.Render(
                objects,
                {{4, 1}, {4, 1}, molga::GameOutputScaleMode::Native},
                renderer, defaultShader);
            REQUIRE(frame.presented);
            REQUIRE(frame.lightingApplied);
            const auto pixel = ReadTopLeftPixel(target.Id(), 1, 0, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
            target.Unbind();
            return pixel;
        };

        // Height is an unrestricted finite world-unit value. A light below
        // the flat receiver contributes nothing, while height zero at the
        // exact receiver position uses the deterministic +Z fallback.
        REQUIRE(light->SetHeight(-1.0f));
        const int expectedAmbient = LinearSrgbByte(0.25f);
        const auto belowReceiver = renderPrimaryPixel();
        CHECK(NearByte(belowReceiver[0], expectedAmbient, 3));
        REQUIRE(light->SetHeight(0.0f));
        const auto zeroVectorFallback = renderPrimaryPixel();
        CHECK(NearByte(zeroVectorFallback[0], expectedLit, 5));

        // The same affect mask filters receivers in the lit shader.
        light->SetAffectMask(0u);
        const auto maskedReceiver = renderPrimaryPixel();
        CHECK(NearByte(maskedReceiver[0], expectedAmbient, 3));
        light->SetAffectMask(0xFFFFFFFFu);

        // Exercise the published radial/falloff equation and pseudo-3D height
        // with a half-unit lateral offset from the sampled receiver.
        lightObject->GetComponent<Transform>()->SetPosition(1.0f, 0.5f);
        REQUIRE(light->SetHeight(1.0f));
        REQUIRE(light->SetRadius(1.0f));
        REQUIRE(light->SetFalloff(1.0f));
        const auto falloffOne = renderPrimaryPixel();
        const float lambert = 1.0f / std::sqrt(1.25f);
        const int expectedFalloffOne =
            LinearSrgbByte(0.25f + lambert * 0.5f * 0.5f);
        CHECK(NearByte(falloffOne[0], expectedFalloffOne, 5));

        REQUIRE(light->SetFalloff(4.0f));
        const auto falloffFour = renderPrimaryPixel();
        const int expectedFalloffFour =
            LinearSrgbByte(0.25f + lambert * 0.0625f * 0.5f);
        CHECK(NearByte(falloffFour[0], expectedFalloffFour, 5));
        CHECK(falloffFour[0] < falloffOne[0]);

        REQUIRE(light->SetRadius(0.4f));
        const auto outsideRadius = renderPrimaryPixel();
        CHECK(NearByte(outsideRadius[0], expectedAmbient, 3));

        // Disabling the last lit camera evicts its instance-keyed pipeline and
        // returns both viewports to the original Unlit pixels.
        litCamera.camera->SetLightingEnabled(false);
        target.Bind();
        const auto disabled = output.Render(
            objects,
            {{4, 1}, {4, 1}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        CHECK_FALSE(disabled.lightingApplied);
        CHECK(disabled.lightingPasses == 0);
        CHECK(output.CachedLightingPipelineCount() == 0);
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 1, 0, 0),
                      255, 255, 255));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 1, 2, 0),
                      255, 255, 255));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Lighting and shadow failures stay camera-local and render Unlit fallbacks") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(defaultShader->IsValid());
    REQUIRE(ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string()) != nullptr);

    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        std::vector<std::shared_ptr<GameObject>> objects;
        const auto fallbackCamera = AddOutputCamera(
            objects, "Fallback Primary", CameraOutputRole::Primary,
            {0.0f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
        const auto unaffectedCamera = AddOutputCamera(
            objects, "Unaffected Secondary", CameraOutputRole::Secondary,
            {0.5f, 0.0f, 0.5f, 1.0f}, 0, Color::Black());
        fallbackCamera.camera->SetLightingEnabled(true);
        fallbackCamera.camera->SetAmbientIntensity(0.25f);

        auto receiverObject =
            std::make_shared<GameObject>("Fallback Receiver");
        receiverObject->AddComponent<Transform>(0.0f, 0.0f);
        SpriteRenderer* receiver =
            receiverObject->AddComponent<SpriteRenderer>();
        receiver->SetSize(1.0f, 1.0f);
        receiver->SetColor(Color::White());
        receiver->SetLightingMode(SpriteLightingMode2D::Lit);
        objects.push_back(receiverObject);

        auto lightObject = std::make_shared<GameObject>("Fallback Light");
        lightObject->AddComponent<Transform>(0.5f, 0.5f);
        PointLight2D* light = lightObject->AddComponent<PointLight2D>();
        REQUIRE(light->SetIntensity(0.5f));
        REQUIRE(light->SetRadius(10.0f));
        REQUIRE(light->SetHeight(0.0f));
        REQUIRE(light->SetFalloff(1.0f));
        objects.push_back(lightObject);

        molga::GameOutputRenderer output;
        Framebuffer target;
        REQUIRE(target.Init(4, 1));

        // batch_lit is intentionally absent. Only the opted-in camera falls
        // back; the legacy camera and the global output continue normally.
        target.Bind();
        const auto lightingFailure = output.Render(
            objects,
            {{4, 1}, {4, 1}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(lightingFailure.presented);
        REQUIRE(lightingFailure.rendered);
        CHECK_FALSE(lightingFailure.lightingApplied);
        CHECK(lightingFailure.lightingFallback);
        CHECK_FALSE(lightingFailure.shadowFallback);
        REQUIRE(lightingFailure.cameraResults.size() == 2);
        const auto* failedCamera = FindCameraResult(
            lightingFailure, fallbackCamera.object->GetID());
        const auto* unaffected = FindCameraResult(
            lightingFailure, unaffectedCamera.object->GetID());
        REQUIRE(failedCamera != nullptr);
        REQUIRE(unaffected != nullptr);
        CHECK(failedCamera->lightingFallback);
        CHECK_FALSE(unaffected->lightingFallback);
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 1, 0, 0),
                      255, 255, 255));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 1, 2, 0),
                      255, 255, 255));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();

        // Recover the required Lit shader but leave shadow_mask_2d absent.
        // The shadow light must remain selected and illuminate unshadowed.
        REQUIRE(ShaderManager::Get().Load(
            "batch_lit", (shaderRoot / "batch_lit.vert").string(),
            (shaderRoot / "batch_lit.frag").string()) != nullptr);
        light->SetCastsShadows(true);
        target.Bind();
        const auto shadowFailure = output.Render(
            objects,
            {{4, 1}, {4, 1}, molga::GameOutputScaleMode::Native},
            renderer, defaultShader);
        REQUIRE(shadowFailure.presented);
        REQUIRE(shadowFailure.rendered);
        CHECK(shadowFailure.lightingApplied);
        CHECK_FALSE(shadowFailure.lightingFallback);
        CHECK(shadowFailure.shadowFallback);
        CHECK(shadowFailure.selectedLightCount == 1);
        CHECK(shadowFailure.shadowedLightCount == 0);
        CHECK(shadowFailure.shadowPasses == 0);
        CHECK(shadowFailure.lightingPasses == 1);
        const int expectedUnshadowed = LinearSrgbByte(0.75f);
        const auto litPixel = ReadTopLeftPixel(target.Id(), 1, 0, 0);
        CHECK(NearByte(litPixel[0], expectedUnshadowed, 5));
        CHECK(NearByte(litPixel[1], expectedUnshadowed, 5));
        CHECK(NearByte(litPixel[2], expectedUnshadowed, 5));
        CHECK(IsColor(ReadTopLeftPixel(target.Id(), 1, 2, 0),
                      255, 255, 255));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
        target.Unbind();
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Authored normals follow sprite rotation and UV flip") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();
    TextureManager::Get().Clear();

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "molga_authored_normal_gl";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "Assets");
    const std::string diffuseGuid =
        "1234567890abcdef1234567890abcdef";
    const std::string normalGuid =
        "abcdef1234567890abcdef1234567890";
    const std::filesystem::path diffusePath =
        root / "Assets" / "diffuse.ppm";
    const std::filesystem::path normalPath =
        root / "Assets" / "normal.ppm";
    WriteSinglePixelPpm(diffusePath, {255, 255, 255});
    WriteSinglePixelPpm(normalPath, {255, 128, 128});
    WriteJsonFile(diffusePath.string() + ".meta", {
        {"guid", diffuseGuid},
        {"importer", "TextureImporter"},
        {"importerVersion", 2},
        {"settings", {{"usage", "Color"}, {"filter", "Nearest"}}}
    });
    WriteJsonFile(normalPath.string() + ".meta", {
        {"guid", normalGuid},
        {"importer", "TextureImporter"},
        {"importerVersion", 2},
        {"settings", {{"usage", "NormalMap"}, {"filter", "Nearest"}}}
    });
    molga::AssetDatabase::Get().Clear();
    molga::AssetDatabase::Get().ScanProject(root / "Assets");

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string()) != nullptr);
    REQUIRE(ShaderManager::Get().Load(
        "batch_lit", (shaderRoot / "batch_lit.vert").string(),
        (shaderRoot / "batch_lit.frag").string()) != nullptr);

    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        std::vector<std::shared_ptr<GameObject>> objects;
        const auto outputCamera = AddOutputCamera(
            objects, "Normal Camera", CameraOutputRole::Primary,
            {0.0f, 0.0f, 1.0f, 1.0f}, 0, Color::Black());
        outputCamera.camera->SetLightingEnabled(true);
        outputCamera.camera->SetAmbientIntensity(0.0f);

        auto lightObject = std::make_shared<GameObject>("Right Light");
        lightObject->AddComponent<Transform>(1.5f, 0.5f);
        PointLight2D* light = lightObject->AddComponent<PointLight2D>();
        REQUIRE(light->SetIntensity(1.0f));
        REQUIRE(light->SetRadius(10.0f));
        REQUIRE(light->SetHeight(0.0f));
        REQUIRE(light->SetFalloff(1.0f));
        objects.push_back(lightObject);

        auto receiverObject = std::make_shared<GameObject>("Normal Receiver");
        Transform* receiverTransform =
            receiverObject->AddComponent<Transform>(0.0f, 0.0f);
        SpriteRenderer* receiver =
            receiverObject->AddComponent<SpriteRenderer>();
        receiver->SetTextureGuid(diffuseGuid);
        receiver->SetSize(1.0f, 1.0f);
        receiver->SetLightingMode(SpriteLightingMode2D::Lit);
        receiver->SetNormalMapGuid(normalGuid);
        objects.push_back(receiverObject);

        molga::GameOutputRenderer output;
        Framebuffer target;
        REQUIRE(target.Init(1, 1));
        const auto renderPixel = [&]() {
            target.Bind();
            const auto result = output.Render(
                objects,
                {{1, 1}, {1, 1}, molga::GameOutputScaleMode::Native},
                renderer, defaultShader);
            REQUIRE(result.presented);
            REQUIRE(result.lightingApplied);
            const auto pixel = ReadTopLeftPixel(target.Id(), 1, 0, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
            target.Unbind();
            return pixel;
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
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    TextureManager::Get().Clear();
    molga::AssetDatabase::Get().Clear();
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("Box and convex occluders cast hard shadow pixels") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* defaultShader = ShaderManager::Get().Load(
        "default", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(defaultShader != nullptr);
    REQUIRE(ShaderManager::Get().Load(
        "batch", (shaderRoot / "batch.vert").string(),
        (shaderRoot / "batch.frag").string()) != nullptr);
    REQUIRE(ShaderManager::Get().Load(
        "batch_lit", (shaderRoot / "batch_lit.vert").string(),
        (shaderRoot / "batch_lit.frag").string()) != nullptr);
    REQUIRE(ShaderManager::Get().Load(
        "shadow_mask_2d", (shaderRoot / "shadow_mask_2d.vert").string(),
        (shaderRoot / "shadow_mask_2d.frag").string()) != nullptr);

    Renderer renderer;
    renderer.Init();
    molga::RenderSystem2D::Get().Init();
    {
        std::vector<std::shared_ptr<GameObject>> objects;
        const auto outputCamera = AddOutputCamera(
            objects, "Shadow Camera", CameraOutputRole::Primary,
            {0.0f, 0.0f, 1.0f, 1.0f}, 0, Color::Black());
        outputCamera.camera->SetLightingEnabled(true);
        outputCamera.camera->SetAmbientIntensity(0.0f);

        auto lightObject = std::make_shared<GameObject>("Shadow Light");
        lightObject->AddComponent<Transform>(1.5f, 2.0f);
        PointLight2D* light = lightObject->AddComponent<PointLight2D>();
        REQUIRE(light->SetIntensity(1.0f));
        REQUIRE(light->SetRadius(100.0f));
        REQUIRE(light->SetHeight(32.0f));
        REQUIRE(light->SetFalloff(1.0f));
        light->SetCastsShadows(true);
        objects.push_back(lightObject);

        auto receiverObject = std::make_shared<GameObject>("Large Receiver");
        receiverObject->AddComponent<Transform>(0.0f, 0.0f);
        SpriteRenderer* receiver =
            receiverObject->AddComponent<SpriteRenderer>();
        receiver->SetSize(8.0f, 4.0f);
        receiver->SetColor(Color::White());
        receiver->SetLightingMode(SpriteLightingMode2D::Lit);
        objects.push_back(receiverObject);

        auto occluderObject = std::make_shared<GameObject>("Occluder");
        occluderObject->AddComponent<Transform>(3.0f, 2.0f);
        ShadowOccluder2D* occluder =
            occluderObject->AddComponent<ShadowOccluder2D>();
        REQUIRE(occluder->SetBox(Vector2::Zero(), {1.0f, 2.0f}));
        objects.push_back(occluderObject);

        molga::GameOutputRenderer output;
        Framebuffer target;
        REQUIRE(target.Init(8, 4));
        const auto renderAndCheck = [&]() {
            target.Bind();
            const auto result = output.Render(
                objects,
                {{8, 4}, {8, 4}, molga::GameOutputScaleMode::Native},
                renderer, defaultShader);
            REQUIRE(result.presented);
            REQUIRE(result.lightingApplied);
            CHECK_FALSE(result.lightingFallback);
            CHECK_FALSE(result.shadowFallback);
            CHECK(result.selectedLightCount == 1);
            CHECK(result.shadowedLightCount == 1);
            CHECK(result.shadowCasterDrawCount == 1);
            CHECK(result.lightingPasses == 1);
            CHECK(result.shadowPasses == 1);
            const auto visible = ReadTopLeftPixel(target.Id(), 4, 1, 1);
            const auto shadowed = ReadTopLeftPixel(target.Id(), 4, 6, 1);
            CHECK(visible[0] > 220);
            CHECK(visible[1] > 220);
            CHECK(visible[2] > 220);
            CHECK(shadowed[0] < 8);
            CHECK(shadowed[1] < 8);
            CHECK(shadowed[2] < 8);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, target.Id());
            target.Unbind();
        };

        renderAndCheck();
        REQUIRE(occluder->SetPolygon({
            {-0.5f, -1.0f}, {0.5f, 0.0f}, {-0.5f, 1.0f}}));
        renderAndCheck();
    }

    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
}

TEST_CASE("Shadow-mask preparation restores GL state and releases its cache") {
    SharedGlContext& context = SharedGlContext::Get();
    REQUIRE(context.Ready());
    context.MakeCurrent();
    ShaderManager::Get().Shutdown();

    const std::filesystem::path shaderRoot(MOLGA_TEST_SHADER_DIR);
    Shader* stateShader = ShaderManager::Get().Load(
        "lighting-state-sentinel", (shaderRoot / "default.vert").string(),
        (shaderRoot / "default.frag").string());
    REQUIRE(stateShader != nullptr);
    REQUIRE(stateShader->IsValid());
    Shader* shadowShader = ShaderManager::Get().Load(
        "shadow_mask_2d", (shaderRoot / "shadow_mask_2d.vert").string(),
        (shaderRoot / "shadow_mask_2d.frag").string());
    REQUIRE(shadowShader != nullptr);
    REQUIRE(shadowShader->IsValid());

    Framebuffer entryDraw;
    REQUIRE(entryDraw.Init(3, 2));
    GLuint entryRead = 0;
    GLuint sentinelVao = 0;
    GLuint sentinelArrayBuffer = 0;
    GLuint sentinelElementBuffer = 0;
    std::array<GLuint, 3> sentinel2D{};
    std::array<GLuint, 3> sentinelArrays{};
    glGenFramebuffers(1, &entryRead);
    glGenVertexArrays(1, &sentinelVao);
    glGenBuffers(1, &sentinelArrayBuffer);
    glGenBuffers(1, &sentinelElementBuffer);
    glGenTextures(static_cast<GLsizei>(sentinel2D.size()),
                  sentinel2D.data());
    glGenTextures(static_cast<GLsizei>(sentinelArrays.size()),
                  sentinelArrays.data());

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, entryDraw.Id());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, entryRead);
    const std::array<GLint, 4> viewport{2, 1, 3, 1};
    const std::array<GLint, 4> scissor{1, 0, 2, 2};
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glEnable(GL_SCISSOR_TEST);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_STENCIL_TEST);
    glEnable(GL_CULL_FACE);
    glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
                        GL_ZERO, GL_ONE);
    glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_MAX);
    glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glStencilMaskSeparate(GL_FRONT, 0x12u);
    glStencilMaskSeparate(GL_BACK, 0x34u);
    glClearColor(0.2f, 0.3f, 0.4f, 0.5f);
    glClearDepth(0.25);
    glClearStencil(7);
    stateShader->Use();
    glBindVertexArray(sentinelVao);
    glBindBuffer(GL_ARRAY_BUFFER, sentinelArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sentinelElementBuffer);
    for (int unit = 0; unit < 3; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D,
                      sentinel2D[static_cast<std::size_t>(unit)]);
        glBindTexture(GL_TEXTURE_2D_ARRAY,
                      sentinelArrays[static_cast<std::size_t>(unit)]);
    }
    glActiveTexture(GL_TEXTURE5);

    molga::LightingFrame2D frame;
    frame.lightingEnabled = true;
    frame.camera.viewportSize = {4, 4};
    molga::ShadowMaskLayerFrame2D shadowLayer;
    shadowLayer.layer = 0;
    shadowLayer.fullCover = true;
    shadowLayer.selectedOccluderCount = 1;
    frame.shadowLayers.push_back(shadowLayer);
    Camera2D camera(4.0f, 4.0f);

    GLuint cachedShadowTexture = 0;
    {
        molga::LightingPipeline2D pipeline;
        molga::LightingPipelinePrepareResult2D first;
        REQUIRE(pipeline.Prepare(frame, camera, first));
        REQUIRE(first.ready);
        CHECK_FALSE(first.shadowFallback);
        CHECK(first.shadowedLightCount == 1);
        CHECK(first.shadowCasterDrawCount == 0);
        CHECK(first.shadowPasses == 1);
        CHECK((pipeline.PreparedSize() == molga::PixelSize{4, 4}));
        cachedShadowTexture = pipeline.ShadowTextureArray();
        REQUIRE(cachedShadowTexture != 0);

        CheckGlState(entryDraw.Id(), entryRead, viewport, scissor,
                     GL_TRUE, GL_FALSE);
        CHECK(glIsEnabled(GL_BLEND) == GL_TRUE);
        CHECK(glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
        CHECK(glIsEnabled(GL_STENCIL_TEST) == GL_TRUE);
        CHECK(glIsEnabled(GL_CULL_FACE) == GL_TRUE);

        GLint value = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &value);
        CHECK(value == static_cast<GLint>(stateShader->GetID()));
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &value);
        CHECK(value == static_cast<GLint>(sentinelVao));
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &value);
        CHECK(value == static_cast<GLint>(sentinelArrayBuffer));
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &value);
        CHECK(value == static_cast<GLint>(sentinelElementBuffer));
        glGetIntegerv(GL_ACTIVE_TEXTURE, &value);
        CHECK(value == GL_TEXTURE5);
        for (int unit = 0; unit < 3; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &value);
            CHECK(value == static_cast<GLint>(
                sentinel2D[static_cast<std::size_t>(unit)]));
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &value);
            CHECK(value == static_cast<GLint>(
                sentinelArrays[static_cast<std::size_t>(unit)]));
        }
        glActiveTexture(GL_TEXTURE5);

        glGetIntegerv(GL_BLEND_SRC_RGB, &value);
        CHECK(value == GL_DST_ALPHA);
        glGetIntegerv(GL_BLEND_DST_RGB, &value);
        CHECK(value == GL_ONE_MINUS_DST_ALPHA);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &value);
        CHECK(value == GL_FUNC_REVERSE_SUBTRACT);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &value);
        CHECK(value == GL_MAX);
        GLboolean depthWrite = GL_TRUE;
        std::array<GLboolean, 4> colorWrite{};
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorWrite.data());
        CHECK(depthWrite == GL_FALSE);
        CHECK((colorWrite == std::array<GLboolean, 4>{
            GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE}));
        glGetIntegerv(GL_STENCIL_WRITEMASK, &value);
        CHECK(value == 0x12);
        glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &value);
        CHECK(value == 0x34);
        std::array<GLfloat, 4> clearColor{};
        GLdouble clearDepth = 1.0;
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor.data());
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
        CHECK(clearColor[0] == doctest::Approx(0.2f));
        CHECK(clearColor[1] == doctest::Approx(0.3f));
        CHECK(clearColor[2] == doctest::Approx(0.4f));
        CHECK(clearColor[3] == doctest::Approx(0.5f));
        CHECK(clearDepth == doctest::Approx(0.25));
        glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &value);
        CHECK(value == 7);

        // A same-size prepare reuses the camera-local texture-array cache.
        molga::LightingPipelinePrepareResult2D second;
        REQUIRE(pipeline.Prepare(frame, camera, second));
        REQUIRE(second.ready);
        CHECK_FALSE(second.shadowFallback);
        CHECK(pipeline.ShadowTextureArray() == cachedShadowTexture);
        CheckGlState(entryDraw.Id(), entryRead, viewport, scissor,
                     GL_TRUE, GL_FALSE);

        molga::LightingFrame2D invalidLayerFrame = frame;
        invalidLayerFrame.shadowLayers.front().layer =
            static_cast<int>(molga::kMaxShadowLights2D);
        molga::LightingPipelinePrepareResult2D invalidLayer;
        REQUIRE(pipeline.Prepare(invalidLayerFrame, camera, invalidLayer));
        REQUIRE(invalidLayer.ready);
        CHECK(invalidLayer.shadowFallback);
        CHECK(invalidLayer.shadowedLightCount == 0);
        CHECK(invalidLayer.shadowCasterDrawCount == 0);
        CHECK(invalidLayer.shadowPasses == 0);
        CheckGlState(entryDraw.Id(), entryRead, viewport, scissor,
                     GL_TRUE, GL_FALSE);
    }
    CHECK(glIsTexture(cachedShadowTexture) == GL_FALSE);

    glUseProgram(0);
    glBindVertexArray(sentinelVao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (int unit = 0; unit < 3; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMaskSeparate(GL_FRONT_AND_BACK, ~GLuint{0});
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0);
    glClearStencil(0);
    glDeleteTextures(static_cast<GLsizei>(sentinel2D.size()),
                     sentinel2D.data());
    glDeleteTextures(static_cast<GLsizei>(sentinelArrays.size()),
                     sentinelArrays.data());
    glDeleteBuffers(1, &sentinelElementBuffer);
    glDeleteBuffers(1, &sentinelArrayBuffer);
    glDeleteVertexArrays(1, &sentinelVao);
    glDeleteFramebuffers(1, &entryRead);
    ShaderManager::Get().Shutdown();
}
