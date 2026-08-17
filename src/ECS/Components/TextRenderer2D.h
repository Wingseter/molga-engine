#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/WorldSort2D.h"
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
    void SetSortingLayer(const std::string& layer) { sortingLayer = layer; }
    const std::string& GetSortingLayer() const { return sortingLayer; }
    void SetSortMode(molga::SortMode2D mode) { sortMode = mode; }
    molga::SortMode2D GetSortMode() const { return sortMode; }
    void SetYSortOffset(float offset) { ySortOffset = offset; }
    float GetYSortOffset() const { return ySortOffset; }
    molga::WorldSortSettings2D GetWorldSortSettings() const {
        return {sortingLayer, sortingOrder, sortMode, ySortOffset};
    }

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
    std::string sortingLayer = "Default";
    molga::SortMode2D sortMode = molga::SortMode2D::Fixed;
    float ySortOffset = 0.0f;
};
