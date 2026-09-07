#include "ECS/Components/UIContentSizeFitter.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

REGISTER_COMPONENT(UIContentSizeFitter)

void UIContentSizeFitter::SetHorizontalFit(UIFitMode value) {
    if (horizontalFit_ == value) return;
    horizontalFit_ = value;
    // 맞춤 정책은 배치만 바꾼다. 색이나 상호작용 대상은 그대로다.
    Invalidate(UIInvalidation::Layout);
}

void UIContentSizeFitter::SetVerticalFit(UIFitMode value) {
    if (verticalFit_ == value) return;
    verticalFit_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UIContentSizeFitter::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
    j["horizontalFit"] = ToCanonicalString(horizontalFit_);
    j["verticalFit"] = ToCanonicalString(verticalFit_);
}

void UIContentSizeFitter::Deserialize(const nlohmann::json& j) {
    // 두 축을 먼저 읽는다. 두 번째 축이 거부됐을 때 첫 축만 바뀐 반쪽 상태가
    // 살아 있는 컴포넌트에 남으면 안 된다.
    const auto horizontal = ReadCanonicalEnum(
        j, "horizontalFit", UIFitMode::Unconstrained, ParseUIFitMode);
    const auto vertical = ReadCanonicalEnum(j, "verticalFit",
                                            UIFitMode::Unconstrained,
                                            ParseUIFitMode);
    SetHorizontalFit(horizontal);
    SetVerticalFit(vertical);
}
