#pragma once

#include "../Event.h"
#include "../../Common/Types.h"

// Fired when two colliders overlap (non-trigger)
struct CollisionEvent : EventBase {
    unsigned int objectA_ID = 0;
    unsigned int objectB_ID = 0;
    Vector2 contactPoint;
    Vector2 normal;
    float penetration = 0.0f;
};

// Fired when a trigger collider overlaps another collider
struct TriggerEvent : EventBase {
    unsigned int trigger_ID = 0;
    unsigned int other_ID = 0;
    bool entered = true;  // true = enter, false = exit
};
