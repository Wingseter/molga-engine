#include "Component.h"
#include <nlohmann/json.hpp>

// Default implementations for serialization
void Component::Serialize(nlohmann::json& j) const {
    // Default: do nothing
}

void Component::Deserialize(const nlohmann::json& j) {
    // Default: do nothing
}
