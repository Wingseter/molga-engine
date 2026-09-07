#include "ECS/Components/UILayoutGroup.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

REGISTER_COMPONENT(UILayoutGroup)

namespace {

// 여백과 간격은 음수가 될 수 없다. 음수 여백은 자식을 부모 밖으로 밀어내
// 나중 clip 교집합을 빈 사각형으로 만들고, 그러면 렌더/히트 기록이 통째로
// 사라진다 — 조용히 아무것도 안 그려지는 실패다.
// -0.0f는 0.0f와 같은 값이므로 이 검사를 통과하지만 비트가 다르다. 직전 값이
// 0이 아니면 setter의 동일성 검사도 통과해 그대로 저장되고, 그러면 논리적으로
// 같은 저작 상태가 편집 순서에 따라 "-0.0"이나 "0.0"으로 남는다.
float RequireNonNegative(float value, const char* what) {
    if (RequireFiniteUIValue(value, what) < 0.0f) {
        ThrowUILayoutInvalid(std::string(what) + " must not be negative");
    }
    return NormalizeUISignedZero(value);
}

float RequirePositive(float value, const char* what) {
    if (!(RequireFiniteUIValue(value, what) > 0.0f)) {
        ThrowUILayoutInvalid(std::string(what) + " must be positive");
    }
    return value;
}

} // namespace

std::string_view ToCanonicalString(UIGridStartCorner value) noexcept {
    switch (value) {
        case UIGridStartCorner::UpperRight: return "UpperRight";
        case UIGridStartCorner::LowerLeft: return "LowerLeft";
        case UIGridStartCorner::LowerRight: return "LowerRight";
        case UIGridStartCorner::UpperLeft: break;
    }
    return "UpperLeft";
}

std::optional<UIGridStartCorner> ParseUIGridStartCorner(
    const std::string& token) {
    if (token == "UpperLeft") return UIGridStartCorner::UpperLeft;
    if (token == "UpperRight") return UIGridStartCorner::UpperRight;
    if (token == "LowerLeft") return UIGridStartCorner::LowerLeft;
    if (token == "LowerRight") return UIGridStartCorner::LowerRight;
    return std::nullopt;
}

std::string_view ToCanonicalString(UIGridFillAxis value) noexcept {
    switch (value) {
        case UIGridFillAxis::Vertical: return "Vertical";
        case UIGridFillAxis::Horizontal: break;
    }
    return "Horizontal";
}

std::optional<UIGridFillAxis> ParseUIGridFillAxis(const std::string& token) {
    if (token == "Horizontal") return UIGridFillAxis::Horizontal;
    if (token == "Vertical") return UIGridFillAxis::Vertical;
    return std::nullopt;
}

