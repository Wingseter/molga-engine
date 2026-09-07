#include "ECS/Components/UITextInput.h"

#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"

#include <algorithm>

REGISTER_COMPONENT(UITextInput)

namespace {

bool Same(const UIAuthoredParagraphStyle& lhs,
          const UIAuthoredParagraphStyle& rhs) noexcept {
    return lhs.fontSizePx == rhs.fontSizePx &&
           lhs.lineSpacing == rhs.lineSpacing && lhs.color == rhs.color &&
           lhs.locale == rhs.locale &&
           lhs.baseDirection == rhs.baseDirection && lhs.wrap == rhs.wrap &&
           lhs.overflow == rhs.overflow && lhs.maxLines == rhs.maxLines &&
           lhs.horizontal == rhs.horizontal && lhs.vertical == rhs.vertical;
}

// UILabel과 같은 경계다. 여기서 자르지 않으면 문서 하나가 셰이퍼에 512px가
// 넘는 em을 요구해 아틀라스를 통째로 소진시킬 수 있다.
UIAuthoredParagraphStyle Canonicalize(const UIAuthoredParagraphStyle& value) {
    UIAuthoredParagraphStyle canonical = value;
    canonical.fontSizePx = std::clamp(
        RequireFiniteUIValue(value.fontSizePx, "paragraphStyle.fontSizePx"),
        1.0f, 512.0f);
    canonical.lineSpacing = std::clamp(
        RequireFiniteUIValue(value.lineSpacing, "paragraphStyle.lineSpacing"),
        0.1f, 10.0f);
    RequireFiniteUIValue(value.color.r, "paragraphStyle.color");
    RequireFiniteUIValue(value.color.g, "paragraphStyle.color");
    RequireFiniteUIValue(value.color.b, "paragraphStyle.color");
    RequireFiniteUIValue(value.color.a, "paragraphStyle.color");
    // 빈 태그는 "locale 없음"이 아니라 root tailoring 요청이다. 빈 문자열을
    // 그대로 두면 분석 계층이 잘못된 태그로 실패한다.
    if (canonical.locale.empty()) canonical.locale = "und";
    return canonical;
}

nlohmann::json ParagraphStyleJson(const UIAuthoredParagraphStyle& style) {
    nlohmann::json out = nlohmann::json::object();
    out["fontSizePx"] = style.fontSizePx;
    out["lineSpacing"] = style.lineSpacing;
    out["color"] = {style.color.r, style.color.g, style.color.b, style.color.a};
    out["locale"] = style.locale;
    out["baseDirection"] = ToCanonicalString(style.baseDirection);
    out["wrap"] = ToCanonicalString(style.wrap);
    out["overflow"] = ToCanonicalString(style.overflow);
    out["maxLines"] = style.maxLines;
    out["horizontalAlignment"] = ToCanonicalString(style.horizontal);
    out["verticalAlignment"] = ToCanonicalString(style.vertical);
    return out;
}

UIAuthoredParagraphStyle ReadParagraphStyle(const nlohmann::json& j) {
    UIAuthoredParagraphStyle style;
    const auto found = j.find("paragraphStyle");
    if (found == j.end()) return style;
    if (!found->is_object()) {
        ThrowUILayoutInvalid("paragraphStyle must be an object");
    }
    const nlohmann::json& source = *found;
    style.fontSizePx = ReadUIFloat(source, "fontSizePx", style.fontSizePx);
    style.lineSpacing = ReadUIFloat(source, "lineSpacing", style.lineSpacing);
    const auto color = source.find("color");
    if (color != source.end()) {
        // 인덱스가 아니라 범위 for로 읽는다. nlohmann의 const
        // operator[](size_type)는 std::vector의 것으로 넘어가 경계 검사를 하지
        // 않으므로, 길이 검사를 앞에 두는 모양이었다면 그 한 줄을 지우는 순간
        // 짧은 배열이 힙 밖을 읽고 쓰레기 float으로 "성공"한다. 여기서는 길이
        // 계약이 gathered 검사 한 곳에만 있고, 그것을 지워도 경계를 넘지 않는다.
        //
        // 원소 타입도 본다. bare get<float>()은 UIComponentSchemaError가 아니라
        // nlohmann::json::type_error를 던져 SceneSerializer의 typed 경계를 그대로
        // 지나치고, 씬 로드를 프로세스 밖으로 날린다.
        if (!color->is_array()) {
            ThrowUILayoutInvalid("paragraphStyle.color must be an RGBA array");
        }
        float channels[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        std::size_t gathered = 0;
        for (const nlohmann::json& channel : *color) {
            if (gathered >= 4) break;
            if (!channel.is_number()) {
                ThrowUILayoutInvalid(
                    "paragraphStyle.color must be an RGBA array");
            }
            channels[gathered++] = channel.get<float>();
        }
        if (gathered < 4) {
            ThrowUILayoutInvalid("paragraphStyle.color must be an RGBA array");
        }
        style.color = {channels[0], channels[1], channels[2], channels[3]};
    }
    style.locale = ReadUIString(source, "locale", style.locale);
    style.baseDirection = ReadCanonicalEnum(source, "baseDirection",
                                            style.baseDirection,
                                            ParseUIBaseDirection);
    style.wrap = ReadCanonicalEnum(source, "wrap", style.wrap, ParseUIWrapMode);
    style.overflow = ReadCanonicalEnum(source, "overflow", style.overflow,
                                       ParseUIOverflowMode);
    style.maxLines = ReadUIUInt(source, "maxLines", style.maxLines);
    style.horizontal = ReadCanonicalEnum(source, "horizontalAlignment",
                                         style.horizontal,
                                         ParseUIHorizontalAlignment);
    style.vertical = ReadCanonicalEnum(source, "verticalAlignment",
                                       style.vertical,
                                       ParseUIVerticalAlignment);
    return style;
}

} // namespace

std::string_view ToCanonicalString(UITextInputContentPolicy value) noexcept {
    switch (value) {
        case UITextInputContentPolicy::Any: break;
    }
    return "Any";
}

std::optional<UITextInputContentPolicy> ParseUITextInputContentPolicy(
    const std::string& token) {
    if (token == "Any") return UITextInputContentPolicy::Any;
    return std::nullopt;
}

std::string_view ToCanonicalString(UITextInputSubmitPolicy value) noexcept {
    switch (value) {
        case UITextInputSubmitPolicy::OnEnter: break;
    }
    return "OnEnter";
}

std::optional<UITextInputSubmitPolicy> ParseUITextInputSubmitPolicy(
    const std::string& token) {
    if (token == "OnEnter") return UITextInputSubmitPolicy::OnEnter;
    return std::nullopt;
}

void UITextInput::SetInitialText(std::string value) {
    if (initialText_ == value) return;
    initialText_ = std::move(value);
    // 초기 문자열은 그려지는 글자이자 고유 크기의 원천이다.
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UITextInput::SetReadOnly(bool value) {
    if (readOnly_ == value) return;
    readOnly_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UITextInput::SetMultiline(bool value) {
    if (multiline_ == value) return;
    multiline_ = value;
    // 줄 수가 달라지면 고유 크기가 달라진다.
    Invalidate(UIInvalidation::Intrinsic);
    Invalidate(UIInvalidation::Interaction);
}

void UITextInput::SetMaxGraphemes(std::uint32_t value) {
    if (maxGraphemes_ == value) return;
    maxGraphemes_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UITextInput::SetContentPolicy(UITextInputContentPolicy value) {
    if (contentPolicy_ == value) return;
    contentPolicy_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UITextInput::SetSubmitPolicy(UITextInputSubmitPolicy value) {
    if (submitPolicy_ == value) return;
    submitPolicy_ = value;
    Invalidate(UIInvalidation::Interaction);
}

void UITextInput::SetFontFamilyGuid(std::string value) {
    if (fontFamilyGuid_ == value) return;
    fontFamilyGuid_ = std::move(value);
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UITextInput::SetParagraphStyle(const UIAuthoredParagraphStyle& value) {
    const UIAuthoredParagraphStyle canonical = Canonicalize(value);
    if (Same(paragraphStyle_, canonical)) return;
    paragraphStyle_ = canonical;
    Invalidate(UIInvalidation::Visual);
    Invalidate(UIInvalidation::Intrinsic);
}

void UITextInput::SetTextViewport(SceneObjectRef value) {
    if (textViewport_ == value) return;
    textViewport_ = value;
    Invalidate(UIInvalidation::Layout);
}

void UITextInput::SetRenderedLabel(SceneObjectRef value) {
    if (renderedLabel_ == value) return;
    renderedLabel_ = value;
    Invalidate(UIInvalidation::Hierarchy);
}

void UITextInput::SetPlaceholderLabel(SceneObjectRef value) {
    if (placeholderLabel_ == value) return;
    placeholderLabel_ = value;
    Invalidate(UIInvalidation::Hierarchy);
}

void UITextInput::Serialize(nlohmann::json& j) const {
    j["schemaVersion"] = CurrentSchemaVersion;
    j["initialText"] = initialText_;
    j["readOnly"] = readOnly_;
    j["multiline"] = multiline_;
    j["maxGraphemes"] = maxGraphemes_;
    j["contentPolicy"] = ToCanonicalString(contentPolicy_);
    j["submitPolicy"] = ToCanonicalString(submitPolicy_);
    j["textViewport"] = UIObjectRefJson(textViewport_);
    j["renderedLabel"] = UIObjectRefJson(renderedLabel_);
    j["placeholderLabel"] = UIObjectRefJson(placeholderLabel_);
    j["fontFamilyGuid"] = fontFamilyGuid_;
    j["paragraphStyle"] = ParagraphStyleJson(paragraphStyle_);
}

void UITextInput::Deserialize(const nlohmann::json& j) {
    // 아는 키만 읽는다. caret/selection/composition/blink 같은 런타임 모양의
    // 키가 섞여 있어도 메모리에 들어오지 않으므로 다음 저장에서 되쓰이지 않는다.
    //
    // 그리고 payload 전체를 먼저 읽는다. 읽으면서 저장하면 뒤쪽 키가 거부됐을
    // 때 앞쪽만 바뀐 반쪽 상태가 남는데, 실행 취소는 살아 있는 컴포넌트에
    // 스냅샷을 다시 읽히므로 그 반쪽 상태가 그대로 문서에 저장된다.
    auto initialText = ReadUIString(j, "initialText", std::string{});
    const bool readOnly = ReadUIBool(j, "readOnly", false);
    const bool multiline = ReadUIBool(j, "multiline", false);
    const std::uint32_t maxGraphemes = ReadUIUInt(j, "maxGraphemes", 0u);
    // 키가 없는 승인된 레거시 payload는 여기서 명시적으로 현재 스키마의
    // 기본값을 얻는다. 값이 있는데 정규 문자열이 아니면 typed 실패다.
    const auto contentPolicy =
        ReadCanonicalEnum(j, "contentPolicy", UITextInputContentPolicy::Any,
                          ParseUITextInputContentPolicy);
    const auto submitPolicy =
        ReadCanonicalEnum(j, "submitPolicy", UITextInputSubmitPolicy::OnEnter,
                          ParseUITextInputSubmitPolicy);
    const SceneObjectRef textViewport = ReadUIObjectRef(j, "textViewport");
    const SceneObjectRef renderedLabel = ReadUIObjectRef(j, "renderedLabel");
    const SceneObjectRef placeholderLabel =
        ReadUIObjectRef(j, "placeholderLabel");
    auto fontFamilyGuid = ReadUIString(j, "fontFamilyGuid", std::string{});
    const UIAuthoredParagraphStyle paragraphStyle = ReadParagraphStyle(j);

    SetInitialText(std::move(initialText));
    SetReadOnly(readOnly);
    SetMultiline(multiline);
    SetMaxGraphemes(maxGraphemes);
    SetContentPolicy(contentPolicy);
    SetSubmitPolicy(submitPolicy);
    SetTextViewport(textViewport);
    SetRenderedLabel(renderedLabel);
    SetPlaceholderLabel(placeholderLabel);
    SetFontFamilyGuid(std::move(fontFamilyGuid));
    SetParagraphStyle(paragraphStyle);
}

void UITextInput::RemapReferences(
    const std::unordered_map<unsigned int, unsigned int>& idRemap) {
    textViewport_.Remap(idRemap);
    renderedLabel_.Remap(idRemap);
    placeholderLabel_.Remap(idRemap);
}
