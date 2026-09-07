#pragma once

#include "ECS/Components/UIComponent.h"

#include <cstdint>

// 한 요소가 부모 레이아웃에 내미는 축별 요구값. 계산된 크기는 담지 않는다 —
// 측정 결과는 스냅샷의 것이고, 여기 두면 저장 파일이 프레임마다 달라진다.
class UILayoutElement : public UIComponent {
public:
    COMPONENT_TYPE(UILayoutElement)

    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    const UIAxisConstraint& Horizontal() const noexcept { return horizontal_; }
    const UIAxisConstraint& Vertical() const noexcept { return vertical_; }
    bool IgnoreLayout() const noexcept { return ignoreLayout_; }

    void SetHorizontal(const UIAxisConstraint& value);
    void SetVertical(const UIAxisConstraint& value);
    void SetIgnoreLayout(bool value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    UIAxisConstraint horizontal_;
    UIAxisConstraint vertical_;
    bool ignoreLayout_ = false;
};
