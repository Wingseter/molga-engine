#include "ECS/Components/UILayoutElement.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>

REGISTER_COMPONENT(UILayoutElement)

namespace {

// 저장 전에 축 하나를 정규형으로 만든다. 정규화를 setter 바깥에 두면 같은
// 저작 의도가 두 개의 서로 다른 저장 표현을 갖게 되고, 그 둘은 캐시에서 서로
// 다른 항목이 된다.
// 부호 있는 0은 마지막에 명시적으로 지운다. std::max(0.0f, -0.0f)가 첫 인자를
// 돌려주는 덕에 우연히 안전하지만, 그 동시값 규칙에 기대면 인자 순서를 바꾸는
// 무해해 보이는 편집 하나가 "-0.0"을 파일에 남긴다.
UIAxisConstraint Canonicalize(const UIAxisConstraint& value) {
    UIAxisConstraint canonical;
    canonical.minimum =
        std::max(0.0f, RequireFiniteUIValue(value.minimum, "axis minimum"));
    canonical.preferred = std::max(
        canonical.minimum,
        RequireFiniteUIValue(value.preferred, "axis preferred"));
    canonical.flexible =
        std::max(0.0f, RequireFiniteUIValue(value.flexible, "axis flexible"));
    canonical.minimum = NormalizeUISignedZero(canonical.minimum);
    canonical.preferred = NormalizeUISignedZero(canonical.preferred);
    canonical.flexible = NormalizeUISignedZero(canonical.flexible);
    return canonical;
}

bool Same(const UIAxisConstraint& lhs, const UIAxisConstraint& rhs) noexcept {
    return lhs.minimum == rhs.minimum && lhs.preferred == rhs.preferred &&
           lhs.flexible == rhs.flexible;
}

nlohmann::json AxisJson(const UIAxisConstraint& axis) {
    nlohmann::json out = nlohmann::json::object();
    out["minimum"] = axis.minimum;
    out["preferred"] = axis.preferred;
    out["flexible"] = axis.flexible;
    return out;
}

UIAxisConstraint ReadAxis(const nlohmann::json& j, const char* key) {
    const auto found = j.find(key);
    if (found == j.end()) return UIAxisConstraint{};
    if (!found->is_object()) {
        ThrowUILayoutInvalid(std::string(key) + " must be an axis object");
    }
    UIAxisConstraint axis;
    axis.minimum = ReadUIFloat(*found, "minimum", 0.0f);
    axis.preferred = ReadUIFloat(*found, "preferred", 0.0f);
    axis.flexible = ReadUIFloat(*found, "flexible", 0.0f);
    return axis;
}

} // namespace

void UILayoutElement::SetHorizontal(const UIAxisConstraint& value) {
    // 정규화가 먼저다. 거부되면 아무 필드도 바뀌지 않고 revision도 그대로다.
    const UIAxisConstraint canonical = Canonicalize(value);
    if (Same(horizontal_, canonical)) return;
    horizontal_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutElement::SetVertical(const UIAxisConstraint& value) {
    const UIAxisConstraint canonical = Canonicalize(value);
    if (Same(vertical_, canonical)) return;
    vertical_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutElement::SetIgnoreLayout(bool value) {
    if (ignoreLayout_ == value) return;
    ignoreLayout_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UILayoutElement::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
    j["horizontal"] = AxisJson(horizontal_);
    j["vertical"] = AxisJson(vertical_);
    j["ignoreLayout"] = ignoreLayout_;
}

void UILayoutElement::Deserialize(const nlohmann::json& j) {
    // Deserialize는 부분 갱신이 아니라 그 payload 상태로의 복원이다(실행 취소가
    // 기존 컴포넌트에 스냅샷을 다시 읽힌다). 키가 없으면 현재 값이 아니라
    // 문서화된 기본값으로 돌아간다.
    //
    // 그리고 먼저 전부 읽는다. 읽는 도중에 저장하면 뒤쪽 키가 거부됐을 때
    // 앞쪽만 바뀐 반쪽 상태가 살아 있는 컴포넌트에 남는다.
    const UIAxisConstraint horizontal = ReadAxis(j, "horizontal");
    const UIAxisConstraint vertical = ReadAxis(j, "vertical");
    const bool ignoreLayout = ReadUIBool(j, "ignoreLayout", false);

    SetHorizontal(horizontal);
    SetVertical(vertical);
    SetIgnoreLayout(ignoreLayout);
}
