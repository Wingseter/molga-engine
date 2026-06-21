#include "SceneViewWindow.h"
#include "../EditorConstants.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Shader.h"
#include "../../Rendering/Camera2D.h"
#include "../../Rendering/RenderPass.h"
#include "../../ECS/GameObject.h"
#include "../../ECS/Components/SpriteRenderer.h"
#include "../../ECS/Components/TilemapRenderer.h"
#include "../../ECS/Components/MarrowRenderer.h"
#include "../../ECS/Components/ParticleSystem.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/TextRenderer2D.h"
#include "../EditorState.h"
#include "../../Common/Log.h"
#include "../../Common/linmath.h"
#include "../../Core/PathService.h"
#include "Editor/Editor.h"
#include "Editor/Commands/ObjectCommands.h"
#include "Editor/ScenePicker.h"
#include "../FontManager.h"
#include "Core/AssetDatabase.h"
#include "Editor/Commands/CreateSpriteFromAssetCommand.h"
#include "Editor/Project.h"
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <glad/glad.h>
#include <algorithm>
#include <cmath>

SceneViewWindow::SceneViewWindow()
    : EditorWindow(EditorConstants::WIN_SCENE) {
    editorCamera_ = std::make_unique<Camera2D>(800.f, 600.f);
}

SceneViewWindow::~SceneViewWindow() {
    if (gridVAO_) { glDeleteVertexArrays(1, &gridVAO_); gridVAO_ = 0; }
    if (gridVBO_) { glDeleteBuffers(1, &gridVBO_); gridVBO_ = 0; }
}

void SceneViewWindow::SetSceneResources(
    Renderer* renderer,
    Shader*   spriteShader,
    std::vector<std::shared_ptr<GameObject>>* objects)
{
    renderer_     = renderer;
    spriteShader_ = spriteShader;
    gameObjects_  = objects;

    // 그리드 셰이더는 GL 컨텍스트가 있을 때 지연 초기화
    InitGridShader();
    InitGridQuad();
}

// ── 초기화 ────────────────────────────────────────────────────────────────────

void SceneViewWindow::InitGridShader() {
    if (gridShaderLoaded_) return;

    auto vertPath = PathService::Get().EngineResource("Shaders/grid.vert").string();
    auto fragPath = PathService::Get().EngineResource("Shaders/grid.frag").string();

    try {
        gridShader_ = std::make_unique<Shader>(vertPath.c_str(), fragPath.c_str());
        gridShaderLoaded_ = true;
    } catch (...) {
        Log::Error("SceneView", "Failed to load grid shader");
        gridShaderLoaded_ = false;
    }
}

