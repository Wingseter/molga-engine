#pragma once

#include "Common/Fixed26_6.h"
#include "Platform/NativeKeyModifiers.h"
#include "Platform/TextInputOwner.h"
#include "Platform/Window.h"
#include "UI/UIDeterministicTick.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UIRuntimeIdentity.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace molga::text {
class TextDiagnosticSink;
}

namespace molga::ui {

// ── Step 4b: 값만 담는 UI 입력 기록 ─────────────────────────────────────────
// 네이티브 이벤트가 통째로 복사되어 오는 자리다. SDL 타입도, 살아 있는 장치
// 상태를 다시 읽는 접근자도 여기 없다 — 하나라도 있으면 "한 배치는 정확히 그
// 순간의 값을 담는다"가 깨지고, 리플레이가 원본과 다른 값을 먹는다.
enum class UIInputEventKind : std::uint8_t {
    Other,
    WindowFocus,
    PointerMotion,
    PointerButton,
    Scroll,
    Key,
    GamepadButton,
    GamepadAxis,
    TextEditing,
    TextCommit,
};

// 텍스트/편집 페이로드. utf8은 ingest에서 **깊은 복사**된 것이고, ownerAtIngest는
// 그 복사 시각의 소유자 도장이다. 이 도장은 나중에 소유권이 바뀌어도 바뀌지
// 않는다 — 그래서 옛 소유자의 편집이 새 소유자에게 흘러들 수 없다.
//
// editingStart/Length는 SDL이 주는 그대로의 **UTF-8 문자 수**이지 바이트 수가
// 아니다. 바이트로 읽으면 ASCII가 아닌 조합에서 조용히 다른 구간이 밑줄 쳐진다.
struct UITextInputEventPayload {
    std::string utf8;
    std::int32_t editingStartUtf8Characters = 0;
    std::int32_t editingLengthUtf8Characters = 0;
    molga::platform::TextInputOwnerStamp ownerAtIngest;
};

// 순서 있는 배치의 원소 하나.
//
// sequence는 프로세스 전역 비순환 단조 할당기가 낸 0이 아닌 값이고, 호스트/창을
// 가로질러 엄격 증가한다. traceOrdinal은 이 배치 안의 자리다. 둘 다 남기는
// 이유: sequence는 재생/역행을 거절하는 데 쓰이고, traceOrdinal은 감사 기록이
// 원래 순서를 그대로 보이게 한다.
//
// logicalDelta와 axisValue는 **부호 있는** 값 그대로다. 불리언으로 접으면
// 저작자가 고른 방향과 세기가 그 자리에서 사라진다.
struct UIInputEvent {
    std::uint64_t sequence = 0;
    std::uint64_t traceOrdinal = 0;
    molga::WindowId windowId = 0;
    UIInputEventKind kind = UIInputEventKind::Other;
    std::uint32_t nativeType = 0;
    molga::FixedPoint logicalPoint;
    bool logicalPointValid = false;
    molga::FixedPoint logicalDelta;
    molga::Fixed26_6 axisValue = molga::Fixed26_6::FromRaw(0);
    // 버튼 번호 / 키 코드 / 게임패드 버튼·축 이름. 종류가 무엇을 뜻하는지
    // 정한다.
    std::uint32_t control = 0;
    molga::platform::NativeKeyModifiers keyModifiers;
    // PointerButton/Key/GamepadButton은 눌림, WindowFocus는 창 활성.
    bool active = false;
    std::optional<UITextInputEventPayload> text;
};

// ── Step 4c: 계획의 단계 ────────────────────────────────────────────────────
// 한 이벤트는 여러 단계로 나뉘어 처리되고, 단계마다 **자기** 대상이 있다.
// 버튼을 눌렀을 때 동작 대상은 버튼이지만 포커스는 그 형제 selectable로,
// 스크롤은 조상 스크롤 뷰로 간다. 이 넷을 한 대상으로 뭉치면 어느 하나가
// 다른 하나의 결과로 검증되고, 그 어긋남은 조용하다.
enum class UIEventStage : std::uint8_t {
    Input,
    Focus,
    Scroll,
    TextInput,
    OwnerTransition,
};

// 스냅샷 N에 대해 얼어붙은 계획 하나.
//
// **여기 담긴 것이 대상의 전부다.** 나중 라우팅은 ECS 상태에서 단계 대상을
// 다시 만들지 않는다 — 그 사이에 컴포넌트가 지워지고 같은 타입이 다시 붙으면
// 재구성된 대상은 다른 컴포넌트다.
struct PlannedUIEvent {
    UIInputEvent event;
    // 이 이벤트가 이 표면의 것인가. 거짓이면 감사 순서에는 남되 어떤 단계
    // 대상도 없고 어떤 상태도 건드리지 않는다.
    bool surfaceEligible = false;

