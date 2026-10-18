#pragma once

#include "Platform/Window.h"
#include "UI/UIInputEvent.h"
#include "UI/UILayoutSnapshot.h"

class World;

namespace molga::text {
class TextDiagnosticSink;
}

namespace molga::ui {

// 점 아래에서 가장 위에 그려진 hit 기록 하나(상호작용 여부와 무관). 게시된
// hitTargets의 정확한 역순을 보고, 점이 사각형과 이미 조상과 교집합인 클립
// 안에 있어야 한다(먼 경계는 열려 있다). 라우터의 스크롤 대상 선택과 Scene View
// 선택이 이 규칙 한 벌을 함께 쓴다.
const UIHitTargetSnapshot* TopmostHitRecordAt(const UISnapshot& snapshot,
                                              molga::FixedPoint point) noexcept;

// 순서 있는 배치를 스냅샷 N에 대해 계획하고, 계획 하나를 발송한다.
//
// **이벤트 벡터를 받는 API가 없다.** 배치를 도는 루프는 프레임 orchestrator
// (Task 12.3의 UISystem::ProcessFrame) 하나뿐이다. 여기에 벡터 오버로드를
// 하나라도 두면 순서를 정하는 주인이 둘이 되고, 그 둘은 언젠가 갈린다.
// tests/test_ui_input.cpp가 그 금지를 컴파일 시각의 것으로 만든다.
class UIInputRouter {
public:
    // 계획 한 걸음. projected를 **순차적으로** 밀고 나간다: 같은 배치의 뒤
    // 이벤트는 앞 이벤트가 투사한 포커스/캡처를 본다. 어떤 콜백도 부르지
    // 않고 N+1을 읽지 않는다.
    PlannedUIEvent PlanNext(const UISnapshot& n,
                            molga::WindowId surfaceWindowId,
                            const UIInputEvent& event,
                            UIPlanningState& projected) const;

    // 이벤트 하나를 그 계획의 Input 단계 대상에게 보낸다. 대상은
    // planned.TargetFor(Input)에서만 오고, 네 필드를 전부 다시 해석한 뒤에만
    // 컴포넌트에 닿는다. 해석에 실패하면 callbackDelivered=false이고
    // resolvedStageTarget은 그대로 남는다 — 누산기가 그 값으로 검증한다.
    UIEventHandlerResult HandleEvent(World& world, const UISnapshot& n,
                                     const PlannedUIEvent& planned,
                                     molga::text::TextDiagnosticSink& sink);
};

}  // namespace molga::ui
