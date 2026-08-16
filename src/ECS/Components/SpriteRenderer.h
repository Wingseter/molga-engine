#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/Material.h"
#include "../../Rendering/LightingTypes2D.h"
#include "../../Rendering/SpriteRef.h"
#include "../../Rendering/WorldSort2D.h"
#include <optional>
#include <string>

class Texture;
class Renderer;
class Shader;
class Camera2D;

class SpriteRenderer : public Component {
public:
    COMPONENT_TYPE(SpriteRenderer)

    SpriteRenderer() = default;

    enum class SizeMode { Custom, Native };

    Material material;

    // Texture
    void SetTexture(Texture* tex);
    Texture* GetTexture() const { return texture; }

    void SetTexturePath(const std::string& path);
    const std::string& GetTexturePath() const { return texturePath; }

    void SetTextureGuid(const std::string& guid);
    const std::string& GetTextureGuid() const { return authoredSprite.textureGuid; }

    void SetSpriteRef(const molga::SpriteRef& value);
    const molga::SpriteRef& GetSpriteRef() const { return authoredSprite; }
    const molga::SpriteRef& GetAuthoredSpriteRef() const { return authoredSprite; }

    // Animator-owned override. It is deliberately runtime-only and is never
    // included in scene/prefab serialization.
    void SetRuntimeSpriteOverride(const molga::SpriteRef& value);
    void ClearRuntimeSpriteOverride();
    bool HasRuntimeSpriteOverride() const { return hasRuntimeSpriteOverride; }
    const molga::SpriteRef& GetRuntimeSpriteOverride() const { return runtimeSpriteOverride; }
    const molga::SpriteRef& GetEffectiveSpriteRef() const {
        return hasRuntimeSpriteOverride ? runtimeSpriteOverride : authoredSprite;
    }

    SpriteLightingMode2D GetLightingMode() const { return lightingMode_; }
    void SetLightingMode(SpriteLightingMode2D mode);

    const std::string& GetNormalMapGuid() const { return normalMapGuid_; }
    void SetNormalMapGuid(const std::string& guid);

    float GetNormalStrength() const { return normalStrength_; }
    bool SetNormalStrength(float strength);

    // Returns nullptr for missing, failed, wrong-usage, or wrong-size normal
    // textures. Size validation follows the effective Animator sprite.
    Texture* GetNormalTexture();
    bool HasUsableNormalTexture();

    // Color/Tint
    void SetColor(const Color& c) { color = c; }
    void SetColor(float r, float g, float b, float a = 1.0f) { color = Color(r, g, b, a); }
    const Color& GetColor() const { return color; }

    // Size (if no texture, or to override texture size)
    void SetSize(float w, float h) { width = w; height = h; sizeMode = SizeMode::Custom; }
    void SetSize(const Vector2& size) { SetSize(size.x, size.y); }
    void SetCustomSize(float w, float h) { width = w; height = h; }
    float GetWidth() const { return GetSize().x; }
    float GetHeight() const { return GetSize().y; }
    Vector2 GetSize() const;
    Vector2 GetCustomSize() const { return Vector2(width, height); }
    void SetSizeMode(SizeMode value) { sizeMode = value; }
    SizeMode GetSizeMode() const { return sizeMode; }
    Vector2 GetPivot() const;
    // Uses the same size, pivot, world scale and rotation as render
    // collection. Scene picking/culling must not reconstruct sprite geometry
    // from Transform position and unscaled authored width/height.
    std::optional<AABB> GetWorldBounds();

    // Flip
    void SetFlipX(bool flip) { flipX = flip; }
    void SetFlipY(bool flip) { flipY = flip; }
    bool GetFlipX() const { return flipX; }
    bool GetFlipY() const { return flipY; }

    // Sorting order (higher = rendered on top)
    void SetSortingOrder(int order) { sortingOrder = order; }
    int GetSortingOrder() const { return sortingOrder; }
    void SetSortingLayer(const std::string& layer) { sortingLayer = layer; }
    const std::string& GetSortingLayer() const { return sortingLayer; }
    void SetSortMode(molga::SortMode2D mode) { sortMode = mode; }
    molga::SortMode2D GetSortMode() const { return sortMode; }
    void SetYSortOffset(float offset) { ySortOffset = offset; }
    float GetYSortOffset() const { return ySortOffset; }
    molga::WorldSortSettings2D GetWorldSortSettings() const {
        return {sortingLayer, sortingOrder, sortMode, ySortOffset};
    }

    // 패스(Begin/End)는 호출자(프레임 루프/RenderPass)가 소유한다.
    // 이 함수는 활성 패스 안에 스프라이트 1개를 제출만 한다.
    void RenderSprite(Renderer* renderer) override;
    void CollectRender(molga::RenderQueue& queue) override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void ResolveAssets() override;

    static nlohmann::json CanonicalizeSerializedData(
        const nlohmann::json& serialized);

    // Editor GUI
    void OnInspectorGUI() override;

private:
    struct VisualSprite {
        Texture* texture = nullptr;
        Frame uv{};
        Vector2 pivot{0.5f, 0.5f};
        Vector2 nativeSize{};
        bool resolved = false;
    };

    void InvalidateAuthoredResolution();
    void InvalidateRuntimeResolution();
    void InvalidateNormalResolution();
    void EnsureSpriteResolution();
    void EnsureNormalResolution();
    VisualSprite GetVisualSprite();

    Texture* texture = nullptr;
    std::string texturePath;
    molga::SpriteRef authoredSprite;
    molga::SpriteRef runtimeSpriteOverride;
    molga::ResolvedSprite authoredResolved;
    molga::ResolvedSprite runtimeResolved;
    bool hasRuntimeSpriteOverride = false;
    bool authoredResolveAttempted = false;
    bool runtimeResolveAttempted = false;
    SpriteLightingMode2D lightingMode_ = SpriteLightingMode2D::Unlit;
    std::string normalMapGuid_;
    float normalStrength_ = 1.0f;
    Texture* normalTexture_ = nullptr;
    bool normalResolveAttempted_ = false;
    bool normalWarningEmitted_ = false;
    bool litCustomMaterialWarningEmitted_ = false;
    Color color = Color::White();

    float width = 32.0f;
    float height = 32.0f;
    SizeMode sizeMode = SizeMode::Native;

    bool flipX = false;
    bool flipY = false;

    int sortingOrder = 0;
    std::string sortingLayer = "Default";
    molga::SortMode2D sortMode = molga::SortMode2D::Fixed;
    float ySortOffset = 0.0f;
};
