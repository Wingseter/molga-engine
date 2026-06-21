#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/Material.h"
#include <string>

class Texture;
class Renderer;
class Shader;
class Camera2D;

class SpriteRenderer : public Component {
public:
    COMPONENT_TYPE(SpriteRenderer)

    SpriteRenderer() = default;

    Material material;

    // Texture
    void SetTexture(Texture* tex) { texture = tex; }
    Texture* GetTexture() const { return texture; }

    void SetTexturePath(const std::string& path) { texturePath = path; }
    const std::string& GetTexturePath() const { return texturePath; }

    void SetTextureGuid(const std::string& g) { textureGuid = g; }
    const std::string& GetTextureGuid() const { return textureGuid; }

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

    // 패스(Begin/End)는 호출자(프레임 루프/RenderPass)가 소유한다.
    // 이 함수는 활성 패스 안에 스프라이트 1개를 제출만 한다.
    void RenderSprite(Renderer* renderer) override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void ResolveAssets() override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    Texture* texture = nullptr;
    std::string texturePath;
    std::string textureGuid;
    Color color = Color::White();

    float width = 32.0f;
    float height = 32.0f;

    bool flipX = false;
    bool flipY = false;

    int sortingOrder = 0;
};
