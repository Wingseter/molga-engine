#include <iostream>
#include <sstream>
#include <memory>

#include "Assets/FontArtifactStore.h"
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
#include "UI/UISystem.h"
#include "Core/PathService.h"
#include "Core/BuildPlan.h"
#include "Core/PrefabRegistry.h"
#include "Core/PersistentStorage.h"
#include "Core/PlayerPrefs.h"
#include "Core/SmokeReport.h"
#include "Core/EventBus.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextRuntimeDependencies.h"
#include "Editor/GameBuilder.h"
#include <imgui.h>
#include <optional>
#include <string_view>

// Settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

static molga::AssetWatcher g_AssetWatcher;

namespace {

// Step 4d/7d: the project root is what the opened project says it is. It is
// never the current working directory and never an Assets path with the word
// "Assets" removed, because both silently produce a different artifact
// authority than the one the editor is actually editing.
bool BindProjectAssetAuthority(const std::filesystem::path& projectRoot) {
    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    if (const molga::FontArtifactStore* bound = database.FontArtifacts()) {
        if (bound->IsProjectAuthorityFor(projectRoot)) return true;
        Log::Error("AssetDatabase",
                   "the bound font artifact store belongs to a different "
                   "project root than " + projectRoot.string());
        return false;
    }
    std::string bindError;
    if (!database.BindFontArtifactStore(
            std::make_shared<const molga::FontArtifactStore>(
                molga::FontArtifactStore::ForProject(projectRoot)),
            &bindError)) {
        Log::Error("AssetDatabase",
                   "could not bind the project font artifact store: " +
                   bindError);
        return false;
    }
    return true;
}

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
    const std::filesystem::path smokeProjectRoot = Project::Get().GetPath();
    if (!BindProjectAssetAuthority(smokeProjectRoot)) {
        report.status = "error";
        report.message = "Could not bind the project font artifact store";
        report.Save(options.reportPath);
        return 3;
    }
    molga::AssetDatabase::Get().ScanProject(smokeProjectRoot / "Assets");
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

// ── Text runtime lifetime ────────────────────────────────────────────────────

// A present-but-malformed seam value returns this instead of nullopt, so the
// caller fails fast rather than falling through to a real application launch.
constexpr int kTextSeamParseError = 2;

// Test-only startup seam. --text-test-return-after-services=<code> returns
// <code> from the scoped startup function immediately after the text lifetime
// is established, so a test can observe the unwind order without a window.
std::optional<int> ParseTextTestReturnAfterServices(int argc, char* argv[]) {
    static constexpr std::string_view kFlag =
        "--text-test-return-after-services=";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument.rfind(kFlag, 0) != 0) continue;
        const std::string value(argument.substr(kFlag.size()));
        try {
            return std::stoi(value);
        } catch (const std::exception&) {
            // Present but unparseable is not the same as absent. Returning
            // nullopt here would launch the real application instead of the
            // seam, which in CI reads as a mystifying hang rather than a
            // broken argument.
            std::cerr << "Invalid --text-test-return-after-services value: "
                      << value << '\n';
            return kTextSeamParseError;
        }
    }
    return std::nullopt;
}

void EmitTextLifetimeEvent(const char* event) {
    std::cout << "MOLGA_TEXT_LIFETIME " << event << std::endl;
}

// Emits its event when destroyed, so declaration order alone fixes where the
// event lands in the unwind sequence: a marker declared before an owner is
// destroyed after that owner.
class TextLifetimeScopeMarker {
public:
    TextLifetimeScopeMarker(bool enabled, const char* event)
        : enabled_(enabled), event_(event) {}
    TextLifetimeScopeMarker(const TextLifetimeScopeMarker&)            = delete;
    TextLifetimeScopeMarker& operator=(const TextLifetimeScopeMarker&) = delete;
    ~TextLifetimeScopeMarker() {
        if (enabled_) EmitTextLifetimeEvent(event_);
    }

private:
    bool        enabled_ = false;
    const char* event_   = nullptr;
};

