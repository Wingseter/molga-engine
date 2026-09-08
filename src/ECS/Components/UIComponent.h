#pragma once

#include "ECS/Component.h"
#include "ECS/SceneObjectRef.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutTypes.h"
#include "Text/UnicodeAnalysis.h"
#include "UI/UIRuntimeInvalidation.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

// ── 저작 스키마의 실패 경계 ──────────────────────────────────────────────────
// virtual void Component::Deserialize(const nlohmann::json&)는 실패를 돌려줄
// 자리가 없다. bool 오버로드를 새로 만들면 재정의를 하나만 빠뜨려도 그 컴포넌트가
// 조용히 옛 경로로 돌아가므로, 실패는 이 typed 예외 하나로만 나간다. 코드는 이미
// 닫힌 진단 집합에서 고른다 — 이 태스크는 열넷 번째 코드를 만들지 않는다.
class UIComponentSchemaError : public std::runtime_error {
public:
    explicit UIComponentSchemaError(
        molga::text::TextDiagnosticCode code,
        const std::string& message = "UI component schema error")
        : std::runtime_error(message), code_(code) {}

    molga::text::TextDiagnosticCode Code() const noexcept { return code_; }

private:
    molga::text::TextDiagnosticCode code_;
};

[[noreturn]] inline void ThrowUILayoutInvalid(const std::string& what) {
    throw UIComponentSchemaError(
        molga::text::TextDiagnosticCode::LayoutInvalid,
        "UI authoring value rejected: " + what);
}

// 저작된 float은 언제나 유한해야 한다. NaN/Inf는 나중 26.6 변환에서 조용히
// 0이나 포화값이 되므로 여기서 잡지 않으면 잘못된 배치가 정상으로 보인다.
inline float RequireFiniteUIValue(float value, const char* what) {
    if (!std::isfinite(value)) ThrowUILayoutInvalid(what);
    return value;
}

// -0.0은 +0.0과 값은 같지만 비트가 달라, 같은 저작 상태가 두 개의 캐시 정체성을
// 만든다. 저장 직전에 한 번만 정규화한다.
inline float NormalizeUISignedZero(float value) noexcept {
    return value == 0.0f ? 0.0f : value;
}

// 읽어 들인 payload가 어느 형식이었는가. 런타임 전용 표식이며 직렬화되지 않고,
// 저장 모양만 결정한다. 저작 상태에서 유도하면 같은 파일을 두 번 읽었을 때
// 서로 다른 형식으로 저장될 수 있다.
enum class LoadedSchema : std::uint8_t { Legacy, Current };

// ── 무효화 ───────────────────────────────────────────────────────────────────
enum class UIInvalidation : std::uint8_t {
    Visual = 1u << 0, Intrinsic = 1u << 1, Layout = 1u << 2,
    Hierarchy = 1u << 3, Interaction = 1u << 4
};
constexpr std::uint8_t UIAllInvalidationBits =
    static_cast<std::uint8_t>(UIInvalidation::Visual) |
    static_cast<std::uint8_t>(UIInvalidation::Intrinsic) |
    static_cast<std::uint8_t>(UIInvalidation::Layout) |
    static_cast<std::uint8_t>(UIInvalidation::Hierarchy) |
    static_cast<std::uint8_t>(UIInvalidation::Interaction);

// 저작된 UI 컴포넌트의 공통 바닥. revision/dirtyMask/cacheable은 전부 런타임
// 전용이다 — 직렬화되지도, prefab override에 들어가지도, 에디터 dirty 판정에
// 쓰이지도 않는다.
class UIComponent : public Component {
public:
    std::uint64_t AuthoredRevision() const noexcept { return revision_; }
    std::uint8_t DirtyMask() const noexcept { return dirtyMask_; }
    bool RevisionCacheable() const noexcept { return revisionCacheable_; }
    void ClearDirtyMask() noexcept { dirtyMask_ = 0; }

