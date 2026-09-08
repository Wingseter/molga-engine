#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include <imgui.h>
#include "WindowManager.h"
#include "SceneOperations.h"
#include "BuildManager.h"
#include "Core/Profiling/FrameProfile.h"
// Task 10.2 Step 9b: 활성 월드에서 오브젝트 목록을 유도하는 접근자가 헤더에
// 인라인으로 있어야 한다. Editor.cpp를 링크하지 않고 Editor 명령만 링크하는
// 테스트 타깃(test_editor_undo_dirty)이 그 접근자를 쓴다.
#include "Core/World.h"

#include "Editor/Commands/CommandHistory.h"
#include "Editor/Selection/SelectionService.h"
#include "Editor/Commands/TransformCommand.h"
#include "Editor/EditorTaskService.h"
#include "Scripting/ScriptReloadService.h"

class GameObject;
class World;
class Renderer;
class TextRenderer;
namespace molga::text { class TextDiagnosticSink; }
class Shader;
class Camera2D;
class HierarchyWindow;
class InspectorWindow;

class Editor {
public:
    static Editor& Get();

    void Init();
    void Shutdown();

    // Main editor update/render
    void Update(float dt);
    void RenderGUI();

    // Scene management
    // Task 10.2 Step 9b: 창들이 보는 권한은 오브젝트 벡터가 아니라 World다.
    // 벡터만 넘기면 그 벡터를 담은 월드의 세대를 알 수 없고, UI 런타임
    // 식별자와 캐시 키가 전부 세대 0을 쓰게 된다 — 세대 0은 어떤 살아 있는
    // 월드도 갖지 않는 값이라 그 순간 정체성 검사가 통째로 무의미해진다.
    void SetActiveWorld(World& world);
    World* GetActiveWorld() { return activeWorld_; }
    std::vector<std::shared_ptr<GameObject>>* GetGameObjects() {
        return ObjectsPtr();
    }

    // Selection
    GameObject* GetSelectedObject() const;
    void SetSelectedObject(GameObject* obj);

    // Create new GameObject
    std::shared_ptr<GameObject> CreateGameObject(const std::string& name = "GameObject");

    molga::CommandHistory& GetCommandHistory() { return commandHistory; }
    molga::CommandHistory& GetAssetCommandHistory() { return assetCommandHistory_; }
    WindowManager& GetWindowManager() { return windowManager; }
    molga::SelectionService& GetSelection() { return selection_; }
    molga::EditorTaskService& GetTaskService() { return taskService; }
    molga::ScriptReloadService& GetScriptReload() { return *reloadService_; }

    void PumpScriptReload(bool isEditMode);
    void LaunchScriptCompile(molga::TaskId id,
                             const std::string& scriptsDir,
                             const std::string& configureCmd,
                             const std::string& buildCmd);

    void SubmitTransformEdit(unsigned int targetId,
                             const molga::TransformState& before,
                             const molga::TransformState& after);

    // Command가 사용하는 저수준 헬퍼
    std::shared_ptr<GameObject> AddExistingObject(std::shared_ptr<GameObject> obj);
    std::shared_ptr<GameObject> InsertExistingObjectAt(
        std::shared_ptr<GameObject> obj, std::size_t index);
    bool TryGetObjectIndex(unsigned int id, std::size_t& index) const;
    void RemoveObjectsByIds(const std::vector<unsigned int>& ids);
    GameObject* FindObjectById(unsigned int id) const;
    void MarkSceneModified();
    std::shared_ptr<GameObject> ShareObjectById(unsigned int id) const;

    // Scene file operations (delegate to SceneOperations)
    void NewScene();
    void SaveScene();
    void SaveSceneAs();
    void OpenScene();

    const std::string& GetCurrentScenePath() const { return sceneOps.GetCurrentPath(); }
    void SetCurrentScenePath(const std::string& path) {
        sceneOps.SetCurrentPath(path);
        sceneOps.ClearModified();
        commandHistory.Clear();
    }

    // SceneView에 렌더 리소스 주입
    // Task 8.2 Step 7d: Scene View와 Game View는 텍스트 권한을 스스로 찾지
    // 않는다. 프로세스 소유자(main)가 해석한 renderer/sink 하나를 여기로
    // 흘려보내고, 두 창은 그것을 순회 문맥에 담아 넘긴다.
    void SetSceneViewResources(Renderer* renderer, Shader* shader,
                               TextRenderer* textRenderer,
                               molga::text::TextDiagnosticSink* textDiagnostics);
    void ProcessPlayUIInput();
    void ResetPlayUIInput();

private:
    Editor() = default;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    // DockSpace
    void BeginDockSpace();
    void EndDockSpace();
    void SetupDefaultLayout(ImGuiID dockspaceId);

    // 활성 월드의 오브젝트 벡터. 언제나 여기서 유도하고 따로 보관하지 않는다.
    std::vector<std::shared_ptr<GameObject>>* ObjectsPtr() const {
        return activeWorld_ ? &activeWorld_->Objects() : nullptr;
    }

    void RenderMenuBar();
    void RenderPlayControls();
    void RenderScriptingMenu();

    // Subsystems
    WindowManager windowManager;
    SceneOperations sceneOps;
    BuildManager buildMgr;
    molga::EditorTaskService taskService;
    std::unique_ptr<molga::ILibraryPort> libraryPort_;
    std::unique_ptr<molga::ScriptReloadService> reloadService_;

    // 소유하지 않는다. 편집/플레이 전환마다 main이 지금 권한 있는 월드를
    // 다시 심는다.
    World* activeWorld_ = nullptr;
    molga::CommandHistory commandHistory;
    molga::CommandHistory assetCommandHistory_;
    molga::SelectionService selection_;

    // DockSpace
    bool firstTimeLayout = true;
    ImGuiID dockspaceId = 0;

public:
    molga::FrameCounters TakeFrameCounters() { auto c = frameCounters_; frameCounters_.Reset(); return c; }
    molga::RenderStats   TakeRenderStats()   { auto r = renderStats_;   renderStats_.Reset();   return r; }
    molga::FrameCounters& FrameCounters() { return frameCounters_; }
    molga::RenderStats&   RenderStats()   { return renderStats_; }

private:
    molga::FrameCounters frameCounters_;
    molga::RenderStats   renderStats_;
};
