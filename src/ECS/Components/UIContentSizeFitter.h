#pragma once

#include "ECS/Components/UIComponent.h"

#include <cstdint>

// 자기 rect를 내용의 고유 크기에 맞추는 저작 정책. 축마다 독립이며, 측정된
// 크기 자체는 여기 살지 않는다.
class UIContentSizeFitter : public UIComponent {
public:
    COMPONENT_TYPE(UIContentSizeFitter)

    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    UIFitMode HorizontalFit() const noexcept { return horizontalFit_; }
    UIFitMode VerticalFit() const noexcept { return verticalFit_; }

    void SetHorizontalFit(UIFitMode value);
    void SetVerticalFit(UIFitMode value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    UIFitMode horizontalFit_ = UIFitMode::Unconstrained;
    UIFitMode verticalFit_ = UIFitMode::Unconstrained;
};
