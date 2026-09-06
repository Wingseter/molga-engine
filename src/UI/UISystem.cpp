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

GameObject* FindById(const std::vector<std::shared_ptr<GameObject>>& objects,
                     unsigned int id) {
    for (const auto& object : objects) {
        if (object && object->GetID() == id) return object.get();
    }
    return nullptr;
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
} // namespace

UISystem& UISystem::Get() {
    static UISystem system;
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

void UISystem::ProcessInput(std::vector<std::shared_ptr<GameObject>>& objects,
                            const Vector2& viewportSize,
                            const UIPointerState& pointer) {
    auto buttons = GatherButtons(objects, viewportSize);
    if (!pointer.valid) ResetPointerCapture();
    ButtonEntry* topmost = nullptr;
    if (pointer.valid) {
        for (auto& entry : buttons) {
            if (entry.button->IsInteractable() && entry.rect.Contains(pointer.position) &&
                (!topmost || IsHigher(entry, *topmost))) topmost = &entry;
        }
    }
    if (capturedOwner_ != &objects) ResetPointerCapture();
    if (pointer.pressedThisFrame && topmost) {
        capturedOwner_ = &objects;
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
        const bool clicked = isCaptured && pointer.releasedThisFrame && pointer.valid &&
                             entry.rect.Contains(pointer.position);
        entry.button->ApplyPointerState(hovered, isCaptured && pointer.down, false);
        if (clicked) clickedId = entry.object->GetID();
    }
    if (pointer.releasedThisFrame || (capturedObjectId_ != 0 && !captured)) {
        ResetPointerCapture();
    }
    if (clickedId != 0) {
        if (GameObject* object = FindById(objects, clickedId)) {
            if (auto* button = object->GetComponent<UIButton>()) {
                button->ApplyPointerState(topmost && topmost->object == object, false, true);
            }
        }
    }
}

GameObject* UISystem::HitTest(World& world, const Vector2& viewportSize,
                              const Vector2& point) const {
    return HitTest(world.Objects(), viewportSize, point);
}

GameObject* UISystem::HitTest(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const Vector2& viewportSize, const Vector2& point) const {
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
    CollectRender(world.Objects(), viewportSize, queue, textRenderer,
                  textDiagnostics, rasterPolicy);
}

void UISystem::CollectRender(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const Vector2& viewportSize, molga::RenderQueue& queue,
    TextRenderer& textRenderer,
    molga::text::TextDiagnosticSink& textDiagnostics,
    const TextRasterPolicy& rasterPolicy) {
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
            const auto layout = textRenderer.Layout(
                BuildLabelRequest(*label, *object, rect), textDiagnostics);
            if (!layout) continue;
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

void UISystem::ResetPointerCapture() {
    capturedOwner_ = nullptr;
    capturedObjectId_ = 0;
}
