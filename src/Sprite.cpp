#include "Sprite.h"
#include "Texture.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Sprite::Sprite(Texture* texture)
    : x(0.0f), y(0.0f), width(64.0f), height(64.0f), rotation(0.0f), texture(texture) {
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
    color[3] = 1.0f;
    // Default UV: full texture
    uv[0] = 0.0f;
    uv[1] = 0.0f;
    uv[2] = 1.0f;
    uv[3] = 1.0f;
}

Sprite::~Sprite() {
    // Sprite does not own the texture, so we don't delete it
}

void Sprite::SetPosition(float x, float y) {
    this->x = x;
    this->y = y;
}

void Sprite::SetSize(float width, float height) {
    this->width = width;
    this->height = height;
}

void Sprite::SetRotation(float degrees) {
    this->rotation = degrees;
}

void Sprite::SetColor(float r, float g, float b, float a) {
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;
}

void Sprite::SetTexture(Texture* texture) {
    this->texture = texture;
}

void Sprite::SetUV(float u0, float v0, float u1, float v1) {
    uv[0] = u0;
    uv[1] = v0;
    uv[2] = u1;
    uv[3] = v1;
}

void Sprite::SetFrame(const Frame& frame) {
    uv[0] = frame.u0;
    uv[1] = frame.v0;
    uv[2] = frame.u1;
    uv[3] = frame.v1;
}

void Sprite::GetModelMatrix(mat4x4 out) {
    mat4x4_identity(out);

    // Translate to position
    mat4x4_translate_in_place(out, x, y, 0.0f);

    // Move origin to center for rotation
    mat4x4_translate_in_place(out, width * 0.5f, height * 0.5f, 0.0f);

    // Rotate around Z axis
    float radians = rotation * (float)M_PI / 180.0f;
    mat4x4 rotated;
    mat4x4_rotate_Z(rotated, out, radians);
    mat4x4_dup(out, rotated);

    // Move origin back
    mat4x4_translate_in_place(out, -width * 0.5f, -height * 0.5f, 0.0f);

    // Scale to size
    mat4x4_scale_aniso(out, out, width, height, 1.0f);
}

AABB Sprite::GetAABB() const {
    return {x, y, width, height};
}

Circle Sprite::GetBoundingCircle() const {
    float radius = std::sqrt(width * width + height * height) * 0.5f;
    return {x + width * 0.5f, y + height * 0.5f, radius};
}

bool Sprite::CollidesWith(const Sprite& other) const {
    return Collision::CheckAABB(GetAABB(), other.GetAABB());
}
