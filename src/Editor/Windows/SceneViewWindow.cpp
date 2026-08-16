#include "SceneViewWindow.h"
#include "../EditorConstants.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Shader.h"
#include "../../Rendering/ShaderManager.h"
#include "../../Rendering/Camera2D.h"
#include "../../Rendering/RenderPass.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/RenderSystem2D.h"
#include "Rendering/WorldRenderTraversal.h"
#include "Rendering/CameraOutputLayout.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/PostProcessProfileResolver.h"
#include "Rendering/LightingFrame2D.h"
#include "Core/Profiling/ProfileScope.h"
#include "../../ECS/GameObject.h"
#include "../../ECS/Components/SpriteRenderer.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/PointLight2D.h"
#include "../../ECS/Components/ShadowOccluder2D.h"
#include "../../ECS/Components/TilemapRenderer.h"
#include "../../ECS/Components/MarrowRenderer.h"
#include "../../ECS/Components/ParticleSystem.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Components/TextRenderer2D.h"
#include "../../ECS/Components/RectTransform.h"
#include "../../UI/UISystem.h"
#include "../EditorState.h"
#include "../../Common/Log.h"
#include "../../Common/linmath.h"
#include "../../Core/PathService.h"
#include "Editor/Editor.h"
#include "Editor/Commands/ObjectCommands.h"
#include "Editor/Commands/ComponentCommands.h"
#include "Editor/Commands/SceneSnapshots.h"
#include "Editor/ScenePicker.h"
#include "../FontManager.h"
#include "Core/AssetDatabase.h"
#include "Editor/Commands/CreateSpriteFromAssetCommand.h"
#include "Editor/Project.h"
#include "Editor/Windows/TilePaletteWindow.h"
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {

Vector2 LightingLocalToWorld(const Transform& transform,
                             const Vector2& local) {
    const Vector2 scale = transform.GetWorldScale();
    const float radians =
        transform.GetWorldRotation() * 3.14159265358979323846f / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const Vector2 scaled{local.x * scale.x, local.y * scale.y};
    const Vector2 origin = transform.GetWorldPosition();
    return {
        origin.x + scaled.x * cosine - scaled.y * sine,
        origin.y + scaled.x * sine + scaled.y * cosine};
}

bool LightingWorldToLocal(const Transform& transform, const Vector2& world,
                          Vector2& local) {
    const Vector2 scale = transform.GetWorldScale();
    constexpr float epsilon = 1.0e-6f;
    if (std::fabs(scale.x) <= epsilon || std::fabs(scale.y) <= epsilon)
        return false;
    const Vector2 origin = transform.GetWorldPosition();
    const Vector2 delta = world - origin;
    const float radians =
        -transform.GetWorldRotation() * 3.14159265358979323846f / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const Vector2 unrotated{
        delta.x * cosine - delta.y * sine,
        delta.x * sine + delta.y * cosine};
    local = {unrotated.x / scale.x, unrotated.y / scale.y};
    return std::isfinite(local.x) && std::isfinite(local.y);
}

float ScreenDistanceSquared(const ImVec2& lhs, const ImVec2& rhs) {
    const float x = lhs.x - rhs.x;
    const float y = lhs.y - rhs.y;
    return x * x + y * y;
}

} // namespace

