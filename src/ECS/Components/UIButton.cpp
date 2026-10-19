#include "ECS/Components/UIButton.h"

#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"
#include "UI/UIInteractionState.h"

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
    if (!interactable_) ClearRuntimePointerState();
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

molga::ui::UIRuntimeTargetIdentity UIButton::RuntimeIdentity() const noexcept {
    const GameObject* owner = GetGameObject();
    const World* world = owner ? owner->GetWorld() : nullptr;
    if (!world) return {};
    return molga::ui::CaptureTarget(*world, *this);
}

bool UIButton::WasClickedThisFrame() const {
    return molga::ui::UIInteractionStateTable::Get()
        .ButtonStateAnySurface(RuntimeIdentity())
        .clickedThisFrame;
}

bool UIButton::IsHovered() const {
    return interactable_ && molga::ui::UIInteractionStateTable::Get()
                                .ButtonStateAnySurface(RuntimeIdentity())
                                .hovered;
}

bool UIButton::IsPressed() const {
    return interactable_ && molga::ui::UIInteractionStateTable::Get()
                                .ButtonStateAnySurface(RuntimeIdentity())
                                .pressed;
}

Color UIButton::ColorFor(const molga::ui::UIButtonPointerState& state) const noexcept {
    if (!interactable_) return disabledColor_;
    if (state.pressed) return pressedColor_;
    if (state.hovered) return hoverColor_;
    return normalColor_;
}

Color UIButton::CurrentColor() const {
    if (!interactable_) return disabledColor_;
    return ColorFor(molga::ui::UIInteractionStateTable::Get()
                        .ButtonStateAnySurface(RuntimeIdentity()));
}

void UIButton::ClearRuntimePointerState() {
    // 지우기에는 소속 확인(CaptureTarget의 World::FindById)을 하지 않는다. 장면
    // 교체가 Objects()를 직접 비우는 도중(SceneSerializer::DeserializeScene →
    // ~GameObject → OnDisable)에는 그 순회가 이미 해제된 앞 오브젝트를 밟는다.
    // 없는 식별자를 지우는 것은 아무 일도 하지 않으므로 확인 없이도 안전하다.
    const GameObject* owner = GetGameObject();
    const World* world = owner ? owner->GetWorld() : nullptr;
    if (!world) return;
    molga::ui::UIRuntimeTargetIdentity identity;
    identity.worldGeneration = world->Generation();
    identity.objectId = owner->GetID();
    identity.componentRuntimeTypeId = GetRuntimeTypeID();
    identity.componentInstanceId = GetInstanceID();
    if (identity) {
        molga::ui::UIInteractionStateTable::Get().ClearIdentityEverywhere(
            identity, nullptr);
    }
}

void UIButton::InvokeOnClick() {
    // 콜백이 이 컴포넌트를 지울 수 있다(예: 씬 로드). 호출 가능한 사본을 만들고
    // 호출 뒤에는 멤버를 건드리지 않는다.
    std::function<void()> callback = onClick_;
    if (callback) callback();
}

void UIButton::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
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
    // hover/press/click은 런타임 상태다(표가 가진다). 저작 revision을 올리지
    // 않고, 붙어 있으면 그 항목만 지운다.
    ClearRuntimePointerState();
}
