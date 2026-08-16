#pragma once

#include "Common/Types.h"
#include "Rendering/PixelSize.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class Camera;
class Camera2D;
class GameObject;
class PointLight2D;
class ShadowOccluder2D;

namespace molga {

inline constexpr std::size_t kMaxPointLights2D = 8;
inline constexpr std::size_t kMaxShadowLights2D = 4;
inline constexpr std::size_t kMaxShadowOccludersPerLight2D = 64;
inline constexpr std::size_t kMaxShadowOccluderVertices2D = 8;
inline constexpr float kShadowGeometryEpsilon2D = 1.0e-4f;

// Immutable, GL-independent camera data used by one lighting render.
struct LightingCameraSnapshot2D {
    PixelSize viewportSize{};
    Vector2 center{};
    Vector2 halfExtents{};
    float rotation = 0.0f;
    AABB viewBounds{};
    std::uint32_t cullingMask = 0xFFFFFFFFu;
};

struct LightingLightSnapshot2D {
    const PointLight2D* source = nullptr;
    std::uint64_t componentInstanceId = 0;
    unsigned int objectId = 0;
    std::size_t sceneOrder = 0;
    int layer = 0;
    Vector2 position{};
    Color color = Color::White();
    float intensity = 1.0f;
    float radius = 128.0f;
    float height = 32.0f;
    float falloff = 2.0f;
    std::uint32_t affectMask = 0xFFFFFFFFu;
    bool castsShadows = false;
    int priority = 0;
    // -1 means this selected light remains unshadowed.
    int shadowLayer = -1;
};

struct LightingOccluderSnapshot2D {
    const ShadowOccluder2D* source = nullptr;
    std::uint64_t componentInstanceId = 0;
    unsigned int objectId = 0;
    std::size_t sceneOrder = 0;
    int layer = 0;
    // Strictly convex, finite, non-zero-area, mathematical CCW world vertices.
    std::vector<Vector2> vertices;
    AABB bounds{};
};

// One occluder is kept as one caster so render telemetry can count casters
// independently of how many silhouette quads its mesh contains.
struct ShadowCasterGeometry2D {
    std::uint64_t occluderInstanceId = 0;
    unsigned int objectId = 0;
    std::size_t sceneOrder = 0;
    bool fullCover = false;
    std::vector<Vector2> vertices;
    std::vector<std::uint32_t> indices;

    bool HasTriangles() const noexcept {
        return indices.size() >= 3 && indices.size() % 3 == 0;
    }
};

struct ShadowMaskLayerFrame2D {
    std::size_t selectedLightIndex = 0;
    int layer = -1;
    bool fullCover = false;
    std::vector<ShadowCasterGeometry2D> casters;
    std::size_t selectedOccluderCount = 0;
    std::size_t discardedOccluderCount = 0;
};

struct LightingFrame2D {
    bool lightingEnabled = false;
    LightingCameraSnapshot2D camera{};
    Color ambientColor = Color::White();
    float ambientIntensity = 0.2f;
    std::vector<LightingLightSnapshot2D> lights;
    std::vector<LightingOccluderSnapshot2D> occluders;
    std::vector<ShadowMaskLayerFrame2D> shadowLayers;
    std::size_t discardedLightCount = 0;
    std::size_t discardedShadowLightCount = 0;

    bool IsUsable() const noexcept {
        return lightingEnabled && camera.viewportSize.IsValid();
    }

    static LightingFrame2D Build(
        const std::vector<std::shared_ptr<GameObject>>& objects,
        const Camera& camera,
        PixelSize viewportSize,
        const Camera2D* viewOverride = nullptr);
};

// Validates strict convexity and finite, non-zero area, then normalizes the
// supplied world-space polygon to mathematical CCW winding.
bool NormalizeConvexPolygon2D(
    std::vector<Vector2>& vertices,
    float epsilon = kShadowGeometryEpsilon2D) noexcept;

bool PointInsideOrOnConvexPolygon2D(
    const Vector2& point,
    const std::vector<Vector2>& ccwVertices,
    float epsilon = kShadowGeometryEpsilon2D) noexcept;

bool CircleIntersectsConvexPolygon2D(
    const Vector2& center,
    float radius,
    const std::vector<Vector2>& ccwVertices,
    float epsilon = kShadowGeometryEpsilon2D) noexcept;

// Produces a filled occluder cap plus quads extruded from light-facing
// silhouette edges. A light inside or epsilon-close to the boundary produces
// fullCover instead of mesh geometry.
ShadowCasterGeometry2D BuildShadowCasterGeometry2D(
    const LightingOccluderSnapshot2D& occluder,
    const Vector2& lightPosition,
    float lightRadius,
    float epsilon = kShadowGeometryEpsilon2D);

} // namespace molga
