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

#include <algorithm>
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
    command.batchKey.texture = texture;
    command.batchKey.isBatchable = true;
    command.isBatchableSprite = true;
    FillVertex(command.vertices[0], rect.x, rect.y, 0.0f, 1.0f, color);
    FillVertex(command.vertices[1], rect.x + rect.width, rect.y, 1.0f, 1.0f, color);
    FillVertex(command.vertices[2], rect.x + rect.width, rect.y + rect.height, 1.0f, 0.0f, color);
    FillVertex(command.vertices[3], rect.x, rect.y + rect.height, 0.0f, 0.0f, color);
    queue.Submit(command);
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
                             molga::RenderQueue& queue) {
    CollectRender(world.Objects(), viewportSize, queue);
}

void UISystem::CollectRender(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const Vector2& viewportSize, molga::RenderQueue& queue) {
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
            TextRenderer& textRenderer = TextRenderer::Get();
            const TextMetrics metrics = textRenderer.MeasureText(
                label->GetText(), label->GetFontGuid(), label->GetFontSizePx(),
                1.0f, label->GetLineSpacing());

            TextDrawParams params;
            params.text = label->GetText();
            params.fontGuid = label->GetFontGuid();
            params.fontSizePx = label->GetFontSizePx();
            params.lineSpacing = label->GetLineSpacing();
            params.color = label->GetColor();
            params.cameraPass = 1;
            params.sortingLayer = canvasOrder;
            params.sortingOrder = label->GetSortingOrder();
            params.y = rect.y;
            if (label->GetVerticalAlignment() == UILabel::VerticalAlignment::Middle) {
                params.y += (rect.height - metrics.height) * 0.5f;
            } else if (label->GetVerticalAlignment() == UILabel::VerticalAlignment::Bottom) {
                params.y += rect.height - metrics.height;
            }

            params.x = rect.x;
            if (label->GetHorizontalAlignment() == UILabel::HorizontalAlignment::Center) {
                params.alignment = TextHorizontalAlignment::Center;
                params.x += rect.width * 0.5f;
            } else if (label->GetHorizontalAlignment() == UILabel::HorizontalAlignment::Right) {
                params.alignment = TextHorizontalAlignment::Right;
                params.x += rect.width;
            }
            textRenderer.CollectText(queue, params);
        }
    }
}

void UISystem::ResetPointerCapture() {
    capturedOwner_ = nullptr;
    capturedObjectId_ = 0;
}
