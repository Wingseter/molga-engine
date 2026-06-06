// Molga Engine Runtime - Standalone game player without editor
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>

#include "Core/Bootstrap.h"
#include "Rendering/Shader.h"
#include "Rendering/Renderer.h"
#include "Core/MolgaTime.h"
#include "Systems/Input.h"
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
#include <nlohmann/json.hpp>

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

int main(int argc, char* argv[]) {
    // Load game configuration
    GameConfig config;
    if (!LoadGameConfig("game.json", config)) {
        std::cout << "Using default configuration" << std::endl;
    }

    WindowConfig wc;
    wc.title = config.gameName;
    wc.width = config.windowWidth;
    wc.height = config.windowHeight;
    wc.fullscreen = config.fullscreen;
    GLFWwindow* window = EngineInit(wc);
    if (!window) return -1;
    g_window = window;

    // Initialize renderer
    auto renderer = std::make_unique<Renderer>();
    renderer->Init();
    auto shader = std::make_unique<Shader>("Shaders/default.vert", "Shaders/default.frag");
    auto camera = std::make_unique<Camera2D>(static_cast<float>(config.windowWidth),
                                             static_cast<float>(config.windowHeight));
    std::vector<std::shared_ptr<GameObject>> gameObjects;

    // Initialize scripting
    RegisterBuiltinScripts();

    // Initialize text renderer
    TextRenderer::Get().Init();

    // Load main scene
    std::cout << "Loading scene: " << config.mainScene << std::endl;
    if (!SceneSerializer::LoadScene(config.mainScene, gameObjects)) {
        std::cerr << "Failed to load main scene!" << std::endl;
        // Continue anyway with empty scene
    }

    std::cout << "Loaded " << gameObjects.size() << " game objects" << std::endl;

    // Main game loop
    while (!glfwWindowShouldClose(window)) {
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();

        // Fixed Update loop
        Time::AccumulateFixedTime(dt);
        while (Time::HasPendingFixedStep()) {
            float fixedDt = Time::GetFixedDeltaTime();
            for (auto& obj : gameObjects) {
                if (obj && obj->IsActive()) {
                    obj->FixedUpdateScripts(fixedDt);
                }
            }
            Time::ConsumeFixedStep();
        }

        // Update all game objects
        for (auto& obj : gameObjects) {
            if (obj && obj->IsActive()) {
                obj->Update(dt);
            }
        }

        // Clear and render
        renderer->Clear(0.1f, 0.1f, 0.15f, 1.0f);

        // Render all game objects
        renderer->Begin(shader.get(), camera.get());
        for (auto& obj : gameObjects) {
            if (obj && obj->IsActive()) {
                auto sr = obj->GetComponent<SpriteRenderer>();
                if (sr) {
                    sr->RenderSprite(renderer.get(), shader.get(), camera.get());
                }
            }
        }
        renderer->End();

        glfwSwapBuffers(window);
        glfwPollEvents();

        // ESC to quit
        if (Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, true);
        }
    }

    // Cleanup (unique_ptrs auto-release; explicit reset for deterministic order)
    gameObjects.clear();
    TextRenderer::Get().Shutdown();
    camera.reset();
    shader.reset();
    renderer.reset();
    EngineShutdown();

    return 0;
}
