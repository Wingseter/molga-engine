#include "UI/UISystem.h"

#include "Core/World.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/GameObject.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Renderer.h"
#include "Rendering/Shader.h"
#include "Rendering/TextRenderer.h"
#include "Rendering/Texture.h"

#include "Common/Fixed26_6.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutTypes.h"
#include "UI/UILayoutSystem.h"
#include "UI/UIRuntimeIdentity.h"

#include <algorithm>
#include <optional>
#include <tuple>
#include <vector>

namespace {
struct ButtonEntry {
    GameObject* object = nullptr;
    UIButton* button = nullptr;
    AABB rect;
    int canvasOrder = 0;
    int order = 0;
    std::size_t traversal = 0;
};

int CanvasOrder(GameObject* object) {
    for (GameObject* node = object; node; node = node->GetParent()) {
        if (auto* canvas = node->GetComponent<UICanvas>()) return canvas->GetSortingOrder();
    }
    return 0;
}

bool IsHierarchyActive(GameObject* object) {
    for (GameObject* node = object; node; node = node->GetParent()) {
        if (!node->IsActive()) return false;
    }
    return true;
}

UICanvas* ActiveCanvas(GameObject* object) {
    for (GameObject* node = object; node; node = node->GetParent()) {
        if (auto* canvas = node->GetComponent<UICanvas>()) {
            return canvas->IsEnabled() ? canvas : nullptr;
        }
    }
    return nullptr;
}

bool IsHigher(const ButtonEntry& a, const ButtonEntry& b) {
    return std::tie(a.canvasOrder, a.order, a.traversal) >
           std::tie(b.canvasOrder, b.order, b.traversal);
}

std::vector<ButtonEntry> GatherButtons(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const Vector2& viewport) {
    std::vector<ButtonEntry> result;
    if (viewport.x <= 0.0f || viewport.y <= 0.0f) return result;
    std::size_t traversal = 0;
    for (const auto& object : objects) {
        ++traversal;
        if (!object) continue;
        auto* button = object->GetComponent<UIButton>();
        if (button) button->ApplyPointerState(false, false, false);
        auto* rect = object->GetComponent<RectTransform>();
        if (!button || !rect || !IsHierarchyActive(object.get()) ||
            !button->IsEnabled() || !rect->IsEnabled() || !ActiveCanvas(object.get())) continue;
        result.push_back({object.get(), button, rect->GetScreenRect(viewport),
                          CanvasOrder(object.get()), button->GetSortingOrder(), traversal});
    }
    return result;
}

void FillVertex(molga::Vertex2D& vertex, float x, float y, float u, float v,
                const Color& color) {
    vertex = {x, y, u, v, color.r, color.g, color.b, color.a};
}

void SubmitRect(molga::RenderQueue& queue, const AABB& rect, const Color& color,
                Texture* texture, int canvasOrder, int sortingOrder) {
    molga::RenderCommand command;
    command.sortKey.cameraPass = 1;
    command.sortKey.sortingLayer = canvasOrder;
    command.sortKey.sortingOrder = sortingOrder;
    command.batchKey.shaderName = "batch";
    if (texture && texture->IsValid()) {
        command.batchKey.texture = texture->Handle();
        command.batchKey.textureSampler = texture->Sampler();
        command.batchKey.textureStableId = texture->StableId();
    }
    command.batchKey.isBatchable = true;
    command.isBatchableSprite = true;
    FillVertex(command.vertices[0], rect.x, rect.y, 0.0f, 0.0f, color);
    FillVertex(command.vertices[1], rect.x + rect.width, rect.y, 1.0f, 0.0f, color);
    FillVertex(command.vertices[2], rect.x + rect.width, rect.y + rect.height, 1.0f, 1.0f, color);
    FillVertex(command.vertices[3], rect.x, rect.y + rect.height, 0.0f, 1.0f, color);
    queue.Submit(command);
}

// ── Task 8.2 Step 6: 라벨 하나를 제약 있는 공유 요청으로 ────────────────────
molga::text::TextHorizontalAlignment ToLayoutAlignment(
    UILabel::HorizontalAlignment value) {
    switch (value) {
        case UILabel::HorizontalAlignment::Center:
            return molga::text::TextHorizontalAlignment::Center;
        case UILabel::HorizontalAlignment::Right:
            return molga::text::TextHorizontalAlignment::Right;
        case UILabel::HorizontalAlignment::Left:
            break;
    }
    return molga::text::TextHorizontalAlignment::Left;
}

molga::text::TextVerticalAlignment ToLayoutAlignment(
    UILabel::VerticalAlignment value) {
    switch (value) {
        case UILabel::VerticalAlignment::Middle:
            return molga::text::TextVerticalAlignment::Middle;
        case UILabel::VerticalAlignment::Bottom:
            return molga::text::TextVerticalAlignment::Bottom;
        case UILabel::VerticalAlignment::Top:
            break;
    }
    return molga::text::TextVerticalAlignment::Top;
}

molga::text::TextLayoutRequest BuildLabelRequest(const UILabel& label,
                                                 const GameObject& object,
                                                 const AABB& rect) {
    molga::text::TextLayoutRequest request;
    request.utf8 = label.GetText();
    // 저작된 family가 이긴다. 없을 때만 schema 1의 폰트 지목이 레거시 단일 face
    // 경로로 간다(Task 8.2 설계 개정): face 하나, fallback 없음.
    const UILabel::FontFamilyView family = label.ResolveFontFamilyView();
    if (!family.familyGuid.empty()) {
        request.style.fontFamilyGuid = family.familyGuid;
    } else if (!family.faceFontGuids.empty()) {
        request.style.legacyFontGuid = family.faceFontGuids.front();
    }
    const auto fontSize = molga::Fixed26_6::FromFloat(label.GetFontSizePx());
    if (fontSize) request.style.shape.fontSize = *fontSize;
    request.style.analysis.locale = label.GetLocale();
    request.style.analysis.baseDirection = label.GetBaseDirection();
    request.style.wrap = label.GetWrapMode();
    request.style.overflow = label.GetOverflowMode();
    request.style.maxLines = label.GetMaxLines();
    request.style.horizontal = ToLayoutAlignment(label.GetHorizontalAlignment());
    request.style.vertical = ToLayoutAlignment(label.GetVerticalAlignment());
    const auto spacing = molga::Fixed26_6::FromFloat(label.GetLineSpacing());
    if (spacing) request.style.lineSpacing = *spacing;
    // 정확한 RectTransform 폭/높이가 그대로 제약이다. 없는 값을 0으로 적으면
    // "폭 0으로 접어라"라는 정당한 요청과 구분되지 않으므로, 변환에 실패한
    // 축은 제약 없음으로 남긴다.
    request.constraints.width = molga::Fixed26_6::FromFloat(rect.width);
    request.constraints.height = molga::Fixed26_6::FromFloat(rect.height);
    request.diagnosticContext.componentType = "UILabel";
    request.diagnosticContext.sceneObjectId = object.GetID();
    return request;
}

// 측정 전용 pass의 진단을 버린다. 같은 내용을 렌더 요청이 이미 제 문맥으로
// 보고하므로, 여기서 한 번 더 흘리면 잘못된 문자열 하나가 프레임마다 두 줄씩
// 찍힌다. 상태가 없으므로 프레임마다 만들어도 할당이 없다.
class DiscardingTextDiagnosticSink final
    : public molga::text::TextDiagnosticSink {
public:
    void Report(molga::text::TextDiagnostic) override {}
};

// Step 5b의 생산자. 레거시 즉시 경로가 아직 화면을 그리는 동안에는 이쪽도
// 계속 게시한다 — 이 경로만이 입력창이 소유한 라벨까지 포함해 확정된 배치를
// 손에 쥐고, `tests/test_ui.cpp`의 프로덕션 진입점 케이스가 그것을 지킨다.
//
// Task 11.1부터 `UILayoutSystem::Build`도 같은 값을 게시한다. 두 생산자가
// 공존해도 세대가 흔들리지 않는 이유는 하나뿐이다: 정체성 계산이
// `UILabelIntrinsicContentIdentity` 한 벌이고, 제약을 벗긴 측정은 글과
// 스타일에만 의존하므로 두 번째 게시가 언제나 "변경 없음"이 된다. 정체성
// 규칙을 여기 다시 쓰면 그 성질이 조용히 깨진다.
//
// 렌더 요청의 결과를 그대로 쓰지 않는 이유: 그 요청은 현재 RectTransform을
// 제약으로 싣고 있다. content fitter가 고유 크기를 읽어 rect를 바꾸면 다음
// 프레임의 제약이 달라지고, 달라진 제약이 다른 고유 크기를 내고, 그 값이 다시
// rect를 바꾼다. 값이 매 프레임 흔들리면 Publish가 매번 참을 돌려주어 의미
// 세대가 계속 오르고, "변경 없는 600 프레임"의 빠른 경로는 영원히 맞지 않는다.
// 그래서 제약만 벗긴 요청으로 한 번 더 측정한다. 그 결과는 텍스트와 스타일에만
// 의존하므로 rect가 어떻게 움직이든 같은 값이고, 두 번째 발행부터 Publish는
// 거짓을 돌려준다. 배치 캐시가 이 요청을 서비스하므로 첫 프레임 뒤로는 셰이핑을
// 다시 하지 않는다.
void PublishLabelIntrinsic(const World& world, const UILabel& label,
                           const molga::text::TextLayoutRequest& renderRequest,
                           TextRenderer& textRenderer) {
    const molga::ui::UIRuntimeTargetIdentity target =
        molga::ui::CaptureTarget(world, label);
    if (!target) return;
    molga::text::TextLayoutRequest measure = renderRequest;
    measure.constraints = molga::text::LayoutConstraints{};
    DiscardingTextDiagnosticSink discard;
    const auto measured = textRenderer.Layout(measure, discard);
    if (!measured || *measured == nullptr) return;
    molga::ui::UIIntrinsicLayoutRegistry::Get().Publish(
        target, molga::ui::UILabelIntrinsicContentIdentity(measure),
        (*measured)->intrinsicSize);
}
} // namespace

