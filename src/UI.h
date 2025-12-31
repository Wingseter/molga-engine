#ifndef MOLGA_UI_H
#define MOLGA_UI_H

#include <vector>
#include <functional>
#include "linmath.h"

class Shader;
class Renderer;

// Base UI Element
class UIElement {
public:
    UIElement();
    virtual ~UIElement() = default;

    virtual void Update(float dt);
    virtual void Render(Renderer* renderer, Shader* shader) = 0;

    void SetPosition(float x, float y);
    void SetSize(float width, float height);
    void SetColor(float r, float g, float b, float a = 1.0f);
    void SetVisible(bool visible);
    void SetEnabled(bool enabled);

    bool Contains(float px, float py) const;

    float x, y;
    float width, height;
    float color[4];
    bool visible;
    bool enabled;
};

// Simple colored panel/box
class Panel : public UIElement {
public:
    Panel();

    void Render(Renderer* renderer, Shader* shader) override;

    void SetBorderColor(float r, float g, float b, float a = 1.0f);
    void SetBorderWidth(float width);

    float borderColor[4];
    float borderWidth;
};

// Progress bar (health bar, loading bar, etc.)
class ProgressBar : public UIElement {
public:
    ProgressBar();

    void Render(Renderer* renderer, Shader* shader) override;

    void SetValue(float value);  // 0.0 to 1.0
    void SetFillColor(float r, float g, float b, float a = 1.0f);
    void SetBackgroundColor(float r, float g, float b, float a = 1.0f);

    float value;  // 0.0 to 1.0
    float fillColor[4];
    float backgroundColor[4];
};

// Clickable button
class Button : public UIElement {
public:
    Button();

    void Update(float dt) override;
    void Render(Renderer* renderer, Shader* shader) override;

    void SetHoverColor(float r, float g, float b, float a = 1.0f);
    void SetPressedColor(float r, float g, float b, float a = 1.0f);
    void SetOnClick(std::function<void()> callback);

    bool IsHovered() const { return hovered; }
    bool IsPressed() const { return pressed; }

    float hoverColor[4];
    float pressedColor[4];
    std::function<void()> onClick;

private:
    bool hovered;
    bool pressed;
    bool wasPressed;
};

// UI Manager to handle all UI elements
class UIManager {
public:
    UIManager();
    ~UIManager();

    void Update(float dt);
    void Render(Renderer* renderer, Shader* shader, float screenWidth, float screenHeight);

    void AddElement(UIElement* element);
    void RemoveElement(UIElement* element);
    void Clear();

private:
    std::vector<UIElement*> elements;
    mat4x4 uiProjection;
};

#endif // MOLGA_UI_H