SceneViewWindow::SceneViewWindow()
    : EditorWindow(EditorConstants::WIN_SCENE),
      preferencePath_(molga::EditorPreferences::DefaultPath()) {
    editorCamera_ = std::make_unique<Camera2D>(800.f, 600.f);
    std::string warning;
    preferences_.Load(preferencePath_, &warning);
    if (!warning.empty()) Log::Warn("EditorPreferences", warning);
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
        Editor::Get().RenderStats().fboResizes++;
    }

    // ── FBO가 아직 미초기화면 생성 ───────────────────────────────────────────
    if (!fbo_.IsValid()) {
        fbo_.Init(static_cast<int>(vpW), static_cast<int>(vpH));
        Editor::Get().RenderStats().fboResizes++;
    }

    // ── 씬을 FBO에 렌더 ──────────────────────────────────────────────────────
    if (fbo_.IsValid() && renderer_) {
        RenderSceneToFBO(vpW, vpH);
    }

    // ── FBO 텍스처를 ImGui 패널에 출력 (UV Y축 반전) ─────────────────────────
    ImVec2 panelPos = ImGui::GetCursorScreenPos();
    bool sceneImageHovered = false;

    if (fbo_.IsValid()) {
        ImTextureID texId = (ImTextureID)(uintptr_t)fbo_.ColorTexture();
        // GL 텍스처는 좌하단 원점이므로 UV Y를 뒤집어야 한다
        ImGui::Image(texId, ImVec2(vpW, vpH), ImVec2(0, 1), ImVec2(1, 0));
        sceneImageHovered = ImGui::IsItemHovered();
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
                    const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
                    if (!record || record->importer != "TextureImporter" || record->importFailed) {
                        Log::Warn("SceneView", "Only a valid texture asset can create a sprite.");
                        guid.clear();
                    }
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

        const bool localSpace = gizmo_.Space() == molga::GizmoSpace::Local;
        if (ImGui::Button(localSpace ? "Local" : "World")) {
            gizmo_.SetSpace(localSpace ? molga::GizmoSpace::World
                                       : molga::GizmoSpace::Local);
        }
        ImGui::SameLine();
        
        ImGui::Spacing(); ImGui::SameLine();
        
        const molga::GizmoSnapSettings snap = gizmo_.SnapSettings();
        const bool snapOff = snap.mode == molga::SnapMode::Off;
        const char* snapLabel = "Snap: Off";
        if (!snapOff && gizmo_.Tool() == molga::GizmoTool::Move) {
            if (snap.step <= 16.f) snapLabel = "Snap: 16";
            else if (snap.step <= 32.f) snapLabel = "Snap: 32";
            else snapLabel = "Snap: 64";
        } else if (!snapOff && gizmo_.Tool() == molga::GizmoTool::Rotate) {
            snapLabel = "Snap: 15 deg";
        } else if (!snapOff && gizmo_.Tool() == molga::GizmoTool::Scale) {
            snapLabel = "Snap: 0.1";
        }
        const bool selectTool = gizmo_.Tool() == molga::GizmoTool::Select;
        if (selectTool) ImGui::BeginDisabled();
        if (ImGui::Button(snapLabel)) {
            if (gizmo_.Tool() == molga::GizmoTool::Move) {
                if (snapOff) gizmo_.SetSnap(molga::SnapMode::Grid, 16.f);
                else if (snap.step <= 16.f) gizmo_.SetSnap(molga::SnapMode::Grid, 32.f);
                else if (snap.step <= 32.f) gizmo_.SetSnap(molga::SnapMode::Grid, 64.f);
                else gizmo_.SetSnap(molga::SnapMode::Off, 0.f);
            } else if (gizmo_.Tool() == molga::GizmoTool::Rotate) {
                gizmo_.SetSnap(snapOff ? molga::SnapMode::Increment
                                       : molga::SnapMode::Off,
                               snapOff ? 15.f : 0.f);
            } else if (gizmo_.Tool() == molga::GizmoTool::Scale) {
                gizmo_.SetSnap(snapOff ? molga::SnapMode::Increment
                                       : molga::SnapMode::Off,
                               snapOff ? 0.1f : 0.f);
            }
        }
        if (selectTool) ImGui::EndDisabled();
        ImGui::SameLine();
        const bool fxEnabled = preferences_.sceneView.fxEnabled;
        if (fxEnabled) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(0.35f, 0.4f, 0.6f, 0.9f));
        }
        if (ImGui::Button("FX")) {
            preferences_.sceneView.fxEnabled = !preferences_.sceneView.fxEnabled;
            SavePreferences();
        }
        if (fxEnabled) ImGui::PopStyleColor();
        ImGui::SameLine();
        const bool litEnabled = preferences_.sceneView.litEnabled;
        if (litEnabled) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImVec4(0.45f, 0.35f, 0.16f, 0.9f));
        }
        if (ImGui::Button("Lit")) {
            preferences_.sceneView.litEnabled =
                !preferences_.sceneView.litEnabled;
            SavePreferences();
        }
        if (litEnabled) ImGui::PopStyleColor();
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ── 입력 처리 (Image 위젯이 렌더된 후) ──────────────────────────────────
    HandleInput(panelPos, ImVec2(vpW, vpH));
    DrawCameraOutputGizmos(panelPos, ImVec2(vpW, vpH));

    std::vector<GameObject*> selectedTargets;
    for (unsigned int id : Editor::Get().GetSelection().SelectedIds()) {
        if (GameObject* object = Editor::Get().FindObjectById(id)) {
            selectedTargets.push_back(object);
        }
    }
    DrawSelectionOutline(panelPos, ImVec2(vpW, vpH));
    bool tilePaintUsed = false;
    if (EditorState::Get().IsEditMode()) {
        if (auto* palette = Editor::Get().GetWindowManager().GetAs<TilePaletteWindow>(
                EditorConstants::WIN_TILE_PALETTE); palette && palette->IsOpen()) {
            float worldX = 0.0f;
            float worldY = 0.0f;
            ScreenToWorld(panelPos, ImVec2(vpW, vpH), ImGui::GetMousePos(), worldX, worldY);
            if (sceneImageHovered || palette->HasActiveStroke()) {
                tilePaintUsed = palette->HandleSceneInput(
                    {worldX, worldY}, sceneImageHovered,
                    sceneImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                    sceneImageHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left),
                    ImGui::IsMouseReleased(ImGuiMouseButton_Left));
            }
            palette->DrawSceneOverlay(ImGui::GetWindowDrawList(), panelPos,
                                      ImVec2(vpW, vpH), ViewportCam());
        }
    }
    const bool lightingHandleUsed = !tilePaintUsed &&
        DrawLightingHandles(panelPos, ImVec2(vpW, vpH));
    bool gizmoUsed = !tilePaintUsed && !lightingHandleUsed &&
        gizmo_.Draw(selectedTargets, ViewportCam(), panelPos, ImVec2(vpW, vpH));

    if (EditorState::Get().IsEditMode() && !tilePaintUsed &&
        !lightingHandleUsed && !gizmoUsed && sceneImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        HandlePick(panelPos, ImVec2(vpW, vpH));
    }

    if (EditorState::Get().IsEditMode() && sceneImageHovered &&
        !ImGui::GetIO().WantTextInput) {
        const ImGuiIO& io = ImGui::GetIO();
#if defined(__APPLE__)
        const bool command = io.KeySuper;
#else
        const bool command = io.KeyCtrl;
#endif
        const auto ids = Editor::Get().GetSelection().SelectedIds();
        if (!ids.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<molga::DeleteObjectsCommand>(ids));
        } else if (!ids.empty() && command && ImGui::IsKeyPressed(ImGuiKey_D)) {
            Editor::Get().GetCommandHistory().Execute(
                std::make_unique<molga::DuplicateObjectsCommand>(ids));
        }
    }

    // ── 우클릭 컨텍스트 메뉴 트리거 ──────────────────────────────────────────
    if (sceneImageHovered && !tilePaintUsed && !lightingHandleUsed &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImVec2 mousePos = ImGui::GetMousePos();
        ScreenToWorld(panelPos, ImVec2(vpW, vpH), mousePos, ctxWorldX_, ctxWorldY_);
        ImGui::OpenPopup("SceneViewContextMenu");
    }

    DrawContextMenu();

    ImGui::End();
}

