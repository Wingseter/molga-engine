#pragma once

namespace Constants {
    constexpr float PI = 3.14159265358979323846f;
    constexpr float TWO_PI = 6.28318530717958647692f;
    constexpr float DEG_TO_RAD = PI / 180.0f;
    constexpr float COLLISION_EPSILON = 0.0001f;

    namespace Camera {
        constexpr float MIN_ZOOM = 0.1f;
        constexpr float MAX_ZOOM = 10.0f;
    }
}
