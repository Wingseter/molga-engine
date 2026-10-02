#include "Component.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace {

std::atomic<std::uint64_t> gNextComponentInstanceId{1};

std::uint64_t AcquireComponentInstanceId() {
    auto candidate =
        gNextComponentInstanceId.load(std::memory_order_relaxed);
    for (;;) {
        // 증가 전에 소진을 확인한다. fetch_add였다면 UINT64_MAX 다음이 0으로
        // 감기고, 0은 "인스턴스 없음"이라서 죽은 컴포넌트 식별자가 되살아난다.
        if (candidate == 0 || candidate == UINT64_MAX) {
            throw std::overflow_error("component instance ID exhausted");
        }
        if (gNextComponentInstanceId.compare_exchange_weak(
                candidate, candidate + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return candidate;
        }
    }
}

} // namespace

std::uint64_t ComponentInstanceIdForTesting() {
    return gNextComponentInstanceId.load(std::memory_order_relaxed);
}

ScopedComponentInstanceIdForTesting::ScopedComponentInstanceIdForTesting(
    std::uint64_t next)
    : previous_(gNextComponentInstanceId.exchange(
          next, std::memory_order_relaxed)),
      seeded_(next) {}

ScopedComponentInstanceIdForTesting::~ScopedComponentInstanceIdForTesting() {
    const std::uint64_t current =
        gNextComponentInstanceId.load(std::memory_order_relaxed);
    // 훅 범위에서 아무것도 발급되지 않았다면(소진 경계값 0/UINT64_MAX가 그렇다)
    // 직전 후보를 그대로 돌려놓는다. 반대로 무언가 발급됐다면 그 id들은 살아
    // 있을 수 있으므로 시퀀스를 뒤로 되감지 않는다 — 되감으면 프로덕션이 같은
    // id를 한 번 더 발급해 "재사용 없음"이 훅 하나로 깨진다.
    //
    // 남는 구멍 하나: 훅을 현재 후보보다 낮게 세우고 그 안에서 발급하면 이미
    // 살아 있는 컴포넌트와 겹치는 id가 그 자리에서 나온다. 그건 복원으로 막을
    // 수 없으니 세우지 말 것.
    gNextComponentInstanceId.store(
        current == seeded_ ? previous_ : std::max(previous_, current),
        std::memory_order_relaxed);
}

Component::Component()
    : instanceId_(AcquireComponentInstanceId()) {}

Component::Component(const Component& other)
    : enabled(other.enabled),
      awoken(other.awoken),
      started(other.started),
      instanceId_(AcquireComponentInstanceId()) {}

Component::Component(Component&& other)
    : gameObject(other.gameObject),
      enabled(other.enabled),
      awoken(other.awoken),
      started(other.started),
      instanceId_(AcquireComponentInstanceId()) {
    // 원본을 관찰 가능하게 바꾸는 유일한 문장이 본문에 있다. 위 할당이 던지면
    // 이 줄은 실행되지 않으므로 실패한 이동은 원본의 소유자를 훔치지 않고,
    // 목적지는 애초에 생성되지 않아 componentMap에 올라갈 수도 없다.
    other.gameObject = nullptr;
}

Component& Component::operator=(const Component& other) {
    if (this == &other) return *this;
    // Ownership and runtime identity belong to the destination instance.
    enabled = other.enabled;
    awoken = other.awoken;
    started = other.started;
    return *this;
}

Component& Component::operator=(Component&& other) noexcept {
    if (this == &other) return *this;
    // Ownership and runtime identity belong to the destination instance.
    enabled = other.enabled;
    awoken = other.awoken;
    started = other.started;
    return *this;
}

// Default implementations for serialization
void Component::Serialize(nlohmann::json& j) const {
    // Default: do nothing
}

void Component::Deserialize(const nlohmann::json& j) {
    // Default: do nothing
}
