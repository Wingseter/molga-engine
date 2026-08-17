#pragma once

#include "../Common/linmath.h"
#include "Common/Types.h"

class Camera2D {
public:
    Camera2D(float screenWidth, float screenHeight);

    void SetPosition(float x, float y);
    void Move(float dx, float dy);
    void SetZoom(float zoom);
    // Pixel-perfect game cameras use a wider, integer-only zoom range without
    // changing the legacy Scene View/orthographic SetZoom contract.
    void SetPixelZoom(int zoom);
    void Zoom(float factor);
    void SetRotation(float degrees);

    void GetViewMatrix(mat4x4 out);
    void GetProjectionMatrix(mat4x4 out);

    float GetX() const { return x; }
    float GetY() const { return y; }
    float GetZoom() const { return zoom; }
    float GetRotation() const { return rotation; }
    float GetScreenWidth() const { return screenWidth; }
    float GetScreenHeight() const { return screenHeight; }
    AABB GetViewBounds() const;

    void SetScreenSize(float width, float height);

private:
    float x, y;
    float zoom;
    float rotation;
    float screenWidth, screenHeight;

    void UpdateMatrices();
    mat4x4 viewMatrix;
    mat4x4 projectionMatrix;
    bool needsUpdate;
};