    // enable/disable은 저작 상태이고 직렬화되지만(SceneSerializer), 기본 구현은
    // revision도 의미 세대도 건드리지 않는다. 그런데 배치는 모든 것을
    // IsEnabled()로 거른다 — EnabledComponent<T>, IsCanvasRoot,
    // ObjectIsInteractionEligible. 그래서 컴포넌트 하나를 끄면 저작 상태가
    // 실제로 달라졌는데도 빠른 경로의 도장은 그대로고, 낡은 스냅샷이 영원히
    // 돌아온다. enabled 값은 canonicalAuthoredPayload에도 들어 있지 않으므로
    // 기하 키조차 두 상태를 구분하지 못한다.
    //
    // 켜고 끄는 것은 이 컴포넌트가 배치·렌더·히트에 참여하는지를 통째로 바꾸는
    // 일이라 모든 축을 무효화한다. revision은 한 번만 올린다 — 축마다 한 번씩
    // 올리면 같은 한 번의 저작이 다섯 개의 서로 다른 정체성을 만든다.
    void SetEnabled(bool value) override {
        if (IsEnabled() == value) return;
        Component::SetEnabled(value);
        Invalidate(static_cast<UIInvalidation>(UIAllInvalidationBits));
    }

#ifdef MOLGA_UI_COMPONENT_TESTING
    // 테스트 타깃에서만 컴파일된다. 데이터 멤버도 가상 함수도 늘리지 않으므로
    // 이 헤더를 매크로 없이 본 molga_core의 레이아웃과 동일하다. 소진 경계는
    // 2^64번 저작하지 않고서는 다른 방법으로 재현할 수 없다.
    void SetAuthoredRevisionForTesting(std::uint64_t value) noexcept {
        revision_ = value;
    }
#endif

protected:
    void Invalidate(UIInvalidation reason) noexcept {
        const bool aggregateCacheable =
            molga::ui::UIRuntimeInvalidationClock::Advance(
                molga::ui::UIRuntimeGenerationKind::SemanticDirty)
                .has_value();
        if (revision_ == std::numeric_limits<std::uint64_t>::max() ||
            !aggregateCacheable) {
            // 소진되면 그 자리에 멈춘다. 감아서 재사용하면 옛 캐시 항목이 새
            // 상태와 같은 정체성을 갖는다. 대신 모든 비트를 세워, 이 컴포넌트를
            // 읽는 쪽이 캐시 없이 전부 다시 만들도록 강제한다.
            revisionCacheable_ = false;
            dirtyMask_ = UIAllInvalidationBits;
            return;
        }
        ++revision_;
        dirtyMask_ |= static_cast<std::uint8_t>(reason);
    }

private:
    std::uint64_t revision_ = 1;
    std::uint8_t dirtyMask_ = 0;
    bool revisionCacheable_ = true;
};

// ── 공유 레이아웃 저작 값 ────────────────────────────────────────────────────
// 계산된 상태가 없는 순수 저작 기록이다. 측정된 크기나 확정된 rect는 여기
// 들어오지 않는다 — 그것들은 스냅샷의 것이다.
struct UIAxisConstraint { float minimum = 0; float preferred = 0; float flexible = 0; };

enum class UILayoutMode : std::uint8_t { Horizontal, Vertical, Grid };
enum class UIGridConstraint : std::uint8_t { Flexible, FixedColumns, FixedRows };
enum class UIFitMode : std::uint8_t { Unconstrained, Min, Preferred };
enum class UIScrollMovement : std::uint8_t { Clamped, Elastic };

// 정수 서수가 아니라 토큰으로 저장한다. 서수로 저장하면 열거 가운데에 값이
// 하나 끼어드는 순간 디스크의 기존 scene이 다른 정책을 뜻하게 된다.
//
// 반환형은 const char*가 아니라 string_view다. const char*였다면 호출부의
// `ToCanonicalString(x) == "Any"`가 문자열 비교가 아니라 포인터 비교가 되고,
// 그것은 두 번역 단위의 같은 리터럴이 링커에서 하나로 합쳐질 때만 통과한다 —
// ASan은 전역마다 redzone을 붙여 그 합침을 없애므로 같은 소스가 그 구성에서만
// 실패한다.
inline std::string_view ToCanonicalString(UILayoutMode value) noexcept {
    switch (value) {
        case UILayoutMode::Vertical: return "Vertical";
        case UILayoutMode::Grid: return "Grid";
        case UILayoutMode::Horizontal: break;
    }
    return "Horizontal";
}
inline std::optional<UILayoutMode> ParseUILayoutMode(const std::string& token) {
    if (token == "Horizontal") return UILayoutMode::Horizontal;
    if (token == "Vertical") return UILayoutMode::Vertical;
    if (token == "Grid") return UILayoutMode::Grid;
    return std::nullopt;
}

inline std::string_view ToCanonicalString(UIGridConstraint value) noexcept {
    switch (value) {
        case UIGridConstraint::FixedColumns: return "FixedColumns";
        case UIGridConstraint::FixedRows: return "FixedRows";
        case UIGridConstraint::Flexible: break;
    }
    return "Flexible";
}
inline std::optional<UIGridConstraint> ParseUIGridConstraint(
    const std::string& token) {
    if (token == "Flexible") return UIGridConstraint::Flexible;
    if (token == "FixedColumns") return UIGridConstraint::FixedColumns;
    if (token == "FixedRows") return UIGridConstraint::FixedRows;
    return std::nullopt;
}

