#include "Rendering/LightingFrame2D.h"

#include "Common/Constants.h"
#include "ECS/Component.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/PointLight2D.h"
#include "ECS/Components/ShadowOccluder2D.h"
#include "ECS/GameObject.h"
#include "Rendering/Camera2D.h"
#include "Rendering/WorldRenderTraversal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace molga {
namespace {

bool IsFinite(float value) noexcept {
    return std::isfinite(value);
}

bool IsFinite(const Vector2& value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y);
}

bool IsFinite(const Color& value) noexcept {
    return IsFinite(value.r) && IsFinite(value.g) &&
           IsFinite(value.b) && IsFinite(value.a);
}

float Cross(const Vector2& lhs, const Vector2& rhs) noexcept {
    return lhs.x * rhs.y - lhs.y * rhs.x;
}

float Cross(const Vector2& origin, const Vector2& a,
            const Vector2& b) noexcept {
    return Cross(a - origin, b - origin);
}

float SignedTwiceArea(const std::vector<Vector2>& vertices) noexcept {
    float area = 0.0f;
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        area += Cross(vertices[index], vertices[(index + 1) % vertices.size()]);
    }
    return area;
}

AABB BoundsOf(const std::vector<Vector2>& vertices) noexcept {
    if (vertices.empty()) return {};
    float minimumX = vertices.front().x;
    float maximumX = vertices.front().x;
    float minimumY = vertices.front().y;
    float maximumY = vertices.front().y;
    for (const Vector2& vertex : vertices) {
        minimumX = std::min(minimumX, vertex.x);
        maximumX = std::max(maximumX, vertex.x);
        minimumY = std::min(minimumY, vertex.y);
        maximumY = std::max(maximumY, vertex.y);
    }
    return {minimumX, minimumY, maximumX - minimumX, maximumY - minimumY};
}

float DistanceSquared(const Vector2& lhs, const Vector2& rhs) noexcept {
    const Vector2 delta = lhs - rhs;
    return delta.LengthSquared();
}

float DistanceToSegmentSquared(const Vector2& point, const Vector2& start,
                               const Vector2& end) noexcept {
    const Vector2 segment = end - start;
    const float lengthSquared = segment.LengthSquared();
    if (lengthSquared <= std::numeric_limits<float>::epsilon()) {
        return DistanceSquared(point, start);
    }
    const float t = std::clamp(
        (point - start).Dot(segment) / lengthSquared, 0.0f, 1.0f);
    return DistanceSquared(point, start + segment * t);
}

bool CircleIntersectsBounds(const Vector2& center, float radius,
                            const AABB& bounds, float epsilon) noexcept {
    const float closestX = std::clamp(center.x, bounds.Left(), bounds.Right());
    const float closestY = std::clamp(center.y, bounds.Top(), bounds.Bottom());
    const float expandedRadius = radius + epsilon;
    return DistanceSquared(center, {closestX, closestY}) <=
           expandedRadius * expandedRadius;
}

bool CircleIntersectsCameraView(const Vector2& center, float radius,
                                const LightingCameraSnapshot2D& camera) noexcept {
    if (!IsFinite(center) || !IsFinite(radius) || radius <= 0.0f) return false;
    const float radians = camera.rotation * Constants::DEG_TO_RAD;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const Vector2 delta = center - camera.center;
    const float localX = cosine * delta.x - sine * delta.y;
    const float localY = sine * delta.x + cosine * delta.y;
    const float distanceX =
        std::max(std::abs(localX) - camera.halfExtents.x, 0.0f);
    const float distanceY =
        std::max(std::abs(localY) - camera.halfExtents.y, 0.0f);
    return distanceX * distanceX + distanceY * distanceY <= radius * radius;
}

AABB CameraViewBounds(const LightingCameraSnapshot2D& camera) noexcept {
    const float radians = camera.rotation * Constants::DEG_TO_RAD;
    const float cosine = std::abs(std::cos(radians));
    const float sine = std::abs(std::sin(radians));
    const float extentX =
        cosine * camera.halfExtents.x + sine * camera.halfExtents.y;
    const float extentY =
        sine * camera.halfExtents.x + cosine * camera.halfExtents.y;
    return {camera.center.x - extentX, camera.center.y - extentY,
            extentX * 2.0f, extentY * 2.0f};
}

} // namespace

