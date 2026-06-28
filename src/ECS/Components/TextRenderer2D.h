#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
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
    std::string fontName = "default";
    int sortingOrder = 0;
};
