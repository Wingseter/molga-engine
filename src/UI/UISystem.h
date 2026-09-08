#pragma once

#include "Common/Types.h"
#include "UI/UILayoutSystem.h"

#include <cstdint>
#include <memory>
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

struct UIPointerState {
    Vector2 position;
    bool down = false;
    bool pressedThisFrame = false;
    bool releasedThisFrame = false;
    bool valid = true;
};

class UISystem {
public:
    static UISystem& Get();

    // Must run before script Update. Only the topmost button can capture a
    // press; release clicks exactly once when still inside the captured rect.
    void ProcessInput(World& world, const Vector2& viewportSize,
                      const UIPointerState& pointer);
    // ── Task 8.2 Step 6/6a: 라벨은 공유 파이프라인으로만 그려진다 ──────────
    // renderer/sink/래스터 정책을 바깥 소유자가 건넨다. 기본값을 가진 오버로드도
    // sink 없는 오버로드도 만들지 않는다: 그런 오버로드가 하나라도 있으면 UI가
    // TextRenderer::Get()을 스스로 부르는 경로가 다시 생기고, 그 순간 UI와
    // 월드가 서로 다른 서비스를 볼 수 있게 된다.
    void CollectRender(World& world, const Vector2& viewportSize,
                       molga::RenderQueue& queue, TextRenderer& textRenderer,
                       molga::text::TextDiagnosticSink& textDiagnostics,
                       const TextRasterPolicy& rasterPolicy);
    GameObject* HitTest(World& world, const Vector2& viewportSize,
                        const Vector2& point) const;
    void ResetPointerCapture();

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

    const void* capturedOwner_ = nullptr;
    unsigned int capturedObjectId_ = 0;
    molga::ui::UILayoutSystem layout_;
    // 값이 아니라 이름이다. 제공자도 서비스도 진입점이 프레임 루프보다 오래
    // 소유하며, 여기에 사본을 두면 그 사본이 진짜와 갈릴 수 있다.
    const molga::ui::UITextInputVisualStateProvider* inputVisualStates_ = nullptr;
    molga::text::TextLayoutService* textLayout_ = nullptr;
};