UISystem::~UISystem() {
    molga::ui::SetUIWorldReleaseHandler(nullptr);
    molga::ui::SetUIDeviceRetireHandler(nullptr);
}

UISystem& UISystem::Get() {
    static UISystem system;
    // 이 시설이 처음 만들어지는 순간에만 등록한다. World가 세대를 은퇴시킬 때
    // 부를 이름이 있어야 죽은 월드의 기하 LRU와 스냅샷 칸이 회수된다. UI를
    // 한 번도 만들지 않은 프로세스에서는 핸들러가 없고, 비울 캐시도 없다.
    static const bool releaseHandlerInstalled = [] {
        molga::ui::SetUIWorldReleaseHandler([](std::uint64_t generation) {
            UISystem::Get().OnWorldReleased(generation);
        });
        // 장치 축의 나머지 절반. GraphicsDevice가 은퇴할 때 그 세대에 묶인
        // 스냅샷/슬롯을 놓는다 — 알리지 않으면 죽은 세대의 텍스처 핸들과
        // 바인딩 수명 토큰을 UI가 계속 들고, 그 토큰이 다음 종료를 막는다.
        molga::ui::SetUIDeviceRetireHandler([](std::uint64_t generation) {
            UISystem::Get().ClearFullSnapshotBindingCache(generation);
        });
        return true;
    }();
    (void)releaseHandlerInstalled;
    return system;
}

