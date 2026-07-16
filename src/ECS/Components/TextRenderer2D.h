#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include <algorithm>
#include <string>

class Renderer;

class TextRenderer2D : public Component {
public:
    COMPONENT_TYPE(TextRenderer2D)

    enum class Alignment {
        Left,
        Center,
        Right
    };

    TextRenderer2D() = default;

    // Getters / Setters
    void SetText(const std::string& val) { text = val; }
    const std::string& GetText() const { return text; }

    void SetColor(const Color& val) { color = val; }
    const Color& GetColor() const { return color; }

    void SetScale(float val) { scale = val; }
    float GetScale() const { return scale; }

    void SetAlignment(Alignment val) { alignment = val; }
    Alignment GetAlignment() const { return alignment; }

    void SetFontName(const std::string& val) { fontName = val; }
    const std::string& GetFontName() const { return fontName; }

    void SetFontGuid(const std::string& val) { fontGuid = val; }
    const std::string& GetFontGuid() const { return fontGuid; }

    void SetFontSizePx(float val) { fontSizePx = std::clamp(val, 1.0f, 512.0f); }
    float GetFontSizePx() const { return fontSizePx; }

    void SetLineSpacing(float val) { lineSpacing = std::clamp(val, 0.1f, 10.0f); }
    float GetLineSpacing() const { return lineSpacing; }

    void SetSortingOrder(int val) { sortingOrder = val; }
    int GetSortingOrder() const { return sortingOrder; }

    // Lifecycle
    void RenderSprite(Renderer* renderer) override;
    void CollectRender(molga::RenderQueue& queue) override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    std::string text = "Text";
    Color color = Color::White();
    float scale = 1.0f;
    Alignment alignment = Alignment::Left;
    std::string fontGuid;
    float fontSizePx = 16.0f;
    float lineSpacing = 1.2f;
    // Kept for source and scene compatibility. New assets identify fonts by GUID.
    std::string fontName = "default";
    int sortingOrder = 0;
};
