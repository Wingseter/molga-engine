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

// 저작 setter는 정규값이 실제로 달라졌을 때만 무효화한다. 같은 값을 다시
// 쓰는 것(에디터가 매 프레임 인스펙터 값을 되쓰는 경로)이 캐시를 무너뜨리면
// 안 되기 때문이다.
void UILabel::SetText(std::string value) {
    if (text_ == value) return;
    text_ = std::move(value);
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UILabel::SetFontGuid(std::string value) {
    if (fontGuid_ == value) return;
    fontGuid_ = std::move(value);
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UILabel::SetFontSizePx(float value) {
    // std::clamp는 NaN을 그대로 통과시킨다. 저장된 NaN은 자기 자신과 같지
    // 않으므로 이후 모든 대입이 무효화를 일으켜 이 라벨의 스냅샷 빠른 경로를
    // 영구히 무너뜨리고, Serialize는 JSON null을 써 다음 로드를 nlohmann의
    // type_error로 날린다. 자르기는 레거시 계약 그대로 두고 유한성만 건다.
    const float canonical =
        std::clamp(RequireFiniteUIValue(value, "fontSizePx"), 1.0f, 512.0f);
    if (fontSizePx_ == canonical) return;
    fontSizePx_ = canonical;
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}
void UILabel::SetLineSpacing(float value) {
    const float canonical =
        std::clamp(RequireFiniteUIValue(value, "lineSpacing"), 0.1f, 10.0f);
    if (lineSpacing_ == canonical) return;
    lineSpacing_ = canonical;
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UILabel::SetColor(const Color& value) {
    if (color_ == value) return;
    color_ = value;
    // 색은 배치를 바꾸지 않는다. 가장 좁은 비트 하나만 세운다.
    Invalidate(UIInvalidation::Visual);
}

void UILabel::SetBaseDirection(molga::text::BaseDirection value) {
    if (baseDirection_ == value) return;
    baseDirection_ = value;
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UILabel::SetWrapMode(molga::text::TextWrapMode value) {
    if (wrap_ == value) return;
    wrap_ = value;
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UILabel::SetOverflowMode(molga::text::TextOverflowMode value) {
    if (overflow_ == value) return;
    overflow_ = value;
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UILabel::SetMaxLines(std::uint32_t value) {
    if (maxLines_ == value) return;
    maxLines_ = value;
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UILabel::SetHorizontalAlignment(HorizontalAlignment value) {
    if (horizontalAlignment_ == value) return;
    horizontalAlignment_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UILabel::SetVerticalAlignment(VerticalAlignment value) {
    if (verticalAlignment_ == value) return;
    verticalAlignment_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UILabel::SetSortingOrder(int value) {
    if (sortingOrder_ == value) return;
    sortingOrder_ = value;
    Invalidate(UIInvalidation::Visual);
}

void UILabel::SetLocale(std::string value) {
    // 빈 태그는 "locale 없음"이 아니라 root tailoring 요청이다. 빈 문자열을 그대로
    // 두면 분석 계층이 잘못된 태그로 실패한다.
    std::string canonical =
        value.empty() ? std::string("und") : std::move(value);
    if (locale_ == canonical) return;
    locale_ = std::move(canonical);
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UILabel::SetFontFamilyGuid(std::string value) {
    const bool authoredFamily = !value.empty();
    const bool changed = fontFamilyGuid_ != value;
    fontFamilyGuid_ = std::move(value);
    // schema 2에는 fontGuid 키가 없다. family를 저작하는 것이 legacy 표현을
    // 대체할 값을 공급하는 유일한 행위이므로, 이때만 legacy 표식을 내려놓는다.
    // 그러지 않으면 저작한 family가 저장에서 조용히 사라진다.
    if (authoredFamily) {
        loadedLegacyFontGuid_ = false;
    }
    if (!changed) return;
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
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

    j["schemaVersion"] = CurrentSchemaVersion;
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
    //
    // Serialize와 같은 상수를 본다. 한쪽만 리터럴로 두면 CurrentSchemaVersion을
    // 올릴 때 쓰는 쪽과 legacy 판정이 컴파일 오류도 시험 실패도 없이 어긋나고,
    // 그 표식이 바이트 호환되지 않는 두 payload 모양 중 어느 것을 쓸지 정한다.
    loadedLegacyFontGuid_ =
        schemaVersion < static_cast<int>(CurrentSchemaVersion);

    SetText(j.value("text", text_));
    SetFontGuid(j.value("fontGuid", fontGuid_));
    SetFontSizePx(j.value("fontSizePx", fontSizePx_));
    SetLineSpacing(j.value("lineSpacing", lineSpacing_));
    if (j.contains("color") && j["color"].is_array() && j["color"].size() >= 4) {
        SetColor({j["color"][0].get<float>(), j["color"][1].get<float>(),
                  j["color"][2].get<float>(), j["color"][3].get<float>()});
    }
    SetHorizontalAlignment(static_cast<HorizontalAlignment>(
        std::clamp(j.value("horizontalAlignment", static_cast<int>(horizontalAlignment_)), 0, 2)));
    SetVerticalAlignment(static_cast<VerticalAlignment>(
        std::clamp(j.value("verticalAlignment", static_cast<int>(verticalAlignment_)), 0, 2)));
    SetSortingOrder(j.value("sortingOrder", sortingOrder_));

    if (schemaVersion >= static_cast<int>(CurrentSchemaVersion)) {
        // SetFontFamilyGuid는 legacy 표식을 내려놓는다. 여기서는 표식이 이미
        // payload에서 정해졌으므로 필드에 직접 넣고 무효화만 따로 건다.
        const std::string family = j.value("fontFamilyGuid", fontFamilyGuid_);
        if (fontFamilyGuid_ != family) {
            fontFamilyGuid_ = family;
            Invalidate(UIInvalidation::Visual);
            Invalidate(UIInvalidation::Intrinsic);
        }
        SetLocale(j.value("locale", locale_));
        SetBaseDirection(BaseDirectionFromToken(
            j.value("baseDirection", std::string(ToToken(baseDirection_)))));
        SetWrapMode(WrapModeFromToken(j.value("wrap", std::string(ToToken(wrap_)))));
        SetOverflowMode(OverflowModeFromToken(
            j.value("overflow", std::string(ToToken(overflow_)))));
        SetMaxLines(j.value("maxLines", maxLines_));
    } else {
        // schema 1 payload를 읽는 것은 부분 갱신이 아니라 그 상태로 되돌리는
        // 일이다. 실행 취소는 기존 컴포넌트에 스냅샷을 다시 Deserialize하므로,
        // schema 2 전용 값을 남겨 두면 표식은 legacy인데 family view는 저작된
        // family를 가리키는 상태가 되어 저장 형식과 다른 폰트로 셰이핑된다.
        if (!fontFamilyGuid_.empty()) {
            fontFamilyGuid_.clear();
            Invalidate(UIInvalidation::Visual);
            Invalidate(UIInvalidation::Intrinsic);
        }
        SetLocale("und");
        SetBaseDirection(molga::text::BaseDirection::Auto);
        SetWrapMode(molga::text::TextWrapMode::NoWrap);
        SetOverflowMode(molga::text::TextOverflowMode::Overflow);
        SetMaxLines(0);
    }
}
