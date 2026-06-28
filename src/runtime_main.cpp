// Molga Engine Runtime - Standalone game player without editor
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <algorithm>

#include "Core/Bootstrap.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Core/Profiling/ProfileScope.h"
#include "Core/MolgaTime.h"
#include "Systems/Input.h"
#include "Core/World.h"
#include "Rendering/Camera2D.h"
#include "Systems/Audio.h"
#include "Rendering/TextRenderer.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/TilemapRenderer.h"
#include "ECS/Components/MarrowRenderer.h"
#include "ECS/Components/ParticleSystem.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/TextRenderer2D.h"
#include "Core/SceneSerializer.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/BuiltinScripts.h"
#include "Rendering/RenderPass.h"
#include "Core/PathService.h"
#include "Core/SmokeReport.h"
#include "Core/EventBus.h"
#include "Core/ProjectSettings.h"
#include "Core/GameConfig.h"
#include "Scripting/ScriptPackageLoader.h"
#include <nlohmann/json.hpp>
#include <optional>

GLFWwindow* g_window = nullptr;

namespace {

struct RuntimeSmokeOptions {
    bool enabled = false;
    int frames = 3;
    std::filesystem::path reportPath;
};

std::optional<RuntimeSmokeOptions> ParseRuntimeSmoke(int argc, char** argv) {
    RuntimeSmokeOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--smoke") {
            options.enabled = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            try {
                options.frames = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        } else if (arg == "--report" && i + 1 < argc) {
            options.reportPath = argv[++i];
        } else {
            return std::nullopt;
        }
    }
    if (options.enabled &&
        (options.frames < 1 || options.reportPath.empty())) {
        return std::nullopt;
    }
    return options;
}

