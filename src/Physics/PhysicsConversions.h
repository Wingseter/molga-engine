#pragma once

#include "../Common/Types.h"
#include <box2d/box2d.h>

namespace PhysicsConversions {
    // Standard conversion factor: 100 pixels = 1 meter
    constexpr float DEFAULT_PPM = 100.0f;

    inline b2Vec2 ToMeters(const Vector2& pixels, float ppm = DEFAULT_PPM) {
        return b2Vec2{ pixels.x / ppm, pixels.y / ppm };
    }

    inline Vector2 ToPixels(const b2Vec2& meters, float ppm = DEFAULT_PPM) {
        return Vector2{ meters.x * ppm, meters.y * ppm };
    }

    inline float ToRadians(float degrees) {
        return degrees * (3.14159265359f / 180.0f);
    }

    inline float ToDegrees(float radians) {
        return radians * (180.0f / 3.14159265359f);
    }
}
