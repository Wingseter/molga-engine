#include "Core/ProjectSettings.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Rendering/ShaderManager.h"
#include "ECS/Component.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/GameObject.h"
#include "doctest.h"

#include <array>
#include <filesystem>
#include <GLFW/glfw3.h>
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

    ~SharedGlContext() {
        if (window_) glfwDestroyWindow(window_);
        if (initialized_) glfwTerminate();
    }

    bool Ready() const { return ready_; }
    void MakeCurrent() const {
        if (window_) glfwMakeContextCurrent(window_);
    }

private:
    SharedGlContext() {
        if (!glfwInit()) return;
        initialized_ = true;
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        window_ = glfwCreateWindow(64, 64, "framebuffer-test", nullptr, nullptr);
        if (!window_) return;
        glfwMakeContextCurrent(window_);
        ready_ = gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0;
    }

    GLFWwindow* window_ = nullptr;
    bool initialized_ = false;
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
    if (!context.Ready()) {
        MESSAGE("GLFW unavailable; skipping GL-context framebuffer test");
        return;
    }
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

TEST_CASE("IntegerFit presents nearest pixels, black bars and crop while restoring GL state") {
    SharedGlContext& context = SharedGlContext::Get();
    if (!context.Ready()) {
        MESSAGE("GLFW unavailable; skipping GL-context output presentation test");
        return;
    }
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
