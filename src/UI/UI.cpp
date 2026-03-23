#include "UI.h"
#include "Renderer.h"
#include "Shader.h"
#include "Sprite.h"
#include "Input.h"
#include <algorithm>

// ============ UIElement ============

UIElement::UIElement()
    : x(0.0f), y(0.0f), width(100.0f), height(30.0f), visible(true), enabled(true) {
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
    color[3] = 1.0f;
}

void UIElement::Update(float dt) {
    // Base implementation does nothing
}

void UIElement::SetPosition(float x, float y) {
    this->x = x;
    this->y = y;
}

void UIElement::SetSize(float width, float height) {
    this->width = width;
    this->height = height;
}

void UIElement::SetColor(float r, float g, float b, float a) {
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;
}

void UIElement::SetVisible(bool visible) {
    this->visible = visible;
}

void UIElement::SetEnabled(bool enabled) {
    this->enabled = enabled;
}

bool UIElement::Contains(float px, float py) const {
    return px >= x && px <= x + width && py >= y && py <= y + height;
}

// ============ Panel ============

Panel::Panel() : borderWidth(0.0f) {
    borderColor[0] = 0.0f;
    borderColor[1] = 0.0f;
    borderColor[2] = 0.0f;
    borderColor[3] = 1.0f;
}

void Panel::Render(Renderer* renderer, Shader* shader) {
    if (!visible) return;

    Sprite sprite;

    // Draw border first if we have one
    if (borderWidth > 0.0f) {
        sprite.SetPosition(x - borderWidth, y - borderWidth);
        sprite.SetSize(width + borderWidth * 2.0f, height + borderWidth * 2.0f);
        sprite.SetColor(borderColor[0], borderColor[1], borderColor[2], borderColor[3]);
        renderer->DrawSprite(&sprite);
    }

    // Draw main panel
    sprite.SetPosition(x, y);
    sprite.SetSize(width, height);
    sprite.SetColor(color[0], color[1], color[2], color[3]);
    renderer->DrawSprite(&sprite);
}

void Panel::SetBorderColor(float r, float g, float b, float a) {
    borderColor[0] = r;
    borderColor[1] = g;
    borderColor[2] = b;
    borderColor[3] = a;
}

void Panel::SetBorderWidth(float width) {
    borderWidth = width;
}

// ============ ProgressBar ============

ProgressBar::ProgressBar() : value(1.0f) {
    // Default fill color: green
    fillColor[0] = 0.2f;
    fillColor[1] = 0.8f;
    fillColor[2] = 0.2f;
    fillColor[3] = 1.0f;

    // Default background color: dark gray
    backgroundColor[0] = 0.2f;
    backgroundColor[1] = 0.2f;
    backgroundColor[2] = 0.2f;
    backgroundColor[3] = 1.0f;
}

void ProgressBar::Render(Renderer* renderer, Shader* shader) {
    if (!visible) return;

    Sprite sprite;

    // Draw background
    sprite.SetPosition(x, y);
    sprite.SetSize(width, height);
    sprite.SetColor(backgroundColor[0], backgroundColor[1], backgroundColor[2], backgroundColor[3]);
    renderer->DrawSprite(&sprite);

    // Draw fill (clamped to 0-1)
    float clampedValue = std::max(0.0f, std::min(1.0f, value));
    if (clampedValue > 0.0f) {
        sprite.SetPosition(x, y);
        sprite.SetSize(width * clampedValue, height);
        sprite.SetColor(fillColor[0], fillColor[1], fillColor[2], fillColor[3]);
        renderer->DrawSprite(&sprite);
    }
}

void ProgressBar::SetValue(float value) {
    this->value = value;
}

void ProgressBar::SetFillColor(float r, float g, float b, float a) {
    fillColor[0] = r;
    fillColor[1] = g;
    fillColor[2] = b;
    fillColor[3] = a;
}

void ProgressBar::SetBackgroundColor(float r, float g, float b, float a) {
    backgroundColor[0] = r;
    backgroundColor[1] = g;
    backgroundColor[2] = b;
    backgroundColor[3] = a;
}

// ============ Button ============

Button::Button() : hovered(false), pressed(false), wasPressed(false), onClick(nullptr) {
    // Default color: gray
    color[0] = 0.4f;
    color[1] = 0.4f;
    color[2] = 0.4f;
    color[3] = 1.0f;

    // Default hover color: lighter gray
    hoverColor[0] = 0.5f;
    hoverColor[1] = 0.5f;
    hoverColor[2] = 0.5f;
    hoverColor[3] = 1.0f;

    // Default pressed color: darker gray
    pressedColor[0] = 0.3f;
    pressedColor[1] = 0.3f;
    pressedColor[2] = 0.3f;
    pressedColor[3] = 1.0f;
}

void Button::Update(float dt) {
    if (!visible || !enabled) {
        hovered = false;
        pressed = false;
        return;
    }

    float mx = Input::GetMouseX();
    float my = Input::GetMouseY();

    hovered = Contains(mx, my);

    wasPressed = pressed;
    pressed = hovered && Input::GetMouseButton(0);

    // Trigger onClick when released after being pressed
    if (wasPressed && !pressed && hovered && onClick) {
        onClick();
    }
}

void Button::Render(Renderer* renderer, Shader* shader) {
    if (!visible) return;

    Sprite sprite;
    sprite.SetPosition(x, y);
    sprite.SetSize(width, height);

    if (pressed) {
        sprite.SetColor(pressedColor[0], pressedColor[1], pressedColor[2], pressedColor[3]);
    } else if (hovered) {
        sprite.SetColor(hoverColor[0], hoverColor[1], hoverColor[2], hoverColor[3]);
    } else {
        sprite.SetColor(color[0], color[1], color[2], color[3]);
    }

    renderer->DrawSprite(&sprite);
}

void Button::SetHoverColor(float r, float g, float b, float a) {
    hoverColor[0] = r;
    hoverColor[1] = g;
    hoverColor[2] = b;
    hoverColor[3] = a;
}

void Button::SetPressedColor(float r, float g, float b, float a) {
    pressedColor[0] = r;
    pressedColor[1] = g;
    pressedColor[2] = b;
    pressedColor[3] = a;
}

void Button::SetOnClick(std::function<void()> callback) {
    onClick = callback;
}

// ============ UIManager ============

UIManager::UIManager() {
    mat4x4_identity(uiProjection);
}

UIManager::~UIManager() {
    // UIManager does not own elements, so don't delete them
}

void UIManager::Update(float dt) {
    for (UIElement* element : elements) {
        if (element->enabled) {
            element->Update(dt);
        }
    }
}

void UIManager::Render(Renderer* renderer, Shader* shader, float screenWidth, float screenHeight) {
    if (!shader) return;

    // Set up orthographic projection for UI (screen coordinates)
    mat4x4_ortho(uiProjection, 0.0f, screenWidth, screenHeight, 0.0f, -1.0f, 1.0f);

    shader->Use();
    shader->SetMat4("projection", (float*)uiProjection);

    // Reset model matrix for UI
    mat4x4 identity;
    mat4x4_identity(identity);

    for (UIElement* element : elements) {
        if (element->visible) {
            element->Render(renderer, shader);
        }
    }
}

void UIManager::AddElement(UIElement* element) {
    elements.push_back(element);
}

void UIManager::RemoveElement(UIElement* element) {
    elements.erase(std::remove(elements.begin(), elements.end(), element), elements.end());
}

void UIManager::Clear() {
    elements.clear();
}
