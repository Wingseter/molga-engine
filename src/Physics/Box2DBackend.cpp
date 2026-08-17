#include "Box2DBackend.h"

b2WorldId Box2DBackend::CreateWorld(float gravityX, float gravityY) {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = b2Vec2{ gravityX, gravityY };
    return b2CreateWorld(&worldDef);
}

void Box2DBackend::DestroyWorld(b2WorldId worldId) {
    b2DestroyWorld(worldId);
}
