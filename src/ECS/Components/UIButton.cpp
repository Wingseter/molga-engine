#include "ECS/Components/UIButton.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>

REGISTER_COMPONENT(UIButton)

namespace {
nlohmann::json ColorJson(const Color& color) {
    return {color.r, color.g, color.b, color.a};
}
Color ReadColor(const nlohmann::json& j, const char* key, Color fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4) return fallback;
    return {j[key][0].get<float>(), j[key][1].get<float>(),
            j[key][2].get<float>(), j[key][3].get<float>()};
}
} // namespace

void UIButton::SetInteractable(bool value) {
    if (interactable_ == value) return;
    interactable_ = value;
    if (!interactable_) ClearPointerState();
    Invalidate(UIInvalidation::Interaction);
}

void UIButton::SetNormalColor(const Color& value) {
    if (normalColor_ == value) return;
    normalColor_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UIButton::SetHoverColor(const Color& value) {
    if (hoverColor_ == value) return;
    hoverColor_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UIButton::SetPressedColor(const Color& value) {
    if (pressedColor_ == value) return;
    pressedColor_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UIButton::SetDisabledColor(const Color& value) {
    if (disabledColor_ == value) return;
    disabledColor_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UIButton::SetSortingOrder(int value) {
    if (sortingOrder_ == value) return;
    sortingOrder_ = value;
    Invalidate(UIInvalidation::Visual);
}

Color UIButton::CurrentColor() const {
    if (!interactable_) return disabledColor_;
    if (pressed_) return pressedColor_;
    if (hovered_) return hoverColor_;
    return normalColor_;
}

void UIButton::ApplyPointerState(bool hovered, bool pressed, bool clicked) {
    hovered_ = interactable_ && hovered;
    pressed_ = interactable_ && pressed;
    clickedThisFrame_ = interactable_ && clicked;
    // A callback may delete this component (for example via a scene load), so
    // keep an invocation-safe copy and do not touch members after calling it.
    std::function<void()> callback = clickedThisFrame_ ? onClick_ : std::function<void()>{};
    if (callback) callback();
}

void UIButton::Serialize(nlohmann::json& j) const {
    j["interactable"] = interactable_;
    j["normalColor"] = ColorJson(normalColor_);
    j["hoverColor"] = ColorJson(hoverColor_);
    j["pressedColor"] = ColorJson(pressedColor_);
    j["disabledColor"] = ColorJson(disabledColor_);
    j["sortingOrder"] = sortingOrder_;
}

void UIButton::Deserialize(const nlohmann::json& j) {
    SetInteractable(j.value("interactable", interactable_));
    SetNormalColor(ReadColor(j, "normalColor", normalColor_));
    SetHoverColor(ReadColor(j, "hoverColor", hoverColor_));
    SetPressedColor(ReadColor(j, "pressedColor", pressedColor_));
    SetDisabledColor(ReadColor(j, "disabledColor", disabledColor_));
    SetSortingOrder(j.value("sortingOrder", sortingOrder_));
    // hover/press/click은 런타임 상태다. 저작 revision을 올리지 않는다.
    hovered_ = pressed_ = clickedThisFrame_ = false;
}
