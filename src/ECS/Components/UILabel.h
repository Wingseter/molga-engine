#pragma once

#include "ECS/Component.h"
#include "Common/Types.h"

#include <string>

class UILabel : public Component {
public:
    COMPONENT_TYPE(UILabel)

    enum class HorizontalAlignment { Left, Center, Right };
    enum class VerticalAlignment { Top, Middle, Bottom };

    const std::string& GetText() const { return text_; }
    void SetText(std::string value) { text_ = std::move(value); }
    const std::string& GetFontGuid() const { return fontGuid_; }
    void SetFontGuid(std::string value) { fontGuid_ = std::move(value); }
    float GetFontSizePx() const { return fontSizePx_; }
    void SetFontSizePx(float value);
    float GetLineSpacing() const { return lineSpacing_; }
    void SetLineSpacing(float value);
    const Color& GetColor() const { return color_; }
    void SetColor(const Color& value) { color_ = value; }
    HorizontalAlignment GetHorizontalAlignment() const { return horizontalAlignment_; }
    void SetHorizontalAlignment(HorizontalAlignment value) { horizontalAlignment_ = value; }
    VerticalAlignment GetVerticalAlignment() const { return verticalAlignment_; }
    void SetVerticalAlignment(VerticalAlignment value) { verticalAlignment_ = value; }
    int GetSortingOrder() const { return sortingOrder_; }
    void SetSortingOrder(int value) { sortingOrder_ = value; }

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    std::string text_ = "Label";
    std::string fontGuid_;
    float fontSizePx_ = 24.0f;
    float lineSpacing_ = 1.2f;
    Color color_ = Color::White();
    HorizontalAlignment horizontalAlignment_ = HorizontalAlignment::Center;
    VerticalAlignment verticalAlignment_ = VerticalAlignment::Middle;
    int sortingOrder_ = 1;
};
