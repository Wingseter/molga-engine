#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/TextRenderer.h"
#include "../../Rendering/WorldSort2D.h"
#include "Text/TextLayoutTypes.h"
#include "Text/UnicodeAnalysis.h"
#include <algorithm>
#include <optional>
#include <string>

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

    // Task 8.2 Step 1d: 이 배율은 Transform의 월드 배율과 곱해지는 컴포넌트
    // 자신의 몫이다. 옛 이름 SetScale은 "월드 배율"과 구분되지 않아, 둘 중
    // 어느 쪽을 만지는지가 호출부에서 보이지 않았다.
    void SetComponentScale(float val) { scale = val; }
    float GetComponentScale() const { return scale; }

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

    // ── Task 8.2 Step 7/7a/7b: 공유 파이프라인으로 가는 유일한 진입점 ────────
    // 한 인자짜리 CollectRender도 RenderSprite도 재정의하지 않는다. 둘 다
    // renderer/서비스/sink 권한을 스스로 찾아야 하는 모양이고, 그것이 곧 이
    // 프로세스에 두 번째 텍스트 서비스가 생기는 길이다. 그래서 문맥을 요구하는
    // 이쪽만 있고, 문맥 없는 호출에는 Component의 기본 no-op이 남는다.
    void CollectRender(molga::RenderQueue& queue,
                       const WorldRenderCollectionContext& context) override;

    // Step 7: 제약 없는 공유 요청. 폭/높이를 만들어 내지 않는다(설계 7.3).
    molga::text::TextLayoutRequest BuildLayoutRequest() const;
    // Step 7a/7b: scale-rotate-translate affine과 프레임 권한에서 온 정렬/래스터
    // 값. 형제 Transform이 없거나 배율이 래스터 정책을 벗어나면 nullopt다.
    std::optional<TextCollectContext> BuildWorldTextContext(
        const WorldRenderCollectionContext& context,
        molga::text::TextDiagnosticSink& sink) const;

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