// ── 씬 렌더 ──────────────────────────────────────────────────────────────────

void SceneViewWindow::RenderSceneToFBO(float vpW, float vpH) {
    std::shared_ptr<const molga::PostProcessProfile2D> profile;
    if (preferences_.sceneView.fxEnabled && gameObjects_) {
        Camera* mainCamera = molga::GameOutputRenderer::FindMainCamera(*gameObjects_);
        if (mainCamera && mainCamera->IsPostProcessEnabled() &&
            !mainCamera->GetPostProcessProfileGuid().empty()) {
            const auto resolved = molga::PostProcessProfileResolver::Get().Resolve(
                mainCamera->GetPostProcessProfileGuid());
            if (resolved && resolved.profile->HasActiveEffects()) profile = resolved.profile;
        }
    }

    const molga::PixelSize size{static_cast<int>(vpW), static_cast<int>(vpH)};
    if (profile) {
        std::string error;
        if (postProcessPipeline_.Prepare(size, *profile, &error)) {
            {
                ScopedFramebufferBinding binding(postProcessPipeline_.SceneTarget());
                DrawSceneBase();
            }
            const molga::PostProcessExecutionResult execution =
                postProcessPipeline_.Execute(*profile, fbo_.Id(), size);
            if (execution.success) {
                renderer_->Stats().postProcessPasses += execution.passes;
                Editor::Get().RenderStats().postProcessPasses += execution.passes;
                ScopedFramebufferBinding binding(fbo_);
                DrawUI(vpW, vpH);
                return;
            }
            error = execution.error;
        }
        const std::string warningKey = std::to_string(size.width) + "x" +
            std::to_string(size.height) + ":" + error;
        if (postProcessWarnings_.insert(warningKey).second) {
            Log::Warn("SceneView", "FX preview unavailable: " + error);
        }
    }

    ScopedFramebufferBinding binding(fbo_);
    DrawSceneBase();
    DrawUI(vpW, vpH);
}

