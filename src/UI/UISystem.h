#pragma once

#include "Common/Types.h"

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
    void ProcessInput(std::vector<std::shared_ptr<GameObject>>& objects,
                      const Vector2& viewportSize,
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
    void CollectRender(const std::vector<std::shared_ptr<GameObject>>& objects,
                       const Vector2& viewportSize,
                       molga::RenderQueue& queue, TextRenderer& textRenderer,
                       molga::text::TextDiagnosticSink& textDiagnostics,
                       const TextRasterPolicy& rasterPolicy);
    GameObject* HitTest(World& world, const Vector2& viewportSize,
                        const Vector2& point) const;
    GameObject* HitTest(const std::vector<std::shared_ptr<GameObject>>& objects,
                        const Vector2& viewportSize,
                        const Vector2& point) const;
    void ResetPointerCapture();

private:
    const void* capturedOwner_ = nullptr;
    unsigned int capturedObjectId_ = 0;
};
