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
namespace molga::text { class TextDiagnosticSink; }

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
    molga::ui::UISnapshotPtr BuildLayout(
        World& world, molga::WindowId surfaceWindowId,
        molga::FixedSize logicalViewport,
        molga::text::TextDiagnosticSink& textDiagnostics);
    void OnWorldReleased(std::uint64_t worldGeneration);

private:
    // 정적 파괴 순서 방어. 이 시설이 먼저 죽고 나서 어떤 World가 소멸하면
    // 등록된 핸들러는 죽은 객체를 부른다. 파괴될 때 이름을 거둬들인다.
    ~UISystem();

    const void* capturedOwner_ = nullptr;
    unsigned int capturedObjectId_ = 0;
    molga::ui::UILayoutSystem layout_;
};
