#pragma once

#include "ECS/Components/UIComponent.h"
#include "Common/Types.h"
#include "UI/UIRuntimeIdentity.h"

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
    // hover/press/click은 이 컴포넌트의 멤버가 아니다. UIInteractionStateTable이
    // 표면 키로 들고 있고, 아래 getter는 그 표를 읽는다(창을 모르는 호출부를
    // 위해 같은 월드 세대의 모든 표면을 OR 한다).
    bool WasClickedThisFrame() const;
    bool IsHovered() const;
    bool IsPressed() const;
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
    // 저작 색 + 표의 hover/pressed(어느 표면이든).
    Color CurrentColor() const;

    // ── 레거시 포인터 경로(UISystem::ProcessInput) 전용 어댑터 ──────────────
    // 입력 라우터는 이것을 부르지 않는다. 예약 표면 {0, worldGeneration}에 쓰고,
    // clicked이면 콜백을 부른다. Task 12.3이 레거시 경로와 함께 지운다.
    void ApplyPointerState(bool hovered, bool pressed, bool clicked);
    // 라우터가 Click을 판정한 **뒤에만** 부른다. 콜백 사본을 호출하고 멤버를
    // 건드리지 않는다(콜백이 이 컴포넌트를 지울 수 있다). 상호작용 여부는 여기서
    // 다시 묻지 않는다 — 그 권위는 스냅샷 N이다.
    void InvokeOnClick();
    // 소유자 월드에서 잡은 완전한 식별자. 월드에 없으면 빈 값.
    molga::ui::UIRuntimeTargetIdentity RuntimeIdentity() const noexcept;
    void OnDisable() override { ClearRuntimePointerState(); }

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    // 모든 표면에서 이 버튼의 항목을 지우고 잡힌 캡처를 푼다.
    void ClearRuntimePointerState();
    bool interactable_ = true;
    Color normalColor_{0.25f, 0.30f, 0.42f, 1.0f};
    Color hoverColor_{0.35f, 0.42f, 0.58f, 1.0f};
    Color pressedColor_{0.17f, 0.20f, 0.30f, 1.0f};
    Color disabledColor_{0.25f, 0.25f, 0.25f, 0.55f};
    int sortingOrder_ = 0;
    std::function<void()> onClick_;
};
