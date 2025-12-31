#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>
#include <memory>

#include "Shader.h"
#include "Renderer.h"
#include "Time.h"
#include "Input.h"
#include "Camera2D.h"
#include "Scene.h"
#include "Audio.h"
#include "Editor/ImGuiLayer.h"
#include "Editor/EditorState.h"
#include "Editor/Editor.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/BoxCollider2D.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/BuiltinScripts.h"
#include "Scenes/MenuScene.h"
#include "Scenes/GameScene.h"
#include "TextRenderer.h"
#include <imgui.h>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// Settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// Global resources (shared between scenes)
Renderer* g_renderer = nullptr;
Shader* g_shader = nullptr;
Camera2D* g_camera = nullptr;
GLFWwindow* g_window = nullptr;

// Editor scene objects
std::vector<std::shared_ptr<GameObject>> g_editorObjects;

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Molga Engine", nullptr, nullptr);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    g_window = window;  // Store global reference for quit functionality
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize systems
    Time::Init();
    Input::Init(window);
    Audio::Init();
    ImGuiLayer::Init(window);

    // Initialize global resources
    g_renderer = new Renderer();
    g_renderer->Init();
    g_shader = new Shader("src/Shaders/default.vert", "src/Shaders/default.frag");
    g_camera = new Camera2D(static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));

    // Initialize Scripting
    RegisterBuiltinScripts();

    // Initialize Text Renderer
    TextRenderer::Get().Init();

    // Initialize Editor
    Editor::Get().Init();
    Editor::Get().SetGameObjects(&g_editorObjects);

    // Create sample GameObjects for editor demo
    {
        auto player = std::make_shared<GameObject>("Player");
        auto transform = player->AddComponent<Transform>();
        transform->SetPosition(100.0f, 100.0f);
        auto spriteRenderer = player->AddComponent<SpriteRenderer>();
        spriteRenderer->SetColor(0.2f, 0.8f, 0.3f, 1.0f);
        spriteRenderer->SetSize(32.0f, 32.0f);
        player->AddComponent<BoxCollider2D>();
        g_editorObjects.push_back(player);

        auto enemy = std::make_shared<GameObject>("Enemy");
        transform = enemy->AddComponent<Transform>();
        transform->SetPosition(300.0f, 200.0f);
        spriteRenderer = enemy->AddComponent<SpriteRenderer>();
        spriteRenderer->SetColor(0.8f, 0.2f, 0.2f, 1.0f);
        spriteRenderer->SetSize(32.0f, 32.0f);
        enemy->AddComponent<BoxCollider2D>();
        g_editorObjects.push_back(enemy);

        auto ground = std::make_shared<GameObject>("Ground");
        transform = ground->AddComponent<Transform>();
        transform->SetPosition(0.0f, 500.0f);
        spriteRenderer = ground->AddComponent<SpriteRenderer>();
        spriteRenderer->SetColor(0.4f, 0.4f, 0.5f, 1.0f);
        spriteRenderer->SetSize(800.0f, 100.0f);
        ground->AddComponent<BoxCollider2D>();
        g_editorObjects.push_back(ground);
    }

    // Create scenes
    SceneManager::AddScene("Menu", std::make_shared<MenuScene>());
    SceneManager::AddScene("Game", std::make_shared<GameScene>());
    SceneManager::ChangeScene("Menu");

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();

        // Get editor state
        EditorState& editorState = EditorState::Get();

        // Update title with mode indicator
        std::ostringstream title;
        title << "Molga Engine - FPS: " << static_cast<int>(Time::GetFPS())
              << " | Scene: " << SceneManager::GetCurrentSceneName();
        if (editorState.IsEditMode()) {
            title << " [EDIT]";
        } else if (editorState.IsPlayMode()) {
            title << " [PLAYING]";
        } else if (editorState.IsPaused()) {
            title << " [PAUSED]";
        }
        glfwSetWindowTitle(window, title.str().c_str());

        // Only update scene and game objects in Play mode
        if (editorState.IsPlayMode()) {
            float scaledDt = dt * editorState.GetTimeScale();
            SceneManager::Update(scaledDt);

            // Update ECS GameObjects scripts
            for (auto& obj : g_editorObjects) {
                if (obj && obj->IsActive()) {
                    obj->Update(scaledDt);
                }
            }
        }

        // Always render (even in Edit mode)
        SceneManager::Render(g_renderer, g_shader, g_camera);

        // Render ECS GameObjects
        g_renderer->Begin(g_shader, g_camera);
        for (auto& obj : g_editorObjects) {
            if (obj && obj->IsActive()) {
                auto sr = obj->GetComponent<SpriteRenderer>();
                if (sr) {
                    sr->RenderSprite(g_renderer, g_shader, g_camera);
                }
            }
        }
        g_renderer->End();

        // ImGui Editor UI
        ImGuiLayer::BeginFrame();
        Editor::Get().Update(dt);
        Editor::Get().RenderGUI();
        ImGuiLayer::EndFrame();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    Editor::Get().Shutdown();
    g_editorObjects.clear();
    ImGuiLayer::Shutdown();
    SceneManager::Clear();
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
