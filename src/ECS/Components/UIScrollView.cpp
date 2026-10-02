#include "ECS/Components/UIScrollView.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

REGISTER_COMPONENT(UIScrollView)

namespace {

// 정규화 위치는 [0,1]을 벗어날 수 없다. 벗어난 값을 조용히 자르면 저작자가
// 의도한 위치와 파일에 남은 위치가 달라지고, 그 차이는 다음 저장에서 굳는다.
float RequireNormalized(float value, const char* what) {
    if (RequireFiniteUIValue(value, what) < 0.0f || value > 1.0f) {
        ThrowUILayoutInvalid(std::string(what) + " must lie in [0,1]");
    }
    return NormalizeUISignedZero(value);
}

float RequireNonNegative(float value, const char* what) {
    if (RequireFiniteUIValue(value, what) < 0.0f) {
        ThrowUILayoutInvalid(std::string(what) + " must not be negative");
    }
    return NormalizeUISignedZero(value);
}

} // namespace

void UIScrollView::SetViewport(SceneObjectRef value) {
    if (viewport_ == value) return;
    viewport_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetContent(SceneObjectRef value) {
    if (content_ == value) return;
    content_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetHorizontal(bool value) {
    if (horizontal_ == value) return;
    horizontal_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetVertical(bool value) {
    if (vertical_ == value) return;
    vertical_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetMovement(UIScrollMovement value) {
    if (value == UIScrollMovement::Elastic && !(elasticity_ > 0.0f)) {
        ThrowUILayoutInvalid(
            "elastic movement requires a positive elasticity");
    }
    if (movement_ == value) return;
    movement_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetElasticity(float value) {
    RequireFiniteUIValue(value, "elasticity");
    if (movement_ == UIScrollMovement::Elastic && !(value > 0.0f)) {
        ThrowUILayoutInvalid(
            "elastic movement requires a positive elasticity");
    }
    const float canonical = NormalizeUISignedZero(value);
    if (elasticity_ == canonical) return;
    elasticity_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetInertia(bool value) {
    if (inertia_ == value) return;
    inertia_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetDecelerationRate(float value) {
    const float canonical = RequireNonNegative(value, "decelerationRate");
    if (decelerationRate_ == canonical) return;
    decelerationRate_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetScrollSensitivity(float value) {
    const float canonical = RequireNonNegative(value, "scrollSensitivity");
    if (scrollSensitivity_ == canonical) return;
    scrollSensitivity_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetInitialNormalizedX(float value) {
    const float canonical = RequireNormalized(value, "initialNormalizedX");
    if (initialNormalizedX_ == canonical) return;
    initialNormalizedX_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::SetInitialNormalizedY(float value) {
    const float canonical = RequireNormalized(value, "initialNormalizedY");
    if (initialNormalizedY_ == canonical) return;
    initialNormalizedY_ = canonical;
    Invalidate(UIInvalidation::Layout);
}

void UIScrollView::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
    j["viewport"] = UIObjectRefJson(viewport_);
    j["content"] = UIObjectRefJson(content_);
    j["horizontal"] = horizontal_;
    j["vertical"] = vertical_;
    j["movement"] = ToCanonicalString(movement_);
    j["elasticity"] = elasticity_;
    j["inertia"] = inertia_;
    j["decelerationRate"] = decelerationRate_;
    j["scrollSensitivity"] = scrollSensitivity_;
    j["initialNormalizedX"] = initialNormalizedX_;
    j["initialNormalizedY"] = initialNormalizedY_;
}

void UIScrollView::Deserialize(const nlohmann::json& j) {
    // payload 전체를 먼저 읽는다. 읽으면서 저장하면 뒤쪽 키의 타입이 어긋났을
    // 때 앞쪽만 바뀐 반쪽 상태가 남는데, 이 함수는 실행 취소와 prefab override
    // 적용에서 살아 있는 컴포넌트에 불린다.
    const SceneObjectRef viewport = ReadUIObjectRef(j, "viewport");
    const SceneObjectRef content = ReadUIObjectRef(j, "content");
    const bool horizontal = ReadUIBool(j, "horizontal", true);
    const bool vertical = ReadUIBool(j, "vertical", true);
    const float elasticity = ReadUIFloat(j, "elasticity", 0.125f);
    const auto movement = ReadCanonicalEnum(j, "movement",
                                            UIScrollMovement::Clamped,
                                            ParseUIScrollMovement);
    const bool inertia = ReadUIBool(j, "inertia", true);
    const float decelerationRate = ReadUIFloat(j, "decelerationRate", 0.875f);
    const float scrollSensitivity = ReadUIFloat(j, "scrollSensitivity", 1.0f);
    const float initialX = ReadUIFloat(j, "initialNormalizedX", 0.0f);
    const float initialY = ReadUIFloat(j, "initialNormalizedY", 0.0f);

    // 짝 불변식(Elastic이면 elasticity > 0)은 두 값을 함께 봐야 판정된다.
    // payload 자체가 모순이면 아무것도 건드리기 전에 거부한다.
    if (movement == UIScrollMovement::Elastic && !(elasticity > 0.0f)) {
        ThrowUILayoutInvalid("elastic movement requires a positive elasticity");
    }

    SetViewport(viewport);
    SetContent(content);
    SetHorizontal(horizontal);
    SetVertical(vertical);
    // 두 setter 모두 "지금 값 + 새 값"으로 불변식을 보므로 순서가 중요하다.
    // 목표가 Clamped면 제약을 먼저 풀고, 목표가 Elastic이면 양의 elasticity를
    // 먼저 세운다. 어느 쪽이든 중간 상태가 불변식을 깨지 않으므로, 이전 값을
    // 지우기 위한 중간 대입이 필요 없다 — 그런 중간 대입은 아무것도 달라지지
    // 않은 재역직렬화에서도 revision과 집계 세대를 두 칸씩 태운다.
    if (movement == UIScrollMovement::Clamped) {
        SetMovement(movement);
        SetElasticity(elasticity);
    } else {
        SetElasticity(elasticity);
        SetMovement(movement);
    }
    SetInertia(inertia);
    SetDecelerationRate(decelerationRate);
    SetScrollSensitivity(scrollSensitivity);
    SetInitialNormalizedX(initialX);
    SetInitialNormalizedY(initialY);
}

void UIScrollView::RemapReferences(
    const std::unordered_map<unsigned int, unsigned int>& idRemap) {
    viewport_.Remap(idRemap);
    content_.Remap(idRemap);
}
