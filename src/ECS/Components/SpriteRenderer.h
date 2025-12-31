#ifndef MOLGA_SPRITE_RENDERER_COMPONENT_H
#define MOLGA_SPRITE_RENDERER_COMPONENT_H

#include "../Component.h"
#include "../../Common/Types.h"
#include <string>

class Texture;
class Renderer;
class Shader;
class Camera2D;

class SpriteRenderer : public Component {
public:
    COMPONENT_TYPE(SpriteRenderer)

    SpriteRenderer() = default;

    // Texture
    void SetTexture(Texture* tex) { texture = tex; }
    Texture* GetTexture() const { return texture; }

    void SetTexturePath(const std::string& path) { texturePath = path; }
    const std::string& GetTexturePath() const { return texturePath; }

    // Color/Tint
    void SetColor(const Color& c) { color = c; }
    void SetColor(float r, float g, float b, float a = 1.0f) { color = Color(r, g, b, a); }
    const Color& GetColor() const { return color; }

    // Size (if no texture, or to override texture size)
    void SetSize(float w, float h) { width = w; height = h; }
    void SetSize(const Vector2& size) { width = size.x; height = size.y; }
    float GetWidth() const { return width; }
    float GetHeight() const { return height; }
    Vector2 GetSize() const { return Vector2(width, height); }

    // Flip
    void SetFlipX(bool flip) { flipX = flip; }
    void SetFlipY(bool flip) { flipY = flip; }
    bool GetFlipX() const { return flipX; }
    bool GetFlipY() const { return flipY; }

    // Sorting order (higher = rendered on top)
    void SetSortingOrder(int order) { sortingOrder = order; }
    int GetSortingOrder() const { return sortingOrder; }

    // Render using external renderer
    void RenderSprite(Renderer* renderer, Shader* shader, Camera2D* camera);

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    Texture* texture = nullptr;
    std::string texturePath;
    Color color = Color::White();

    float width = 32.0f;
    float height = 32.0f;

    bool flipX = false;
    bool flipY = false;

    int sortingOrder = 0;
};

#endif // MOLGA_SPRITE_RENDERER_COMPONENT_H
