#pragma once

#include "Common/Fixed26_6.h"
#include "Text/TextDiagnostic.h"
#include "UI/UIDeterministicTick.h"
#include "UI/UIInputEvent.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UIRuntimeIdentity.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class World;

namespace molga::ui {

// 이름 붙은 축. 게임패드 축 값은 이 이름이 가리키는 축 하나에만 더해진다 —
// "두 축에 나눠 넣기"는 저작자가 고른 축을 조용히 바꾸는 일이다.
enum class UIScrollAxis : std::uint8_t { Horizontal, Vertical };

// 게임패드 축 이벤트(UIInputEvent::control)가 어느 스크롤 축을 가리키는가.
// control 도메인(사용자 결정 2026-10-18)을 스크롤 쪽에서 아는 유일한 곳이다:
// Input::GamepadAxis의 오른쪽 스틱만 스크롤한다(RightX -> 가로, RightY -> 세로).
// 왼쪽 스틱과 두 트리거, 모르는 값은 nullopt — 스크롤 단계가 무동작이다.
std::optional<UIScrollAxis> ScrollAxisOf(std::uint32_t control) noexcept;

// 입력 사건 하나. 부호 있는 X/Y 변위와 정확한 부호 있는 축 값을 함께 든다:
// 휠/포인터는 logicalDelta로, 게임패드는 axisValue로 온다. 둘 다 의도적으로
// 0이 아니면 더해진다(둘 중 하나를 버리면 두 장치를 함께 쓰는 프레임에서
// 한쪽 입력이 사라진다).
struct UIScrollInput {
    molga::FixedPoint logicalDelta;
    molga::Fixed26_6 axisValue = molga::Fixed26_6::FromRaw(0);
    UIScrollAxis axis = UIScrollAxis::Vertical;
};

// 런타임 스크롤 상태. **직렬화되지 않는다** — 씬/prefab JSON에도, prefab
// override에도, 에디터 dirty 상태에도 들어가지 않는다. 저작 상태는
// UIScrollView의 initialNormalizedX/Y뿐이고, 이 둘은 그 값에서 유도되는
// 실행 시점의 값이다.
//
// offset은 논리 변위(26.6), velocity는 초당 논리 변위(26.6)다.
struct UIScrollState {
    molga::FixedPoint offset;
    molga::FixedPoint velocity;
};

// 한 번의 입력/한 tick이 무엇을 더럽혔는가. Task 12의 "프레임당 사건 하나"
// orchestrator가 이 값을 소비한다.
struct UIScrollMutation {
    bool changed = false;
    bool arrangementDirty = false;
    bool renderDirty = false;
    // 오프셋 변화는 글의 제약을 바꾸지 않는다. 참으로 두면 스크롤 한 칸마다
    // 보이는 모든 문단이 다시 셰이핑되고, 그것이 스크롤이 무거워지는 고전적인
    // 경로다.
    bool textShapeDirty = false;
    explicit operator bool() const noexcept { return changed; }
};

// ── Step 5c: 검증된 Q6 곱 ───────────────────────────────────────────────────
// (a.raw * b.raw) / 64 를 0에서 먼 쪽으로 반올림한다. 중간 곱은 int64_t이고,
// 절댓값 연산 전에 int64_t로 승격한 뒤 마지막에 int32_t 범위를 검사한다 —
// int32_t에서 절댓값을 취하면 INT32_MIN 하나가 UB다. 포화도 랩도 없다.
std::optional<molga::Fixed26_6> MulQ6NearestAway(molga::Fixed26_6 a,
                                                 molga::Fixed26_6 b) noexcept;

// (a.raw * b.raw) / 64 를 0 방향으로 자른다. Step 5e amendment(2026-10-01)의
// 속도 감쇠 전용이다: 0에서 먼 쪽으로 반올림하면 |v| * retained / 64 가 0이
// 아닌 고정점을 가진다(2 * 48 / 64 = 1.5 -> 2). 자르면 retained <= 63 인 모든
// 0이 아닌 |v|가 매 tick 엄격히 줄어 유한한 tick 안에 0에 닿는다. 중간 곱은
// int64_t이고 마지막에 int32_t 범위를 검사한다 — 포화도 랩도 없다.
std::optional<molga::Fixed26_6> MulQ6TowardZero(molga::Fixed26_6 a,
                                                molga::Fixed26_6 b) noexcept;

// ── 결정적 식별자 키 스크롤 상태 ────────────────────────────────────────────
// 상태는 완전한 런타임 식별자 {worldGeneration, objectId, componentRuntimeTypeId,
// componentInstanceId}로만 키를 잡는다. 오브젝트 id만으로 키를 잡으면 씬을 다시
// 열었을 때 같은 번호를 받은 다른 스크롤 뷰가 죽은 뷰의 오프셋을 물려받는다.
//
// 프로세스에 하나뿐인 인스턴스가 Get()이다. UILayoutSystem::Build가 배치
// 직전에 이 인스턴스에서 변위를 읽으므로(UIIntrinsicLayoutRegistry와 같은
// 모양), 두 번째 인스턴스가 생기면 입력을 받는 표와 배치가 읽는 표가 갈린다.
class UIScrollSystem {
public:
    static UIScrollSystem& Get();

