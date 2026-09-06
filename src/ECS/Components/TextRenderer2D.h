#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/WorldSort2D.h"
#include "Text/TextLayoutTypes.h"
#include "Text/UnicodeAnalysis.h"
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

    void SetFontFamilyGuid(const std::string& val);
    const std::string& GetFontFamilyGuid() const { return fontFamilyGuid; }
    // 이 컴포넌트가 schema 1(폰트를 fontGuid/fontName으로 지목하던 형식)에서
    // 읽혔는가. 런타임 전용 표식이라 직렬화되지 않으며, 저장 모양만 결정한다.
    bool LoadedLegacyFontGuid() const { return loadedLegacyFontGuid; }

    void SetLocale(const std::string& val);
    const std::string& GetLocale() const { return locale; }

    void SetBaseDirection(molga::text::BaseDirection val) { baseDirection = val; }
    molga::text::BaseDirection GetBaseDirection() const { return baseDirection; }

    // 월드 텍스트에는 저작된 layout bound가 없다(설계 7.3). 접을 기준이 없으니
    // wrap/overflow는 저작 필드가 아니라 고정된 계약이고, 줄은 명시적 개행에서만
    // 나뉜다. 스키마에 폭/높이를 만들면 그 계약이 말만 남으므로 저장하지 않는다.
    constexpr molga::text::TextWrapMode WrapMode() const {
        return molga::text::TextWrapMode::NoWrap;
    }
    constexpr molga::text::TextOverflowMode OverflowMode() const {
        return molga::text::TextOverflowMode::Overflow;
    }
    constexpr bool HasAuthoredLayoutBounds() const { return false; }

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
    std::string fontFamilyGuid;
    std::string locale = "und";
    molga::text::BaseDirection baseDirection = molga::text::BaseDirection::Auto;
    float fontSizePx = 16.0f;
    float lineSpacing = 1.2f;
    // Kept for source and scene compatibility. New assets identify fonts by GUID.
    std::string fontName = "default";
    int sortingOrder = 0;
    std::string sortingLayer = "Default";
    molga::SortMode2D sortMode = molga::SortMode2D::Fixed;
    float ySortOffset = 0.0f;
    bool loadedLegacyFontGuid = false;
};