bool NormalizeConvexPolygon2D(std::vector<Vector2>& vertices,
                              float epsilon) noexcept {
    epsilon = std::max(epsilon, 0.0f);
    if (vertices.size() < 3 ||
        vertices.size() > kMaxShadowOccluderVertices2D) {
        return false;
    }
    if (!std::all_of(vertices.begin(), vertices.end(),
                     [](const Vector2& vertex) { return IsFinite(vertex); })) {
        return false;
    }

    const float signedArea = SignedTwiceArea(vertices);
    if (!IsFinite(signedArea) || std::abs(signedArea) <= epsilon) return false;
    if (signedArea < 0.0f) std::reverse(vertices.begin(), vertices.end());

    for (std::size_t index = 0; index < vertices.size(); ++index) {
        const std::size_t nextIndex = (index + 1U) % vertices.size();
        const Vector2& a = vertices[index];
        const Vector2& b = vertices[nextIndex];
        const float edgeLength = (b - a).Length();
        if (!IsFinite(edgeLength) || edgeLength <= epsilon) return false;
        const float tolerance = epsilon * std::max(1.0f, edgeLength);
        for (std::size_t vertexIndex = 0;
             vertexIndex < vertices.size(); ++vertexIndex) {
            if (vertexIndex == index || vertexIndex == nextIndex) continue;
            const float side = Cross(a, b, vertices[vertexIndex]);
            if (!IsFinite(side) || side <= tolerance) return false;
        }
    }
    return true;
}

bool PointInsideOrOnConvexPolygon2D(
    const Vector2& point,
    const std::vector<Vector2>& ccwVertices,
    float epsilon) noexcept {
    epsilon = std::max(epsilon, 0.0f);
    if (!IsFinite(point) || ccwVertices.size() < 3) return false;
    for (std::size_t index = 0; index < ccwVertices.size(); ++index) {
        const Vector2& start = ccwVertices[index];
        const Vector2& end = ccwVertices[(index + 1) % ccwVertices.size()];
        if (!IsFinite(start) || !IsFinite(end)) return false;
        const float edgeLength = (end - start).Length();
        if (!IsFinite(edgeLength) || edgeLength <= epsilon) return false;
        const float tolerance = epsilon * edgeLength;
        if (Cross(start, end, point) < -tolerance) return false;
    }
    return true;
}

bool CircleIntersectsConvexPolygon2D(
    const Vector2& center,
    float radius,
    const std::vector<Vector2>& ccwVertices,
    float epsilon) noexcept {
    epsilon = std::max(epsilon, 0.0f);
    if (!IsFinite(center) || !IsFinite(radius) || radius < 0.0f ||
        ccwVertices.size() < 3) {
        return false;
    }
    if (PointInsideOrOnConvexPolygon2D(center, ccwVertices, epsilon)) {
        return true;
    }
    const float expandedRadius = radius + epsilon;
    const float radiusSquared = expandedRadius * expandedRadius;
    for (std::size_t index = 0; index < ccwVertices.size(); ++index) {
        const Vector2& start = ccwVertices[index];
        const Vector2& end = ccwVertices[(index + 1) % ccwVertices.size()];
        if (!IsFinite(start) || !IsFinite(end)) return false;
        if (DistanceToSegmentSquared(center, start, end) <= radiusSquared) {
            return true;
        }
    }
    return false;
}

