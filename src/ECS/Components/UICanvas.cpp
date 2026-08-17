#include "ECS/Components/UICanvas.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>
#include <cmath>

REGISTER_COMPONENT(UICanvas)

void UICanvas::SetReferenceResolution(const Vector2& value) {
    referenceResolution_.x = std::max(value.x, 1.0f);
    referenceResolution_.y = std::max(value.y, 1.0f);
}

void UICanvas::SetMatchWidthOrHeight(float value) {
    matchWidthOrHeight_ = std::clamp(value, 0.0f, 1.0f);
}

float UICanvas::ScaleFactor(const Vector2& viewportSize) const {
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return 1.0f;
    const float widthScale = viewportSize.x / referenceResolution_.x;
    const float heightScale = viewportSize.y / referenceResolution_.y;
    // Geometric interpolation matches Unity CanvasScaler and remains stable
    // when one dimension is much larger than the other.
    return std::pow(std::max(widthScale, 0.0001f), 1.0f - matchWidthOrHeight_) *
           std::pow(std::max(heightScale, 0.0001f), matchWidthOrHeight_);
}

Vector2 UICanvas::LogicalSize(const Vector2& viewportSize) const {
    const float scale = ScaleFactor(viewportSize);
    return scale > 0.0f ? viewportSize / scale : viewportSize;
}

void UICanvas::Serialize(nlohmann::json& j) const {
    j["referenceResolution"] = {referenceResolution_.x, referenceResolution_.y};
    j["matchWidthOrHeight"] = matchWidthOrHeight_;
    j["sortingOrder"] = sortingOrder_;
}

void UICanvas::Deserialize(const nlohmann::json& j) {
    if (j.contains("referenceResolution") &&
        j["referenceResolution"].is_array() &&
        j["referenceResolution"].size() >= 2) {
        SetReferenceResolution({j["referenceResolution"][0].get<float>(),
                                j["referenceResolution"][1].get<float>()});
    }
    SetMatchWidthOrHeight(j.value("matchWidthOrHeight", matchWidthOrHeight_));
    sortingOrder_ = j.value("sortingOrder", sortingOrder_);
}
