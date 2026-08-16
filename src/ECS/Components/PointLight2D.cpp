#include "ECS/Components/PointLight2D.h"

#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

REGISTER_COMPONENT(PointLight2D)

namespace {

bool IsFinite(const Color& color) noexcept {
    return std::isfinite(color.r) && std::isfinite(color.g) &&
           std::isfinite(color.b) && std::isfinite(color.a);
}

template <typename T>
bool TryReadNumber(const nlohmann::json& json, const char* key, T& out) {
    const auto found = json.find(key);
    if (found == json.end() || !found->is_number()) return false;
    try {
        const double value = found->get<double>();
        if (!std::isfinite(value) ||
            value < static_cast<double>(std::numeric_limits<T>::lowest()) ||
            value > static_cast<double>(std::numeric_limits<T>::max())) {
            return false;
        }
        out = static_cast<T>(value);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

bool PointLight2D::SetColor(const Color& color) noexcept {
    if (!IsFinite(color)) return false;
    color_ = color;
    return true;
}

bool PointLight2D::SetIntensity(float intensity) noexcept {
    if (!std::isfinite(intensity)) return false;
    intensity_ = std::clamp(intensity, MinIntensity, MaxIntensity);
    return true;
}

bool PointLight2D::SetRadius(float radius) noexcept {
    if (!std::isfinite(radius)) return false;
    radius_ = std::clamp(radius, MinRadius, MaxRadius);
    return true;
}

bool PointLight2D::SetHeight(float height) noexcept {
    if (!std::isfinite(height)) return false;
    height_ = height;
    return true;
}

bool PointLight2D::SetFalloff(float falloff) noexcept {
    if (!std::isfinite(falloff)) return false;
    falloff_ = std::clamp(falloff, MinFalloff, MaxFalloff);
    return true;
}

Vector2 PointLight2D::GetWorldPosition() const noexcept {
    if (!gameObject) return Vector2::Zero();
    const Transform* transform = gameObject->GetComponent<Transform>();
    return transform ? transform->GetWorldPosition() : Vector2::Zero();
}

void PointLight2D::Serialize(nlohmann::json& json) const {
    json["color"] = {color_.r, color_.g, color_.b, color_.a};
    json["intensity"] = intensity_;
    json["radius"] = radius_;
    json["height"] = height_;
    json["falloff"] = falloff_;
    json["affectMask"] = affectMask_;
    json["castsShadows"] = castsShadows_;
    json["priority"] = priority_;
}

void PointLight2D::Deserialize(const nlohmann::json& json) {
    const auto color = json.find("color");
    if (color != json.end() && color->is_array() && color->size() >= 4U) {
        try {
            SetColor({(*color)[0].get<float>(), (*color)[1].get<float>(),
                      (*color)[2].get<float>(), (*color)[3].get<float>()});
        } catch (...) {
            // Keep the previous valid color.
        }
    }

    float number = 0.0f;
    if (TryReadNumber(json, "intensity", number)) SetIntensity(number);
    if (TryReadNumber(json, "radius", number)) SetRadius(number);
    if (TryReadNumber(json, "height", number)) SetHeight(number);
    if (TryReadNumber(json, "falloff", number)) SetFalloff(number);

    const auto mask = json.find("affectMask");
    if (mask != json.end()) {
        try {
            if (mask->is_number_unsigned()) {
                const auto raw = mask->get<unsigned long long>();
                if (raw <= std::numeric_limits<std::uint32_t>::max()) {
                    affectMask_ = static_cast<std::uint32_t>(raw);
                }
            } else if (mask->is_number_integer()) {
                const auto raw = mask->get<long long>();
                if (raw >= 0 && static_cast<unsigned long long>(raw) <=
                                    std::numeric_limits<std::uint32_t>::max()) {
                    affectMask_ = static_cast<std::uint32_t>(raw);
                }
            }
        } catch (...) {
            // Keep the previous valid mask.
        }
    }
    if (const auto casts = json.find("castsShadows");
        casts != json.end() && casts->is_boolean()) {
        castsShadows_ = casts->get<bool>();
    }
    if (const auto priority = json.find("priority");
        priority != json.end() && priority->is_number_integer()) {
        try {
            const auto raw = priority->get<long long>();
            if (raw >= std::numeric_limits<int>::min() &&
                raw <= std::numeric_limits<int>::max()) {
                priority_ = static_cast<int>(raw);
            }
        } catch (...) {
            // Keep the previous valid priority.
        }
    }
}