void UISystem::ProcessInput(World& world, const Vector2& viewportSize,
                            const UIPointerState& pointer) {
    auto& objects = world.Objects();
    auto buttons = GatherButtons(objects, viewportSize);
    if (!pointer.valid) ResetPointerCapture();
    ButtonEntry* topmost = nullptr;
    if (pointer.valid) {
        for (auto& entry : buttons) {
            if (entry.button->IsInteractable() && entry.rect.Contains(pointer.position) &&
                (!topmost || IsHigher(entry, *topmost))) {
                topmost = &entry;
            }
        }
    }

    if (capturedOwner_ != &world) ResetPointerCapture();
    if (pointer.pressedThisFrame && topmost) {
        capturedOwner_ = &world;
        capturedObjectId_ = topmost->object->GetID();
    }

    ButtonEntry* captured = nullptr;
    for (auto& entry : buttons) {
        if (entry.object->GetID() == capturedObjectId_) {
            captured = &entry;
            break;
        }
    }
    unsigned int clickedId = 0;
    for (auto& entry : buttons) {
        const bool hovered = topmost && topmost->object == entry.object;
        const bool isCaptured = captured == &entry;
        const bool clicked = isCaptured && pointer.releasedThisFrame &&
                             pointer.valid && entry.rect.Contains(pointer.position);
        entry.button->ApplyPointerState(hovered, isCaptured && pointer.down, false);
        if (clicked) clickedId = entry.object->GetID();
    }

    if (pointer.releasedThisFrame || (capturedObjectId_ != 0 && !captured)) {
        ResetPointerCapture();
    }
    // Dispatch only after the raw-entry iteration has finished. A click callback
    // may clear the world or transition scenes and invalidate every entry above.
    if (clickedId != 0) {
        if (GameObject* object = world.FindById(clickedId)) {
            if (auto* button = object->GetComponent<UIButton>()) {
                button->ApplyPointerState(topmost && topmost->object == object, false, true);
            }
        }
    }
}

