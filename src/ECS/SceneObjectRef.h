#pragma once
#include <unordered_map>

class GameObject;
class World;

// 살아있는 씬 GameObject 참조. 안정적인 id로 직렬화되고, 런타임에는 소유
// World를 통해 포인터로 해석된다. Instantiate/Prefab 복제 시 같은 서브트리
// 내부 참조는 Remap()으로 새 id에 맞춰진다.
//
// 저장소는 targetId 하나뿐이다. objectId 같은 두 번째 멤버를 두면 직렬화가
// 갱신한 쪽과 런타임이 읽는 쪽이 갈라질 수 있으므로, ObjectId()는 접근자일
// 뿐 별도의 데이터가 아니다.
struct SceneObjectRef {
    unsigned int targetId = 0;

    bool IsSet() const noexcept { return targetId != 0; }
    void Clear() noexcept { targetId = 0; }
    unsigned int ObjectId() const noexcept { return targetId; }
    GameObject* Resolve(World&) const noexcept;
    const GameObject* Resolve(const World&) const noexcept;
    void Remap(const std::unordered_map<unsigned int, unsigned int>& ids) {
        const auto found = ids.find(targetId);
        if (found != ids.end()) targetId = found->second;
    }
    friend bool operator==(SceneObjectRef lhs, SceneObjectRef rhs) {
        return lhs.targetId == rhs.targetId;
    }
    friend bool operator!=(SceneObjectRef lhs, SceneObjectRef rhs) {
        return !(lhs == rhs);
    }
};

using ObjectRef = SceneObjectRef;
