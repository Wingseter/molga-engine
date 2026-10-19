#pragma once

#include "Editor/EditorPreferences.h"
#include "Editor/GameViewLayout.h"
#include "Editor/Windows/EditorWindow.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/PixelSize.h"
#include "Platform/NativeInputEvent.h"
#include "Platform/Window.h"
#include "UI/UIFixedTickClock.h"
#include "UI/UILayoutSystem.h"

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

class GameObject;
class World;
class Renderer;
class Shader;

class GameViewWindow : public EditorWindow {
public:
    GameViewWindow();
    ~GameViewWindow() override;

    void OnGUI() override;
    void SetSceneResources(
        Renderer* renderer,
        Shader* spriteShader,
        World* world,
        TextRenderer* textRenderer,
        molga::text::TextDiagnosticSink* textDiagnostics);
    // Task 10.2 Step 9d: Game View도 오브젝트 벡터가 아니라 World를 본다.
    // 월드가 없으면 그 표면은 렌더도 입력 처리도 건너뛴다 — 세대 0으로
    // 만들어진 UI 식별자는 어떤 살아 있는 월드도 가리키지 않는다.
    void SetActiveWorld(World* world) { world_ = world; }

    // Task 12.3 Step 6: 이번 루프 반복의 네이티브 입력 배치. main이 PollEvents 바로
    // 뒤에 한 번 넘긴다. 다음 호출이 덮어쓰므로 쓰이지 않은 배치는 버려진다.
    void SetFrameNativeInput(molga::platform::NativeInputBatch&& batch);

    // Called before gameplay scripts. Input is sampled from this panel's
    // native platform window and mapped to exact game-output pixels. The frame's
    // native batch goes through UISystem::ProcessFrame exactly once and its
    // retained N+1 snapshot is what OnGUI renders (frame order: ProcessFrame ->
    // script Update -> render).
    void ProcessPlayInput();
    void ResetPlayInput();
    // 종료 단계용: 소유권만 놓는다. 월드도 UISystem도 건드리지 않는다 — 이 시점에
    // world_는 이미 죽었을 수 있다.
    void ReleaseRetainedUISnapshots();

private:
    molga::PixelSize RequestedOutputSize() const;
    molga::PixelSize RequestedLogicalSize() const;
    molga::GameOutputScaleMode RequestedScaleMode() const;
    void DrawToolbar();
    void SavePreferences();
    void LoseInputFocus();
    // 포인터 이탈을 UI에 알린다(hover/capture/pressed만). world_를 읽지 않는다.
    void DepartUIPointer();

    Renderer* renderer_ = nullptr;
    Shader* spriteShader_ = nullptr;
    // Task 8.2 Step 7d: 이 프레임의 텍스트 권한. main이 주입한 그 하나다.
    TextRenderer* textRenderer_ = nullptr;
    molga::text::TextDiagnosticSink* textDiagnostics_ = nullptr;
    World* world_ = nullptr;
    molga::RenderTarget outputTarget_;
    molga::GameOutputRenderer outputRenderer_;

    molga::EditorPreferences preferences_;
    std::filesystem::path preferencePath_;
    molga::PixelSize activeOutput_{};
    molga::PixelSize lastAttemptedOutput_{};
    molga::PixelSize logicalOutput_{};
    molga::OutputPresentationLayout presentation_{};
    molga::GameViewLayout layout_{};
    std::string outputError_;
    bool hasOutputCamera_ = false;

    ImVec2 imageScreenOrigin_{0.0f, 0.0f};
    ImGuiID platformViewportId_ = 0;
    bool imageValid_ = false;
    bool inputFocused_ = false;
    // 직전 OnGUI에서 ImGui가 이미지 위라고 보고했는가(포커스를 만드는 눌림 판정용).
    bool imageHovered_ = false;
    molga::WindowId inputSourceWindow_ = 0;

    // ── Task 12.3 Step 6 ────────────────────────────────────────────────────
    molga::platform::NativeInputBatch pendingBatch_;
    // 표면마다 하나. 월드/창이 바뀌어도 되돌리지 않는다: 장부는 (창, 세대)별로 서고,
    // 인덱스는 늘기만 하면 된다. 일시정지 뒤에 되돌리면 장부보다 낮아져 거절된다.
    molga::ui::UIFixedTickClock tickClock_;
    std::uint64_t frameIndex_ = 0;
    // ProcessPlayInput가 게시한 이번 프레임의 N+1. OnGUI가 가져가 그린다 — 프레임을
    // 넘겨 쥐지 않는다(수명 토큰이 장치 종료를 막는다).
    molga::ui::UISnapshotPtr renderSnapshot_;
    // 마지막으로 프레임을 처리한 표면의 이름. 포인터 이탈은 world_를 읽지 않고 이것을 쓴다
    // (씬 전환 커밋 뒤의 world_는 이미 파괴됐을 수 있다).
    molga::WindowId uiSurfaceWindow_ = 0;
    std::uint64_t uiSurfaceWorldGeneration_ = 0;
    // 자식 창의 보이는 영역(스크롤바 제외, 화면 좌표). 100%에서 이미지가 그 밖으로
    // 삐져나와도 가려진 부분은 포인터를 받지 않는다.
    bool childClipValid_ = false;
    ImVec2 childClipMin_{0.0f, 0.0f};
    ImVec2 childClipMax_{0.0f, 0.0f};
};
