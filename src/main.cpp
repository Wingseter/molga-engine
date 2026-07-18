#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>
#include <memory>

#include "Core/AssetDatabase.h"
#include "Editor/Watcher/AssetWatcher.h"
#include "Core/Bootstrap.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderSystem2D.h"
#include "Core/MolgaTime.h"
#include "Systems/Input.h"
// removed Core/Scene.h include
#include "Systems/Audio.h"
#include "Editor/ImGuiLayer.h"
#include "Editor/EditorState.h"
#include "Editor/Editor.h"
#include "Core/Profiling/ProfileScope.h"
#include "Core/Profiling/ProfilerService.h"
#include "Editor/Windows/ProjectWindow.h"
#include "Editor/Project.h"
#include "ECS/BuiltinComponents.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/Camera.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/BuiltinScripts.h"
#include "Scripting/ScriptManager.h"
#include "Scripting/ScriptCompiler.h"
#include "Editor/SceneDocument.h"
#include "Rendering/TextRenderer.h"
#include "Core/PathService.h"
#include "Core/BuildPlan.h"
#include "Core/PrefabRegistry.h"
#include "Core/PersistentStorage.h"
#include "Core/PlayerPrefs.h"
#include "Core/SmokeReport.h"
#include "Core/EventBus.h"
#include "Editor/GameBuilder.h"
#include <imgui.h>
#include <optional>

// Settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

GLFWwindow* g_window = nullptr;
static molga::AssetWatcher g_AssetWatcher;

namespace {

struct EditorSceneCatalogData {
    SceneRuntime::SceneCatalog catalog;
    std::string currentSceneId;
    bool valid = false;
};

EditorSceneCatalogData BuildEditorSceneCatalog(const BuildProfile& profile,
                                                const std::filesystem::path& projectRoot,
                                                const std::string& currentScenePath) {
    EditorSceneCatalogData result;
    BuildPlan plan;
    std::string error;
    if (!BuildPlanBuilder::Build(profile, projectRoot.string(), profile.target,
                                 "", plan, error)) {
        Log::Error("SceneRuntime", "Could not build editor scene catalog: " + error);
        return result;
    }

    std::error_code pathError;
    const std::filesystem::path current = currentScenePath.empty()
        ? std::filesystem::path{}
        : std::filesystem::weakly_canonical(currentScenePath, pathError);
    for (const auto& entry : plan.sceneEntries) {
        result.catalog.emplace(entry.sceneId, entry.sourceAbsolutePath);
        if (!current.empty()) {
            std::error_code entryError;
            const auto candidate = std::filesystem::weakly_canonical(
                entry.sourceAbsolutePath, entryError);
            if (!entryError && candidate == current) result.currentSceneId = entry.sceneId;
        }
    }

    result.valid = !result.catalog.empty() && !result.currentSceneId.empty();
    if (!result.valid && !currentScenePath.empty()) {
        Log::Error("SceneRuntime",
                   "The active editor scene is not registered in the Build Profile: " +
                       currentScenePath);
    }
    return result;
}

struct SmokeBuildOptions {
    std::filesystem::path projectRoot;
    std::filesystem::path outputRoot;
    std::filesystem::path reportPath;
};

std::optional<SmokeBuildOptions> ParseSmokeBuild(int argc, char** argv) {
    if (argc != 5 || std::string_view(argv[1]) != "--smoke-build") {
        return std::nullopt;
    }
    return SmokeBuildOptions{argv[2], argv[3], argv[4]};
}

int RunSmokeBuild(const SmokeBuildOptions& options) {
    SmokeReport report;
    report.executable = "molga_engine";

    if (!Project::Get().Open(options.projectRoot.string())) {
        report.status = "error";
        report.message = "Could not open smoke project";
        report.Save(options.reportPath);
        return 3;
    }

    const BuildProfile& profile = Project::Get().GetBuildProfile();
    report.scenePath = profile.startupScene;

    // Headless builds must initialize the same project asset context as the
    // interactive editor before deserializing the startup scene. Otherwise
    // PrefabRegistry searches beside the editor executable and silently omits
    // otherwise valid project prefab instances from the smoke World.
    PathService::Get().SetAssetRoot(Project::Get().GetPath());
    molga::AssetDatabase::Get().ScanProject(Project::Get().GetAssetsPath());
    PrefabRegistry::Get().ScanAssets();

    // Set script compiler path and load script library if present
    ScriptCompiler::Get().SetProjectPath(options.projectRoot.string());
    std::string userLibPath = ScriptCompiler::Get().GetCompiledLibraryPath();
    if (!userLibPath.empty() && std::filesystem::exists(userLibPath)) {
        ScriptManager::Get().LoadScriptLibrary(userLibPath);
    }

    World world;
    std::filesystem::path p(profile.startupScene);
    const auto scenePath = p.is_absolute() ? p : options.projectRoot / p;
    if (!world.LoadFromFile(scenePath.string())) {
        report.status = "error";
        report.message = "Could not load smoke scene";
        report.Save(options.reportPath);
        return 3;
    }

    BuildSettings settings;
    settings.profile = Project::Get().GetBuildProfile();
    settings.projectRoot = Project::Get().GetPath();
    settings.profile.outputPath = options.outputRoot.string();

    if (!GameBuilder::Get().Build(settings)) {
        report.status = "error";
        report.message = GameBuilder::Get().GetLastError();
        report.Save(options.reportPath);
        return 3;
    }

    report.status = "ok";
    report.message = "Build completed";
    report.objectCount = world.Objects().size();
    report.Save(options.reportPath);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);
    RegisterBuiltinComponents();
    RegisterBuiltinScripts();