void SceneViewWindow::DrawSceneBase() {
    glClearColor(0.18f, 0.18f, 0.22f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 알파 블렌딩 활성화
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 그리드 먼저 그리기
    DrawGrid();

    // 그 위에 스프라이트
    DrawSprites();

}

void SceneViewWindow::SavePreferences() {
    // Merge with the latest file so Game View and Scene View can save their
    // independent sections without reverting each other's state.
    molga::EditorPreferences latest;
    std::string ignored;
    latest.Load(preferencePath_, &ignored);
    latest.sceneView = preferences_.sceneView;
    std::string error;
    if (!latest.SaveAtomic(preferencePath_, &error) && !error.empty()) {
        Log::Warn("EditorPreferences", error);
    }
    preferences_.gameView = latest.gameView;
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

    // Reset stats before drawing
    renderer_->ResetStats();

    auto& fc = Editor::Get().FrameCounters();

    Camera2D* activeCamera = editorCamera_.get();

    // Collect render commands
    molga::RenderQueue queue;
    queue.SetViewBounds(activeCamera->GetViewBounds());
    Camera* previewCamera = preferences_.sceneView.litEnabled
        ? molga::GameOutputRenderer::FindMainCamera(*gameObjects_) : nullptr;
    const auto collectOverride =
        [&](Component& component, molga::RenderQueue& target) {
            if (dynamic_cast<SpriteRenderer*>(&component)) ++fc.sprites;
            if (auto* tilemap = dynamic_cast<TilemapRenderer*>(&component)) {
                tilemap->CollectRender(target);
                fc.tileChunks += tilemap->GetLastSubmittedChunkCount();
                return true;
            }
            if (auto* particles = dynamic_cast<ParticleSystem*>(&component)) {
                if (!EditorState::Get().IsPlayMode() &&
                    particles->TryGetEditorPreviewEmitter()) {
                    // The preview replaces the live emitter at the same
                    // component traversal slot without touching authored state.
                    particles->UpdateEditorPreview(ImGui::GetIO().DeltaTime);
                    particles->CollectEditorPreviewRender(target);
                    fc.particles +=
                        particles->TryGetEditorPreviewEmitter()->GetActiveCount();
                } else {
                    particles->CollectRender(target);
                    fc.particles += particles->GetEmitter().GetActiveCount();
                }
                return true;
            }
            if (dynamic_cast<TextRenderer2D*>(&component)) ++fc.text;
            return false;
        };
    {
        MOLGA_PROFILE_SCOPE("RenderQueue.Collect", molga::ProfileCategory::Rendering);
        if (previewCamera) {
            molga::CollectWorldRender(
                *gameObjects_, queue, previewCamera->GetCullingMask(),
                collectOverride);
        } else {
            molga::CollectWorldRender(*gameObjects_, queue, collectOverride);
        }
    }

    molga::LightingRenderContext2D lightingContext;
    const molga::LightingRenderContext2D* lightingContextPointer = nullptr;
    if (previewCamera && previewCamera->IsLightingEnabled() &&
        queue.HasLitReceivers()) {
        Shader* litShader = ShaderManager::Get().Get("batch_lit");
        const molga::PixelSize size{
            std::max(1, fbo_.Width()), std::max(1, fbo_.Height())};
        if (!litShader || !litShader->IsValid()) {
            queue.ForceUnlit();
            if (lightingWarnings_.insert("shader").second) {
                Log::Warn(
                    "SceneView",
                    "Lit preview unavailable: batch_lit shader is invalid.");
            }
        } else {
            const molga::LightingFrame2D frame =
                molga::LightingFrame2D::Build(
                    *gameObjects_, *previewCamera, size, activeCamera);
            if (frame.discardedLightCount > 0 &&
                lightingWarnings_.insert("light-budget").second) {
                Log::Warn(
                    "SceneView",
                    "Lit preview PointLight2D budget exceeded; later lights "
                    "were deterministically excluded.");
            }
            if (frame.discardedShadowLightCount > 0 &&
                lightingWarnings_.insert("shadow-light-budget").second) {
                Log::Warn(
                    "SceneView",
                    "Lit preview shadow-light budget exceeded; extra selected "
                    "lights remain unshadowed.");
            }
            const bool occluderBudgetExceeded = std::any_of(
                frame.shadowLayers.begin(), frame.shadowLayers.end(),
                [](const molga::ShadowMaskLayerFrame2D& layer) {
                    return layer.discardedOccluderCount > 0;
                });
            if (occluderBudgetExceeded &&
                lightingWarnings_.insert("occluder-budget").second) {
                Log::Warn(
                    "SceneView",
                    "Lit preview shadow-occluder budget exceeded; later "
                    "occluders were deterministically excluded.");
            }
            renderer_->Stats().selectedLightCount +=
                static_cast<int>(frame.lights.size());
            molga::LightingPipelinePrepareResult2D prepared;
            if (lightingPipeline_.Prepare(frame, *activeCamera, prepared) &&
                prepared.ready) {
                lightingContext = lightingPipeline_.ContextForTarget(
                    size, {0, 0, size.width, size.height});
                if (lightingContext.IsUsable()) {
                    lightingContextPointer = &lightingContext;
                    ++renderer_->Stats().lightingPasses;
                    renderer_->Stats().shadowPasses += prepared.shadowPasses;
                    renderer_->Stats().shadowedLightCount +=
                        prepared.shadowedLightCount;
                    renderer_->Stats().shadowCasterDrawCount +=
                        prepared.shadowCasterDrawCount;
                }
                if (prepared.shadowFallback &&
                    lightingWarnings_.insert("shadow:" + prepared.error).second) {
                    Log::Warn(
                        "SceneView",
                        "Lit preview shadow fallback: " + prepared.error);
                }
            } else {
                queue.ForceUnlit();
                const std::string key = "context:" + prepared.error;
                if (lightingWarnings_.insert(key).second) {
                    Log::Warn(
                        "SceneView",
                        "Lit preview unavailable: " + prepared.error);
                }
            }
        }
    }

    {
        molga::RenderPass pass(*renderer_, spriteShader_, activeCamera);
        molga::RenderSystem2D::Get().Render(
            queue, renderer_, activeCamera, lightingContextPointer);
    }

    // Scene View resets the shared Renderer for its editor-camera pass. Keep
    // Game View's camera/PostFX totals independent of unordered window order.
    const int outputCameraPasses =
        Editor::Get().RenderStats().outputCameraPasses;
    const int postProcessPasses =
        Editor::Get().RenderStats().postProcessPasses;
    const int lightingPasses =
        Editor::Get().RenderStats().lightingPasses;
    const int shadowPasses =
        Editor::Get().RenderStats().shadowPasses;
    const int selectedLightCount =
        Editor::Get().RenderStats().selectedLightCount;
    const int shadowedLightCount =
        Editor::Get().RenderStats().shadowedLightCount;
    const int shadowCasterDrawCount =
        Editor::Get().RenderStats().shadowCasterDrawCount;
    Editor::Get().RenderStats() = renderer_->Stats();
    Editor::Get().RenderStats().outputCameraPasses += outputCameraPasses;
    Editor::Get().RenderStats().postProcessPasses += postProcessPasses;
    Editor::Get().RenderStats().lightingPasses += lightingPasses;
    Editor::Get().RenderStats().shadowPasses += shadowPasses;
    Editor::Get().RenderStats().selectedLightCount += selectedLightCount;
    Editor::Get().RenderStats().shadowedLightCount += shadowedLightCount;
    Editor::Get().RenderStats().shadowCasterDrawCount +=
        shadowCasterDrawCount;
    Editor::Get().FrameCounters().drawCalls = renderer_->Stats().drawCalls;
}

// ── 입력 처리 ─────────────────────────────────────────────────────────────────

void SceneViewWindow::DrawUI(float vpW, float vpH) {
    if (!renderer_ || !spriteShader_ || !gameObjects_) return;
    molga::RenderQueue queue;
    UISystem::Get().CollectRender(*gameObjects_, {vpW, vpH}, queue);
    if (queue.GetCommands().empty()) return;

    Camera2D uiCamera(vpW, vpH);
    molga::RenderPass pass(*renderer_, spriteShader_, &uiCamera);
    molga::RenderSystem2D::Get().Render(queue, renderer_, &uiCamera);
}

void SceneViewWindow::DrawCameraOutputGizmos(ImVec2 panelPos,
                                              ImVec2 panelSize) {
    if (!gameObjects_ || gameObjects_->empty() || panelSize.x <= 0.0f ||
        panelSize.y <= 0.0f) {
        return;
    }

    molga::PixelSize logicalSize{800, 600};
    if (Project::Get().IsOpen()) {
        const auto& window = Project::Get().GetBuildProfile().window;
        if (window.width > 0 && window.height > 0)
            logicalSize = {window.width, window.height};
    }
    const molga::CameraOutputLayout layout =
        molga::CameraOutputLayout::Build(*gameObjects_, logicalSize);
    if (layout.Entries().empty()) return;

    static constexpr ImU32 colors[] = {
        IM_COL32(64, 210, 255, 235),
        IM_COL32(255, 185, 64, 235),
        IM_COL32(100, 230, 130, 235),
        IM_COL32(205, 120, 255, 235),
        IM_COL32(255, 110, 145, 235),
        IM_COL32(95, 155, 255, 235),
        IM_COL32(230, 220, 90, 235),
        IM_COL32(90, 225, 205, 235),
    };
    const auto colorFor = [](const molga::CameraOutputEntry& entry) {
        constexpr std::size_t colorCount = sizeof(colors) / sizeof(colors[0]);
        return colors[entry.sceneOrder % colorCount];
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(panelPos,
        ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y), true);

    // World-space 2D frusta. The corners come only from the immutable layout
    // snapshot; drawing these lines does not submit anything to the scene FBO.
    for (const molga::CameraOutputEntry& entry : layout.Entries()) {
        if (!entry.renderable) continue;
        const auto worldCorners =
            molga::CameraFrustumWorldCorners(entry.view);
        ImVec2 screenCorners[4];
        for (std::size_t index = 0; index < worldCorners.size(); ++index) {
            float screenX = 0.0f;
            float screenY = 0.0f;
            molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                                 worldCorners[index].x, worldCorners[index].y,
                                 screenX, screenY);
            screenCorners[index] =
                ImVec2(panelPos.x + screenX, panelPos.y + screenY);
        }

        const bool selected =
            Editor::Get().GetSelection().IsSelected(entry.cameraObjectId);
        const float thickness = selected ? 3.0f :
            entry.role == CameraOutputRole::Primary ? 2.0f : 1.5f;
        const ImU32 color = colorFor(entry);
        drawList->AddPolyline(screenCorners, 4, color,
                              ImDrawFlags_Closed, thickness);

        const Vector2 center = molga::CameraViewLocalToWorld(
            entry.view,
            static_cast<float>(entry.view.viewportSize.width) * 0.5f,
            static_cast<float>(entry.view.viewportSize.height) * 0.5f);
        float centerX = 0.0f;
        float centerY = 0.0f;
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             center.x, center.y, centerX, centerY);
        const ImVec2 screenCenter(panelPos.x + centerX, panelPos.y + centerY);
        drawList->AddLine(ImVec2(screenCenter.x - 5.0f, screenCenter.y),
                          ImVec2(screenCenter.x + 5.0f, screenCenter.y),
                          color, thickness);
        drawList->AddLine(ImVec2(screenCenter.x, screenCenter.y - 5.0f),
                          ImVec2(screenCenter.x, screenCenter.y + 5.0f),
                          color, thickness);

        const char* role = entry.role == CameraOutputRole::Primary ? "P" : "S";
        char label[160];
        const GameObject* owner = entry.camera ? entry.camera->GetGameObject() : nullptr;
        std::snprintf(label, sizeof(label), "%s  %s  d%d  %dx%d",
                      role, owner ? owner->GetName().c_str() : "Camera",
                      entry.depth, entry.viewport.width, entry.viewport.height);
        drawList->AddText(ImVec2(screenCorners[0].x + 4.0f,
                                 screenCorners[0].y + 4.0f), color, label);
    }

    // A normalized output-layout inset makes split and PIP viewport placement
    // visible without turning Scene View into a second composition renderer.
    if (panelSize.x >= 180.0f && panelSize.y >= 120.0f) {
        float previewWidth = std::min(220.0f, panelSize.x * 0.30f);
        float previewHeight = previewWidth *
            static_cast<float>(logicalSize.height) /
            static_cast<float>(logicalSize.width);
        const float maxPreviewHeight = std::min(140.0f, panelSize.y * 0.28f);
        if (previewHeight > maxPreviewHeight) {
            previewHeight = maxPreviewHeight;
            previewWidth = previewHeight *
                static_cast<float>(logicalSize.width) /
                static_cast<float>(logicalSize.height);
        }

        const ImVec2 previewMin(
            panelPos.x + panelSize.x - previewWidth - 10.0f,
            panelPos.y + 28.0f);
        const ImVec2 previewMax(previewMin.x + previewWidth,
                                previewMin.y + previewHeight);
        drawList->AddRectFilled(
            ImVec2(previewMin.x - 5.0f, previewMin.y - 22.0f),
            ImVec2(previewMax.x + 5.0f, previewMax.y + 5.0f),
            IM_COL32(12, 14, 20, 190), 3.0f);
        char title[96];
        std::snprintf(title, sizeof(title), "Camera Output  %dx%d",
                      logicalSize.width, logicalSize.height);
        drawList->AddText(ImVec2(previewMin.x, previewMin.y - 18.0f),
                          IM_COL32(220, 220, 225, 220), title);
        drawList->AddRect(previewMin, previewMax,
                          IM_COL32(170, 170, 180, 220), 0.0f, 0, 1.0f);

        for (const molga::CameraOutputEntry& entry : layout.Entries()) {
            const float left = previewMin.x + previewWidth *
                static_cast<float>(entry.viewport.x) / logicalSize.width;
            const float top = previewMin.y + previewHeight *
                static_cast<float>(entry.viewport.y) / logicalSize.height;
            const float right = previewMin.x + previewWidth *
                static_cast<float>(entry.viewport.x + entry.viewport.width) /
                logicalSize.width;
            const float bottom = previewMin.y + previewHeight *
                static_cast<float>(entry.viewport.y + entry.viewport.height) /
                logicalSize.height;
            const ImU32 color = entry.renderable
                ? colorFor(entry) : IM_COL32(255, 75, 75, 235);
            const bool selected =
                Editor::Get().GetSelection().IsSelected(entry.cameraObjectId);
            drawList->AddRect(ImVec2(left, top), ImVec2(right, bottom), color,
                              0.0f, 0, selected ? 3.0f : 1.5f);
            if (right - left >= 18.0f && bottom - top >= 12.0f) {
                drawList->AddText(ImVec2(left + 3.0f, top + 1.0f), color,
                    entry.role == CameraOutputRole::Primary ? "P" : "S");
            }
        }
    }

    drawList->PopClipRect();
}

