#ifndef MOLGA_SPRITE_H
#define MOLGA_SPRITE_H

#include "linmath.h"

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

    void GetModelMatrix(mat4x4 out);

    float x, y;
    float width, height;
    float rotation;
    float color[4];
    Texture* texture;
};

#endif // MOLGA_SPRITE_H
