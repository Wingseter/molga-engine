#pragma once

#include "Common/Types.h"
#include "UI/UILayoutSystem.h"

#include "UI/UIFocusSystem.h"
#include "UI/UIInputEvent.h"
#include "UI/UIInputRouter.h"
#include "UI/UIInteractionState.h"
#include "UI/UIRenderCollector.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class GameObject;
class World;
// Task 8.2: UI 텍스트도 월드와 같은 renderer/서비스/sink 권한 위에서만 모인다.
// 값으로 담지 않으므로 선언만 있으면 되고, 그 덕에 UISystem.h가 렌더링 헤더를
// 끌어오지 않는다.
class TextRenderer;
struct TextRasterPolicy;
namespace molga { class RenderQueue; }
namespace molga::text { class TextDiagnosticSink; class TextLayoutService; }

class UISystem {
public:
    static UISystem& Get();

    // ── Task 12.3 Step 5: UI는 게시된 스냅샷으로만 그려진다 ─────────────────
    // 스냅샷과 물리 변환을 호출자가 건넨다. 월드를 다시 읽는 오버로드는 없다 —
    // 그런 경로가 하나라도 있으면 화면이 스냅샷과 갈리고, 그 차이는 스냅샷을
    // 다시 지어야만 보인다. 래스터 정책은 변환이 정하므로 인자로 받지 않는다.
    // renderer/sink는 바깥 소유자가 건넨 그 하나다(Task 8.2): UI가 스스로
    // TextRenderer::Get()을 찾는 경로를 만들지 않는다.
    void CollectRender(const molga::ui::UISnapshot& snapshot,
                       const molga::ui::UIPhysicalTransform& transform,
                       molga::RenderQueue& queue, TextRenderer& textRenderer,
                       molga::text::TextDiagnosticSink& textDiagnostics);
    // Scene View 선택: 같은 스냅샷에서 맨 위의 hit 기록(그린 순서의 역순, 점이
    // 사각형과 클립 안)을 고르고, 그 기록의 오브젝트를 돌려준다. 없으면 nullptr.
    GameObject* PickObject(World& world, const molga::ui::UISnapshot& snapshot,
                           molga::FixedPoint logicalPoint) const;
    // 관찰 seam: 가장 최근 수집 하나의 회계.
    std::uint64_t RenderCollectorDroppedItemCountForTesting() const noexcept;
    std::uint64_t RenderCollectorSubmittedItemCountForTesting() const noexcept;

    // ── Task 10.2 Step 9a: 프로덕션 배치의 유일한 입구 ──────────────────────
    // UISystem이 하나뿐인 UILayoutSystem을 소유하고, 배치는 전부 여기를 지난다.
    // 두 번째 UILayoutSystem이 생기면 두 캐시가 서로 다른 스냅샷을 게시해
    // 같은 프레임의 렌더와 hit-test가 다른 기하를 보게 된다.
    //
    // surfaceWindowId는 호출자가 건네는 정확한 UI 표면 창이다. 전역 키보드
    // 창을 짐작하는 오버로드는 두지 않는다 — 짐작한 창은 분리된 표면에서
    // 조용히 틀린다.
    //
    // Task 11.1 Step 3i: 편집 상태 제공자와 정확한 렌더러 소유 TextLayoutService를
    // 명시적으로 받는다. 기본 인자를 가진 짧은 오버로드를 남기지 않는 이유는
    // 하나다 — 그런 오버로드가 있으면 진짜 제공자를 넘기는 것을 한 표면에서
    // 빠뜨려도 컴파일이 통과하고, 그 표면만 조용히 편집 상태 없이 그려진다.
    molga::ui::UISnapshotPtr BuildLayout(
        World& world, molga::WindowId surfaceWindowId,
        molga::FixedSize logicalViewport,
        const molga::ui::UITextInputVisualStateProvider& inputVisualStates,
        molga::text::TextLayoutService& textLayout,
        molga::text::TextDiagnosticSink& textDiagnostics);

    // ── Task 11.1 A3: 새 서명이 요구하는 두 값의 프로덕션 주인 ──────────────
    // Step 3i가 Build에 편집 상태 제공자와 공유 TextLayoutService를 넣었다. 그
    // 둘의 주인은 진입점(에디터 main / 패키징된 런타임)이다: 텍스트 서비스는
    // TextRenderer가 초기화된 뒤에야 존재하고, 진짜 제공자는 Task 14가 설치한다.
    //
    // 지금 이것을 설치해 두는 이유는 하나다 — Task 11.2가 스냅샷으로 그리기
    // 시작할 때 배선을 새로 만들 필요 없이 소비만 하면 되고, 그때 두 진입점이
    // 서로 다른 서비스를 집어 드는 일이 생기지 않는다. 설치가 없으면
    // 아래 짧은 BuildLayout은 nullptr을 돌려주고 조용히 그리지 않는다.
    void InstallLayoutDependencies(
        const molga::ui::UITextInputVisualStateProvider& inputVisualStates,
        molga::text::TextLayoutService& textLayout) noexcept;
    bool HasLayoutDependencies() const noexcept;
    // 설치된 의존물로 배치를 짓는다. 설치되지 않았으면 nullptr이다 —
    // 짐작한 서비스로 그리느니 그리지 않는다.
    molga::ui::UISnapshotPtr BuildLayout(
        World& world, molga::WindowId surfaceWindowId,
        molga::FixedSize logicalViewport,
        molga::text::TextDiagnosticSink& textDiagnostics);

