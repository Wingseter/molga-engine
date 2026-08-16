#include "ECS/Components/ShadowOccluder2D.h"

#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <utility>

REGISTER_COMPONENT(ShadowOccluder2D)

namespace {

constexpr float kGeometryEpsilon = 1.0e-5f;
constexpr float kPi = 3.14159265358979323846f;

bool IsFinite(const Vector2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float SignedAreaTwice(const std::vector<Vector2>& vertices) noexcept {
    double area = 0.0;
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const Vector2& current = vertices[index];
        const Vector2& next = vertices[(index + 1U) % vertices.size()];
        area += static_cast<double>(current.x) * next.y -
                static_cast<double>(current.y) * next.x;
    }
    return static_cast<float>(area);
}

std::vector<Vector2> DefaultPolygon() {
    return {
        {-50.0f, -50.0f},
        { 50.0f, -50.0f},
        { 50.0f,  50.0f},
        {-50.0f,  50.0f},
    };
}

bool TryReadVector2(const nlohmann::json& value, Vector2& result) {
    if (!value.is_array() || value.size() < 2U ||
        !value[0].is_number() || !value[1].is_number()) {
        return false;
    }
    try {
        const double x = value[0].get<double>();
        const double y = value[1].get<double>();
        if (!std::isfinite(x) || !std::isfinite(y) ||
            x < -std::numeric_limits<float>::max() ||
            x > std::numeric_limits<float>::max() ||
            y < -std::numeric_limits<float>::max() ||
            y > std::numeric_limits<float>::max()) {
            return false;
        }
        result = {static_cast<float>(x), static_cast<float>(y)};
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

ShadowOccluder2D::ShadowOccluder2D()
    : vertices_(DefaultPolygon()) {}

bool ShadowOccluder2D::SetShape(ShadowOccluderShape2D shape) noexcept {
    if (shape != ShadowOccluderShape2D::Box &&
        shape != ShadowOccluderShape2D::Polygon) {
        return false;
    }
    if (shape == ShadowOccluderShape2D::Polygon) {
        std::vector<Vector2> normalized;
        if (!ValidateAndNormalizePolygon(vertices_, normalized)) return false;
        vertices_ = std::move(normalized);
    } else if (!IsFinite(offset_) || !IsFinite(size_) ||
               size_.x <= kGeometryEpsilon || size_.y <= kGeometryEpsilon) {
        return false;
    }
    shape_ = shape;
    shapeValid_ = true;
    return true;
}

bool ShadowOccluder2D::SetBox(const Vector2& offset,
                              const Vector2& size) noexcept {
    if (!IsFinite(offset) || !IsFinite(size) ||
        size.x <= kGeometryEpsilon || size.y <= kGeometryEpsilon) {
        return false;
    }
    shape_ = ShadowOccluderShape2D::Box;
    offset_ = offset;
    size_ = size;
    shapeValid_ = true;
    return true;
}

bool ShadowOccluder2D::SetPolygon(
    const std::vector<Vector2>& vertices) noexcept {
    std::vector<Vector2> normalized;
    if (!ValidateAndNormalizePolygon(vertices, normalized)) return false;
    shape_ = ShadowOccluderShape2D::Polygon;
    vertices_ = std::move(normalized);
    shapeValid_ = true;
    return true;
}

void ShadowOccluder2D::ResetToDefaultBox() noexcept {
    shape_ = ShadowOccluderShape2D::Box;
    offset_ = Vector2::Zero();
    size_ = {100.0f, 100.0f};
    vertices_ = DefaultPolygon();
    shapeValid_ = true;
}

bool ShadowOccluder2D::ValidateAndNormalizePolygon(
    const std::vector<Vector2>& input,
    std::vector<Vector2>& normalized) noexcept {
    normalized.clear();
    if (input.size() < MinPolygonVertices ||
        input.size() > MaxPolygonVertices) {
        return false;
    }
    if (std::any_of(input.begin(), input.end(),
                    [](const Vector2& value) { return !IsFinite(value); })) {
        return false;
    }

    const float area = SignedAreaTwice(input);
    if (!std::isfinite(area) || std::abs(area) <= kGeometryEpsilon) {
        return false;
    }
    normalized = input;
    if (area < 0.0f) std::reverse(normalized.begin(), normalized.end());

    // Consecutive turns alone are insufficient: a self-intersecting star can
    // keep the same turn sign. A strictly convex CCW polygon has every
    // non-edge vertex strictly on the interior (left) side of every edge.
    for (std::size_t edgeIndex = 0;
         edgeIndex < normalized.size(); ++edgeIndex) {
        const std::size_t nextIndex =
            (edgeIndex + 1U) % normalized.size();
        const Vector2 edge =
            normalized[nextIndex] - normalized[edgeIndex];
        const float edgeLength = edge.Length();
        if (!std::isfinite(edgeLength) ||
            edgeLength <= kGeometryEpsilon) {
            return false;
        }
        const float tolerance =
            kGeometryEpsilon * std::max(1.0f, edgeLength);
        for (std::size_t vertexIndex = 0;
             vertexIndex < normalized.size(); ++vertexIndex) {
            if (vertexIndex == edgeIndex || vertexIndex == nextIndex) {
                continue;
            }
            const float side = edge.Cross(
                normalized[vertexIndex] - normalized[edgeIndex]);
            if (!std::isfinite(side) || side <= tolerance) {
                return false;
            }
        }
    }
    return true;
}

std::vector<Vector2> ShadowOccluder2D::GetWorldVertices() const {
    if (!shapeValid_ || !gameObject) return {};
    const Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return {};

    const Vector2 worldPosition = transform->GetWorldPosition();
    const Vector2 worldScale = transform->GetWorldScale();
    const float worldRotation = transform->GetWorldRotation();
    if (!IsFinite(worldPosition) || !IsFinite(worldScale) ||
        !std::isfinite(worldRotation) ||
        std::abs(worldScale.x) <= kGeometryEpsilon ||
        std::abs(worldScale.y) <= kGeometryEpsilon) {
        return {};
    }

    std::vector<Vector2> local;
    if (shape_ == ShadowOccluderShape2D::Box) {
        const Vector2 half = size_ * 0.5f;
        local = {
            {offset_.x - half.x, offset_.y - half.y},
            {offset_.x + half.x, offset_.y - half.y},
            {offset_.x + half.x, offset_.y + half.y},
            {offset_.x - half.x, offset_.y + half.y},
        };
    } else {
        local = vertices_;
    }

    const float radians = worldRotation * kPi / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    std::vector<Vector2> world;
    world.reserve(local.size());
    for (const Vector2& point : local) {
        const float x = point.x * worldScale.x;
        const float y = point.y * worldScale.y;
        world.push_back({
            x * cosine - y * sine + worldPosition.x,
            x * sine + y * cosine + worldPosition.y,
        });
    }
    if (SignedAreaTwice(world) < 0.0f) {
        std::reverse(world.begin(), world.end());
    }
    return world;
}

void ShadowOccluder2D::Serialize(nlohmann::json& json) const {
    json["shape"] =
        shape_ == ShadowOccluderShape2D::Polygon ? "Polygon" : "Box";
    json["offset"] = {offset_.x, offset_.y};
    json["size"] = {size_.x, size_.y};
    json["vertices"] = nlohmann::json::array();
    for (const Vector2& vertex : vertices_) {
        json["vertices"].push_back({vertex.x, vertex.y});
    }
}

void ShadowOccluder2D::Deserialize(const nlohmann::json& json) {
    bool wantsPolygon = false;
    bool shapeValid = true;
    if (const auto shape = json.find("shape"); shape != json.end()) {
        if (!shape->is_string()) {
            shapeValid = false;
        } else {
            const std::string value = shape->get<std::string>();
            wantsPolygon = value == "Polygon";
            shapeValid = value == "Box" || wantsPolygon;
        }
    }

    Vector2 parsedOffset = offset_;
    Vector2 parsedSize = size_;
    bool offsetValid = true;
    bool sizeValid = true;
    if (const auto offset = json.find("offset"); offset != json.end()) {
        offsetValid = TryReadVector2(*offset, parsedOffset);
    }
    if (const auto size = json.find("size"); size != json.end()) {
        sizeValid = TryReadVector2(*size, parsedSize) &&
                    parsedSize.x > kGeometryEpsilon &&
                    parsedSize.y > kGeometryEpsilon;
    }

    if (!wantsPolygon) {
        shape_ = ShadowOccluderShape2D::Box;
        if (offsetValid) offset_ = parsedOffset;
        if (sizeValid) size_ = parsedSize;
        shapeValid_ = shapeValid && offsetValid && sizeValid;
        return;
    }

    shape_ = ShadowOccluderShape2D::Polygon;
    if (offsetValid) offset_ = parsedOffset;
    if (sizeValid) size_ = parsedSize;
    std::vector<Vector2> parsed;
    bool parsedAll = true;
    const auto serialized = json.find("vertices");
    if (serialized == json.end() || !serialized->is_array()) {
        parsedAll = false;
    } else {
        parsed.reserve(serialized->size());
        for (const auto& value : *serialized) {
            Vector2 vertex;
            if (!TryReadVector2(value, vertex)) {
                parsedAll = false;
                break;
            }
            parsed.push_back(vertex);
        }
    }

    std::vector<Vector2> normalized;
    if (parsedAll && ValidateAndNormalizePolygon(parsed, normalized)) {
        vertices_ = std::move(normalized);
        // Box-only offset/size fields are serialized for round-tripping but
        // do not participate in the Polygon validity contract.
        shapeValid_ = shapeValid;
    } else {
        // Keep finite authored vertices so the Inspector can display/recover
        // the damaged polygon, but exclude it from lighting snapshots.
        vertices_ = std::move(parsed);
        shapeValid_ = false;
    }
}