inline std::string_view ToCanonicalString(UIFitMode value) noexcept {
    switch (value) {
        case UIFitMode::Min: return "Min";
        case UIFitMode::Preferred: return "Preferred";
        case UIFitMode::Unconstrained: break;
    }
    return "Unconstrained";
}
inline std::optional<UIFitMode> ParseUIFitMode(const std::string& token) {
    if (token == "Unconstrained") return UIFitMode::Unconstrained;
    if (token == "Min") return UIFitMode::Min;
    if (token == "Preferred") return UIFitMode::Preferred;
    return std::nullopt;
}

inline std::string_view ToCanonicalString(UIScrollMovement value) noexcept {
    switch (value) {
        case UIScrollMovement::Elastic: return "Elastic";
        case UIScrollMovement::Clamped: break;
    }
    return "Clamped";
}
inline std::optional<UIScrollMovement> ParseUIScrollMovement(
    const std::string& token) {
    if (token == "Clamped") return UIScrollMovement::Clamped;
    if (token == "Elastic") return UIScrollMovement::Elastic;
    return std::nullopt;
}

// ── 공유 문단 정책 토큰 ──────────────────────────────────────────────────────
// 설계 7.3이 문서화한 토큰 그대로다. UILabel이 이미 디스크에 쓰고 있는 철자와
// 한 글자도 달라서는 안 된다 — 같은 정책이 컴포넌트마다 다른 문자열로 남으면
// 문서를 사람이 읽을 수도, 도구가 비교할 수도 없다.
inline std::string_view ToCanonicalString(molga::text::BaseDirection value) noexcept {
    switch (value) {
        case molga::text::BaseDirection::LeftToRight: return "LTR";
        case molga::text::BaseDirection::RightToLeft: return "RTL";
        case molga::text::BaseDirection::Auto: break;
    }
    return "Auto";
}
inline std::optional<molga::text::BaseDirection> ParseUIBaseDirection(
    const std::string& token) {
    if (token == "Auto") return molga::text::BaseDirection::Auto;
    if (token == "LTR") return molga::text::BaseDirection::LeftToRight;
    if (token == "RTL") return molga::text::BaseDirection::RightToLeft;
    return std::nullopt;
}

inline std::string_view ToCanonicalString(molga::text::TextWrapMode value) noexcept {
    switch (value) {
        case molga::text::TextWrapMode::Word: return "Word";
        case molga::text::TextWrapMode::Grapheme: return "Grapheme";
        case molga::text::TextWrapMode::NoWrap: break;
    }
    return "NoWrap";
}
inline std::optional<molga::text::TextWrapMode> ParseUIWrapMode(
    const std::string& token) {
    if (token == "NoWrap") return molga::text::TextWrapMode::NoWrap;
    if (token == "Word") return molga::text::TextWrapMode::Word;
    if (token == "Grapheme") return molga::text::TextWrapMode::Grapheme;
    return std::nullopt;
}

inline std::string_view ToCanonicalString(
    molga::text::TextOverflowMode value) noexcept {
    switch (value) {
        case molga::text::TextOverflowMode::Clip: return "Clip";
        case molga::text::TextOverflowMode::Ellipsis: return "Ellipsis";
        case molga::text::TextOverflowMode::Overflow: break;
    }
    return "Overflow";
}
inline std::optional<molga::text::TextOverflowMode> ParseUIOverflowMode(
    const std::string& token) {
    if (token == "Overflow") return molga::text::TextOverflowMode::Overflow;
    if (token == "Clip") return molga::text::TextOverflowMode::Clip;
    if (token == "Ellipsis") return molga::text::TextOverflowMode::Ellipsis;
    return std::nullopt;
}

inline std::string_view ToCanonicalString(
    molga::text::TextHorizontalAlignment value) noexcept {
    switch (value) {
        case molga::text::TextHorizontalAlignment::Center: return "Center";
        case molga::text::TextHorizontalAlignment::Right: return "Right";
        case molga::text::TextHorizontalAlignment::Left: break;
    }
    return "Left";
}
inline std::optional<molga::text::TextHorizontalAlignment>
ParseUIHorizontalAlignment(const std::string& token) {
    if (token == "Left") return molga::text::TextHorizontalAlignment::Left;
    if (token == "Center") return molga::text::TextHorizontalAlignment::Center;
    if (token == "Right") return molga::text::TextHorizontalAlignment::Right;
    return std::nullopt;
}

