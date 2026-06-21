#pragma once

#include "EditorWindow.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/Camera2D.h"
#include "Editor/ViewportMath.h"
#include <imgui.h>
#include <memory>
#include <vector>
#include <functional>

class Renderer;
class Shader;
class GameObject;
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
        std::vector<std::shared_ptr<GameObject>>* objects
    );

    // 씬 변경 시 오브젝트 목록 갱신
    void SetGameObjects(std::vector<std::shared_ptr<GameObject>>* objects) {
        gameObjects_ = objects;
    }

private:
    // 렌더 리소스
    Renderer*   renderer_     = nullptr;
    Shader*     spriteShader_ = nullptr;
    std::vector<std::shared_ptr<GameObject>>* gameObjects_ = nullptr;

    // 오프스크린 FBO
    Framebuffer fbo_;

    // 에디터 전용 Camera2D
    std::unique_ptr<Camera2D> editorCamera_;

    // 그리드 셰이더
    std::unique_ptr<Shader> gridShader_;
    unsigned int gridVAO_ = 0;
    unsigned int gridVBO_ = 0;
    bool gridShaderLoaded_ = false;

    // 뷰포트 크기 추적
    float vpWidth_  = 0.f;
    float vpHeight_ = 0.f;

    // 입력 상태
    bool  isPanning_    = false;
    float lastMouseX_   = 0.f;
    float lastMouseY_   = 0.f;

    // 우클릭 컨텍스트 메뉴: 우클릭한 지점의 월드 좌표(생성 위치)
    float ctxWorldX_ = 0.f;
    float ctxWorldY_ = 0.f;

    // 초기화
    void InitGridShader();
    void InitGridQuad();

    // 씬 렌더
    void RenderSceneToFBO(float vpW, float vpH);
    void DrawGrid();
    void DrawSprites();

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
};
