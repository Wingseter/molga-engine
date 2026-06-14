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
#include "Rendering/Renderer.h"
#include "Core/MolgaTime.h"
#include "Systems/Input.h"
#include "Core/World.h"
#include "Rendering/Camera2D.h"
#include "Systems/Audio.h"
#include "Rendering/TextRenderer.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/BoxCollider2D.h"
#include "Core/SceneSerializer.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/BuiltinScripts.h"
#include "Rendering/RenderPass.h"
#include "Core/PathService.h"
#include "Core/SmokeReport.h"
#include <nlohmann/json.hpp>
#include <optional>

// Game configuration
struct GameConfig {
    std::string gameName = "Molga Game";
    std::string mainScene = "scenes/main.json";
    int windowWidth = 800;
    int windowHeight = 600;
    bool fullscreen = false;
};

GLFWwindow* g_window = nullptr;

bool LoadGameConfig(const std::string& path, GameConfig& config) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Could not open game config: " << path << std::endl;
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("gameName")) config.gameName = j["gameName"];
        if (j.contains("mainScene")) config.mainScene = j["mainScene"];
        if (j.contains("windowWidth")) config.windowWidth = j["windowWidth"];
        if (j.contains("windowHeight")) config.windowHeight = j["windowHeight"];
        if (j.contains("fullscreen")) config.fullscreen = j["fullscreen"];

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing game config: " << e.what() << std::endl;
        return false;
    }
}

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
    std::string configPath = (PathService::Get().ExecutableDir() / "game.json").string();
    if (!LoadGameConfig(configPath, config)) {
        std::cout << "Using default configuration" << std::endl;
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
    auto vertPath = PathService::Get().EngineResource("Shaders/default.vert").string();
    auto fragPath = PathService::Get().EngineResource("Shaders/default.frag").string();
    auto shader = std::make_unique<Shader>(vertPath.c_str(), fragPath.c_str());
    auto camera = std::make_unique<Camera2D>(static_cast<float>(config.windowWidth),
                                             static_cast<float>(config.windowHeight));
    World world;

    // Initialize scripting
    RegisterBuiltinScripts();

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

        // sortingOrder 오름차순으로 그릴 스프라이트 수집
        std::vector<std::pair<int, SpriteRenderer*>> drawList;
        for (auto& obj : world.Objects()) {
            if (obj && obj->IsActive()) {
                if (auto sr = obj->GetComponent<SpriteRenderer>()) {
                    drawList.emplace_back(sr->GetSortingOrder(), sr);
                }
            }
        }
        std::stable_sort(drawList.begin(), drawList.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        renderer->Clear(0.1f, 0.1f, 0.15f, 1.0f);
        {
            molga::RenderPass pass(*renderer, shader.get(), camera.get());
            for (auto& [order, sr] : drawList) {
                sr->RenderSprite(renderer.get());
            }
        }

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
        SmokeReport report;
        report.executable = "molga_runtime";
        report.status = AllSpriteAssetsResolved(world) ? "ok" : "error";
        report.scenePath = config.mainScene;
        report.objectCount = world.Objects().size();
        report.frames = renderedFrames;
        report.assetsResolved = AllSpriteAssetsResolved(world);
        report.message = report.assetsResolved
            ? "Runtime smoke completed"
            : "One or more sprite assets failed to resolve";
        report.Save(smoke->reportPath);
        exitCode = report.assetsResolved ? 0 : 4;
    }

    // Cleanup (unique_ptrs auto-release; explicit reset for deterministic order)
    world.Clear();
    TextRenderer::Get().Shutdown();
    camera.reset();
    shader.reset();
    renderer.reset();
    EngineShutdown();

    return exitCode;
}
