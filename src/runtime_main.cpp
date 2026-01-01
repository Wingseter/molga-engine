// Molga Engine Runtime - Standalone game player without editor
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>

#include "Shader.h"
#include "Renderer.h"
#include "Time.h"
#include "Input.h"
#include "Camera2D.h"
#include "Audio.h"
#include "TextRenderer.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/BoxCollider2D.h"
#include "Core/SceneSerializer.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/BuiltinScripts.h"
#include <nlohmann/json.hpp>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// Game configuration
struct GameConfig {
    std::string gameName = "Molga Game";
    std::string mainScene = "scenes/main.json";
    int windowWidth = 800;
    int windowHeight = 600;
    bool fullscreen = false;
};

// Global resources
Renderer* g_renderer = nullptr;
Shader* g_shader = nullptr;
Camera2D* g_camera = nullptr;
GLFWwindow* g_window = nullptr;
std::vector<std::shared_ptr<GameObject>> g_gameObjects;

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

    // Initialize GLFW
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // Create window
    GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    GLFWwindow* window = glfwCreateWindow(config.windowWidth, config.windowHeight,
                                          config.gameName.c_str(), monitor, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    g_window = window;
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize systems
    Time::Init();
    Input::Init(window);
    Audio::Init();

    // Initialize renderer
    g_renderer = new Renderer();
    g_renderer->Init();
    g_shader = new Shader("Shaders/default.vert", "Shaders/default.frag");
    g_camera = new Camera2D(static_cast<float>(config.windowWidth),
                            static_cast<float>(config.windowHeight));

    // Initialize scripting
    RegisterBuiltinScripts();

    // Initialize text renderer
    TextRenderer::Get().Init();

    // Load main scene
    std::cout << "Loading scene: " << config.mainScene << std::endl;
    if (!SceneSerializer::LoadScene(config.mainScene, g_gameObjects)) {
        std::cerr << "Failed to load main scene!" << std::endl;
        // Continue anyway with empty scene
    }

    std::cout << "Loaded " << g_gameObjects.size() << " game objects" << std::endl;

    // Main game loop
    while (!glfwWindowShouldClose(window)) {
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();

        // Update all game objects
        for (auto& obj : g_gameObjects) {
            if (obj && obj->IsActive()) {
                obj->Update(dt);
            }
        }

        // Clear and render
        g_renderer->Clear(0.1f, 0.1f, 0.15f, 1.0f);

        // Render all game objects
        g_renderer->Begin(g_shader, g_camera);
        for (auto& obj : g_gameObjects) {
            if (obj && obj->IsActive()) {
                auto sr = obj->GetComponent<SpriteRenderer>();
                if (sr) {
                    sr->RenderSprite(g_renderer, g_shader, g_camera);
                }
            }
        }
        g_renderer->End();

        glfwSwapBuffers(window);
        glfwPollEvents();

        // ESC to quit
        if (Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, true);
        }
    }

    // Cleanup
    g_gameObjects.clear();
    TextRenderer::Get().Shutdown();
    delete g_camera;
    delete g_shader;
    delete g_renderer;
    Audio::Shutdown();
    glfwTerminate();

    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}