inline std::string_view ToCanonicalString(
    molga::text::TextVerticalAlignment value) noexcept {
    switch (value) {
        case molga::text::TextVerticalAlignment::Middle: return "Middle";
        case molga::text::TextVerticalAlignment::Bottom: return "Bottom";
        case molga::text::TextVerticalAlignment::Top: break;
    }
    return "Top";
}
inline std::optional<molga::text::TextVerticalAlignment>
ParseUIVerticalAlignment(const std::string& token) {
    if (token == "Top") return molga::text::TextVerticalAlignment::Top;
    if (token == "Middle") return molga::text::TextVerticalAlignment::Middle;
    if (token == "Bottom") return molga::text::TextVerticalAlignment::Bottom;
    return std::nullopt;
}

// ── payload 읽기 헬퍼 ────────────────────────────────────────────────────────
// 키가 없으면 명시적인 migration 기본값이다. 있으면 정규 문자열이어야 하며,
// 숫자 서수를 포함해 그 밖의 무엇도 암묵 대체가 아니라 typed 실패다.
template <typename Enum, typename ParseFn>
Enum ReadCanonicalEnum(const nlohmann::json& j, const char* key, Enum fallback,
                       ParseFn parse) {
    const auto found = j.find(key);
    if (found == j.end()) return fallback;
    if (!found->is_string()) {
        ThrowUILayoutInvalid(std::string(key) + " must be a canonical string");
    }
    const auto parsed = parse(found->get<std::string>());
    if (!parsed) {
        ThrowUILayoutInvalid(std::string(key) + " has no such value: " +
                             found->get<std::string>());
    }
    return *parsed;
}

inline float ReadUIFloat(const nlohmann::json& j, const char* key,
                         float fallback) {
    const auto found = j.find(key);
    if (found == j.end()) return fallback;
    if (!found->is_number()) {
        ThrowUILayoutInvalid(std::string(key) + " must be a number");
    }
    return found->get<float>();
}

inline bool ReadUIBool(const nlohmann::json& j, const char* key,
                       bool fallback) {
    const auto found = j.find(key);
    if (found == j.end()) return fallback;
    if (!found->is_boolean()) {
        ThrowUILayoutInvalid(std::string(key) + " must be a boolean");
    }
    return found->get<bool>();
}

// is_number_unsigned()는 부호가 아니라 저장 형식을 묻는 술어다. nlohmann은
// C++ int를 대입하면 number_integer로 담고 텍스트에서 파싱한 양의 정수만
// number_unsigned로 담으므로, 그것으로 걸렀다면 인스펙터가 std::int64_t로
// 만들어 넣는 완전히 유효한 값이 전부 거부된다. 값으로 판정한다.
//
// 상한도 함께 본다. 잘라 담으면 maxGraphemes는 "제한 없음"이 되고 targetId는
// "설정 안 됨"이 되는데, 둘 다 조용한 fail-open이다.
inline std::uint32_t ReadUIUInt(const nlohmann::json& j, const char* key,
                                std::uint32_t fallback) {
    const auto found = j.find(key);
    if (found == j.end()) return fallback;
    if (!found->is_number_integer()) {
        ThrowUILayoutInvalid(std::string(key) + " must be a non-negative integer");
    }
    const auto value = found->get<std::int64_t>();
    if (value < 0 ||
        value > static_cast<std::int64_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
        ThrowUILayoutInvalid(std::string(key) + " must be a non-negative integer");
    }
    return static_cast<std::uint32_t>(value);
}

inline std::string ReadUIString(const nlohmann::json& j, const char* key,
                                const std::string& fallback) {
    const auto found = j.find(key);
    if (found == j.end()) return fallback;
    if (!found->is_string()) {
        ThrowUILayoutInvalid(std::string(key) + " must be a string");
    }
    return found->get<std::string>();
}

// 씬 참조는 언제나 {"targetId": n} 한 모양으로만 저장된다. 포인터도, 이름도,
// 해석된 런타임 핸들도 파일에 남지 않는다 — 그런 값은 프로세스마다 다르다.
inline nlohmann::json UIObjectRefJson(SceneObjectRef ref) {
    nlohmann::json out = nlohmann::json::object();
    out["targetId"] = ref.targetId;
    return out;
}

inline SceneObjectRef ReadUIObjectRef(const nlohmann::json& j,
                                      const char* key) {
    const auto found = j.find(key);
    if (found == j.end()) return SceneObjectRef{};
    if (!found->is_object()) {
        ThrowUILayoutInvalid(std::string(key) + " must be a {targetId} object");
    }
    SceneObjectRef ref;
    ref.targetId = ReadUIUInt(*found, "targetId", 0u);
    return ref;
}