    void OnWorldReleased(std::uint64_t worldGeneration);

    // ── Task 12.3 Step 4: 순서 있는 이벤트 배치를 도는 유일한 루프 ──────────
    // 한 프레임의 UI 입력은 전부 여기를 지난다. 순서는 이렇다:
    //   1 의존물/창 가드  2 스냅샷 N 빌드  3 Revalidate  4 클릭 가장자리 정리 + 씨앗
    //   5 시퀀스 장부 검증(배치 전체)  6 계획(PlanNext + ProjectEvent)  7 발송
    //   (이벤트마다 라우터 -> 포커스 -> 스크롤 -> Revalidate)  8 tick  9 N+1 게시.
    // 라우팅·포커스·스크롤은 배치 내내 N만 본다. 콜백은 N+1에만 보이는 것을 바꿀 수 있다.
    // N+1은 많아야 한 번 빌드되고 그 뒤에는 다시 빌드하지 않는다.
    molga::ui::UIFrameResult ProcessFrame(
        World& world, const molga::ui::UIFrameInput& input,
        molga::text::TextLayoutService& textLayout,
        molga::text::TextDiagnosticSink& textDiagnostics);

    // Game View 패널 이탈: hover/capture/pressed만. 키보드·게임패드 포커스는 남는다.
    void OnSurfacePointerDeparted(molga::WindowId windowId,
                                  std::uint64_t worldGeneration,
                                  molga::text::TextDiagnosticSink& textDiagnostics);

    // 관찰 seam. 장부와 기하 빌드 수는 밖에서 이 값으로만 보인다.
    std::uint64_t LayoutGeometryBuildCountForTesting() const noexcept;
    std::uint64_t LastCompletedNativeSequenceForTesting(
        molga::ui::UISurfaceKey) const noexcept;
    std::uint64_t LastCompletedTickForTesting(
        molga::ui::UISurfaceKey) const noexcept;

    // ── Task 11.2 Step 7d/7g: 장치 수명 사건의 유일한 라우팅 ────────────────
    // 배치 시스템은 하나뿐이고 그 소유자는 UISystem이므로, 장치를 만드는 쪽과
    // 부수는 쪽은 이 두 함수만 안다. 소유자를 건너뛰고 UILayoutSystem을 직접
    // 부르는 두 번째 경로가 생기면 그 경로만 두 캐시 중 하나를 잊게 된다.
    void OnDeviceGenerationChanged(std::uint64_t oldGeneration,
                                   std::uint64_t newGeneration);
    void ClearFullSnapshotBindingCache(std::uint64_t deviceGeneration);
    // 관찰 seam. 종료가 실제로 위 함수를 부르는지는 밖에서 이 값으로만
    // 보인다 — 단계 표식은 "표식을 냈다"는 사실이고 "그 일이 일어났다"는
    // 사실이 아니다. 그 둘을 구별하지 못하는 시험은 호출을 지운 구현에서도
    // 통과한다.
    std::size_t FullSnapshotCacheEntryCountForWorldDevice(
        molga::ui::UISnapshotWorldDeviceSlotKey) const noexcept;

private:
    // 정적 파괴 순서 방어. 이 시설이 먼저 죽고 나서 어떤 World가 소멸하면
    // 등록된 핸들러는 죽은 객체를 부른다. 파괴될 때 이름을 거둬들인다.
    ~UISystem();

    molga::ui::UILayoutSystem layout_;
    // 값이 아니라 이름이다. 제공자도 서비스도 진입점이 프레임 루프보다 오래
    // 소유하며, 여기에 사본을 두면 그 사본이 진짜와 갈릴 수 있다.
    const molga::ui::UITextInputVisualStateProvider* inputVisualStates_ = nullptr;
    molga::text::TextLayoutService* textLayout_ = nullptr;

    // ── Task 12.3 Step 4: 프레임 오케스트레이터가 소유하는 상태 ─────────────
    // 라우터/포커스는 데이터 멤버가 없는 규칙 묶음이다. 상태는 표면 표에 산다.
    molga::ui::UIRenderCollector renderCollector_;
    molga::ui::UIInputRouter inputRouter_;
    molga::ui::UIFocusSystem focusSystem_;
    // 표면별 "마지막으로 끝까지 처리한" 네이티브 시퀀스/tick 인덱스. 재생과 역행을
    // 거절하는 장부이고, 월드 세대가 은퇴하면 그 세대의 키를 지운다.
    std::unordered_map<molga::ui::UISurfaceKey, std::uint64_t,
                       molga::ui::UISurfaceKeyHash>
        lastCompletedNativeSequenceBySurface_;
    std::unordered_map<molga::ui::UISurfaceKey, std::uint64_t,
                       molga::ui::UISurfaceKeyHash>
        lastCompletedTickBySurface_;
    // 월드 세대당 한 번만 보고하는 장부.
    std::unordered_set<std::uint64_t> depsReported_;
    std::unordered_set<std::uint64_t> reflowDeferredReported_;
};

namespace molga::ui::detail {
// 시험 seam: N+1을 게시한 **직후**, 반환 직전에 불린다. 프로덕션은 설치하지 않는다.
// 여기서 일어난 무효화만 ReflowDeferred로 보고되고, 같은 프레임에 다시 빌드하지
// 않는다.
using UIFramePublicationObserver = void (*)(World&, const UISnapshot&);
void SetUIFramePublicationObserverForTest(UIFramePublicationObserver) noexcept;
}  // namespace molga::ui::detail
