#ifndef MOLGA_SPRITE_H
#define MOLGA_SPRITE_H

#include "linmath.h"
#include "SpriteSheet.h"
#include "Collision.h"

class Texture;

class Sprite {
public:
    Sprite(Texture* texture = nullptr);
    ~Sprite();

    void SetPosition(float x, float y);
    void SetSize(float width, float height);
    void SetRotation(float degrees);
    void SetColor(float r, float g, float b, float a = 1.0f);
    void SetTexture(Texture* texture);
    void SetUV(float u0, float v0, float u1, float v1);
    void SetFrame(const Frame& frame);

    void GetModelMatrix(mat4x4 out);

    // Collision
    AABB GetAABB() const;
    Circle GetBoundingCircle() const;
    bool CollidesWith(const Sprite& other) const;

    float x, y;
    float width, height;
    float rotation;
    float color[4];
    float uv[4];  // u0, v0, u1, v1
    Texture* texture;
};

#endif // MOLGA_SPRITE_H