    // 원본과 호환되는 동작 대상 필드.
    std::optional<UIRuntimeTargetIdentity> targetFromSnapshotN;
    std::optional<UIStableComponentKey> canonicalTargetFromSnapshotN;

    std::optional<UIFrozenTarget> focusTargetFromSnapshotN;
    // 방향 입력이 N에서 해석한 이동 **목적지**. Task 12.2의
    // UIFocusSystem::ProjectEvent가 채운다.
    std::optional<UIFrozenTarget> focusDestinationFromSnapshotN;
    std::optional<UIFrozenTarget> textInputTargetFromSnapshotN;
    // 안쪽에서 바깥쪽으로. 순서 자체가 계약이다.
    std::vector<UIFrozenTarget> scrollTargetsFromSnapshotN;
    std::optional<UIFrozenTarget> ownerTransitionTargetFromSnapshotN;

    // 단계 대상을 얻는 **유일한** 통로. 핸들러가 형제 필드를 직접 읽으면
    // 언젠가 다른 단계의 대상으로 자기 결과를 검증한다.
    std::optional<UIFrozenTarget> TargetFor(UIEventStage stage,
                                            std::size_t ordinal = 0) const;
    std::size_t ScrollTargetCount() const noexcept {
        return scrollTargetsFromSnapshotN.size();
    }
};

// 한 표면이 소유하는 투사 상태. 배치 계획이 이 값을 순차적으로 밀고 나간다.
//
// 표면 창과 월드 세대를 함께 든다. 창만으로 키를 잡으면 씬이 교체된 뒤에도
// 옛 포커스가 살아남고, 세대만으로 잡으면 분리된 두 창이 서로의 포커스를
// 나눠 갖는다.
struct UIPlanningState {
    molga::WindowId surfaceWindowId = 0;
    std::uint64_t surfaceWorldGeneration = 0;
    std::optional<UIRuntimeTargetIdentity> focused;
    std::optional<UIRuntimeTargetIdentity> pointerCapture;
    std::optional<UIRuntimeTargetIdentity> hovered;
    molga::FixedPoint pointer;
    bool pointerValid = false;
    bool nativeWindowFocused = false;
};

// ── Step 4d: 한 이벤트 핸들러의 결과 ────────────────────────────────────────
enum class UIEventAction : std::uint16_t {
    None = 0,
    Hover = 1u << 0,
    Press = 1u << 1,
    Click = 1u << 2,
    Focus = 1u << 3,
    Navigate = 1u << 4,
    Scroll = 1u << 5,
    Edit = 1u << 6,
    Submit = 1u << 7,
    Release = 1u << 8,
};

constexpr std::uint16_t UIEventActionBit(UIEventAction action) noexcept {
    return static_cast<std::uint16_t>(action);
}

struct UIEventHandlerResult {
    std::uint64_t sequence = 0;
    UIEventStage stage = UIEventStage::Input;
    std::uint16_t actionMask = 0;
    bool consumed = false;
    bool callbackDelivered = false;
    bool arrangementDirty = false;
    bool visualDirty = false;
    std::size_t stageTargetOrdinal = 0;
    // 핸들러가 실제로 해석한 단계 대상. 누산기는 이 값을 계획의
    // TargetFor(stage, ordinal)과 필드 하나하나까지 맞춰 본다.
    std::optional<UIFrozenTarget> resolvedStageTarget;
    std::optional<UIRuntimeTargetIdentity> resultingFocus;
};

// ── Step 4e: 이벤트 하나의 감사 기록 ────────────────────────────────────────
struct UIEventDispatchRecord {
    std::uint64_t sequence = 0;
    std::uint64_t traceOrdinal = 0;
    UIInputEventKind kind = UIInputEventKind::Other;
    // 이 이벤트의 **하나뿐인** 주 대상. 단계마다 다른 대상이 있어도 감사
    // 기록은 하나만 이름한다.
    std::optional<UIRuntimeTargetIdentity> runtimeTarget;
    std::optional<UIStableComponentKey> canonicalTarget;
    std::uint16_t actionMask = 0;
    bool consumed = false;
    bool delivered = false;
    bool arrangementDirty = false;
    bool visualDirty = false;
    std::optional<UIRuntimeTargetIdentity> resultingFocus;
};

// 누산기가 결과 하나를 어떻게 처리했는가.
//
// 값으로 돌려준다. 진단 sink만으로 알리면 "보고했다"는 사실과 "합치지 않았다"는
// 사실을 구별하지 못하고, 표식만 보는 시험은 합치기를 지운 구현에서도 통과한다.
enum class UIEventMergeStatus : std::uint8_t {
    Merged,
    RejectedSequence,
    RejectedStageTarget,
    // 해석된 단계 대상이 없는 결과가 효과(동작/소비/전달/포커스)를 주장했다.
    // 부적격(외부 창) 계획에는 dirty 플래그조차 들어올 수 없다.
    RejectedTargetless,
    RejectedSpent,
};

// 한 이벤트의 단계 결과들을 하나의 감사 기록으로 모은다.
//
// 주 대상은 정확한 규칙 하나로 정해진다(Step 6b): 도장 받은 텍스트 대상;
// 포인터/키/게임패드는 동작 대상; 창 포커스와 외부 창 이벤트는 대상 없음;
// 스크롤은 **소비된 첫 스크롤 결과**가 자기 소비자를 한 번 채운다. 포커스
// 단계의 결과는 주 대상을 절대 대신하지 않는다.
class UIEventDispatchAccumulator {
public:
    // sink는 **명시**다. 기본 인자로 숨기면 진단 없이 조용히 거절하는 경로가
    // 생기고, 그 경로에서 대상 충돌은 아무 데도 남지 않는다.
    UIEventDispatchAccumulator(const PlannedUIEvent& planned,
                               molga::text::TextDiagnosticSink& sink);
    // 계획을 가리킬 뿐 복사하지 않는다. 임시 계획에 묶이면 —
    // `acc(router.PlanNext(...), sink)` — 첫 Merge가 이미 죽은 값을 읽으므로
    // 그 모양은 컴파일되지 않는다.
    UIEventDispatchAccumulator(const PlannedUIEvent&&,
                               molga::text::TextDiagnosticSink&) = delete;
    // 복사본은 같은 이벤트의 두 번째 감사 기록을 낸다.
    UIEventDispatchAccumulator(const UIEventDispatchAccumulator&) = delete;
    UIEventDispatchAccumulator& operator=(const UIEventDispatchAccumulator&) =
        delete;

