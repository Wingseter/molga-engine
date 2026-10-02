#pragma once

#include "Common/Types.h"
#include "ECS/Components/UIComponent.h"
#include "Text/TextLayoutTypes.h"
#include "Text/UnicodeAnalysis.h"

#include <cstdint>
#include <string>
#include <unordered_map>

// ── Milestone A 정책 표면 ────────────────────────────────────────────────────
// 값이 하나뿐인 것은 미완성이 아니라 계약이다. 숫자 정책이나 OnBlur 제출은
// 승인된 설계가 아직 정의하지 않았으므로, 그런 값을 담은 문서는 암묵 대체가
// 아니라 typed 실패로 거절된다 — 대체하면 저작자가 요청한 적 없는 정책으로
// 동작하는 입력창이 조용히 만들어진다.
enum class UITextInputContentPolicy : std::uint8_t { Any };
enum class UITextInputSubmitPolicy : std::uint8_t { OnEnter };

std::string_view ToCanonicalString(UITextInputContentPolicy value) noexcept;
std::optional<UITextInputContentPolicy> ParseUITextInputContentPolicy(
    const std::string&);
std::string_view ToCanonicalString(UITextInputSubmitPolicy value) noexcept;
std::optional<UITextInputSubmitPolicy> ParseUITextInputSubmitPolicy(
    const std::string&);

// 입력창이 저작하는 문단 스타일 전부. UILabel schema 2가 쓰는 것과 같은 정책
// 열거를 그대로 소비하므로, 같은 문단 정책이 두 컴포넌트에서 같은 토큰으로
// 남는다. 측정된 크기나 확정된 줄은 여기 없다.
struct UIAuthoredParagraphStyle {
    float fontSizePx = 24.0f;
    float lineSpacing = 1.25f;
    Color color = Color::White();
    std::string locale = "und";
    molga::text::BaseDirection baseDirection = molga::text::BaseDirection::Auto;
    molga::text::TextWrapMode wrap = molga::text::TextWrapMode::NoWrap;
    molga::text::TextOverflowMode overflow =
        molga::text::TextOverflowMode::Overflow;
    std::uint32_t maxLines = 0;
    molga::text::TextHorizontalAlignment horizontal =
        molga::text::TextHorizontalAlignment::Left;
    molga::text::TextVerticalAlignment vertical =
        molga::text::TextVerticalAlignment::Top;
};

// 편집 가능한 텍스트 입력의 저작 상태.
//
// caret/selection/composition/blink/현재 값은 하나도 여기 없고, 저장되지도
// 않는다. 그런 키가 payload에 섞여 들어와도 무시되며 되쓰이지 않는다 —
// 실행 취소가 스냅샷을 다시 읽힐 때 죽은 편집 상태가 되살아나면, 사용자가
// 지운 글자가 문서에 남는다.
class UITextInput : public UIComponent {
public:
    COMPONENT_TYPE(UITextInput)

    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    const std::string& InitialText() const noexcept { return initialText_; }
    bool ReadOnly() const noexcept { return readOnly_; }
    bool Multiline() const noexcept { return multiline_; }
    std::uint32_t MaxGraphemes() const noexcept { return maxGraphemes_; }
    UITextInputContentPolicy ContentPolicy() const noexcept {
        return contentPolicy_;
    }
    UITextInputSubmitPolicy SubmitPolicy() const noexcept {
        return submitPolicy_;
    }
    const std::string& FontFamilyGuid() const noexcept {
        return fontFamilyGuid_;
    }
    const UIAuthoredParagraphStyle& ParagraphStyle() const noexcept {
        return paragraphStyle_;
    }
    SceneObjectRef TextViewport() const noexcept { return textViewport_; }
    SceneObjectRef RenderedLabel() const noexcept { return renderedLabel_; }
    SceneObjectRef PlaceholderLabel() const noexcept {
        return placeholderLabel_;
    }

    void SetInitialText(std::string value);
    void SetReadOnly(bool value);
    void SetMultiline(bool value);
    void SetMaxGraphemes(std::uint32_t value);
    void SetContentPolicy(UITextInputContentPolicy value);
    void SetSubmitPolicy(UITextInputSubmitPolicy value);
    void SetFontFamilyGuid(std::string value);
    void SetParagraphStyle(const UIAuthoredParagraphStyle& value);
    void SetTextViewport(SceneObjectRef value);
    void SetRenderedLabel(SceneObjectRef value);
    void SetPlaceholderLabel(SceneObjectRef value);

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void RemapReferences(
        const std::unordered_map<unsigned int, unsigned int>& idRemap) override;

private:
    std::string initialText_;
    bool readOnly_ = false;
    bool multiline_ = false;
    std::uint32_t maxGraphemes_ = 0;
    UITextInputContentPolicy contentPolicy_ = UITextInputContentPolicy::Any;
    UITextInputSubmitPolicy submitPolicy_ = UITextInputSubmitPolicy::OnEnter;
    std::string fontFamilyGuid_;
    UIAuthoredParagraphStyle paragraphStyle_;
    SceneObjectRef textViewport_;
    SceneObjectRef renderedLabel_;
    SceneObjectRef placeholderLabel_;
};