ShadowCasterGeometry2D BuildShadowCasterGeometry2D(
    const LightingOccluderSnapshot2D& occluder,
    const Vector2& lightPosition,
    float lightRadius,
    float epsilon) {
    ShadowCasterGeometry2D result;
    result.occluderInstanceId = occluder.componentInstanceId;
    result.objectId = occluder.objectId;
    result.sceneOrder = occluder.sceneOrder;
    epsilon = std::max(epsilon, 0.0f);

    std::vector<Vector2> polygon = occluder.vertices;
    if (!IsFinite(lightPosition) || !IsFinite(lightRadius) ||
        lightRadius <= 0.0f ||
        !NormalizeConvexPolygon2D(polygon, epsilon)) {
        return result;
    }
    if (PointInsideOrOnConvexPolygon2D(lightPosition, polygon, epsilon)) {
        result.fullCover = true;
        return result;
    }

    result.vertices = polygon;
    for (std::size_t index = 1; index + 1 < polygon.size(); ++index) {
        result.indices.push_back(0);
        result.indices.push_back(static_cast<std::uint32_t>(index));
        result.indices.push_back(static_cast<std::uint32_t>(index + 1));
    }

    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Vector2& start = polygon[index];
        const Vector2& end = polygon[(index + 1) % polygon.size()];
        const float edgeLength = (end - start).Length();
        const float tolerance = epsilon * edgeLength;
        // For a mathematical CCW convex polygon, the light lies on the
        // non-interior side of precisely the silhouette-facing edge chain.
        if (Cross(start, end, lightPosition) > tolerance) continue;

        // Scale the complete edge about the light by one common factor. If
        // endpoints are merely normalized to the radius, their connecting
        // chord lies inside the light circle and leaves the sector behind it
        // incorrectly unshadowed.
        const float edgeDistance = std::sqrt(
            DistanceToSegmentSquared(lightPosition, start, end));
        if (!IsFinite(edgeDistance) || edgeDistance <= epsilon) {
            result.fullCover = true;
            result.vertices.clear();
            result.indices.clear();
            return result;
        }
        const float targetDistance =
            std::max(lightRadius + epsilon, edgeDistance + epsilon);
        const float extrusionScale = targetDistance / edgeDistance;
        const Vector2 farEnd =
            lightPosition + (end - lightPosition) * extrusionScale;
        const Vector2 farStart =
            lightPosition + (start - lightPosition) * extrusionScale;
        if (!IsFinite(farEnd) || !IsFinite(farStart)) {
            result.vertices.clear();
            result.indices.clear();
            return result;
        }
        const std::uint32_t base =
            static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.push_back(start);
        result.vertices.push_back(end);
        result.vertices.push_back(farEnd);
        result.vertices.push_back(farStart);
        result.indices.insert(result.indices.end(), {
            base, base + 1, base + 2,
            base, base + 2, base + 3});
    }
    return result;
}

