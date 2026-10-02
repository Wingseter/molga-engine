#include "ECS/Components/RectTransform.h"

#include "ECS/Components/UICanvas.h"
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>

REGISTER_COMPONENT(RectTransform)

namespace {
// std::clamp(NaN, 0, 1)은 NaN을 그대로 돌려준다. 그래서 정규화 전에 유한성을
// 먼저 확인해야 한다 — NaN 앵커는 나중 26.6 변환에서 조용히 실패하고, 그
// 실패는 "폭 0" 같은 정당해 보이는 사각형으로 나타난다.
Vector2 CanonicalAnchor(const Vector2& value, const char* what) {
    RequireFiniteUIValue(value.x, what);
    RequireFiniteUIValue(value.y, what);
    return {NormalizeUISignedZero(std::clamp(value.x, 0.0f, 1.0f)),
            NormalizeUISignedZero(std::clamp(value.y, 0.0f, 1.0f))};
}

// -0.0은 +0.0과 값이 같지만 비트가 달라, 같은 저작 상태가 두 개의 캐시
// 정체성을 만든다. 저장 직전에 한 번만 정규화한다.
Vector2 CanonicalOffset(const Vector2& value, const char* what) {
    RequireFiniteUIValue(value.x, what);
    RequireFiniteUIValue(value.y, what);
    return {NormalizeUISignedZero(value.x), NormalizeUISignedZero(value.y)};
}
} // namespace

void RectTransform::SetAnchorMin(const Vector2& value) {
    const Vector2 canonical = CanonicalAnchor(value, "RectTransform.anchorMin");
    if (anchorMin_ == canonical) return;
    anchorMin_ = canonical;
    Invalidate(UIInvalidation::Layout);
}
void RectTransform::SetAnchorMax(const Vector2& value) {
    const Vector2 canonical = CanonicalAnchor(value, "RectTransform.anchorMax");
    if (anchorMax_ == canonical) return;
    anchorMax_ = canonical;
    Invalidate(UIInvalidation::Layout);
}
void RectTransform::SetAnchors(const Vector2& minimum, const Vector2& maximum) {
    SetAnchorMin(minimum);
    SetAnchorMax(maximum);
}
void RectTransform::SetPivot(const Vector2& value) {
    const Vector2 canonical = CanonicalAnchor(value, "RectTransform.pivot");
    if (pivot_ == canonical) return;
    pivot_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void RectTransform::SetAnchoredPosition(const Vector2& value) {
    const Vector2 canonical =
        CanonicalOffset(value, "RectTransform.anchoredPosition");
    if (anchoredPosition_ == canonical) return;
    anchoredPosition_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void RectTransform::SetSizeDelta(const Vector2& value) {
    const Vector2 canonical = CanonicalOffset(value, "RectTransform.sizeDelta");
    if (sizeDelta_ == canonical) return;
    sizeDelta_ = canonical;
    Invalidate(UIInvalidation::Layout);
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
    j["schemaVersion"] = CurrentSchemaVersion;
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
    // 세터를 거쳐야 한다. 되돌리기와 prefab override는 살아 있는 컴포넌트에
    // 다시 Deserialize하므로, 직접 대입하면 값만 바뀌고 Layout 무효화가 일어나지
    // 않는다. 위의 anchor/pivot은 이미 세터를 쓰고 있었다.
    SetAnchoredPosition(read("anchoredPosition", anchoredPosition_));
    SetSizeDelta(read("sizeDelta", sizeDelta_));
}
