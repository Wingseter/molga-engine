#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>
#include <memory>
#include <algorithm>

#include "Core/Bootstrap.h"
#include "Rendering/Shader.h"
#include "Rendering/Renderer.h"
#include "Core/MolgaTime.h"
#include "Systems/Input.h"
#include "Rendering/Camera2D.h"
// removed Core/Scene.h include
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
#include "Editor/SceneDocument.h"
#include "Rendering/TextRenderer.h"
#include "Rendering/RenderPass.h"
#include "Core/PathService.h"
#include <imgui.h>

// Settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

GLFWwindow* g_window = nullptr;

int main(int argc, char* argv[]) {
    PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);

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
    auto vertPath = PathService::Get().EngineResource("Shaders/default.vert").string();
    auto fragPath = PathService::Get().EngineResource("Shaders/default.frag").string();
    auto shader = std::make_unique<Shader>(vertPath.c_str(), fragPath.c_str());
    auto camera = std::make_unique<Camera2D>(static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));
    SceneDocument sceneDoc;

    // Initialize Scripting
    RegisterBuiltinScripts();

    // Initialize Text Renderer
    TextRenderer::Get().Init();

    // Initialize Editor
    Editor::Get().Init();
    Editor::Get().SetGameObjects(&sceneDoc.EditWorld().Objects());

    EditorState& editorState = EditorState::Get();
    editorState.SetPlayCallbacks(
        [&sceneDoc]() {  // Edit → Play
            Editor::Get().SetSelectedObject(nullptr);
            Editor::Get().GetCommandHistory().Clear();
            sceneDoc.EnterPlay();
            sceneDoc.ActiveWorld().ResolveAssets();
            Editor::Get().SetGameObjects(&sceneDoc.ActiveWorld().Objects());
        },
        [&sceneDoc]() {  // Play/Pause → Stop
            Editor::Get().SetSelectedObject(nullptr);
            Editor::Get().GetCommandHistory().Clear();
            Editor::Get().SetGameObjects(&sceneDoc.EditWorld().Objects());
            sceneDoc.ExitPlay();
        });

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

    if (projectLoaded && Project::Get().IsOpen()) {
        PathService::Get().SetAssetRoot(Project::Get().GetPath());
        sceneDoc.EditWorld().ResolveAssets();
    }

    // If window was not closed during project selection, run editor
    if (!glfwWindowShouldClose(window)) {
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
                  << " | Scene: " << sceneDoc.ActiveWorld().Name();
            if (editorState.IsEditMode()) {
                title << " [EDIT]";
            } else if (editorState.IsPlayMode()) {
                title << " [PLAYING]";
            } else if (editorState.IsPaused()) {
                title << " [PAUSED]";
            }
            glfwSetWindowTitle(window, title.str().c_str());

            // Play 모드에서만 ActiveWorld(=playWorld)를 시뮬레이션한다.
            if (editorState.IsPlayMode() && sceneDoc.IsPlaying()) {
                float scaledDt = dt * editorState.GetTimeScale();

                Time::AccumulateFixedTime(scaledDt);
                while (Time::HasPendingFixedStep()) {
                    sceneDoc.ActiveWorld().FixedStep(Time::GetFixedDeltaTime());
                    Time::ConsumeFixedStep();
                }
                sceneDoc.ActiveWorld().Update(scaledDt);
                sceneDoc.ActiveWorld().LateUpdate(scaledDt);
            }

            // sortingOrder 오름차순으로 그릴 스프라이트 수집
            std::vector<std::pair<int, SpriteRenderer*>> drawList;
            for (auto& obj : sceneDoc.ActiveWorld().Objects()) {
                if (obj && obj->IsActive()) {
                    if (auto sr = obj->GetComponent<SpriteRenderer>()) {
                        drawList.emplace_back(sr->GetSortingOrder(), sr);
                    }
                }
            }
            std::stable_sort(drawList.begin(), drawList.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);
            {
                molga::RenderPass pass(*renderer, shader.get(), camera.get());
                for (auto& [order, sr] : drawList) {
                    sr->RenderSprite(renderer.get());
                }
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
    ImGuiLayer::Shutdown();
    TextRenderer::Get().Shutdown();
    camera.reset();
    shader.reset();
    renderer.reset();
    EngineShutdown();
    return 0;
}
