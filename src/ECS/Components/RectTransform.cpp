#include "ECS/Components/RectTransform.h"

#include "ECS/Components/UICanvas.h"
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>

REGISTER_COMPONENT(RectTransform)

namespace {
Vector2 Clamp01(const Vector2& value) {
    return {std::clamp(value.x, 0.0f, 1.0f),
            std::clamp(value.y, 0.0f, 1.0f)};
}
} // namespace

void RectTransform::SetAnchorMin(const Vector2& value) {
    anchorMin_ = Clamp01(value);
}
void RectTransform::SetAnchorMax(const Vector2& value) {
    anchorMax_ = Clamp01(value);
}
void RectTransform::SetAnchors(const Vector2& minimum, const Vector2& maximum) {
    SetAnchorMin(minimum);
    SetAnchorMax(maximum);
}
void RectTransform::SetPivot(const Vector2& value) {
    pivot_ = Clamp01(value);
}

AABB RectTransform::ResolveIn(const AABB& parentRect) const {
    const Vector2 parentSize{parentRect.width, parentRect.height};
    const Vector2 anchorSpan{(anchorMax_.x - anchorMin_.x) * parentSize.x,
                             (anchorMax_.y - anchorMin_.y) * parentSize.y};
    const Vector2 size{anchorSpan.x + sizeDelta_.x,
                       anchorSpan.y + sizeDelta_.y};
    const Vector2 anchorReference{
        parentRect.x + parentSize.x *
            (anchorMin_.x + (anchorMax_.x - anchorMin_.x) * pivot_.x),
        parentRect.y + parentSize.y *
            (anchorMin_.y + (anchorMax_.y - anchorMin_.y) * pivot_.y)};
    return {anchorReference.x + anchoredPosition_.x - size.x * pivot_.x,
            anchorReference.y + anchoredPosition_.y - size.y * pivot_.y,
            size.x,
            size.y};
}

const UICanvas* RectTransform::FindCanvas() const {
    for (GameObject* node = gameObject; node; node = node->GetParent()) {
        if (const auto* canvas = node->GetComponent<UICanvas>()) return canvas;
    }
    return nullptr;
}

AABB RectTransform::ResolveLogical(const Vector2& viewportSize) const {
    const UICanvas* canvas = FindCanvas();
    if (!canvas) return {};

    const GameObject* parent = gameObject ? gameObject->GetParent() : nullptr;
    while (parent) {
        if (const auto* parentRect = parent->GetComponent<RectTransform>()) {
            return ResolveIn(parentRect->ResolveLogical(viewportSize));
        }
        parent = parent->GetParent();
    }

    const Vector2 logical = canvas->LogicalSize(viewportSize);
    return ResolveIn({0.0f, 0.0f, logical.x, logical.y});
}

AABB RectTransform::GetScreenRect(const Vector2& viewportSize) const {
    const UICanvas* canvas = FindCanvas();
    if (!canvas) return {};
    AABB rect = ResolveLogical(viewportSize);
    const float scale = canvas->ScaleFactor(viewportSize);
    rect.x *= scale;
    rect.y *= scale;
    rect.width *= scale;
    rect.height *= scale;
    return rect;
}

void RectTransform::Serialize(nlohmann::json& j) const {
    j["anchorMin"] = {anchorMin_.x, anchorMin_.y};
    j["anchorMax"] = {anchorMax_.x, anchorMax_.y};
    j["pivot"] = {pivot_.x, pivot_.y};
    j["anchoredPosition"] = {anchoredPosition_.x, anchoredPosition_.y};
    j["sizeDelta"] = {sizeDelta_.x, sizeDelta_.y};
}

void RectTransform::Deserialize(const nlohmann::json& j) {
    auto read = [&j](const char* key, Vector2 fallback) {
        if (j.contains(key) && j[key].is_array() && j[key].size() >= 2) {
            return Vector2{j[key][0].get<float>(), j[key][1].get<float>()};
        }
        return fallback;
    };
    SetAnchorMin(read("anchorMin", anchorMin_));
    SetAnchorMax(read("anchorMax", anchorMax_));
    SetPivot(read("pivot", pivot_));
    anchoredPosition_ = read("anchoredPosition", anchoredPosition_);
    sizeDelta_ = read("sizeDelta", sizeDelta_);
}