    UIEventMergeStatus Merge(const UIEventHandlerResult& result);
    // 두 번째 Finish는 거절된다: ReferenceInvalid를 보고하고 대상도 플래그도
    // 없는 빈 기록을 돌려준다. 같은 이벤트가 감사에 두 번 주장을 남길 수 없다.
    UIEventDispatchRecord Finish() &&;

    bool Spent() const noexcept { return spent_; }

private:
    const PlannedUIEvent* planned_ = nullptr;
    molga::text::TextDiagnosticSink* sink_ = nullptr;
    UIEventDispatchRecord record_;
    // 스크롤이 주 대상을 채울 수 있는 상태인가. 도장 받은 텍스트/동작 대상이
    // 이미 채운 경우에는 거짓이다.
    bool primaryOpenForScroll_ = false;
    bool spent_ = false;
};

// ── Step 4f: 프레임 감사 입출력 ─────────────────────────────────────────────
//
// frameIndex는 **여기에만** 있다. UISnapshot이나 그 캐시 키에 들어가면 변경
// 없는 프레임마다 값이 달라져 "같은 저작 상태는 같은 스냅샷을 재사용한다"가
// 그 자리에서 깨진다.
//
// pointerAtBatchStart와 nativeWindowFocusedAtBatchStart는 take-once 호스트
// 배치가 함께 실어 보낸 씨앗이고, orderedEvents의 **어떤** 이벤트보다 앞선
// 값이다. 부르는 쪽이 폴링 뒤의 현재 상태로 바꿔치기하면, 배치 안의 첫 스크롤이
// 이미 움직인 커서 위치에서 대상을 고른다.
struct UIFrameInput {
    std::uint64_t frameIndex = 0;
    molga::WindowId windowId = 0;
    molga::FixedSize logicalViewport;
    std::vector<UIInputEvent> orderedEvents;
    std::vector<UIDeterministicTick> uiTicks;
    molga::FixedPoint pointerAtBatchStart;
    bool pointerAtBatchStartValid = false;
    bool nativeWindowFocusedAtBatchStart = false;
};

struct UIFrameResult {
    std::uint64_t frameIndex = 0;
    UISnapshotPtr interactionSnapshot;
    UISnapshotPtr renderSnapshot;
    std::vector<UIEventDispatchRecord> dispatches;
};

}  // namespace molga::ui