void SceneViewWindow::InitGridQuad() {
    if (gridVAO_) return;

    // NDC 풀스크린 쿼드 (-1 ~ 1)
    float verts[] = {
        -1.f, -1.f,
         1.f, -1.f,
         1.f,  1.f,
        -1.f, -1.f,
         1.f,  1.f,
        -1.f,  1.f,
    };

    glGenVertexArrays(1, &gridVAO_);
    glGenBuffers(1, &gridVBO_);
    glBindVertexArray(gridVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// ── OnGUI ─────────────────────────────────────────────────────────────────────

void SceneViewWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(title.c_str(), &isOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float  vpW   = avail.x;
    float  vpH   = avail.y;

    if (vpW < 8.f || vpH < 8.f) {
        ImGui::End();
        return;
    }

    // ── 패널 크기 변경 시 FBO / 카메라 리사이즈 ──────────────────────────────
    // float 소수점 흔들림 방지: int로 캐스팅 후 비교
    int ivpW = static_cast<int>(vpW);
    int ivpH = static_cast<int>(vpH);
    if (ivpW != fbo_.Width() || ivpH != fbo_.Height()) {
        vpWidth_  = vpW;
        vpHeight_ = vpH;
        fbo_.Resize(ivpW, ivpH);
        editorCamera_->SetScreenSize(vpW, vpH);
    }

    // ── FBO가 아직 미초기화면 생성 ───────────────────────────────────────────
    if (!fbo_.IsValid()) {
        fbo_.Init(static_cast<int>(vpW), static_cast<int>(vpH));
    }

    // ── 씬을 FBO에 렌더 ──────────────────────────────────────────────────────
    if (fbo_.IsValid() && renderer_) {
        RenderSceneToFBO(vpW, vpH);
    }

    // ── FBO 텍스처를 ImGui 패널에 출력 (UV Y축 반전) ─────────────────────────
    ImVec2 panelPos = ImGui::GetCursorScreenPos();

    if (fbo_.IsValid()) {
        ImTextureID texId = (ImTextureID)(uintptr_t)fbo_.ColorTexture();
        // GL 텍스처는 좌하단 원점이므로 UV Y를 뒤집어야 한다
        ImGui::Image(texId, ImVec2(vpW, vpH), ImVec2(0, 1), ImVec2(1, 0));
        
        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_GUID");
            if (!p) p = ImGui::AcceptDragDropPayload("TEXTURE_PATH"); // 구 payload 호환
            if (p) {
                std::string guid;
                if (std::strcmp(p->DataType, "ASSET_GUID") == 0) {
                    guid.assign(static_cast<const char*>(p->Data), p->DataSize - 1);
                } else { // TEXTURE_PATH → guid 변환
                    std::string path(static_cast<const char*>(p->Data), p->DataSize - 1);
                    guid = molga::AssetDatabase::Get().GuidForSource(
                        Project::Get().GetRelativePath(path));
                }
                if (!guid.empty()) {
                    float worldX = 0.0f;
                    float worldY = 0.0f;
                    ScreenToWorld(panelPos, ImVec2(vpW, vpH), ImGui::GetMousePos(), worldX, worldY);
                    Vector2 world(worldX, worldY);
                    
                    auto* activeObjects = Editor::Get().GetGameObjects();
                    if (activeObjects) {
                        auto cmd = std::make_unique<molga::CreateSpriteFromAssetCommand>(
                            guid, "Sprite", world, activeObjects);
                        auto* createdObj = cmd->created(); // Execute() 전에는 nullptr일 수 있음. 아래에서 Execute 후 캐스팅합니다.
                        Editor::Get().GetCommandHistory().Execute(std::move(cmd));
                        
                        // commandHistory.Execute() 가 command를 소유해서 실행했으므로, 
                        // 생성된 오브젝트는 activeObjects의 맨 뒤에 추가되어 있습니다.
                        if (!activeObjects->empty()) {
                            Editor::Get().SetSelectedObject(activeObjects->back().get());
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    } else {
        // 폴백: placeholder 사각형
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 tl = panelPos;
        ImVec2 br = ImVec2(tl.x + vpW, tl.y + vpH);
        dl->AddRectFilled(tl, br, IM_COL32(30, 30, 50, 255));
        dl->AddRect(tl, br, IM_COL32(100, 100, 150, 255));
        ImGui::Dummy(ImVec2(vpW, vpH));
    }

    // ── 오버레이 텍스트 (좌상단) — GetWindowDrawList로 이 창 안에만 표시 ──────
    {
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        ImVec2 overlayPos = panelPos;
        wdl->AddText(
            ImVec2(overlayPos.x + 8.f, overlayPos.y + 8.f),
            IM_COL32(200, 200, 200, 180),
            "Scene | Wheel: Zoom (cursor)  MMB+Drag: Pan  F: Frame All"
        );
        // 카메라 정보 — 뷰포트 중심의 월드 좌표를 표시
        float zoom = editorCamera_->GetZoom();
        float camX = editorCamera_->GetX();
        float camY = editorCamera_->GetY();
        float centerX = camX + vpW * 0.5f;
        float centerY = camY + vpH * 0.5f;
        char camInfo[128];
        snprintf(camInfo, sizeof(camInfo), "Cam: (%.1f, %.1f)  Zoom: %.2fx",
            centerX, centerY, zoom);
        wdl->AddText(
            ImVec2(overlayPos.x + 8.f, overlayPos.y + 24.f),
            IM_COL32(180, 180, 180, 140),
            camInfo
        );
    }

    // ── 툴바 오버레이 (Floating Toolbar) ──────────────────────────────────────
    {
        ImGui::SetCursorScreenPos(ImVec2(panelPos.x + 8.f, panelPos.y + 40.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.25f, 0.7f));
        
        const char* toolNames[] = { "Select", "Move", "Rotate", "Scale" };
        molga::GizmoTool tools[] = { molga::GizmoTool::Select, molga::GizmoTool::Move, molga::GizmoTool::Rotate, molga::GizmoTool::Scale };
        for (int i = 0; i < 4; ++i) {
            bool active = (gizmo_.Tool() == tools[i]);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.4f, 0.6f, 0.9f));
            if (ImGui::Button(toolNames[i])) {
                gizmo_.SetTool(tools[i]);
            }
            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        
        ImGui::Spacing(); ImGui::SameLine();
        
        static int currentSnap = 0; // 0: Off, 1: 16px, 2: 32px, 3: 64px
        const char* snapNames[] = { "Snap: Off", "Snap: 16", "Snap: 32", "Snap: 64" };
        if (ImGui::Button(snapNames[currentSnap])) {
            currentSnap = (currentSnap + 1) % 4;
            if (currentSnap == 0) gizmo_.SetSnap(molga::SnapMode::Off, 0.f);
            else if (currentSnap == 1) gizmo_.SetSnap(molga::SnapMode::Grid, 16.f);
            else if (currentSnap == 2) gizmo_.SetSnap(molga::SnapMode::Grid, 32.f);
            else if (currentSnap == 3) gizmo_.SetSnap(molga::SnapMode::Grid, 64.f);
        }
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ── 입력 처리 (Image 위젯이 렌더된 후) ──────────────────────────────────
    HandleInput(panelPos, ImVec2(vpW, vpH));

    GameObject* primaryTarget = Editor::Get().FindObjectById(Editor::Get().GetSelection().PrimaryId());
    DrawSelectionOutline(panelPos, ImVec2(vpW, vpH));
    bool gizmoUsed = gizmo_.Draw(primaryTarget, ViewportCam(), panelPos, ImVec2(vpW, vpH));

    if (!gizmoUsed && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        HandlePick(panelPos, ImVec2(vpW, vpH));
    }

    // ── 우클릭 컨텍스트 메뉴 트리거 ──────────────────────────────────────────
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImVec2 mousePos = ImGui::GetMousePos();
        ScreenToWorld(panelPos, ImVec2(vpW, vpH), mousePos, ctxWorldX_, ctxWorldY_);
        ImGui::OpenPopup("SceneViewContextMenu");
    }

    DrawContextMenu();

    ImGui::End();
}

// ── 씬 렌더 ──────────────────────────────────────────────────────────────────

void SceneViewWindow::RenderSceneToFBO(float vpW, float vpH) {
    fbo_.Bind();

    Camera* mainCam = nullptr;
    if (EditorState::Get().IsPlayMode() && gameObjects_) {
        for (auto& obj : *gameObjects_) {
            if (obj && obj->IsActive()) {
                if (auto cam = obj->GetComponent<Camera>()) {
                    if (cam->IsEnabled() && cam->IsMain()) {
                        if (!mainCam || cam->GetDepth() > mainCam->GetDepth()) {
                            mainCam = cam;
                        }
                    }
                }
            }
        }
    }

    // 배경 클리어
    if (mainCam) {
        Color bg = mainCam->GetBackgroundColor();
        glClearColor(bg.r, bg.g, bg.b, bg.a);
    } else {
        glClearColor(0.18f, 0.18f, 0.22f, 1.f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 알파 블렌딩 활성화
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 그리드 먼저 그리기
    DrawGrid();

    // 그 위에 스프라이트
    DrawSprites();

    fbo_.Unbind();
}

void SceneViewWindow::DrawGrid() {
    if (!gridShaderLoaded_ || !gridShader_ || !gridVAO_) return;

    // 투영·뷰 행렬 역행렬 계산
    mat4x4 proj, view, projView, invProjView;
    editorCamera_->GetProjectionMatrix(proj);
    editorCamera_->GetViewMatrix(view);
    mat4x4_mul(projView, proj, view);

    // linmath은 행 우선이므로 전치 후 역행렬
    mat4x4 pv;
    mat4x4_dup(pv, projView);
    mat4x4_invert(invProjView, pv);

    // 줌에 따라 그리드 간격 자동 조절
    float zoom    = editorCamera_->GetZoom();
    float spacing = 64.f;  // 기본 픽셀 단위 (1 월드 단위 = 1px 기준)
    // 줌이 작으면 간격을 크게
    if (zoom < 0.1f)      spacing = 512.f;
    else if (zoom < 0.25f) spacing = 256.f;
    else if (zoom < 0.5f)  spacing = 128.f;
    else if (zoom > 4.f)   spacing = 32.f;

    gridShader_->Use();
    gridShader_->SetMat4("invProjView", (float*)invProjView);
    gridShader_->SetFloat("gridSpacing", spacing);
    gridShader_->SetVec4("gridColor", 0.4f, 0.4f, 0.5f, 0.5f);
    gridShader_->SetVec4("originColor", 0.8f, 0.8f, 0.8f, 1.0f);
    gridShader_->SetFloat("lineWidth", 1.0f);

    glBindVertexArray(gridVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void SceneViewWindow::DrawSprites() {
    if (!renderer_ || !spriteShader_ || !gameObjects_) return;

    // 정렬 후 렌더
    std::vector<std::pair<int, Component*>> drawList;
    for (auto& obj : *gameObjects_) {
        if (obj && obj->IsActive()) {
            if (auto sr = obj->GetComponent<SpriteRenderer>()) {
                drawList.emplace_back(sr->GetSortingOrder(), sr);
            }
            if (auto tm = obj->GetComponent<TilemapRenderer>()) {
                drawList.emplace_back(tm->GetSortingOrder(), tm);
            }
            if (auto mr = obj->GetComponent<MarrowRenderer>()) {
                drawList.emplace_back(mr->GetSortingOrder(), mr);
            }
            if (auto ps = obj->GetComponent<ParticleSystem>()) {
                drawList.emplace_back(ps->GetSortingOrder(), ps);
            }
            if (auto tr = obj->GetComponent<TextRenderer2D>()) {
                drawList.emplace_back(tr->GetSortingOrder(), tr);
            }
        }
    }
    std::stable_sort(drawList.begin(), drawList.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    Camera2D* activeCamera = editorCamera_.get();
    if (EditorState::Get().IsPlayMode() && gameObjects_) {
        for (auto& obj : *gameObjects_) {
            if (obj && obj->IsActive()) {
                if (auto cam = obj->GetComponent<Camera>()) {
                    if (cam->IsEnabled() && cam->IsMain()) {
                        if (!activeCamera || !dynamic_cast<Camera2D*>(activeCamera) || cam->GetDepth() > static_cast<Camera*>(obj->GetComponent<Camera>())->GetDepth()) {
                            // Wait, let's keep it simple: just pick the main camera
                        }
                    }
                }
            }
        }
        // Let's rewrite the camera search to match exactly
        Camera* mainCam = nullptr;
        for (auto& obj : *gameObjects_) {
            if (obj && obj->IsActive()) {
                if (auto cam = obj->GetComponent<Camera>()) {
                    if (cam->IsEnabled() && cam->IsMain()) {
                        if (!mainCam || cam->GetDepth() > mainCam->GetDepth()) {
                            mainCam = cam;
                        }
                    }
                }
            }
        }
        if (mainCam) {
            activeCamera = mainCam->GetCamera2D();
        }
    }

    {
        molga::RenderPass pass(*renderer_, spriteShader_, activeCamera);
        for (auto& [order, comp] : drawList) {
            comp->RenderSprite(renderer_);
        }
    }
}

// ── 입력 처리 ─────────────────────────────────────────────────────────────────

void SceneViewWindow::HandleInput(ImVec2 panelPos, ImVec2 panelSize) {
    ImVec2 mousePos = ImGui::GetMousePos();
    bool hovered = ImGui::IsItemHovered();

    // ── F키: Frame All — 씬 오브젝트 AABB를 화면에 맞춤 ────────────────────
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_F)) {
        FrameAll(panelSize);
    }

    if (hovered && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) gizmo_.SetTool(molga::GizmoTool::Select);
        if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmo_.SetTool(molga::GizmoTool::Move);
        if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmo_.SetTool(molga::GizmoTool::Rotate);
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmo_.SetTool(molga::GizmoTool::Scale);
    }

    // ── 마우스 휠 줌 (커서 위치 기준) ───────────────────────────────────────
    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
            float factor = (wheel > 0.f) ? 1.1f : (1.f / 1.1f);

            // 커서의 패널 내 정규화 좌표 (0~1)
            float nx = (mousePos.x - panelPos.x) / panelSize.x;
            float ny = (mousePos.y - panelPos.y) / panelSize.y;

            // 커서가 가리키는 월드 좌표 (줌 전)
            float zoom    = editorCamera_->GetZoom();
            float camX    = editorCamera_->GetX();
            float camY    = editorCamera_->GetY();
            float halfW   = panelSize.x * 0.5f / zoom;
            float halfH   = panelSize.y * 0.5f / zoom;
            float worldX  = camX + (nx - 0.5f) * 2.f * halfW;
            float worldY  = camY + (ny - 0.5f) * 2.f * halfH;

            // 줌 적용
            editorCamera_->Zoom(factor);

            // 줌 후 동일한 월드 좌표가 같은 화면 위치에 오도록 카메라 보정
            float newZoom = editorCamera_->GetZoom();
            float newHalfW = panelSize.x * 0.5f / newZoom;
            float newHalfH = panelSize.y * 0.5f / newZoom;
            float newCamX  = worldX - (nx - 0.5f) * 2.f * newHalfW;
            float newCamY  = worldY - (ny - 0.5f) * 2.f * newHalfH;
            editorCamera_->SetPosition(newCamX, newCamY);
        }
    }

    // ── 중간 버튼(MMB)으로만 패닝 — Space+좌클릭 제거(Phase 3 클릭 선택과 충돌) ──
    bool mmb = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    bool panActive = (hovered || isPanning_) && mmb;

    if (panActive) {
        if (!isPanning_) {
            isPanning_  = true;
            lastMouseX_ = mousePos.x;
            lastMouseY_ = mousePos.y;
        } else {
            float dx = mousePos.x - lastMouseX_;
            float dy = mousePos.y - lastMouseY_;
            // 화면 픽셀 이동을 월드 이동으로 변환 (줌 역수)
            float invZoom = 1.f / editorCamera_->GetZoom();
            editorCamera_->Move(-dx * invZoom, -dy * invZoom);
            lastMouseX_ = mousePos.x;
            lastMouseY_ = mousePos.y;
        }
    } else {
        isPanning_ = false;
    }

    // 커서 변경
    if (isPanning_) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
}

// ── Frame All (F키) ───────────────────────────────────────────────────────────

void SceneViewWindow::FrameAll(ImVec2 panelSize) {
    if (!gameObjects_ || gameObjects_->empty()) {
        // 오브젝트 없으면 원점으로 리셋
        editorCamera_->SetPosition(-panelSize.x * 0.5f, -panelSize.y * 0.5f);
        editorCamera_->SetZoom(1.f);
        return;
    }

    float minX =  1e9f, minY =  1e9f;
    float maxX = -1e9f, maxY = -1e9f;
    int   count = 0;

    for (auto& obj : *gameObjects_) {
        if (!obj || !obj->IsActive()) continue;
        if (auto* tr = obj->GetComponent<Transform>()) {
            auto pos = tr->GetPosition();
            minX = std::min(minX, pos.x);
            minY = std::min(minY, pos.y);
            maxX = std::max(maxX, pos.x);
            maxY = std::max(maxY, pos.y);
            ++count;
        }
    }

    if (count == 0) {
        editorCamera_->SetPosition(-panelSize.x * 0.5f, -panelSize.y * 0.5f);
        editorCamera_->SetZoom(1.f);
        return;
    }

    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    editorCamera_->SetPosition(cx - panelSize.x * 0.5f, cy - panelSize.y * 0.5f);

    // AABB 크기 + 20% 여백에 맞는 줌 계산
    float spanX = (maxX - minX) + 200.f;
    float spanY = (maxY - minY) + 200.f;
    if (spanX < 1.f) spanX = 1.f;
    if (spanY < 1.f) spanY = 1.f;

    float zoomX = panelSize.x / spanX;
    float zoomY = panelSize.y / spanY;
    float newZoom = std::min(zoomX, zoomY);
    newZoom = std::max(0.01f, std::min(newZoom, 50.f));
    editorCamera_->SetZoom(newZoom);
}

void SceneViewWindow::DrawContextMenu() {
    if (ImGui::BeginPopup("SceneViewContextMenu")) {
        if (ImGui::MenuItem((std::string(Icons::Cube) + " Create Empty").c_str())) {
            CreateObjectAt("New GameObject", "", ctxWorldX_, ctxWorldY_);
        }
        if (ImGui::BeginMenu((std::string(Icons::Image) + " Create 2D Object").c_str())) {
            if (ImGui::MenuItem((std::string(Icons::Image) + " Sprite").c_str())) {
                CreateObjectAt("Sprite", "SpriteRenderer", ctxWorldX_, ctxWorldY_);
            }
            if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Tilemap").c_str())) {
                CreateObjectAt("Tilemap", "TilemapRenderer", ctxWorldX_, ctxWorldY_);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

void SceneViewWindow::CreateObjectAt(const char* name, const std::string& compType, float worldX, float worldY) {
    auto cmd = std::make_unique<molga::CreateObjectCommand>(name);
    Editor::Get().GetCommandHistory().Execute(std::move(cmd));
    
    if (GameObject* obj = Editor::Get().GetSelectedObject()) {
        if (auto* transform = obj->GetComponent<Transform>()) {
            transform->SetPosition(worldX, worldY);
        }
        if (compType == "SpriteRenderer") {
            obj->AddComponent<SpriteRenderer>();
        } else if (compType == "TilemapRenderer") {
            obj->AddComponent<TilemapRenderer>();
        }
        Editor::Get().MarkSceneModified();
    }
}

void SceneViewWindow::ScreenToWorld(ImVec2 panelPos, ImVec2 panelSize, ImVec2 screen,
                                    float& outX, float& outY) const {
    molga::ScreenToWorld(ViewportCam(), panelSize.x, panelSize.y,
                         screen.x - panelPos.x, screen.y - panelPos.y, outX, outY);
}

molga::ViewportCamera SceneViewWindow::ViewportCam() const {
    return { editorCamera_->GetX(), editorCamera_->GetY(), editorCamera_->GetZoom() };
}

void SceneViewWindow::HandlePick(ImVec2 panelPos, ImVec2 panelSize) {
    if (!gameObjects_) return;
    ImVec2 m = ImGui::GetMousePos();
    float wx, wy;
    molga::ScreenToWorld(ViewportCam(), panelSize.x, panelSize.y,
                         m.x - panelPos.x, m.y - panelPos.y, wx, wy);

    std::vector<molga::PickCandidate> cands;
    for (auto& obj : *gameObjects_) {
        if (!obj || !obj->IsActive()) continue;
        auto* tr = obj->GetComponent<Transform>();
        auto* sr = obj->GetComponent<SpriteRenderer>();
        if (!tr || !sr) continue;
        Vector2 wp = tr->GetWorldPosition();
        cands.push_back({ obj->GetID(), wp.x, wp.y,
                          sr->GetWidth() * 0.5f, sr->GetHeight() * 0.5f,
                          sr->GetSortingOrder() });
    }
    auto hits = molga::PickAt(cands, wx, wy);

    auto& sel = Editor::Get().GetSelection();
    if (hits.empty()) {
        sel.Clear(molga::SelectionSource::SceneView);
    } else {
        sel.Select(hits.front(), molga::SelectionSource::SceneView);
    }
}

void SceneViewWindow::DrawSelectionOutline(ImVec2 panelPos, ImVec2 panelSize) {
    if (!gameObjects_) return;
    auto& sel = Editor::Get().GetSelection();
    if (!sel.HasSelection()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (unsigned int id : sel.SelectedIds()) {
        GameObject* go = Editor::Get().FindObjectById(id);
        if (!go) continue;
        auto* tr = go->GetComponent<Transform>();
        auto* sr = go->GetComponent<SpriteRenderer>();
        if (!tr || !sr) continue;
        Vector2 wp = tr->GetWorldPosition();
        float hw = sr->GetWidth() * 0.5f, hh = sr->GetHeight() * 0.5f;
        float sx0, sy0, sx1, sy1;
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             wp.x - hw, wp.y - hh, sx0, sy0);
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             wp.x + hw, wp.y + hh, sx1, sy1);
        bool primary = (id == sel.PrimaryId());
        dl->AddRect(ImVec2(panelPos.x + sx0, panelPos.y + sy0),
                    ImVec2(panelPos.x + sx1, panelPos.y + sy1),
                    primary ? IM_COL32(255, 170, 0, 255) : IM_COL32(255, 170, 0, 140),
                    0.f, 0, primary ? 2.f : 1.f);
    }
}
