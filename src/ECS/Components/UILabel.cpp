#include "ECS/Components/UILabel.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>

REGISTER_COMPONENT(UILabel)

namespace {

using molga::text::BaseDirection;
using molga::text::TextOverflowMode;
using molga::text::TextWrapMode;

// 설계 7.3이 문서화한 토큰 그대로 쓴다. 정수 서수로 저장하면 열거에 값이
// 끼어드는 순간 디스크의 기존 scene이 다른 정책을 뜻하게 된다.
const char* ToToken(BaseDirection value) {
    switch (value) {
        case BaseDirection::LeftToRight: return "LTR";
        case BaseDirection::RightToLeft: return "RTL";
        case BaseDirection::Auto: break;
    }
    return "Auto";
}

BaseDirection BaseDirectionFromToken(const std::string& token) {
    if (token == "LTR") return BaseDirection::LeftToRight;
    if (token == "RTL") return BaseDirection::RightToLeft;
    return BaseDirection::Auto;
}

const char* ToToken(TextWrapMode value) {
    switch (value) {
        case TextWrapMode::Word: return "Word";
        case TextWrapMode::Grapheme: return "Grapheme";
        case TextWrapMode::NoWrap: break;
    }
    return "NoWrap";
}

TextWrapMode WrapModeFromToken(const std::string& token) {
    if (token == "Word") return TextWrapMode::Word;
    if (token == "Grapheme") return TextWrapMode::Grapheme;
    return TextWrapMode::NoWrap;
}

const char* ToToken(TextOverflowMode value) {
    switch (value) {
        case TextOverflowMode::Clip: return "Clip";
        case TextOverflowMode::Ellipsis: return "Ellipsis";
        case TextOverflowMode::Overflow: break;
    }
    return "Overflow";
}

TextOverflowMode OverflowModeFromToken(const std::string& token) {
    if (token == "Clip") return TextOverflowMode::Clip;
    if (token == "Ellipsis") return TextOverflowMode::Ellipsis;
    return TextOverflowMode::Overflow;
}

} // namespace

void UILabel::SetFontSizePx(float value) {
    fontSizePx_ = std::clamp(value, 1.0f, 512.0f);
}
void UILabel::SetLineSpacing(float value) {
    lineSpacing_ = std::clamp(value, 0.1f, 10.0f);
}

void UILabel::SetLocale(std::string value) {
    // 빈 태그는 "locale 없음"이 아니라 root tailoring 요청이다. 빈 문자열을 그대로
    // 두면 분석 계층이 잘못된 태그로 실패한다.
    locale_ = value.empty() ? std::string("und") : std::move(value);
}

void UILabel::SetFontFamilyGuid(std::string value) {
    const bool authoredFamily = !value.empty();
    fontFamilyGuid_ = std::move(value);
    // schema 2에는 fontGuid 키가 없다. family를 저작하는 것이 legacy 표현을
    // 대체할 값을 공급하는 유일한 행위이므로, 이때만 legacy 표식을 내려놓는다.
    // 그러지 않으면 저작한 family가 저장에서 조용히 사라진다.
    if (authoredFamily) {
        loadedLegacyFontGuid_ = false;
    }
}

UILabel::FontFamilyView UILabel::ResolveFontFamilyView() const {
    FontFamilyView view;
    if (!fontFamilyGuid_.empty()) {
        view.familyGuid = fontFamilyGuid_;
        return view;
    }
    if (!fontGuid_.empty()) {
        view.faceFontGuids.push_back(fontGuid_);
        view.implicitOneFace = true;
    }
    return view;
}

void UILabel::Serialize(nlohmann::json& j) const {
    if (loadedLegacyFontGuid_) {
        // 아직 이관되지 않은 payload는 읽은 형식 그대로 돌려준다. 여기에 새 키가
        // 하나라도 새면 migration 전 scene 파일이 조용히 다시 쓰인다.
        j["text"] = text_;
        j["fontGuid"] = fontGuid_;
        j["fontSizePx"] = fontSizePx_;
        j["lineSpacing"] = lineSpacing_;
        j["color"] = {color_.r, color_.g, color_.b, color_.a};
        j["horizontalAlignment"] = static_cast<int>(horizontalAlignment_);
        j["verticalAlignment"] = static_cast<int>(verticalAlignment_);
        j["sortingOrder"] = sortingOrder_;
        return;
    }

    j["schemaVersion"] = 2;
    j["text"] = text_;
    // 8.2 이전에는 UI 렌더 경로도 에디터 인스펙터의 폰트 슬롯도 이 키에서만
    // 폰트를 찾는다(UISystem, EditorPropertyDescriptor의 AssetTypeFor). schema 2가
    // 키를 빼면 새로 만든 라벨은 폰트를 지목할 방법 자체를 잃는다. family가
    // 렌더를 지휘하기 시작하는 8.2 이후에야 이 키가 legacy 잔재가 된다.
    j["fontGuid"] = fontGuid_;
    j["fontFamilyGuid"] = fontFamilyGuid_;
    j["fontSizePx"] = fontSizePx_;
    j["lineSpacing"] = lineSpacing_;
    j["color"] = {color_.r, color_.g, color_.b, color_.a};
    j["locale"] = locale_;
    j["baseDirection"] = ToToken(baseDirection_);
    j["wrap"] = ToToken(wrap_);
    j["overflow"] = ToToken(overflow_);
    j["maxLines"] = maxLines_;
    j["horizontalAlignment"] = static_cast<int>(horizontalAlignment_);
    j["verticalAlignment"] = static_cast<int>(verticalAlignment_);
    j["sortingOrder"] = sortingOrder_;
}

void UILabel::Deserialize(const nlohmann::json& j) {
    const int schemaVersion = j.value("schemaVersion", 1);
    // 표식은 payload에서만 온다. 저작 상태에서 유도하면 같은 파일을 두 번
    // 읽었을 때 서로 다른 형식으로 저장될 수 있다.
    loadedLegacyFontGuid_ = schemaVersion < 2;

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

    if (schemaVersion >= 2) {
        fontFamilyGuid_ = j.value("fontFamilyGuid", fontFamilyGuid_);
        SetLocale(j.value("locale", locale_));
        baseDirection_ = BaseDirectionFromToken(
            j.value("baseDirection", std::string(ToToken(baseDirection_))));
        wrap_ = WrapModeFromToken(j.value("wrap", std::string(ToToken(wrap_))));
        overflow_ = OverflowModeFromToken(
            j.value("overflow", std::string(ToToken(overflow_))));
        maxLines_ = j.value("maxLines", maxLines_);
    } else {
        // schema 1 payload를 읽는 것은 부분 갱신이 아니라 그 상태로 되돌리는
        // 일이다. 실행 취소는 기존 컴포넌트에 스냅샷을 다시 Deserialize하므로,
        // schema 2 전용 값을 남겨 두면 표식은 legacy인데 family view는 저작된
        // family를 가리키는 상태가 되어 저장 형식과 다른 폰트로 셰이핑된다.
        fontFamilyGuid_.clear();
        locale_ = "und";
        baseDirection_ = molga::text::BaseDirection::Auto;
        wrap_ = molga::text::TextWrapMode::NoWrap;
        overflow_ = molga::text::TextOverflowMode::Overflow;
        maxLines_ = 0;
    }
}