void UILayoutGroup::SetMode(UILayoutMode value) {
    if (mode_ == value) return;
    mode_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetPadding(float left, float right, float top,
                               float bottom) {
    // 네 값 전부를 먼저 검증한다. 하나씩 저장하면서 검증하면 세 번째 값에서
    // 거부됐을 때 앞의 두 값만 바뀐 반쪽 상태가 남는다.
    const float canonicalLeft = RequireNonNegative(left, "paddingLeft");
    const float canonicalRight = RequireNonNegative(right, "paddingRight");
    const float canonicalTop = RequireNonNegative(top, "paddingTop");
    const float canonicalBottom = RequireNonNegative(bottom, "paddingBottom");
    if (paddingLeft_ == canonicalLeft && paddingRight_ == canonicalRight &&
        paddingTop_ == canonicalTop && paddingBottom_ == canonicalBottom) {
        // 비교는 값으로 하지만 저장은 정규형으로 한다. -0.0f는 여기서 같다고
        // 판정되어 조기 반환하므로 부호 비트가 들어올 자리가 없다.
        return;
    }
    paddingLeft_ = canonicalLeft;
    paddingRight_ = canonicalRight;
    paddingTop_ = canonicalTop;
    paddingBottom_ = canonicalBottom;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetSpacingX(float value) {
    const float canonical = RequireNonNegative(value, "spacingX");
    if (spacingX_ == canonical) return;
    spacingX_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetSpacingY(float value) {
    const float canonical = RequireNonNegative(value, "spacingY");
    if (spacingY_ == canonical) return;
    spacingY_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetChildHorizontalAlignment(
    molga::text::TextHorizontalAlignment value) {
    if (childHorizontalAlignment_ == value) return;
    childHorizontalAlignment_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetChildVerticalAlignment(
    molga::text::TextVerticalAlignment value) {
    if (childVerticalAlignment_ == value) return;
    childVerticalAlignment_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetControlChildWidth(bool value) {
    if (controlChildWidth_ == value) return;
    controlChildWidth_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetControlChildHeight(bool value) {
    if (controlChildHeight_ == value) return;
    controlChildHeight_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetChildForceExpandWidth(bool value) {
    if (childForceExpandWidth_ == value) return;
    childForceExpandWidth_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetChildForceExpandHeight(bool value) {
    if (childForceExpandHeight_ == value) return;
    childForceExpandHeight_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetCellSize(float x, float y) {
    // 두 값 모두 먼저 검증한다. y가 거부됐을 때 x만 바뀐 상태가 남으면 안 된다.
    RequirePositive(x, "cellSizeX");
    RequirePositive(y, "cellSizeY");
    if (cellSizeX_ == x && cellSizeY_ == y) return;
    cellSizeX_ = x;
    cellSizeY_ = y;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetStartCorner(UIGridStartCorner value) {
    if (startCorner_ == value) return;
    startCorner_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetFillAxis(UIGridFillAxis value) {
    if (fillAxis_ == value) return;
    fillAxis_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetGridConstraint(UIGridConstraint value) {
    if (gridConstraint_ == value) return;
    gridConstraint_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::SetConstraintCount(std::uint32_t value) {
    // 0열/0행 격자는 나눗셈이 없는 배치 규칙조차 정의되지 않는다.
    if (value == 0) ThrowUILayoutInvalid("constraintCount must be positive");
    if (constraintCount_ == value) return;
    constraintCount_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutGroup::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
    j["mode"] = ToCanonicalString(mode_);
    j["paddingLeft"] = paddingLeft_;
    j["paddingRight"] = paddingRight_;
    j["paddingTop"] = paddingTop_;
    j["paddingBottom"] = paddingBottom_;
    j["spacingX"] = spacingX_;
    j["spacingY"] = spacingY_;
    j["childHorizontalAlignment"] = ToCanonicalString(childHorizontalAlignment_);
    j["childVerticalAlignment"] = ToCanonicalString(childVerticalAlignment_);
    j["controlChildWidth"] = controlChildWidth_;
    j["controlChildHeight"] = controlChildHeight_;
    j["childForceExpandWidth"] = childForceExpandWidth_;
    j["childForceExpandHeight"] = childForceExpandHeight_;
    j["cellSizeX"] = cellSizeX_;
    j["cellSizeY"] = cellSizeY_;
    j["startCorner"] = ToCanonicalString(startCorner_);
    j["fillAxis"] = ToCanonicalString(fillAxis_);
    j["gridConstraint"] = ToCanonicalString(gridConstraint_);
    j["constraintCount"] = constraintCount_;
}

void UILayoutGroup::Deserialize(const nlohmann::json& j) {
    // 검증된 setter만 통과한다. 필드를 직접 대입하면 디스크의 값이 검증을
    // 건너뛰고, 그 순간 파일이 setter로는 만들 수 없는 상태를 갖는다.
    //
    // 그리고 payload 전체를 먼저 읽는다. 읽으면서 저장하면 열 번째 키의 타입이
    // 어긋났을 때 앞의 아홉 개만 바뀐 반쪽 상태가 남는데, 이 함수는 실행 취소와
    // prefab override 적용에서 살아 있는 컴포넌트에 불린다.
    const auto mode =
        ReadCanonicalEnum(j, "mode", UILayoutMode::Horizontal, ParseUILayoutMode);
    const float paddingLeft = ReadUIFloat(j, "paddingLeft", 0.0f);
    const float paddingRight = ReadUIFloat(j, "paddingRight", 0.0f);
    const float paddingTop = ReadUIFloat(j, "paddingTop", 0.0f);
    const float paddingBottom = ReadUIFloat(j, "paddingBottom", 0.0f);
    const float spacingX = ReadUIFloat(j, "spacingX", 0.0f);
    const float spacingY = ReadUIFloat(j, "spacingY", 0.0f);
    const auto childHorizontal = ReadCanonicalEnum(
        j, "childHorizontalAlignment",
        molga::text::TextHorizontalAlignment::Left, ParseUIHorizontalAlignment);
    const auto childVertical = ReadCanonicalEnum(
        j, "childVerticalAlignment", molga::text::TextVerticalAlignment::Top,
        ParseUIVerticalAlignment);
    const bool controlWidth = ReadUIBool(j, "controlChildWidth", false);
    const bool controlHeight = ReadUIBool(j, "controlChildHeight", false);
    const bool expandWidth = ReadUIBool(j, "childForceExpandWidth", false);
    const bool expandHeight = ReadUIBool(j, "childForceExpandHeight", false);
    const float cellSizeX = ReadUIFloat(j, "cellSizeX", 100.0f);
    const float cellSizeY = ReadUIFloat(j, "cellSizeY", 100.0f);
    const auto startCorner = ReadCanonicalEnum(
        j, "startCorner", UIGridStartCorner::UpperLeft, ParseUIGridStartCorner);
    const auto fillAxis = ReadCanonicalEnum(
        j, "fillAxis", UIGridFillAxis::Horizontal, ParseUIGridFillAxis);
    const auto gridConstraint =
        ReadCanonicalEnum(j, "gridConstraint", UIGridConstraint::Flexible,
                          ParseUIGridConstraint);
    const std::uint32_t constraintCount = ReadUIUInt(j, "constraintCount", 1u);

    SetMode(mode);
    SetPadding(paddingLeft, paddingRight, paddingTop, paddingBottom);
    SetSpacingX(spacingX);
    SetSpacingY(spacingY);
    SetChildHorizontalAlignment(childHorizontal);
    SetChildVerticalAlignment(childVertical);
    SetControlChildWidth(controlWidth);
    SetControlChildHeight(controlHeight);
    SetChildForceExpandWidth(expandWidth);
    SetChildForceExpandHeight(expandHeight);
    SetCellSize(cellSizeX, cellSizeY);
    SetStartCorner(startCorner);
    SetFillAxis(fillAxis);
    SetGridConstraint(gridConstraint);
    SetConstraintCount(constraintCount);
}
