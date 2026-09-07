#pragma once

#include "ECS/Components/UIComponent.h"
#include "Text/TextLayoutTypes.h"

#include <cstdint>

// 격자 채우기가 어느 모서리에서 시작하는가. UILayoutGroup 전용이라 공용
// 헤더가 아니라 여기 산다.
enum class UIGridStartCorner : std::uint8_t {
    UpperLeft, UpperRight, LowerLeft, LowerRight
};
enum class UIGridFillAxis : std::uint8_t { Horizontal, Vertical };

std::string_view ToCanonicalString(UIGridStartCorner value) noexcept;
std::optional<UIGridStartCorner> ParseUIGridStartCorner(const std::string&);
std::string_view ToCanonicalString(UIGridFillAxis value) noexcept;
std::optional<UIGridFillAxis> ParseUIGridFillAxis(const std::string&);

// 자식을 줄/열/격자로 배치하는 저작 정책 전부. 확정된 자식 rect는 여기 없다.
//
// 자식 정렬은 UILabel과 같은 문단 정렬 열거를 쓴다. 축마다 타입이 달라
// (TextHorizontalAlignment / TextVerticalAlignment) 두 축을 바꿔 대입하는
// 실수가 컴파일에서 걸린다 — Task 8.1이 놓쳤던 바로 그 종류의 결함이다.
class UILayoutGroup : public UIComponent {
public:
    COMPONENT_TYPE(UILayoutGroup)

    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    UILayoutMode Mode() const noexcept { return mode_; }
    float PaddingLeft() const noexcept { return paddingLeft_; }
    float PaddingRight() const noexcept { return paddingRight_; }
    float PaddingTop() const noexcept { return paddingTop_; }
    float PaddingBottom() const noexcept { return paddingBottom_; }
    float SpacingX() const noexcept { return spacingX_; }
    float SpacingY() const noexcept { return spacingY_; }
    molga::text::TextHorizontalAlignment ChildHorizontalAlignment() const noexcept {
        return childHorizontalAlignment_;
    }
    molga::text::TextVerticalAlignment ChildVerticalAlignment() const noexcept {
        return childVerticalAlignment_;
    }
    bool ControlChildWidth() const noexcept { return controlChildWidth_; }
    bool ControlChildHeight() const noexcept { return controlChildHeight_; }
    bool ChildForceExpandWidth() const noexcept { return childForceExpandWidth_; }
    bool ChildForceExpandHeight() const noexcept { return childForceExpandHeight_; }
    float CellSizeX() const noexcept { return cellSizeX_; }
    float CellSizeY() const noexcept { return cellSizeY_; }
    UIGridStartCorner StartCorner() const noexcept { return startCorner_; }
    UIGridFillAxis FillAxis() const noexcept { return fillAxis_; }
    UIGridConstraint GridConstraint() const noexcept { return gridConstraint_; }
    std::uint32_t ConstraintCount() const noexcept { return constraintCount_; }

    void SetMode(UILayoutMode value);
    void SetPadding(float left, float right, float top, float bottom);
    void SetSpacingX(float value);
    void SetSpacingY(float value);
    void SetChildHorizontalAlignment(molga::text::TextHorizontalAlignment value);
    void SetChildVerticalAlignment(molga::text::TextVerticalAlignment value);
    void SetControlChildWidth(bool value);
    void SetControlChildHeight(bool value);
    void SetChildForceExpandWidth(bool value);
    void SetChildForceExpandHeight(bool value);
    void SetCellSize(float x, float y);
    void SetStartCorner(UIGridStartCorner value);
    void SetFillAxis(UIGridFillAxis value);
    void SetGridConstraint(UIGridConstraint value);
    void SetConstraintCount(std::uint32_t value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    UILayoutMode mode_ = UILayoutMode::Horizontal;
    float paddingLeft_ = 0.0f;
    float paddingRight_ = 0.0f;
    float paddingTop_ = 0.0f;
    float paddingBottom_ = 0.0f;
    float spacingX_ = 0.0f;
    float spacingY_ = 0.0f;
    molga::text::TextHorizontalAlignment childHorizontalAlignment_ =
        molga::text::TextHorizontalAlignment::Left;
    molga::text::TextVerticalAlignment childVerticalAlignment_ =
        molga::text::TextVerticalAlignment::Top;
    bool controlChildWidth_ = false;
    bool controlChildHeight_ = false;
    bool childForceExpandWidth_ = false;
    bool childForceExpandHeight_ = false;
    // 격자 칸은 0이 될 수 없다. 0이면 한 칸에 무한한 자식이 들어가고 배치가
    // 끝나지 않는다.
    float cellSizeX_ = 100.0f;
    float cellSizeY_ = 100.0f;
    UIGridStartCorner startCorner_ = UIGridStartCorner::UpperLeft;
    UIGridFillAxis fillAxis_ = UIGridFillAxis::Horizontal;
    UIGridConstraint gridConstraint_ = UIGridConstraint::Flexible;
    std::uint32_t constraintCount_ = 1;
};