// Everything after PathService::InitFromExecutable runs inside this scope. The
// text runtime lifetime guard is declared here, ahead of every text service,
// renderer and host handle below, so each of the returns further down unwinds
// them in reverse and leaves the guard's u_cleanup last. std::exit, _Exit and
// quick_exit are banned in this scope, and no text service may be owned by a
// static: either would skip exactly the destructors this ordering depends on.
int RunEditorAfterPaths(int argc, char* argv[], bool textSeamRequested,
                        int textSeamCode) {
    // Headless packaging runs before the text lifetime: it constructs no Game
    // View text service, so it has nothing to unwind past.
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

    // The one ICU lifetime this process gets, established before any Game View
    // text service exists. On failure the ImGui shell stays fully usable: the
    // typed diagnostic is reported through the editor log, Game View text is
    // marked unavailable, and no fallback text renderer is attempted.
    //
    // The sink type differs from the runtime's on purpose and must survive any
    // later extraction of this seam: Step 10 has the editor keep running and
    // surface the diagnostic through its console, so it logs; Step 11 has the
    // runtime print and exit 4, so it collects instead. Flattening the two into
    // one helper would erase a required difference in failure policy.
    molga::text::LoggerTextDiagnosticSink textDiagnostics(/*maxRememberedKeys=*/64);
    std::optional<molga::text::TextRuntimeLifetimeGuard> textGuard =
        molga::text::TextRuntimeLifetimeGuard::Create(
            molga::text::TextDependencyConfig::FromEngineTextRoot(
                PathService::Get().EngineResource("Engine/Text"),
                /*packagedRuntime=*/false),
            textDiagnostics);
    if (!textGuard) {
        // TextRuntimeDependencies::Get().IsReady() stays false for the rest of
        // the process, and that is the availability signal every Game View text
        // service consults before it constructs an ICU or HarfBuzz object.
        Log::Error("text-runtime",
                   "Game View text is unavailable: the verified Engine/Text "
                   "pair beside the editor could not be loaded. The editor "
                   "shell remains usable.");
    }

    // Named for what it observes, not for what follows it: this marker is
    // destroyed immediately before the guard, so it marks the instant guard
    // shutdown is about to begin, not its completion.
    const TextLifetimeScopeMarker textGuardShutdownMarker(
        textSeamRequested, "runtime_guard_shutdown_begins");

    if (textSeamRequested) {
        // Stands in for the Game View text host every later milestone declares
        // here: a client handle that must be gone before the guard shuts down.
        const TextLifetimeScopeMarker textHandleMarker(
            true, "last_text_handle_destroyed");
        std::optional<molga::text::TextRuntimeClientHandle> textClient =
            molga::text::TextRuntimeClientHandle::Acquire();
        if (!textClient) {
            std::cerr << "Text runtime is not ready; no client handle\n";
            return 6;
        }
        return textSeamCode;
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
    auto host = EngineInit(wc);
    if (!host) return -1;

    ImGuiLayer::Init(*host);

    // Initialize resources (local to main)
    auto renderer = std::make_unique<Renderer>();
    std::string rendererError;
    if (!renderer->Init(&rendererError)) {
        std::cerr << "Renderer initialization failed: " << rendererError << '\n';
        ImGuiLayer::Shutdown();
        EngineShutdown(host);
        return -1;
    }
    molga::RenderSystem2D::Get().Init();
    Shader* shader = ShaderManager::Get().Get("default");
    if (!shader) {
        std::cerr << "Renderer shader bundle has no default entry\n";
        molga::RenderSystem2D::Get().Shutdown();
        renderer.reset();
        ImGuiLayer::Shutdown();
        EngineShutdown(host);
        return -1;
    }
    SceneDocument sceneDoc;

    // ── Task 8.2 Step 3b/7f: 공유 텍스트 서비스 한 벌 ────────────────────────
    // 텍스트 런타임 guard는 이 함수 맨 위에 이미 서 있고, 이 renderer는 그
    // 다음이다. database는 프로세스가 소유하는 그 권한이며 나중에 프로젝트/
    // 봉인된 폰트 산출물 저장소를 받고 이 renderer보다 오래 산다.
    //
    // 실패해도 에디터 셸은 그대로 쓸 수 있다: 텍스트가 없는 상태가 되고,
    // 사유는 textDiagnostics(에디터 콘솔)로 나간다. 대체 렌더러는 세우지
    // 않는다 — 조용히 다른 폰트로 그리는 것이 텍스트가 없는 것보다 나쁘다.
    // 프로세스 인스턴스는 정적 저장 수명이 아니라 heap에 있고, 아래
    // ShutdownRendererThenTextGpuResources가 guard의 u_cleanup 전에 부순다.
    if (!TextRenderer::Get().Init(molga::AssetDatabase::Get(), textDiagnostics)) {
        Log::Error("text-runtime",
                   "Rendered text is unavailable: the shared text services "
                   "could not be initialized. The editor shell remains "
                   "usable.");
    }

    // ── Task 11.1 Step 3i/A3: 배치의 두 의존물을 진입점이 소유한다 ─────────
    // Build는 이제 편집 상태 제공자와 정확한 공유 TextLayoutService를 요구한다.
    // 그 서비스는 TextRenderer가 초기화된 *뒤에야* 존재하므로 설치 지점은 바로
    // 여기다. 진짜 제공자는 Task 14가 설치하고, 그때까지는 값이 비어 있음을
    // 명시적으로 말하는 Empty 제공자가 그 자리를 지킨다 — 기본 인자로 숨기면
    // 한 표면에서 빠뜨려도 컴파일이 통과한다.
    UISystem::Get().InstallLayoutDependencies(
        molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
        TextRenderer::Get().LayoutService());

    // Initialize Editor
    Editor::Get().Init();
    Editor::Get().SetActiveWorld(sceneDoc.EditWorld());
    // SceneView에 렌더 리소스 주입 (FBO 렌더 활성화)
    Editor::Get().SetSceneViewResources(renderer.get(), shader,
                                        &TextRenderer::Get(), &textDiagnostics);

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
            Editor::Get().SetActiveWorld(sceneDoc.ActiveWorld());
            
            World& pw = sceneDoc.ActiveWorld();
            Editor::Get().GetSelection().Rebind(
                [&pw](unsigned int id) { return pw.FindById(id) != nullptr; });
            return true;
        },
        [&sceneDoc]() {  // Play/Pause → Stop
            Editor::Get().GetCommandHistory().Clear();
            Editor::Get().ResetPlayUIInput();
            Editor::Get().SetActiveWorld(sceneDoc.EditWorld());
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
    while (!host->ShouldClose() && !projectLoaded) {
        host->PollEvents();
        if (host->ShouldClose()) break;

        molga::BeginFrameResult acquired = host->BeginFrame();
        if (acquired.status == molga::FrameAcquireStatus::Unavailable) continue;
        if (acquired.status == molga::FrameAcquireStatus::Fatal ||
            !renderer->BeginFrame(std::move(acquired.frame), &rendererError)) {
            std::cerr << "GPU frame acquisition failed: "
                      << (acquired.error.empty() ? rendererError : acquired.error)
                      << '\n';
            host->RequestClose();
            break;
        }

        // Clear first, then draw ImGui
        renderer->Clear(0.1f, 0.1f, 0.12f, 1.0f);

        ImGuiLayer::BeginFrame();
        projectWindow.OnGUI();
        if (!ImGuiLayer::EndFrame(*renderer, &rendererError)) {
            std::cerr << "Editor frame submission failed: " << rendererError << '\n';
            host->RequestClose();
        }

        // Check if project was selected
        if (projectWindow.HasProjectSelected()) {
            projectLoaded = true;
            std::cout << "[Main] Project selected: " << projectWindow.GetSelectedProjectPath() << std::endl;
        }

    }

    if (projectLoaded && Project::Get().IsOpen()) {
        namespace fs = std::filesystem;
        PathService::Get().SetAssetRoot(Project::Get().GetPath());

        const fs::path openedProjectRoot = Project::Get().GetPath();
        if (!BindProjectAssetAuthority(openedProjectRoot)) {
            std::cerr << "Could not bind the project font artifact store\n";
            host->RequestClose();
        }
        molga::AssetDatabase::Get().ScanProject(openedProjectRoot / "Assets");
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
            Editor::Get().SetActiveWorld(sceneDoc.EditWorld());
            Editor::Get().SetCurrentScenePath(mainScene.string());
            // 씬 로드 후 SceneView 리소스 재주입 (오브젝트 목록 갱신)
            Editor::Get().SetSceneViewResources(renderer.get(), shader,
                                        &TextRenderer::Get(), &textDiagnostics);
            std::cout << "[Main] Loaded project startup scene: " << mainScene << std::endl;
        } else {
            std::cerr << "[Main] Project startup scene not found or invalid: "
                      << mainScene << std::endl;
        }
    }

    if (!host->ShouldClose()) {
        // Main editor loop
        while (!host->ShouldClose()) {
            host->PollEvents();
            if (host->ShouldClose()) break;
            Time::Update();
            // Task 6.3: 프레임 번호가 정해진 바로 다음, 어떤 텍스트도 큐에
            // 담기기 전에 이 프레임의 glyph 수집을 연다. 어휘적 범위가 루프
            // 본문 전체이므로 아래의 모든 continue/break가 이 안에서 일어나고,
            // 수집은 명령들이 page 토큰을 다 복사한 뒤에야 닫힌다.
            auto glyphCollection = TextRenderer::Get().BeginGlyphCollection(
                static_cast<std::uint64_t>(Time::GetFrameCount()));
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
            host->SetTitle(title.str());

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

            molga::BeginFrameResult acquired = host->BeginFrame();
            if (acquired.status == molga::FrameAcquireStatus::Unavailable) continue;
            if (acquired.status == molga::FrameAcquireStatus::Fatal ||
                !renderer->BeginFrame(std::move(acquired.frame), &rendererError)) {
                std::cerr << "GPU frame acquisition failed: "
                          << (acquired.error.empty() ? rendererError : acquired.error)
                          << '\n';
                host->RequestClose();
                break;
            }

            // Game output is rendered exclusively by GameViewWindow. The
            // editor backbuffer only hosts ImGui and remains camera-independent.
            renderer->Clear(0.12f, 0.12f, 0.15f, 1.0f);

            // ImGui Editor UI
            {
                MOLGA_PROFILE_SCOPE("Editor.UI", molga::ProfileCategory::EditorUI);
                ImGuiLayer::BeginFrame();
                Editor::Get().Update(dt);
                Editor::Get().RenderGUI();
                if (!ImGuiLayer::EndFrame(*renderer, &rendererError)) {
                    std::cerr << "Editor frame submission failed: "
                              << rendererError << '\n';
                    host->RequestClose();
                }
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
                    Editor::Get().SetActiveWorld(sceneDoc.ActiveWorld());
                    Editor::Get().GetSelection().UnlockInspector();
                    Editor::Get().GetSelection().Clear(molga::SelectionSource::Code);
                }
            }

            Editor::Get().PumpScriptReload(editorState.IsEditMode());

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
    // Task 6.3/8.2 Step 7f: 텍스트/atlas GPU 자원은 renderer가 GPU idle을
    // 증명하고 반납 큐를 비운 다음에만 파괴되고, 텍스트 서비스는 그 다음,
    // guard의 종결 u_cleanup은 맨 마지막이다. 그 순서는 이 함수 한 곳에만
    // 적혀 있다. 프로세스 인스턴스도 성공 시 그 안에서 놓이므로, guard가
    // 죽은 뒤에 도는 소멸자가 남지 않는다.
    if (!ShutdownRendererThenTextGpuResources(*renderer, TextRenderer::Get(),
                                              textDiagnostics)) {
        // 거절되었다는 것은 종료 순서가 뒤집혔다는 뜻이다. 붙들려 있는 page
        // 위에서 부수는 것보다 OS가 회수하게 두는 쪽이 싸므로, 여기서는
        // 사유만 남기고 인스턴스를 그대로 둔다(Task 6의 잠정 종결 분기).
        Log::Error("TextRenderer",
                   "Text GPU teardown was refused during editor shutdown.");
    }
    molga::RenderSystem2D::Get().Shutdown();
    ShaderManager::Get().Shutdown();
    renderer.reset();
    EngineShutdown(host);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);
    RegisterBuiltinComponents();
    RegisterBuiltinScripts();

    const std::optional<int> textSeam =
        ParseTextTestReturnAfterServices(argc, argv);
    const int code = RunEditorAfterPaths(argc, argv, textSeam.has_value(),
                                         textSeam.value_or(0));

    // The scoped function has returned, so the guard is destroyed. Gate on the
    // terminal state itself: !IsReady() is also true for a process whose guard
    // was never created, which would make this event prove nothing.
    if (textSeam) {
        if (molga::text::TextRuntimeDependencies::Get().WasTerminallyCleaned()) {
            EmitTextLifetimeEvent("u_cleanup");
        }
        EmitTextLifetimeEvent("process_return");
    }
    return code;
}
