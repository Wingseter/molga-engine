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
    interactable_ = j.value("interactable", interactable_);
    normalColor_ = ReadColor(j, "normalColor", normalColor_);
    hoverColor_ = ReadColor(j, "hoverColor", hoverColor_);
    pressedColor_ = ReadColor(j, "pressedColor", pressedColor_);
    disabledColor_ = ReadColor(j, "disabledColor", disabledColor_);
    sortingOrder_ = j.value("sortingOrder", sortingOrder_);
    hovered_ = pressed_ = clickedThisFrame_ = false;
}
