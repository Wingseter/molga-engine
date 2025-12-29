#include "Camera2D.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Camera2D::Camera2D(float screenWidth, float screenHeight)
    : x(0.0f), y(0.0f), zoom(1.0f), rotation(0.0f),
      screenWidth(screenWidth), screenHeight(screenHeight), needsUpdate(true) {
    mat4x4_identity(viewMatrix);
    mat4x4_identity(projectionMatrix);
    UpdateMatrices();
}

void Camera2D::SetPosition(float x, float y) {
    this->x = x;
    this->y = y;
    needsUpdate = true;
}

void Camera2D::Move(float dx, float dy) {
    x += dx;
    y += dy;
    needsUpdate = true;
}

void Camera2D::SetZoom(float zoom) {
    this->zoom = zoom;
    if (this->zoom < 0.1f) this->zoom = 0.1f;
    if (this->zoom > 10.0f) this->zoom = 10.0f;
    needsUpdate = true;
}

void Camera2D::Zoom(float factor) {
    SetZoom(zoom * factor);
}

void Camera2D::SetRotation(float degrees) {
    this->rotation = degrees;
    needsUpdate = true;
}

void Camera2D::SetScreenSize(float width, float height) {
    screenWidth = width;
    screenHeight = height;
    needsUpdate = true;
}

void Camera2D::UpdateMatrices() {
    if (!needsUpdate) return;

    // Projection: orthographic with origin at top-left
    mat4x4_ortho(projectionMatrix, 0.0f, screenWidth, screenHeight, 0.0f, -1.0f, 1.0f);

    // View matrix: translate, rotate, scale
    mat4x4_identity(viewMatrix);

    // Move to center of screen
    mat4x4_translate_in_place(viewMatrix, screenWidth / 2.0f, screenHeight / 2.0f, 0.0f);

    // Apply zoom
    mat4x4_scale_aniso(viewMatrix, viewMatrix, zoom, zoom, 1.0f);

    // Apply rotation
    float radians = rotation * (float)M_PI / 180.0f;
    mat4x4 rotated;
    mat4x4_rotate_Z(rotated, viewMatrix, radians);
    mat4x4_dup(viewMatrix, rotated);

    // Move back from center and apply camera position
    mat4x4_translate_in_place(viewMatrix, -screenWidth / 2.0f - x, -screenHeight / 2.0f - y, 0.0f);

    needsUpdate = false;
}

void Camera2D::GetViewMatrix(mat4x4 out) {
    UpdateMatrices();
    mat4x4_dup(out, viewMatrix);
}

void Camera2D::GetProjectionMatrix(mat4x4 out) {
    UpdateMatrices();
    mat4x4_dup(out, projectionMatrix);
}
