#include "UI/UIRuntimeIdentity.h"

#include "Core/World.h"
#include "ECS/Component.h"
#include "ECS/GameObject.h"

namespace molga::ui {
namespace {

// 두 오버로드가 공유하는 유일한 네 필드 술어. const_cast 없이 하나로 묶을 수
// 있는 이유는 World::FindById가 const World에서도 GameObject*를 돌려주기
// 때문이다 — const 오버로드는 그 결과를 const로 좁혀서 반환한다.
// 해석은 렌더/입력 경로에서 레코드마다 돈다. 여기서 할당을 하면 noexcept
// 약속을 지키려고 모든 예외를 삼키는 catch가 필요해지고, 그 catch는 진짜
// 실패와 "대상 없음"을 구분하지 못한다. 그래서 할당하지 않는 조회만 쓴다.
Component* ResolveMatchingComponent(
    const World& world, const UIRuntimeTargetIdentity& identity) noexcept {
    // 1. 월드 세대. 교체된 World는 오브젝트 id를 그대로 재발급할 수 있다.
    if (identity.worldGeneration == 0 ||
        identity.worldGeneration != world.Generation()) {
        return nullptr;
    }
    // 2. 오브젝트 id.
    if (identity.objectId == 0) return nullptr;
    GameObject* owner = world.FindById(identity.objectId);
    if (!owner) return nullptr;
    // 3. 컴포넌트 런타임 타입 id.
    Component* candidate =
        owner->FindComponentByTypeId(identity.componentRuntimeTypeId);
    if (!candidate) return nullptr;
    // 4. 컴포넌트 인스턴스 id. 여기까지 와야 포인터가 나간다: 같은 타입을
    // 지웠다 다시 붙인 컴포넌트는 위 세 검사를 모두 통과한다.
    if (identity.componentInstanceId == 0 ||
        candidate->GetInstanceID() != identity.componentInstanceId) {
        return nullptr;
    }
    return candidate;
}

} // namespace

UIRuntimeTargetIdentity CaptureTarget(const World& world,
                                      const Component& component) {
    UIRuntimeTargetIdentity identity;
    const GameObject* owner = component.GetGameObject();
    if (!owner) return identity;
    // 소유자가 정말 이 world의 오브젝트인지 포인터로 확인한다. GameObject가
    // 들고 있는 World* 만 믿으면 아직 편입되지 않은(또는 이미 빠진) 오브젝트로
    // 식별자를 만들 수 있고, 그러면 캡처는 성공했는데 ResolveTarget이 쓰는
    // FindById로는 영영 해석되지 않는 식별자가 나온다.
    if (world.FindById(owner->GetID()) != owner) return identity;

    const std::uint64_t worldGeneration = world.Generation();
    const unsigned int objectId = owner->GetID();
    const std::uint64_t componentInstanceId = component.GetInstanceID();
    if (worldGeneration == 0 || objectId == 0 || componentInstanceId == 0) {
        return identity;
    }

    identity.worldGeneration = worldGeneration;
    identity.objectId = objectId;
    identity.componentRuntimeTypeId = component.GetRuntimeTypeID();
    identity.componentInstanceId = componentInstanceId;
    return identity;
}

Component* ResolveTarget(World& world,
                         const UIRuntimeTargetIdentity& identity) noexcept {
    return ResolveMatchingComponent(world, identity);
}

const Component* ResolveTarget(
    const World& world, const UIRuntimeTargetIdentity& identity) noexcept {
    return ResolveMatchingComponent(world, identity);
}

} // namespace molga::ui