    if (argc > 1 && std::string_view(argv[1]) == "--smoke-build") {
        const auto options = ParseSmokeBuild(argc, argv);
        if (!options) {
            std::cerr
                << "Usage: molga_engine --smoke-build "
                << "<project-root> <output-root> <report-path>\n";
            return 2;
        }
        return RunSmokeBuild(*options);
    }

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
    molga::RenderSystem2D::Get().Init();
    auto vertPath = PathService::Get().EngineResource("Shaders/default.vert").string();
    auto fragPath = PathService::Get().EngineResource("Shaders/default.frag").string();
    Shader* shader = ShaderManager::Get().Load("default", vertPath, fragPath);

    auto batchVertPath = PathService::Get().EngineResource("Shaders/batch.vert").string();
    auto batchFragPath = PathService::Get().EngineResource("Shaders/batch.frag").string();
    ShaderManager::Get().Load("batch", batchVertPath, batchFragPath);
    SceneDocument sceneDoc;

    // Initialize Text Renderer
    TextRenderer::Get().Init();

    // Initialize Editor
    Editor::Get().Init();
    Editor::Get().SetGameObjects(&sceneDoc.EditWorld().Objects());
    // SceneView에 렌더 리소스 주입 (FBO 렌더 활성화)
    Editor::Get().SetSceneViewResources(renderer.get(), shader);