GameObject* UISystem::HitTest(World& world, const Vector2& viewportSize,
                              const Vector2& point) const {
    const auto& objects = world.Objects();
    GameObject* result = nullptr;
    std::tuple<int, int, std::size_t> best{-2147483647, -2147483647, 0};
    std::size_t traversal = 0;
    for (const auto& object : objects) {
        ++traversal;
        if (!object || !IsHierarchyActive(object.get())) continue;
        auto* rect = object->GetComponent<RectTransform>();
        if (!rect || !rect->IsEnabled() || !ActiveCanvas(object.get()) ||
            !rect->GetScreenRect(viewportSize).Contains(point)) continue;
        auto* button = object->GetComponent<UIButton>();
        auto* image = object->GetComponent<UIImage>();
        auto* label = object->GetComponent<UILabel>();
        // Canvas/root layout rectangles are not visual hit targets. Treating a
        // stretch Canvas as selectable would swallow every world-space pick.
        if ((!button || !button->IsEnabled()) &&
            (!image || !image->IsEnabled()) &&
            (!label || !label->IsEnabled())) continue;
        int order = 0;
        if (button && button->IsEnabled()) order = button->GetSortingOrder();
        else if (image && image->IsEnabled()) order = image->GetSortingOrder();
        else if (label && label->IsEnabled()) order = label->GetSortingOrder();
        const auto key = std::make_tuple(CanvasOrder(object.get()), order, traversal);
        if (!result || key > best) {
            result = object.get();
            best = key;
        }
    }
    return result;
}

