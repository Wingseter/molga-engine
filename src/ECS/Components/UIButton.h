#pragma once

#include "ECS/Components/UIComponent.h"
#include "Common/Types.h"

#include <cstdint>
#include <functional>

class UIButton : public UIComponent {
public:
    COMPONENT_TYPE(UIButton)

    // RectTransform과 같은 이유로 표식이 없다: 저작 모양이 바뀐 적이 없어
    // schema 1이 레거시 문서의 모양과 같다. hover/press/click은 런타임 상태라
    // 저작 스키마에 들어가지 않으므로 이 버전이 덮는 대상도 아니다.
    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    void SetOnClick(std::function<void()> callback) { onClick_ = std::move(callback); }
    bool WasClickedThisFrame() const { return clickedThisFrame_; }
    bool IsHovered() const { return hovered_; }
    bool IsPressed() const { return pressed_; }
    bool IsInteractable() const { return interactable_; }
    void SetInteractable(bool value);

    const Color& GetNormalColor() const { return normalColor_; }
    const Color& GetHoverColor() const { return hoverColor_; }
    const Color& GetPressedColor() const { return pressedColor_; }
    const Color& GetDisabledColor() const { return disabledColor_; }
    void SetNormalColor(const Color& value);
    void SetHoverColor(const Color& value);
    void SetPressedColor(const Color& value);
    void SetDisabledColor(const Color& value);
    int GetSortingOrder() const { return sortingOrder_; }
    void SetSortingOrder(int value);
    Color CurrentColor() const;

    // Called only by UISystem after topmost/capture arbitration.
    void ApplyPointerState(bool hovered, bool pressed, bool clicked);
    void OnDisable() override { ClearPointerState(); }

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    void ClearPointerState() {
        hovered_ = false;
        pressed_ = false;
        clickedThisFrame_ = false;
    }
    bool interactable_ = true;
    bool hovered_ = false;
    bool pressed_ = false;
    bool clickedThisFrame_ = false;
    Color normalColor_{0.25f, 0.30f, 0.42f, 1.0f};
    Color hoverColor_{0.35f, 0.42f, 0.58f, 1.0f};
    Color pressedColor_{0.17f, 0.20f, 0.30f, 1.0f};
    Color disabledColor_{0.25f, 0.25f, 0.25f, 0.55f};
    int sortingOrder_ = 0;
    std::function<void()> onClick_;
};