    EditorState& editorState = EditorState::Get();
    editorState.SetPlayCallbacks(
        [&sceneDoc]() -> bool {  // Edit → Play
            const auto catalog = BuildEditorSceneCatalog(
                Project::Get().GetBuildProfile(), Project::Get().GetPath(),
                Editor::Get().GetCurrentScenePath());
            if (!catalog.valid) {
                Log::Error("SceneRuntime",
                           "Could not enter Play mode without a registered active scene.");
                return false;
            }
            if (!sceneDoc.EnterPlay(catalog.catalog, catalog.currentSceneId)) {
                Log::Error("SceneRuntime", "Could not enter Play mode with a cloned scene World.");
                return false;
            }
            Editor::Get().GetCommandHistory().Clear();
            Editor::Get().ResetPlayUIInput();
            Editor::Get().SetGameObjects(&sceneDoc.ActiveWorld().Objects());
            
            World& pw = sceneDoc.ActiveWorld();
            Editor::Get().GetSelection().Rebind(
                [&pw](unsigned int id) { return pw.FindById(id) != nullptr; });
            return true;
        },
        [&sceneDoc]() {  // Play/Pause → Stop
            Editor::Get().GetCommandHistory().Clear();
            Editor::Get().ResetPlayUIInput();
            Editor::Get().SetGameObjects(&sceneDoc.EditWorld().Objects());
            sceneDoc.ExitPlay();
            
            World& ew = sceneDoc.EditWorld();
            Editor::Get().GetSelection().Rebind(
                [&ew](unsigned int id) { return ew.FindById(id) != nullptr; });
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
        namespace fs = std::filesystem;
        PathService::Get().SetAssetRoot(Project::Get().GetPath());

        molga::AssetDatabase::Get().ScanProject(Project::Get().GetAssetsPath());
        g_AssetWatcher.Prime(Project::Get().GetAssetsPath());

        const BuildProfile& profile = Project::Get().GetBuildProfile();
        if (!PersistentStorage::ConfigureEditor(Project::Get().GetPath(),
                                                profile.companyName,
                                                profile.gameName)) {
            Log::Error("PersistentStorage", "Could not configure editor Play storage.");
        }
        fs::path p(profile.startupScene);
        const fs::path mainScene = p.is_absolute() ? p : fs::path(Project::Get().GetPath()) / p;
        if (sceneDoc.Open(mainScene.string())) {
            sceneDoc.EditWorld().ResolveAssets();
            Editor::Get().SetGameObjects(&sceneDoc.EditWorld().Objects());
            Editor::Get().SetCurrentScenePath(mainScene.string());
            // 씬 로드 후 SceneView 리소스 재주입 (오브젝트 목록 갱신)
            Editor::Get().SetSceneViewResources(renderer.get(), shader);
            std::cout << "[Main] Loaded project startup scene: " << mainScene << std::endl;
        } else {
            std::cerr << "[Main] Project startup scene not found or invalid: "
                      << mainScene << std::endl;
        }
    }

    if (!glfwWindowShouldClose(window)) {
        // Main editor loop
        while (!glfwWindowShouldClose(window)) {
            Time::Update();
            if (EditorState::Get().IsEditMode()) Input::Update();
            else if (EditorState::Get().IsPaused()) Input::ReleaseAll();
            float dt = Time::GetDeltaTime();

            if (projectLoaded) {
                static float assetPollTimer = 0.0f;
                assetPollTimer += dt;
                if (assetPollTimer > 0.5f) {
                    auto ch = g_AssetWatcher.Poll(Project::Get().GetAssetsPath());
                    for (auto& a : ch.added)   molga::AssetDatabase::Get().OnSourceAdded(a);
                    for (auto& r : ch.removed) molga::AssetDatabase::Get().OnSourceRemoved(r);
                    for (auto& m : ch.modified) {
                        std::string g = molga::AssetDatabase::Get().GuidForSource(m);
                        if (!g.empty()) molga::AssetDatabase::Get().Reimport(g);
                    }
                    assetPollTimer = 0.0f;
                }
            }

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

                Editor::Get().ProcessPlayUIInput();

                Time::AccumulateFixedTime(scaledDt);
                while (Time::HasPendingFixedStep()) {
                    sceneDoc.ActiveWorld().FixedStep(Time::GetFixedDeltaTime());
                    Time::ConsumeFixedStep();
                }
                sceneDoc.ActiveWorld().Update(scaledDt);
                sceneDoc.ActiveWorld().EvaluateAnimations(scaledDt);
                sceneDoc.ActiveWorld().LateUpdate(scaledDt);
                sceneDoc.ActiveWorld().FlushDeferred(scaledDt);
            }
            // Mixer fades and completed one-shot reclamation are engine-level,
            // so they continue while the editor is paused or in edit mode.
            Audio::Update(dt);

            // Game output is rendered exclusively by GameViewWindow. The
            // editor backbuffer only hosts ImGui and remains camera-independent.
            renderer->Clear(0.12f, 0.12f, 0.15f, 1.0f);

            // ImGui Editor UI
            {
                MOLGA_PROFILE_SCOPE("Editor.UI", molga::ProfileCategory::EditorUI);
                ImGuiLayer::BeginFrame();
                Editor::Get().Update(dt);
                Editor::Get().RenderGUI();
                ImGuiLayer::EndFrame();
            }

            EventBus::ProcessQueue();

            // Scene requests made by scripts or queued-event handlers commit only
            // after the old World has completed all work for this frame.
            if (sceneDoc.IsPlaying()) {
                SceneRuntime* sceneRuntime = sceneDoc.PlayRuntime();
                if (sceneRuntime && sceneRuntime->IsSceneLoadPending() &&
                    sceneRuntime->CommitPendingLoad()) {
                    Time::ResetFixedAccumulator();
                    // Serialized scene IDs are local to a scene. Never let a
                    // play-mode undo command captured in the outgoing scene
                    // bind to an unrelated same-ID object after a transition.
                    Editor::Get().GetCommandHistory().Clear();
                    Editor::Get().ResetPlayUIInput();
                    Editor::Get().SetGameObjects(&sceneDoc.ActiveWorld().Objects());
                    Editor::Get().GetSelection().UnlockInspector();
                    Editor::Get().GetSelection().Clear(molga::SelectionSource::Code);
                }
            }

            Editor::Get().PumpScriptReload(editorState.IsEditMode());

            glfwSwapBuffers(window);
            glfwPollEvents();

            // 한 프레임의 스코프·카운터를 굳혀 ring buffer에 넣는다.
            molga::FrameCounters frameCounters = Editor::Get().TakeFrameCounters();
            molga::RenderStats   renderStats   = Editor::Get().TakeRenderStats();
            molga::ProfilerService::Get().EndFrame(
                static_cast<unsigned long long>(Time::GetFrameCount()),
                dt, frameCounters, renderStats);
        }
    }

    // Cleanup (deterministic order; unique_ptrs auto-release)
    sceneDoc.ExitPlay();
    PlayerPrefs::Shutdown();
    Project::Get().Close();
    Editor::Get().Shutdown();
    ImGuiLayer::Shutdown();
    TextRenderer::Get().Shutdown();
    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    renderer.reset();
    EngineShutdown();
    return 0;
}
