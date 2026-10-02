#pragma once

#include "ECS/Components/UIComponent.h"
#include "Common/Types.h"
#include "Text/TextLayoutTypes.h"
#include "Text/UnicodeAnalysis.h"

#include <cstdint>
#include <string>
#include <vector>

class UILabel : public UIComponent {
public:
    COMPONENT_TYPE(UILabel)

    enum class HorizontalAlignment { Left, Center, Right };
    enum class VerticalAlignment { Top, Middle, Bottom };

    // 저작된 family가 없는 레거시 라벨의 메모리 표현. 셰이핑 쪽이 legacy와 신규를
    // 한 경로로 다룰 수 있도록 legacy fontGuid를 face 하나짜리 암묵 family로
    // 보여 준다. 저장은 이 view가 아니라 로드한 형식을 따른다.
    struct FontFamilyView {
        std::string familyGuid;
        std::vector<std::string> faceFontGuids;
        bool implicitOneFace = false;
    };

    static constexpr std::uint32_t CurrentSchemaVersion = 2;

    const std::string& GetText() const { return text_; }
    void SetText(std::string value);
    const std::string& GetFontGuid() const { return fontGuid_; }
    void SetFontGuid(std::string value);
    const std::string& GetFontFamilyGuid() const { return fontFamilyGuid_; }
    void SetFontFamilyGuid(std::string value);
    FontFamilyView ResolveFontFamilyView() const;
    // 이 컴포넌트가 schema 1(폰트를 fontGuid로 지목하던 형식)에서 읽혔는가.
    // 런타임 전용 표식이라 직렬화되지 않으며, 저장 모양만 결정한다.
    bool LoadedLegacyFontGuid() const { return loadedLegacyFontGuid_; }
    // 같은 표식을 공용 이름으로도 노출한다. 저작 스키마를 다루는 쪽이
    // 컴포넌트마다 다른 이름을 외우지 않아도 되게 한다.
    LoadedSchema GetLoadedSchema() const {
        return loadedLegacyFontGuid_ ? LoadedSchema::Legacy
                                     : LoadedSchema::Current;
    }
    float GetFontSizePx() const { return fontSizePx_; }
    void SetFontSizePx(float value);
    float GetLineSpacing() const { return lineSpacing_; }
    void SetLineSpacing(float value);
    const Color& GetColor() const { return color_; }
    void SetColor(const Color& value);
    const std::string& GetLocale() const { return locale_; }
    void SetLocale(std::string value);
    molga::text::BaseDirection GetBaseDirection() const { return baseDirection_; }
    void SetBaseDirection(molga::text::BaseDirection value);
    molga::text::TextWrapMode GetWrapMode() const { return wrap_; }
    void SetWrapMode(molga::text::TextWrapMode value);
    molga::text::TextOverflowMode GetOverflowMode() const { return overflow_; }
    void SetOverflowMode(molga::text::TextOverflowMode value);
    std::uint32_t GetMaxLines() const { return maxLines_; }
    void SetMaxLines(std::uint32_t value);
    HorizontalAlignment GetHorizontalAlignment() const { return horizontalAlignment_; }
    void SetHorizontalAlignment(HorizontalAlignment value);
    VerticalAlignment GetVerticalAlignment() const { return verticalAlignment_; }
    void SetVerticalAlignment(VerticalAlignment value);
    int GetSortingOrder() const { return sortingOrder_; }
    void SetSortingOrder(int value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    std::string text_ = "Label";
    std::string fontGuid_;
    std::string fontFamilyGuid_;
    float fontSizePx_ = 24.0f;
    float lineSpacing_ = 1.2f;
    Color color_ = Color::White();
    std::string locale_ = "und";
    molga::text::BaseDirection baseDirection_ = molga::text::BaseDirection::Auto;
    // 레거시 라벨은 줄바꿈도 생략도 하지 않았다. 기본값이 그 동작과 달라지면
    // 8.2 이전에 스키마만 추가해도 렌더 결과가 바뀐다.
    molga::text::TextWrapMode wrap_ = molga::text::TextWrapMode::NoWrap;
    molga::text::TextOverflowMode overflow_ = molga::text::TextOverflowMode::Overflow;
    std::uint32_t maxLines_ = 0;
    HorizontalAlignment horizontalAlignment_ = HorizontalAlignment::Center;
    VerticalAlignment verticalAlignment_ = VerticalAlignment::Middle;
    int sortingOrder_ = 1;
    bool loadedLegacyFontGuid_ = false;
};
