#pragma once

#include "Platform/Window.h"
#include "UI/UIInputEvent.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UIRuntimeIdentity.h"

#include <cstdint>
#include <optional>

class World;

namespace molga::text {
class TextDiagnosticSink;
}

namespace molga::ui {

// 방향 색인은 UIHitTargetSnapshot::navigation.explicitTargets의 [Up, Down, Left,
// Right] 순서다.
enum class UINavigationDirection : std::uint8_t {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3
};

// UIInputEvent::control 도메인(사용자 결정 2026-10-18)을 아는 유일한 곳이다.
// Key는 Input::KeyCode, GamepadButton은 Input::GamepadButton. active=false,
// 축 이벤트, 방향이 아닌 키/버튼은 방향이 아니다 — 게임패드 스틱은 포커스를
// 옮기지 않는다.
std::optional<UINavigationDirection> UINavigationDirectionOf(
    const UIInputEvent& event) noexcept;

// 포커스/캡처의 **상태**는 UIInteractionStateTable에 산다(프로세스에 하나, 표면
// 키별). 이 클래스는 데이터 멤버가 없고, 그 표를 쓰는 세 번째 규칙 묶음이다:
// 포커스 이동·정리·창 포커스 상실. 여러 인스턴스가 같은 표를 본다.
//
// 이벤트 벡터를 받는 API가 없다(Step 7). 비-final이어야 한다: 시험이 멤버
// 이름 특성으로 그 금지를 검사한다.
class UIFocusSystem {
public:
    // 계획 단계. N만 읽고, 콜백도 세대도 표도 건드리지 않는다. 부적격 계획에는
    // 아무것도 쓰지 않는다.
    void ProjectEvent(const UISnapshot& n, PlannedUIEvent& plan,
                      UIPlanningState& projected) const;

    // 발송 단계. Focus 단계 대상만 해석한다(planned.TargetFor(Focus)).
    UIEventHandlerResult HandleEvent(World& world, const UISnapshot& n,
                                     const PlannedUIEvent& planned,
                                     molga::text::TextDiagnosticSink& sink);

    // 프레임 시작과 콜백 뒤. 포커스·캡처·hover·버튼 항목의 네 필드를 다시
    // 해석해 교체·삭제·비활성·상호작용 불가가 된 대상을 정리한다.
    void Revalidate(World& world, const UISnapshot& n,
                    molga::text::TextDiagnosticSink& sink);

    std::optional<UIRuntimeTargetIdentity> Focused(
        molga::WindowId windowId, std::uint64_t worldGeneration) const;
    std::optional<UIRuntimeTargetIdentity> CapturedPointer(
        molga::WindowId windowId, std::uint64_t worldGeneration) const;

    // 포인터 이탈: hover/capture/pressed만. 키보드·게임패드 포커스는 남는다.
    void ClearPointerState(molga::WindowId windowId,
                           std::uint64_t worldGeneration,
                           molga::text::TextDiagnosticSink& sink);
    // 네이티브 창 포커스 상실: 그 창의 모든 세대에서 포커스·캡처·hover·pressed
    // 전부.
    void OnNativeWindowFocusLost(molga::WindowId windowId,
                                 molga::text::TextDiagnosticSink& sink);
    // 세대를 올리지 않는다: 죽은 월드의 표는 어떤 스냅샷에도 보이지 않는다.
    void OnWorldReleased(std::uint64_t worldGeneration);
};

}  // namespace molga::ui