void UISystem::CollectRender(World& world, const Vector2& viewportSize,
                             molga::RenderQueue& queue,
                             TextRenderer& textRenderer,
                             molga::text::TextDiagnosticSink& textDiagnostics,
                             const TextRasterPolicy& rasterPolicy) {
    const auto& objects = world.Objects();
    for (const auto& object : objects) {
        if (!object || !IsHierarchyActive(object.get())) continue;
        auto* rectTransform = object->GetComponent<RectTransform>();
        if (!rectTransform || !rectTransform->IsEnabled() || !ActiveCanvas(object.get())) continue;
        const AABB rect = rectTransform->GetScreenRect(viewportSize);
        const int canvasOrder = CanvasOrder(object.get());

        if (auto* image = object->GetComponent<UIImage>(); image && image->IsEnabled()) {
            SubmitRect(queue, rect, image->GetTint(), image->GetTexture(),
                       canvasOrder, image->GetSortingOrder());
        }
        if (auto* button = object->GetComponent<UIButton>(); button && button->IsEnabled()) {
            SubmitRect(queue, rect, button->CurrentColor(), nullptr,
                       canvasOrder, button->GetSortingOrder());
        }
        if (auto* label = object->GetComponent<UILabel>(); label && label->IsEnabled() &&
            !label->GetText().empty()) {
            // Step 6: 하나의 제약 있는 공유 요청. 정렬은 배치가 제약 안에서
            // 처리하므로, 예전처럼 측정값에서 원점을 되짚지 않는다 — 그렇게
            // 되짚은 원점은 셰이핑이 실제로 낸 줄과 어긋날 수 있다.
            const molga::text::TextLayoutRequest request =
                BuildLabelRequest(*label, *object, rect);
            const auto layout = textRenderer.Layout(request, textDiagnostics);
            if (!layout) continue;
            // 확정된 배치를 손에 쥔 지점에서만 고유 크기를 게시할 수 있다.
            PublishLabelIntrinsic(world, *label, request, textRenderer);
            // Step 6a: UI는 항등 + 평행이동이다. 논리 UI 원점이 tx/ty 전부다.
            TextCollectContext context;
            context.layoutToOutput.tx = rect.x;
            context.layoutToOutput.ty = rect.y;
            context.color = label->GetColor();
            context.cameraPass = 1;
            context.sortingLayer = canvasOrder;
            context.sortingOrder = label->GetSortingOrder();
            context.rasterPolicy = rasterPolicy;
            textRenderer.CollectLayout(queue, **layout, context,
                                       textDiagnostics);
        }
    }
}

molga::ui::UISnapshotPtr UISystem::BuildLayout(
    World& world, molga::WindowId surfaceWindowId,
    molga::FixedSize logicalViewport,
    const molga::ui::UITextInputVisualStateProvider& inputVisualStates,
    molga::text::TextLayoutService& textLayout,
    molga::text::TextDiagnosticSink& textDiagnostics) {
    return layout_.Build(world, surfaceWindowId, logicalViewport,
                         inputVisualStates, textLayout, textDiagnostics);
}

void UISystem::OnDeviceGenerationChanged(std::uint64_t oldGeneration,
                                         std::uint64_t newGeneration) {
    layout_.OnDeviceGenerationChanged(oldGeneration, newGeneration);
}

void UISystem::ClearFullSnapshotBindingCache(std::uint64_t deviceGeneration) {
    layout_.ClearFullSnapshotBindingCache(deviceGeneration);
}

std::size_t UISystem::FullSnapshotCacheEntryCountForWorldDevice(
    molga::ui::UISnapshotWorldDeviceSlotKey slot) const noexcept {
    return layout_.FullSnapshotCacheEntryCountForWorldDevice(slot);
}

void UISystem::InstallLayoutDependencies(
    const molga::ui::UITextInputVisualStateProvider& inputVisualStates,
    molga::text::TextLayoutService& textLayout) noexcept {
    inputVisualStates_ = &inputVisualStates;
    textLayout_ = &textLayout;
}

bool UISystem::HasLayoutDependencies() const noexcept {
    return inputVisualStates_ != nullptr && textLayout_ != nullptr;
}

molga::ui::UISnapshotPtr UISystem::BuildLayout(
    World& world, molga::WindowId surfaceWindowId,
    molga::FixedSize logicalViewport,
    molga::text::TextDiagnosticSink& textDiagnostics) {
    if (!HasLayoutDependencies()) return nullptr;
    return layout_.Build(world, surfaceWindowId, logicalViewport,
                         *inputVisualStates_, *textLayout_, textDiagnostics);
}

void UISystem::OnWorldReleased(std::uint64_t worldGeneration) {
    layout_.OnWorldReleased(worldGeneration);
}

void UISystem::ResetPointerCapture() {
    capturedOwner_ = nullptr;
    capturedObjectId_ = 0;
}
