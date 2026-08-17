#include "Component.h"
#include <atomic>
#include <nlohmann/json.hpp>

namespace {

std::atomic<std::uint64_t> gNextComponentInstanceId{1};

std::uint64_t NextComponentInstanceId() {
    return gNextComponentInstanceId.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

Component::Component()
    : instanceId_(NextComponentInstanceId()) {}

Component::Component(const Component& other)
    : enabled(other.enabled),
      awoken(other.awoken),
      started(other.started),
      instanceId_(NextComponentInstanceId()) {}

Component::Component(Component&& other) noexcept
    : gameObject(other.gameObject),
      enabled(other.enabled),
      awoken(other.awoken),
      started(other.started),
      instanceId_(NextComponentInstanceId()) {
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