    UIScrollSystem();
    ~UIScrollSystem();
    UIScrollSystem(const UIScrollSystem&) = delete;
    UIScrollSystem& operator=(const UIScrollSystem&) = delete;

    // 부호 있는 휠/포인터 변위와 이름 붙은 축 값을 적용한다. 꺼진 축은
    // **아무 변위도 소비하지 않는다** — 소비하고 버리는 것이 아니다.
    UIScrollMutation ApplyInput(World&, const UISnapshot&,
                                const UIRuntimeTargetIdentity&,
                                const UIScrollInput&,
                                molga::text::TextDiagnosticSink&);

    // 정확히 하나의 검증된 tick만큼 나아간다. 벽시계를 읽지 않는다.
    UIScrollMutation AdvanceTick(World&, const UISnapshot&,
                                 const UIDeterministicTick&,
                                 molga::text::TextDiagnosticSink&);

    // 계획 **하나**를 처리한다. 얼어붙은 self-or-ancestor 사슬을 안쪽부터 걷고,
    // 처음으로 변위를 소비한(ApplyInput.changed) 대상과 그 서수를 Scroll 단계
    // 결과로 보고한다. 휠은 logicalDelta를, 잡힌 포인터 이동(드래그)은 같은
    // logicalDelta를, 게임패드 축은 axisValue와 ScrollAxisOf가 고른 축을 쓴다.
    // 벡터를 받지 않는다 — tests/test_ui_input.cpp가 그 금지를 컴파일 시각에
    // 지킨다.
    UIEventHandlerResult HandleEvent(World&, const UISnapshot&,
                                     const PlannedUIEvent&,
                                     molga::text::TextDiagnosticSink&);

    const UIScrollState* State(const UIRuntimeTargetIdentity&) const;

    // 은퇴한 월드 세대의 상태/실패 집합/tick 커서/진단 기억을 전부 놓는다.
    // 프로덕션 배선은 World -> molga::ui::NotifyUIWorldReleased ->
    // UISystem::OnWorldReleased -> 여기다. 부르는 사람이 없는 회수 함수는
    // 회수하지 않는다(Task 10.2가 비싸게 배운 규칙이다).
    void OnWorldReleased(std::uint64_t worldGeneration);

    // ── Step 6b: 배치가 읽는 입력 ───────────────────────────────────────────
    // 완전한 식별자 순서로 정렬된 원본 필드 벡터다. 정렬은 이 함수 안에서
    // 일어나므로 호출부가 순서를 다시 정할 필요가 없고, 해시 컨테이너의 순회
    // 순서가 캐시 키에 새어 들어가지 않는다.
    std::vector<UIScrollDisplacementCacheIdentity> DisplacementsForWorld(
        std::uint64_t worldGeneration) const;

    // ── 관찰 seam ───────────────────────────────────────────────────────────
    // 마지막 AdvanceTick이 실제로 방문한 식별자를 방문 순서대로. 정렬을 지운
    // 구현은 값이 아니라 **순서**에서만 드러나므로, 이 목록이 그 유일한
    // 관찰자다.
    std::vector<UIRuntimeTargetIdentity> LastVisitedIdentitiesForTesting() const;
    // 한 월드의 상태 전부를 완전한 식별자 순서로 적은 정규 문서. 프로세스
    // 순번(worldGeneration/componentInstanceId)은 들어가지 않는다 — 들어가면
    // 독립적으로 만든 두 실행을 비교할 수 없다.
    std::string StableStateJson(std::uint64_t worldGeneration) const;
    std::size_t StateCountForWorld(std::uint64_t worldGeneration) const noexcept;
    std::size_t StateCount() const noexcept;
    std::uint64_t LastTickIndexForWorld(
        std::uint64_t worldGeneration) const noexcept;
    bool IsFailClosed(const UIRuntimeTargetIdentity&) const noexcept;
    // 손으로 만든 재귀 픽스처의 시작점. 합법 구간 **바깥**의 시작 상태는
    // 입력 경로로는 한 걸음에 도달할 수 없고, 탄성 재귀의 요점이 바로 그
    // 바깥에서 안으로 돌아오는 한 걸음이다.
    void SeedStateForTesting(const UIRuntimeTargetIdentity&,
                             const UIScrollState&);

    // 이름만 공개한다. 정의는 .cpp 안에만 있으므로 이 헤더를 읽는 쪽은 표의
    // 모양을 볼 수 없고, .cpp의 자유 함수 헬퍼는 이 타입을 인자로 받을 수
    // 있다.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace molga::ui
