#include "ECS/SceneObjectRef.h"

#include "Core/World.h"

// 두 오버로드는 한 번역 단위에 함께 산다. 헤더에 인라인으로 두면 const 정의를
// 빠뜨려도 그 오버로드를 부르지 않는 타깃은 여전히 링크되므로, 누락이 링크
// 실패로 드러나지 않는다.

GameObject* SceneObjectRef::Resolve(World& world) const noexcept {
    return IsSet() ? world.FindById(targetId) : nullptr;
}

const GameObject* SceneObjectRef::Resolve(
    const World& world) const noexcept {
    return IsSet() ? world.FindById(targetId) : nullptr;
}
