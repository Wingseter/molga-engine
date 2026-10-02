#include "ECS/Components/UISelectable.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

REGISTER_COMPONENT(UISelectable)

void UISelectable::SetInteractable(bool value) {
    if (interactable_ == value) return;
    interactable_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UISelectable::SetNavigationMode(UINavigationMode value) {
    if (navigationMode_ == value) return;
    navigationMode_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UISelectable::SetNavigateUp(SceneObjectRef value) {
    if (navigateUp_ == value) return;
    navigateUp_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UISelectable::SetNavigateDown(SceneObjectRef value) {
    if (navigateDown_ == value) return;
    navigateDown_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UISelectable::SetNavigateLeft(SceneObjectRef value) {
    if (navigateLeft_ == value) return;
    navigateLeft_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UISelectable::SetNavigateRight(SceneObjectRef value) {
    if (navigateRight_ == value) return;
    navigateRight_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UISelectable::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
    j["interactable"] = interactable_;
    j["navigationMode"] = ToCanonicalString(navigationMode_);
    j["navigateUp"] = UIObjectRefJson(navigateUp_);
    j["navigateDown"] = UIObjectRefJson(navigateDown_);
    j["navigateLeft"] = UIObjectRefJson(navigateLeft_);
    j["navigateRight"] = UIObjectRefJson(navigateRight_);
}

void UISelectable::Deserialize(const nlohmann::json& j) {
    // payload 전체를 먼저 읽는다. 세 번째 참조에서 거부됐을 때 앞의 둘만 바뀐
    // 반쪽 상태가 살아 있는 컴포넌트에 남으면 안 된다.
    const bool interactable = ReadUIBool(j, "interactable", true);
    const auto navigationMode = ReadCanonicalEnum(
        j, "navigationMode", UINavigationMode::Auto, ParseUINavigationMode);
    const SceneObjectRef up = ReadUIObjectRef(j, "navigateUp");
    const SceneObjectRef down = ReadUIObjectRef(j, "navigateDown");
    const SceneObjectRef left = ReadUIObjectRef(j, "navigateLeft");
    const SceneObjectRef right = ReadUIObjectRef(j, "navigateRight");

    SetInteractable(interactable);
    SetNavigationMode(navigationMode);
    SetNavigateUp(up);
    SetNavigateDown(down);
    SetNavigateLeft(left);
    SetNavigateRight(right);
}

void UISelectable::RemapReferences(
    const std::unordered_map<unsigned int, unsigned int>& idRemap) {
    // 네 방향 전부다. 하나라도 빠지면 복제된 서브트리의 포커스 이동이 원본
    // 오브젝트로 튀고, 그 원본이 사라지면 아무 데도 가지 않는다.
    navigateUp_.Remap(idRemap);
    navigateDown_.Remap(idRemap);
    navigateLeft_.Remap(idRemap);
    navigateRight_.Remap(idRemap);
}