bool AllSpriteAssetsResolved(const World& world) {
    for (const auto& object : world.Objects()) {
        const auto* sprite = object->GetComponent<SpriteRenderer>();
        if (sprite != nullptr &&
            !sprite->GetTexturePath().empty() &&
            sprite->GetTexture() == nullptr) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);

    const auto smoke = ParseRuntimeSmoke(argc, argv);
    if (!smoke) {
        std::cerr << "Usage: runtime [--smoke --frames N --report PATH]\n";
        return 2;
    }

    // Load game configuration
    GameConfig config;
    Input::InitializeDefaultActions();
    std::string configPath = (PathService::Get().ExecutableDir() / "game.json").string();
    if (!LoadGameConfig(configPath, config)) {
        std::cerr << "Packaged game config is required: " << configPath << std::endl;
        if (smoke->enabled) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = "Missing or invalid game.json";
            report.Save(smoke->reportPath);
        }
        return 4;
    }

    // Validate required package directories
    const auto exeDir = PathService::Get().ExecutableDir();
    for (const auto& required : { "Assets", "Scenes", "Shaders" }) {
        if (!std::filesystem::exists(exeDir / required)) {
            std::cerr << "Missing package directory: " << (exeDir / required) << std::endl;
            if (smoke->enabled) {
                SmokeReport report;
                report.executable = "molga_runtime";
                report.status = "error";
                report.message = std::string("Missing package directory: ") + required;
                report.Save(smoke->reportPath);
            }
            return 4;
        }
    }

    WindowConfig wc;
    wc.title = config.gameName;
    wc.width = config.windowWidth;
    wc.height = config.windowHeight;
    wc.fullscreen = config.fullscreen;
    wc.visible = !smoke->enabled;
    GLFWwindow* window = EngineInit(wc);
    if (!window) return -1;
    g_window = window;

    // Initialize renderer
    auto renderer = std::make_unique<Renderer>();
    renderer->Init();
    molga::RenderSystem2D::Get().Init();
    auto vertPath = PathService::Get().EngineResource("Shaders/default.vert").string();
    auto fragPath = PathService::Get().EngineResource("Shaders/default.frag").string();
    Shader* shader = ShaderManager::Get().Load("default", vertPath, fragPath);

    auto batchVertPath = PathService::Get().EngineResource("Shaders/batch.vert").string();
    auto batchFragPath = PathService::Get().EngineResource("Shaders/batch.frag").string();
    ShaderManager::Get().Load("batch", batchVertPath, batchFragPath);
    auto camera = std::make_unique<Camera2D>(static_cast<float>(config.windowWidth),
                                             static_cast<float>(config.windowHeight));
    World world;

    // Initialize scripting
    RegisterBuiltinScripts();

    // Load and validate user script package
    std::string loaderError;
    std::string smokeReportPathStr = smoke->reportPath.string();
    if (!ScriptPackageLoader::Load(config, smoke->enabled, smokeReportPathStr, loaderError)) {
        std::cerr << "Script package initialization failed: " << loaderError << std::endl;
        renderer.reset();
        EngineShutdown();
        return 4;
    }

    // Initialize text renderer
    TextRenderer::Get().Init();

    // Load main scene
    std::string scenePath = (PathService::Get().ExecutableDir() / config.mainScene).string();
    std::cout << "Loading scene: " << scenePath << std::endl;
    if (!world.LoadFromFile(scenePath)) {
        std::cerr << "Failed to load main scene!" << std::endl;
        if (smoke->enabled) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = "Failed to load main scene";
            report.Save(smoke->reportPath);
            return 4;
        }
    }

    PathService::Get().SetAssetRoot(PathService::Get().ExecutableDir());
    world.ResolveAssets();

    world.StartPending();

    std::cout << "Loaded " << world.Objects().size() << " game objects" << std::endl;

    int renderedFrames = 0;
    // Main game loop
    while (!glfwWindowShouldClose(window)) {
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();

        // Fixed Update loop
        Time::AccumulateFixedTime(dt);
        while (Time::HasPendingFixedStep()) {
            world.FixedStep(Time::GetFixedDeltaTime());
            Time::ConsumeFixedStep();
        }

        // Update all game objects
        world.Update(dt);
        world.LateUpdate(dt);
        world.FlushDeferred(dt);

        // Collect render commands
        molga::RenderQueue queue;
        {
            MOLGA_PROFILE_SCOPE("RenderQueue.Collect", molga::ProfileCategory::Rendering);
            for (auto& obj : world.Objects()) {
                if (obj && obj->IsActive()) {
                    for (auto* comp : obj->GetComponents()) {
                        if (comp && comp->IsEnabled()) {
                            comp->CollectRender(queue);
                        }
                    }
                }
            }
        }

        Camera* mainCam = nullptr;
        for (const auto& obj : world.Objects()) {
            if (obj && obj->IsActive()) {
                if (auto cam = obj->GetComponent<Camera>()) {
                    if (cam->IsEnabled() && cam->IsMain()) {
                        if (!mainCam || cam->GetDepth() > mainCam->GetDepth()) {
                            mainCam = cam;
                        }
                    }
                }
            }
        }

        if (mainCam) {
            Color clearColor = mainCam->GetBackgroundColor();
            renderer->Clear(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
            {
                molga::RenderPass pass(*renderer, shader, mainCam->GetCamera2D());
                molga::RenderSystem2D::Get().Render(queue, renderer.get(), mainCam->GetCamera2D());
            }
        } else {
            renderer->Clear(0.1f, 0.1f, 0.15f, 1.0f);
            {
                molga::RenderPass pass(*renderer, shader, camera.get());
                molga::RenderSystem2D::Get().Render(queue, renderer.get(), camera.get());
            }
        }

        EventBus::ProcessQueue();

        glfwSwapBuffers(window);
        glfwPollEvents();

        // ESC to quit
        if (Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, true);
        }

        ++renderedFrames;
        if (smoke->enabled && renderedFrames >= smoke->frames) {
            break;
        }
    }

    int exitCode = 0;
    if (smoke->enabled) {
        int scriptComponentCount = 0;
        for (const auto& obj : world.Objects()) {
            if (!obj) continue;
            for (auto* comp : obj->GetComponents()) {
                if (comp && ScriptManager::Get().IsDynamicScript(comp->GetTypeName())) {
                    scriptComponentCount++;
                }
            }
        }

        std::string scriptStatus = config.scripts.enabled ? "loaded" : "none";

        SmokeReport report;
        report.executable = "molga_runtime";
        report.status = AllSpriteAssetsResolved(world) ? "ok" : "error";
        report.scenePath = config.mainScene;
        report.objectCount = world.Objects().size();
        report.frames = renderedFrames;
        report.assetsResolved = AllSpriteAssetsResolved(world);
        if (renderer) {
            auto& stats = renderer->Stats();
            report.drawCalls = stats.drawCalls;
            report.batches = stats.batches;
            report.textureBinds = stats.textureBinds;
            report.shaderSwitches = stats.shaderSwitches;
            report.submittedSprites = stats.submittedSprites;
            report.submittedCommands = stats.submittedCommands;
            report.batchFlushes = stats.batchFlushes;
            report.batchBreaks = stats.batchBreaks;
            report.maxSpritesPerBatch = stats.maxSpritesPerBatch;
            report.verticesUploadedBytes = stats.verticesUploadedBytes;
            report.queueSortNanos = stats.queueSortNanos;
        }
        if (report.assetsResolved) {
            report.message = "Runtime smoke completed. Scripts: " + scriptStatus + 
                             ", UserScriptComponents: " + std::to_string(scriptComponentCount);
        } else {
            report.message = "One or more sprite assets failed to resolve. Scripts: " + scriptStatus + 
                             ", UserScriptComponents: " + std::to_string(scriptComponentCount);
        }
        report.Save(smoke->reportPath);
        exitCode = report.assetsResolved ? 0 : 4;
    }

    // Cleanup (unique_ptrs auto-release; explicit reset for deterministic order)
    world.Clear();
    TextRenderer::Get().Shutdown();
    camera.reset();
    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    renderer.reset();
    EngineShutdown();

    return exitCode;
}
