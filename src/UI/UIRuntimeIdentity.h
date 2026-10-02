#pragma once

#include <cstddef>
#include <cstdint>

class Component;
class World;

namespace molga::ui {

// UI 런타임이 콜백/스냅샷 너머로 들고 다닐 수 있는 유일한 컴포넌트 참조.
// 날 포인터는 컴포넌트를 지우고 같은 타입을 다시 붙이면 같은 주소로 되살아나
// 엉뚱한 대상을 가리킨다. 네 필드가 전부 일치할 때만 같은 대상이다:
// 월드 세대가 월드 교체를, 컴포넌트 인스턴스 id가 컴포넌트 교체를 잡는다.
//
// worldGeneration과 componentInstanceId는 이 프로세스가 지금까지 몇 개를
// 만들었는지에 달린 할당 순번이다. 절대 직렬화하거나 정규 스냅샷 키에 넣지
// 말 것 — 같은 씬을 다시 열기만 해도 값이 달라진다. 런타임 테이블의 키로만
// 쓴다.
//
// 해시가 필요하면 필드별로 섞을 것. objectId가 64비트 멤버 사이에 끼어 있어
// 구조체에 패딩 바이트가 있고, 그 바이트는 값이 정해져 있지 않다. 오브젝트
// 표현을 통째로(예: 바이트 범위로) 해시하면 같은 식별자가 다른 해시를
// 낸다.
struct UIRuntimeTargetIdentity {
    std::uint64_t worldGeneration = 0;
    unsigned int objectId = 0;
    std::size_t componentRuntimeTypeId = 0;
    std::uint64_t componentInstanceId = 0;
    explicit operator bool() const noexcept {
        return worldGeneration != 0 && objectId != 0 &&
               componentInstanceId != 0;
    }
    friend bool operator==(const UIRuntimeTargetIdentity& lhs,
                           const UIRuntimeTargetIdentity& rhs) noexcept {
        return lhs.worldGeneration == rhs.worldGeneration &&
               lhs.objectId == rhs.objectId &&
               lhs.componentRuntimeTypeId == rhs.componentRuntimeTypeId &&
               lhs.componentInstanceId == rhs.componentInstanceId;
    }
    friend bool operator!=(const UIRuntimeTargetIdentity& lhs,
                           const UIRuntimeTargetIdentity& rhs) noexcept {
        return !(lhs == rhs);
    }
};

// 컴포넌트가 소유자를 갖고 그 소유자가 이 world에 속할 때만 네 필드를 채운
// 식별자를 돌려준다. 그렇지 않으면 빈 식별자(operator bool == false)다.
UIRuntimeTargetIdentity CaptureTarget(const World& world,
                                      const Component& component);

// 네 필드를 모두 통과할 때만 포인터를 돌려준다. 어느 하나라도 어긋나면
// nullptr — 마지막 인스턴스 id 검사 전에는 어떤 포인터도 새어 나가지 않는다.
Component* ResolveTarget(World&, const UIRuntimeTargetIdentity&) noexcept;
const Component* ResolveTarget(const World&,
                               const UIRuntimeTargetIdentity&) noexcept;

} // namespace molga::ui
