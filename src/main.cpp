#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>
#include <memory>

#include "Core/Bootstrap.h"
#include "Rendering/Shader.h"
#include "Rendering/Renderer.h"
#include "Core/MolgaTime.h"
#include "Systems/Input.h"
#include "Rendering/Camera2D.h"
#include "Core/Scene.h"
#include "Systems/Audio.h"
#include "Editor/ImGuiLayer.h"
#include "Editor/EditorState.h"
#include "Editor/Editor.h"
#include "Editor/Windows/ProjectWindow.h"
#include "Editor/Project.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/BoxCollider2D.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/BuiltinScripts.h"
#include "Scenes/MenuScene.h"
#include "Scenes/GameScene.h"
#include "Rendering/TextRenderer.h"
#include <imgui.h>

// Settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

GLFWwindow* g_window = nullptr;

int main(int argc, char* argv[]) {
    // Check for project path argument
    std::string projectPath;
    if (argc > 1) {
        projectPath = argv[1];
    }

    WindowConfig wc;
    wc.title = "Molga Engine";
    wc.width = SCR_WIDTH;
    wc.height = SCR_HEIGHT;
    GLFWwindow* window = EngineInit(wc);
    if (!window) return -1;
    g_window = window;

    ImGuiLayer::Init(window);

    // Initialize resources (local to main)
    auto renderer = std::make_unique<Renderer>();
    renderer->Init();
    auto shader = std::make_unique<Shader>("Shaders/default.vert", "Shaders/default.frag");
    auto camera = std::make_unique<Camera2D>(static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));
    std::vector<std::shared_ptr<GameObject>> editorObjects;

    // Initialize Scripting
    RegisterBuiltinScripts();

    // Initialize Text Renderer
    TextRenderer::Get().Init();

    // Initialize Editor
    Editor::Get().Init();
    Editor::Get().SetGameObjects(&editorObjects);

    // Project loading phase
    bool projectLoaded = false;
    ProjectWindow projectWindow;

    // If project path provided via command line, try to open it
    if (!projectPath.empty()) {
        if (Project::Get().Open(projectPath)) {
            projectLoaded = true;
            std::cout << "[Main] Opened project from command line: " << projectPath << std::endl;
        } else {
            std::cerr << "[Main] Failed to open project: " << projectPath << std::endl;
        }
    }

    // Project selection loop (if no project loaded yet)
    while (!glfwWindowShouldClose(window) && !projectLoaded) {
        glfwPollEvents();

        // Clear first, then draw ImGui
        renderer->Clear(0.1f, 0.1f, 0.12f, 1.0f);

        ImGuiLayer::BeginFrame();
        projectWindow.OnGUI();
        ImGuiLayer::EndFrame();

        // Check if project was selected
        if (projectWindow.HasProjectSelected()) {
            projectLoaded = true;
            std::cout << "[Main] Project selected: " << projectWindow.GetSelectedProjectPath() << std::endl;
        }

        glfwSwapBuffers(window);
    }

    // If window was not closed during project selection, run editor
    if (!glfwWindowShouldClose(window)) {
        // Create sample GameObjects for editor demo
        {
            auto player = std::make_shared<GameObject>("Player");
            auto transform = player->AddComponent<Transform>();
            transform->SetPosition(100.0f, 100.0f);
            auto spriteRenderer = player->AddComponent<SpriteRenderer>();
            spriteRenderer->SetColor(0.2f, 0.8f, 0.3f, 1.0f);
            spriteRenderer->SetSize(32.0f, 32.0f);
            player->AddComponent<BoxCollider2D>();
            editorObjects.push_back(player);

            auto enemy = std::make_shared<GameObject>("Enemy");
            transform = enemy->AddComponent<Transform>();
            transform->SetPosition(300.0f, 200.0f);
            spriteRenderer = enemy->AddComponent<SpriteRenderer>();
            spriteRenderer->SetColor(0.8f, 0.2f, 0.2f, 1.0f);
            spriteRenderer->SetSize(32.0f, 32.0f);
            enemy->AddComponent<BoxCollider2D>();
            editorObjects.push_back(enemy);

            auto ground = std::make_shared<GameObject>("Ground");
            transform = ground->AddComponent<Transform>();
            transform->SetPosition(0.0f, 500.0f);
            spriteRenderer = ground->AddComponent<SpriteRenderer>();
            spriteRenderer->SetColor(0.4f, 0.4f, 0.5f, 1.0f);
            spriteRenderer->SetSize(800.0f, 100.0f);
            ground->AddComponent<BoxCollider2D>();
            editorObjects.push_back(ground);
        }

        // Create scenes
        SceneManager::AddScene("Menu", std::make_shared<MenuScene>());
        SceneManager::AddScene("Game", std::make_shared<GameScene>());
        SceneManager::ChangeScene("Menu");

        // Main editor loop
        while (!glfwWindowShouldClose(window)) {
            Time::Update();
            Input::Update();
            float dt = Time::GetDeltaTime();

            // Get editor state
            EditorState& editorState = EditorState::Get();

            // Update title with project name and mode indicator
            std::ostringstream title;
            title << "Molga Engine";
            if (Project::Get().IsOpen()) {
                title << " - " << Project::Get().GetName();
            }
            title << " | FPS: " << static_cast<int>(Time::GetFPS())
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
                for (auto& obj : editorObjects) {
                    if (obj && obj->IsActive()) {
                        obj->Update(scaledDt);
                    }
                }
            }

            // Render based on editor mode
            if (editorState.IsEditMode()) {
                // Edit mode: Render editor scene
                renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);
                renderer->Begin(shader.get(), camera.get());
                for (auto& obj : editorObjects) {
                    if (obj && obj->IsActive()) {
                        auto sr = obj->GetComponent<SpriteRenderer>();
                        if (sr) {
                            sr->RenderSprite(renderer.get(), shader.get(), camera.get());
                        }
                    }
                }
                renderer->End();
            } else {
                // Play/Pause mode: Render game scene
                SceneManager::Render(renderer.get(), shader.get(), camera.get());
            }

            // ImGui Editor UI
            ImGuiLayer::BeginFrame();
            Editor::Get().Update(dt);
            Editor::Get().RenderGUI();
            ImGuiLayer::EndFrame();

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }

    // Cleanup (deterministic order; unique_ptrs auto-release)
    Project::Get().Close();
    Editor::Get().Shutdown();
    editorObjects.clear();
    ImGuiLayer::Shutdown();
    SceneManager::Clear();
    TextRenderer::Get().Shutdown();
    camera.reset();
    shader.reset();
    renderer.reset();
    EngineShutdown();
    return 0;
}
