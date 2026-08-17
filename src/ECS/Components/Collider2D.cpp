#include "Collider2D.h"
#include <nlohmann/json.hpp>
#include <cmath>

void Collider2D::SerializeBase(nlohmann::json& j) const {
    j["offset"] = { offset.x, offset.y };
    j["isTrigger"] = isTrigger;
    j["friction"] = friction;
    j["restitution"] = restitution;
}

void Collider2D::DeserializeBase(const nlohmann::json& j) {
    if (j.contains("offset") && j["offset"].is_array()) {
        offset = Vector2(j["offset"][0], j["offset"][1]);
    }
    if (j.contains("isTrigger")) {
        isTrigger = j["isTrigger"];
    }
    if (j.contains("friction")) SetFriction(j["friction"].get<float>());
    if (j.contains("restitution")) SetRestitution(j["restitution"].get<float>());
}

AABB Collider2D::NormalizeBounds(AABB aabb) {
    if (aabb.width < 0.0f) {
        aabb.x += aabb.width;
        aabb.width = -aabb.width;
    }
    if (aabb.height < 0.0f) {
        aabb.y += aabb.height;
        aabb.height = -aabb.height;
    }
    return aabb;
}
