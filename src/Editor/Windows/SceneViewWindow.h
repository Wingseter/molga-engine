#pragma once

#include "EditorWindow.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/Camera2D.h"
#include "Editor/ViewportMath.h"
#include "Editor/Gizmos/TransformGizmo.h"
#include "Editor/EditorPreferences.h"
#include "Rendering/PostProcessPipeline.h"
#include "Rendering/LightingPipeline2D.h"
#include <imgui.h>
#include <memory>
#include <vector>
#include <functional>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <cstdint>
#include <nlohmann/json.hpp>

class Renderer;
class Shader;
class GameObject;
class World;
// Task 8.2 Step 7d: Scene View의 텍스트 권한. 값으로 담지 않으므로 선언만
// 있으면 된다.
class TextRenderer;
namespace molga::text { class TextDiagnosticSink; }
class SpriteRenderer;
class Transform;

// Scene 뷰 창
// - 전용 FBO에 씬을 렌더하고 ImGui::Image()로 표시
// - 에디터 전용 Camera2D (패닝, 줌)
// - 무한 그리드 + 원점 축 오버레이
class SceneViewWindow : public EditorWindow {
public:
    SceneViewWindow();
    ~SceneViewWindow() override;

    void OnGUI() override;

    // 씬 렌더에 필요한 리소스 주입 (main.cpp에서 호출)
    void SetSceneResources(
        Renderer* renderer,
        Shader*   spriteShader,
        World*    world,
        TextRenderer* textRenderer,
        molga::text::TextDiagnosticSink* textDiagnostics
    );

    // Task 10.2 Step 9c: UI 권한은 오브젝트 벡터가 아니라 World다. 벡터만
    // 들고 있으면 UI가 그 벡터를 담은 월드의 세대를 알 수 없어 런타임
    // 식별자가 세대 0으로 만들어지고, 그 값은 어떤 살아 있는 월드도 갖지
    // 않으므로 정체성 검사가 통째로 무의미해진다. 월드가 없으면 그 표면은
    // 아예 건너뛴다.
    void SetActiveWorld(World* world) { world_ = world; }

private:
    // 렌더 리소스
    Renderer*   renderer_     = nullptr;
    Shader*     spriteShader_ = nullptr;
    World* world_ = nullptr;
    // 세계 스프라이트/카메라 순회는 목록만 필요하다. 목록은 언제나 여기서
    // 유도하고 따로 보관하지 않는다.
    std::vector<std::shared_ptr<GameObject>>* Objects() const;
    // main이 주입한 그 하나. 창이 스스로 TextRenderer::Get()을 부르면 이
    // 프로세스에 두 번째 텍스트 서비스가 생길 수 있다.
    TextRenderer* textRenderer_ = nullptr;
    molga::text::TextDiagnosticSink* textDiagnostics_ = nullptr;

    // Scene View 전용 오프스크린 타깃
    molga::RenderTarget sceneTarget_;
    molga::PostProcessPipeline postProcessPipeline_;
    molga::LightingPipeline2D lightingPipeline_;
    molga::EditorPreferences preferences_;
    std::filesystem::path preferencePath_;
    std::unordered_set<std::string> postProcessWarnings_;
    std::unordered_set<std::string> lightingWarnings_;

    // 에디터 전용 Camera2D
    std::unique_ptr<Camera2D> editorCamera_;

    // 그리드 셰이더
    Shader* gridShader_ = nullptr;
    bool gridShaderLoaded_ = false;

    // 뷰포트 크기 추적
    float vpWidth_  = 0.f;
    float vpHeight_ = 0.f;

    // 입력 상태
    bool  isPanning_    = false;
    float lastMouseX_   = 0.f;
    float lastMouseY_   = 0.f;

    molga::TransformGizmo gizmo_;
    enum class LightingHandleKind {
        None,
        PointRadius,
        PolygonVertex,
    };
    LightingHandleKind lightingHandleKind_ = LightingHandleKind::None;
    unsigned int lightingHandleObjectId_ = 0;
    std::uint64_t lightingHandleInstanceId_ = 0;
    int lightingHandleVertex_ = -1;
    nlohmann::json lightingHandleBefore_;

    // 우클릭 컨텍스트 메뉴: 우클릭한 지점의 월드 좌표(생성 위치)
    float ctxWorldX_ = 0.f;
    float ctxWorldY_ = 0.f;

    // 초기화
    void InitGridShader();

    // 씬 렌더
    void RenderSceneToFBO(float vpW, float vpH);
    void DrawSceneBase();
    void SavePreferences();
    void DrawGrid();
    void DrawSprites();
    void DrawUI(float vpW, float vpH);
    void DrawCameraOutputGizmos(ImVec2 panelPos, ImVec2 panelSize);
    bool DrawLightingHandles(ImVec2 panelPos, ImVec2 panelSize);
    void CancelLightingHandleDrag();
    // 입력 처리
    void HandleInput(ImVec2 panelPos, ImVec2 panelSize);

    // F키: 씬 오브젝트 전체를 뷰에 맞춤
    void FrameAll(ImVec2 panelSize);

    // 우클릭 Create 컨텍스트 메뉴
    void DrawContextMenu();
    // 우클릭 위치에 오브젝트 생성 (compType이 비어있지 않으면 해당 컴포넌트 부착)
    void CreateObjectAt(const char* name, const std::string& compType, float worldX, float worldY);

    // 좌표 변환 유틸: 패널 내 스크린 픽셀 좌표 → 월드 좌표
    void ScreenToWorld(ImVec2 panelPos, ImVec2 panelSize, ImVec2 screen,
                       float& outX, float& outY) const;

    // 현재 에디터 카메라 상태를 ViewportMath 구조로 변환
    molga::ViewportCamera ViewportCam() const;
    // 좌클릭 픽킹: 패널 좌표 클릭 → 후보 수집 → SelectionService
    void HandlePick(ImVec2 panelPos, ImVec2 panelSize);
    // 선택 외곽선 그리기
    void DrawSelectionOutline(ImVec2 panelPos, ImVec2 panelSize);
};
