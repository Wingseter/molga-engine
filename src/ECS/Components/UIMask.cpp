#include "ECS/Components/UIMask.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

REGISTER_COMPONENT(UIMask)

void UIMask::SetClipsDescendants(bool value) {
    if (clipsDescendants_ == value) return;
    clipsDescendants_ = value;
    // 두 축을 따로 올린다. Invalidate는 이유를 하나씩 받으므로(설계가 정한
    // 서명), 잘림이 배치와 히트 양쪽을 바꾼다는 사실은 호출 두 번으로 적는다.
    Invalidate(UIInvalidation::Layout);
    Invalidate(UIInvalidation::Interaction);
}

void UIMask::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
    j["clipsDescendants"] = clipsDescendants_;
}

void UIMask::Deserialize(const nlohmann::json& j) {
    SetClipsDescendants(ReadUIBool(j, "clipsDescendants", true));
}
