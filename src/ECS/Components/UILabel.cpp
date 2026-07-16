#include "ECS/Components/UILabel.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>

REGISTER_COMPONENT(UILabel)

void UILabel::SetFontSizePx(float value) {
    fontSizePx_ = std::clamp(value, 1.0f, 512.0f);
}
void UILabel::SetLineSpacing(float value) {
    lineSpacing_ = std::clamp(value, 0.1f, 10.0f);
}

void UILabel::Serialize(nlohmann::json& j) const {
    j["text"] = text_;
    j["fontGuid"] = fontGuid_;
    j["fontSizePx"] = fontSizePx_;
    j["lineSpacing"] = lineSpacing_;
    j["color"] = {color_.r, color_.g, color_.b, color_.a};
    j["horizontalAlignment"] = static_cast<int>(horizontalAlignment_);
    j["verticalAlignment"] = static_cast<int>(verticalAlignment_);
    j["sortingOrder"] = sortingOrder_;
}

void UILabel::Deserialize(const nlohmann::json& j) {
    text_ = j.value("text", text_);
    fontGuid_ = j.value("fontGuid", fontGuid_);
    SetFontSizePx(j.value("fontSizePx", fontSizePx_));
    SetLineSpacing(j.value("lineSpacing", lineSpacing_));
    if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4) {
        color_ = {j["color"][0].get<float>(), j["color"][1].get<float>(),
                  j["color"][2].get<float>(), j["color"][3].get<float>()};
    }
    horizontalAlignment_ = static_cast<HorizontalAlignment>(
        std::clamp(j.value("horizontalAlignment", static_cast<int>(horizontalAlignment_)), 0, 2));
    verticalAlignment_ = static_cast<VerticalAlignment>(
        std::clamp(j.value("verticalAlignment", static_cast<int>(verticalAlignment_)), 0, 2));
    sortingOrder_ = j.value("sortingOrder", sortingOrder_);
}
