#pragma once

#include <box2d/box2d.h>

/**
 * Molga Engine - Box2D Physics Backend Wrapper
 * 
 * License Note:
 * Box2D is licensed under the MIT License.
 * Copyright (c) 2019-2024 Erin Catto
 * 
 * Version Note:
 * Integrated Box2D v3.0.0
 */

class Box2DBackend {
public:
    static b2WorldId CreateWorld(float gravityX, float gravityY);
    static void DestroyWorld(b2WorldId worldId);
};