void SceneViewWindow::CancelLightingHandleDrag() {
    if (lightingHandleKind_ != LightingHandleKind::None &&
        lightingHandleObjectId_ != 0 && !lightingHandleBefore_.is_null()) {
        if (GameObject* object =
                Editor::Get().FindObjectById(lightingHandleObjectId_)) {
            molga::RestoreComponentSnapshot(object, lightingHandleBefore_);
        }
    }
    lightingHandleKind_ = LightingHandleKind::None;
    lightingHandleObjectId_ = 0;
    lightingHandleInstanceId_ = 0;
    lightingHandleVertex_ = -1;
    lightingHandleBefore_ = nlohmann::json{};
}

bool SceneViewWindow::DrawLightingHandles(ImVec2 panelPos,
                                          ImVec2 panelSize) {
    if (!EditorState::Get().IsEditMode()) {
        if (lightingHandleKind_ != LightingHandleKind::None)
            CancelLightingHandleDrag();
        return false;
    }

    const auto& selected = Editor::Get().GetSelection().SelectedIds();
    if (selected.size() != 1U) {
        if (lightingHandleKind_ != LightingHandleKind::None)
            CancelLightingHandleDrag();
        return false;
    }
    GameObject* object = Editor::Get().FindObjectById(selected.front());
    Transform* transform = object ? object->GetComponent<Transform>() : nullptr;
    if (!object || !transform) {
        if (lightingHandleKind_ != LightingHandleKind::None)
            CancelLightingHandleDrag();
        return false;
    }

    if (lightingHandleKind_ != LightingHandleKind::None) {
        if (object->GetID() != lightingHandleObjectId_) {
            CancelLightingHandleDrag();
            return false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            CancelLightingHandleDrag();
            return true;
        }

        float mouseWorldX = 0.0f;
        float mouseWorldY = 0.0f;
        ScreenToWorld(panelPos, panelSize, ImGui::GetMousePos(),
                      mouseWorldX, mouseWorldY);
        const Vector2 mouseWorld{mouseWorldX, mouseWorldY};
        Component* editedComponent = nullptr;
        if (lightingHandleKind_ == LightingHandleKind::PointRadius) {
            PointLight2D* light = object->GetComponent<PointLight2D>();
            if (!light || light->GetInstanceID() != lightingHandleInstanceId_) {
                CancelLightingHandleDrag();
                return true;
            }
            const Vector2 center = transform->GetWorldPosition();
            const Vector2 delta = mouseWorld - center;
            light->SetRadius(std::sqrt(delta.x * delta.x + delta.y * delta.y));
            editedComponent = light;
        } else {
            ShadowOccluder2D* occluder =
                object->GetComponent<ShadowOccluder2D>();
            if (!occluder ||
                occluder->GetInstanceID() != lightingHandleInstanceId_ ||
                occluder->GetShape() != ShadowOccluderShape2D::Polygon ||
                lightingHandleVertex_ < 0 ||
                static_cast<std::size_t>(lightingHandleVertex_) >=
                    occluder->GetVertices().size()) {
                CancelLightingHandleDrag();
                return true;
            }
            Vector2 local;
            if (LightingWorldToLocal(*transform, mouseWorld, local)) {
                std::vector<Vector2> vertices = occluder->GetVertices();
                vertices[static_cast<std::size_t>(lightingHandleVertex_)] =
                    local;
                occluder->SetPolygon(vertices);
            }
            editedComponent = occluder;
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            nlohmann::json after;
            if (editedComponent)
                after = molga::CaptureComponentSnapshot(editedComponent);
            const std::string componentType =
                editedComponent ? editedComponent->GetTypeName() : std::string{};
            const unsigned int targetId = lightingHandleObjectId_;
            const nlohmann::json before = lightingHandleBefore_;
            lightingHandleKind_ = LightingHandleKind::None;
            lightingHandleObjectId_ = 0;
            lightingHandleInstanceId_ = 0;
            lightingHandleVertex_ = -1;
            lightingHandleBefore_ = nlohmann::json{};
            if (!componentType.empty() && before != after) {
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::BatchComponentSnapshotCommand>(
                        std::vector<molga::ComponentSnapshotChange>{
                            {targetId, componentType, before, std::move(after)}},
                        true));
            }
        }
        return true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(panelPos,
        ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y), true);
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool mouseInsideImage =
        mouse.x >= panelPos.x && mouse.y >= panelPos.y &&
        mouse.x < panelPos.x + panelSize.x &&
        mouse.y < panelPos.y + panelSize.y;
    constexpr float pickRadiusSquared = 9.0f * 9.0f;
    float bestDistance = pickRadiusSquared;
    LightingHandleKind hoveredKind = LightingHandleKind::None;
    int hoveredVertex = -1;
    Component* hoveredComponent = nullptr;

    if (PointLight2D* light = object->GetComponent<PointLight2D>()) {
        const Vector2 centerWorld = transform->GetWorldPosition();
        const Vector2 handleWorld{
            centerWorld.x + light->GetRadius(), centerWorld.y};
        float centerX = 0.0f, centerY = 0.0f;
        float handleX = 0.0f, handleY = 0.0f;
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             centerWorld.x, centerWorld.y, centerX, centerY);
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             handleWorld.x, handleWorld.y, handleX, handleY);
        const ImVec2 center{panelPos.x + centerX, panelPos.y + centerY};
        const ImVec2 handle{panelPos.x + handleX, panelPos.y + handleY};
        draw->AddCircle(center, std::fabs(handle.x - center.x),
                        IM_COL32(255, 205, 75, 210), 64, 1.5f);
        const float distance = ScreenDistanceSquared(mouse, handle);
        const bool hovered = mouseInsideImage && distance <= bestDistance;
        draw->AddCircleFilled(
            handle, hovered ? 6.0f : 5.0f,
            hovered ? IM_COL32(255, 245, 170, 255)
                    : IM_COL32(255, 205, 75, 255), 16);
        if (hovered) {
            bestDistance = distance;
            hoveredKind = LightingHandleKind::PointRadius;
            hoveredComponent = light;
        }
    }

    if (ShadowOccluder2D* occluder =
            object->GetComponent<ShadowOccluder2D>();
        occluder && occluder->IsShapeValid() &&
        occluder->GetShape() == ShadowOccluderShape2D::Polygon) {
        const auto& vertices = occluder->GetVertices();
        std::vector<ImVec2> screenVertices;
        screenVertices.reserve(vertices.size());
        for (const Vector2& local : vertices) {
            const Vector2 world = LightingLocalToWorld(*transform, local);
            float screenX = 0.0f, screenY = 0.0f;
            molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                                 world.x, world.y, screenX, screenY);
            screenVertices.emplace_back(
                panelPos.x + screenX, panelPos.y + screenY);
        }
        if (screenVertices.size() >= 3U) {
            draw->AddPolyline(screenVertices.data(),
                static_cast<int>(screenVertices.size()),
                IM_COL32(100, 215, 255, 225), ImDrawFlags_Closed, 2.0f);
        }
        for (std::size_t index = 0; index < screenVertices.size(); ++index) {
            const float distance =
                ScreenDistanceSquared(mouse, screenVertices[index]);
            const bool hovered = mouseInsideImage && distance <= bestDistance;
            draw->AddCircleFilled(
                screenVertices[index], hovered ? 6.0f : 4.5f,
                hovered ? IM_COL32(195, 245, 255, 255)
                        : IM_COL32(100, 215, 255, 255), 12);
            if (hovered) {
                bestDistance = distance;
                hoveredKind = LightingHandleKind::PolygonVertex;
                hoveredVertex = static_cast<int>(index);
                hoveredComponent = occluder;
            }
        }
    }
    draw->PopClipRect();

    if (hoveredKind != LightingHandleKind::None &&
        hoveredComponent &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        lightingHandleKind_ = hoveredKind;
        lightingHandleObjectId_ = object->GetID();
        lightingHandleInstanceId_ = hoveredComponent->GetInstanceID();
        lightingHandleVertex_ = hoveredVertex;
        lightingHandleBefore_ =
            molga::CaptureComponentSnapshot(hoveredComponent);
        return true;
    }
    return hoveredKind != LightingHandleKind::None;
}

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

    if (GameObject* ui = UISystem::Get().HitTest(
            *gameObjects_, {panelSize.x, panelSize.y},
            {m.x - panelPos.x, m.y - panelPos.y})) {
        if (ImGui::GetIO().KeyShift) {
            Editor::Get().GetSelection().Add(
                ui->GetID(), molga::SelectionSource::SceneView);
        } else {
            Editor::Get().GetSelection().Select(
                ui->GetID(), molga::SelectionSource::SceneView);
        }
        return;
    }

    float wx, wy;
    molga::ScreenToWorld(ViewportCam(), panelSize.x, panelSize.y,
                         m.x - panelPos.x, m.y - panelPos.y, wx, wy);

    std::vector<molga::PickCandidate> cands;
    std::uint64_t sceneSubmission = 0;
    molga::ForEachWorldRenderComponent(
        *gameObjects_, [&](Component& component) {
            const std::uint64_t componentSubmission = sceneSubmission++;
            auto* sprite = dynamic_cast<SpriteRenderer*>(&component);
            if (!sprite) return;
            GameObject* object = sprite->GetGameObject();
            Transform* transform = object ? object->GetComponent<Transform>() : nullptr;
            if (!object || !transform) return;
            const std::optional<AABB> bounds = sprite->GetWorldBounds();
            if (!bounds) return;
            const Vector2 worldPosition = transform->GetWorldPosition();
            molga::SortKey sortKey = molga::MakeWorldSortKey(
                sprite->GetWorldSortSettings(), worldPosition.y);
            sortKey.submissionIndex = componentSubmission;
            cands.push_back({object->GetID(),
                             bounds->x + bounds->width * 0.5f,
                             bounds->y + bounds->height * 0.5f,
                             bounds->width * 0.5f,
                             bounds->height * 0.5f, sortKey});
        });
    auto hits = molga::PickAt(cands, wx, wy);

    auto& sel = Editor::Get().GetSelection();
    if (hits.empty()) {
        if (!ImGui::GetIO().KeyShift) sel.Clear(molga::SelectionSource::SceneView);
    } else {
        if (ImGui::GetIO().KeyShift)
            sel.Add(hits.front(), molga::SelectionSource::SceneView);
        else
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
        if (auto* rect = go->GetComponent<RectTransform>(); rect && rect->FindCanvas()) {
            const AABB ui = rect->GetScreenRect({panelSize.x, panelSize.y});
            bool primary = (id == sel.PrimaryId());
            dl->AddRect(ImVec2(panelPos.x + ui.x, panelPos.y + ui.y),
                        ImVec2(panelPos.x + ui.x + ui.width,
                               panelPos.y + ui.y + ui.height),
                        primary ? IM_COL32(255, 170, 0, 255)
                                : IM_COL32(255, 170, 0, 140),
                        0.f, 0, primary ? 2.f : 1.f);
            continue;
        }
        auto* tr = go->GetComponent<Transform>();
        auto* sr = go->GetComponent<SpriteRenderer>();
        if (!tr || !sr) continue;
        const std::optional<AABB> bounds = sr->GetWorldBounds();
        if (!bounds) continue;
        float sx0, sy0, sx1, sy1;
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             bounds->x, bounds->y, sx0, sy0);
        molga::WorldToScreen(ViewportCam(), panelSize.x, panelSize.y,
                             bounds->x + bounds->width,
                             bounds->y + bounds->height, sx1, sy1);
        bool primary = (id == sel.PrimaryId());
        dl->AddRect(ImVec2(panelPos.x + sx0, panelPos.y + sy0),
                    ImVec2(panelPos.x + sx1, panelPos.y + sy1),
                    primary ? IM_COL32(255, 170, 0, 255) : IM_COL32(255, 170, 0, 140),
                    0.f, 0, primary ? 2.f : 1.f);
    }
}