LightingFrame2D LightingFrame2D::Build(
    const std::vector<std::shared_ptr<GameObject>>& objects,
    const Camera& cameraComponent,
    PixelSize viewportSize,
    const Camera2D* viewOverride) {
    LightingFrame2D result;
    result.camera.viewportSize = viewportSize;
    result.camera.cullingMask = cameraComponent.GetCullingMask();
    result.ambientColor = cameraComponent.GetAmbientColor();
    result.ambientIntensity = cameraComponent.GetAmbientIntensity();

    const GameObject* cameraObject = cameraComponent.GetGameObject();
    if (!viewportSize.IsValid() || !cameraComponent.IsEnabled() ||
        !cameraObject || !cameraObject->IsActive() ||
        !cameraComponent.IsLightingEnabled()) {
        return result;
    }
    const Camera2D* camera = viewOverride
        ? viewOverride
        : cameraComponent.GetCamera2D();
    if (!camera || !IsFinite(camera->GetX()) || !IsFinite(camera->GetY()) ||
        !IsFinite(camera->GetZoom()) || camera->GetZoom() <= 0.0f ||
        !IsFinite(camera->GetRotation())) {
        return result;
    }

    result.lightingEnabled = true;
    result.camera.center = {
        camera->GetX() + static_cast<float>(viewportSize.width) * 0.5f,
        camera->GetY() + static_cast<float>(viewportSize.height) * 0.5f};
    result.camera.halfExtents = {
        static_cast<float>(viewportSize.width) * 0.5f / camera->GetZoom(),
        static_cast<float>(viewportSize.height) * 0.5f / camera->GetZoom()};
    result.camera.rotation = camera->GetRotation();
    result.camera.viewBounds = CameraViewBounds(result.camera);

    std::vector<LightingLightSnapshot2D> candidates;
    std::size_t componentSceneOrder = 0;
    for (const auto& object : objects) {
        if (!object) continue;
        const bool objectActive = object->IsActive();
        for (Component* component : object->GetComponents()) {
            const std::size_t sceneOrder = componentSceneOrder++;
            if (!objectActive || !component || !component->IsEnabled()) continue;

            if (const auto* light = dynamic_cast<const PointLight2D*>(component)) {
                const int layer = NormalizeWorldRenderLayer(object->GetLayer());
                const Vector2 position = light->GetWorldPosition();
                const Color color = light->GetColor();
                const float intensity = light->GetIntensity();
                const float radius = light->GetRadius();
                const float height = light->GetHeight();
                const float falloff = light->GetFalloff();
                if (!WorldRenderLayerMatchesMask(
                        layer, result.camera.cullingMask) ||
                    !IsFinite(position) || !IsFinite(color) ||
                    !IsFinite(intensity) || !IsFinite(radius) ||
                    !IsFinite(height) || !IsFinite(falloff) ||
                    !CircleIntersectsCameraView(
                        position, radius, result.camera)) {
                    continue;
                }
                candidates.push_back({
                    light,
                    light->GetInstanceID(),
                    object->GetID(),
                    sceneOrder,
                    layer,
                    position,
                    color,
                    intensity,
                    radius,
                    height,
                    falloff,
                    light->GetAffectMask(),
                    light->CastsShadows(),
                    light->GetPriority(),
                    -1});
                continue;
            }

            const auto* occluder =
                dynamic_cast<const ShadowOccluder2D*>(component);
            if (!occluder || !occluder->IsShapeValid()) continue;
            std::vector<Vector2> vertices = occluder->GetWorldVertices();
            if (!NormalizeConvexPolygon2D(vertices)) continue;
            result.occluders.push_back({
                occluder,
                occluder->GetInstanceID(),
                object->GetID(),
                sceneOrder,
                NormalizeWorldRenderLayer(object->GetLayer()),
                std::move(vertices),
                {}});
            result.occluders.back().bounds =
                BoundsOf(result.occluders.back().vertices);
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(),
        [](const LightingLightSnapshot2D& lhs,
           const LightingLightSnapshot2D& rhs) {
            if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
            }
            return lhs.sceneOrder < rhs.sceneOrder;
        });
    const std::size_t selectedCount =
        std::min(candidates.size(), kMaxPointLights2D);
    result.discardedLightCount = candidates.size() - selectedCount;
    result.lights.assign(candidates.begin(), candidates.begin() + selectedCount);

    int nextShadowLayer = 0;
    for (LightingLightSnapshot2D& light : result.lights) {
        if (!light.castsShadows) continue;
        if (nextShadowLayer < static_cast<int>(kMaxShadowLights2D)) {
            light.shadowLayer = nextShadowLayer++;
        } else {
            ++result.discardedShadowLightCount;
        }
    }

    for (std::size_t lightIndex = 0;
         lightIndex < result.lights.size(); ++lightIndex) {
        const LightingLightSnapshot2D& light = result.lights[lightIndex];
        if (light.shadowLayer < 0) continue;

        ShadowMaskLayerFrame2D layer;
        layer.selectedLightIndex = lightIndex;
        layer.layer = light.shadowLayer;
        std::vector<const LightingOccluderSnapshot2D*> selectedOccluders;
        selectedOccluders.reserve(kMaxShadowOccludersPerLight2D);
        for (const LightingOccluderSnapshot2D& occluder : result.occluders) {
            if (!WorldRenderLayerMatchesMask(
                    occluder.layer, light.affectMask) ||
                !CircleIntersectsBounds(
                    light.position, light.radius, occluder.bounds,
                    kShadowGeometryEpsilon2D) ||
                !CircleIntersectsConvexPolygon2D(
                    light.position, light.radius, occluder.vertices)) {
                continue;
            }
            if (selectedOccluders.size() <
                kMaxShadowOccludersPerLight2D) {
                selectedOccluders.push_back(&occluder);
            } else {
                ++layer.discardedOccluderCount;
            }
        }
        layer.selectedOccluderCount = selectedOccluders.size();
        layer.casters.reserve(selectedOccluders.size());
        for (const LightingOccluderSnapshot2D* occluder : selectedOccluders) {
            ShadowCasterGeometry2D geometry =
                BuildShadowCasterGeometry2D(
                    *occluder, light.position, light.radius);
            if (geometry.fullCover) {
                layer.fullCover = true;
                layer.casters.push_back(std::move(geometry));
                break;
            }
            if (geometry.HasTriangles()) {
                layer.casters.push_back(std::move(geometry));
            }
        }
        result.shadowLayers.push_back(std::move(layer));
    }
    return result;
}

} // namespace molga
