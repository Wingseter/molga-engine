#include "Text/TextDiagnostic.h"

#include "Common/Log.h"
#include "Common/LogMessage.h"

#include <utility>

namespace molga::text {
namespace {

// 열거자를 벗어난 값(손상된 직렬화, 미래 빌드가 만든 값)에 쓰는 대체 코드.
// 어떤 안정 코드와도 겹치지 않으므로 역파서는 이 문자열을 인식하지 않는다.
constexpr const char* kUnknownStableCode = "TEXT_DIAGNOSTIC_UNKNOWN";

// 역파싱이 훑는 닫힌 코드 목록. 문자열은 StableTextDiagnosticCode 한 곳에만 있으므로
// 같은 코드의 정/역 문자열은 갈라질 수 없지만, 열거자 집합 자체는 여기서 한 번 더
// 나열된다. 코드를 추가할 때 switch와 이 목록을 함께 갱신해야 하고, 둘이 어긋나면
// test_text_runtime_dependencies의 "every declared code is reachable" 케이스가 잡는다.
constexpr TextDiagnosticCode kAllCodes[] = {
    TextDiagnosticCode::DependencyInvalid,
    TextDiagnosticCode::Utf8Invalid,
    TextDiagnosticCode::FontInvalid,
    TextDiagnosticCode::FontFamilyInvalid,
    TextDiagnosticCode::MissingGlyph,
    TextDiagnosticCode::AtlasExhausted,
    TextDiagnosticCode::LayoutInvalid,
    TextDiagnosticCode::LayoutCycle,
    TextDiagnosticCode::ReferenceInvalid,
    TextDiagnosticCode::TextInputUnavailable,
    TextDiagnosticCode::TextInputRangeClamped,
    TextDiagnosticCode::ReflowDeferred,
    TextDiagnosticCode::PackageValidationFailed,
};

// TextSeverity는 사람이 읽는 기존 logger 위에 얹히는 adapter다. Blocker에 정확히
// 대응하는 Log::Severity가 없으므로 가장 가까운 최상위 값 Fatal로 접는다.
Log::Severity ToLogSeverity(TextSeverity severity) noexcept {
    switch (severity) {
        case TextSeverity::Info:    return Log::Severity::Info;
        case TextSeverity::Warning: return Log::Severity::Warning;
        case TextSeverity::Error:   return Log::Severity::Error;
        case TextSeverity::Blocker: return Log::Severity::Fatal;
    }
    return Log::Severity::Error;
}

// 안정 코드와 진단 문맥 전부를 한 줄에 담는다. 구조화 console이 생기기 전까지는
// 이 문자열이 유일한 사용자 노출 경로이므로 어떤 필드도 생략하지 않는다.
std::string HumanReadableMessage(const TextDiagnostic& d) {
    std::string text = std::string(StableTextDiagnosticCode(d.code)) + ": " + d.message;
    text += " [subsystem=" + d.subsystem;
    text += " remediation=" + d.remediation;
    text += " assetGuid=" + d.assetGuid;
    text += " sceneObjectId=" + std::to_string(d.sceneObjectId);
    text += " componentType=" + d.componentType;
    text += " sourceByteRange=" + std::to_string(d.sourceByteRange.begin) + ":" +
            std::to_string(d.sourceByteRange.end);
    text += "]";
    return text;
}

} // namespace

const char* StableTextDiagnosticCode(TextDiagnosticCode code) noexcept {
    switch (code) {
        case TextDiagnosticCode::DependencyInvalid:     return "TEXT_DEPENDENCY_INVALID";
        case TextDiagnosticCode::Utf8Invalid:           return "TEXT_UTF8_INVALID";
        case TextDiagnosticCode::FontInvalid:           return "TEXT_FONT_INVALID";
        case TextDiagnosticCode::FontFamilyInvalid:     return "TEXT_FONT_FAMILY_INVALID";
        case TextDiagnosticCode::MissingGlyph:          return "TEXT_MISSING_GLYPH";
        case TextDiagnosticCode::AtlasExhausted:        return "TEXT_ATLAS_EXHAUSTED";
        case TextDiagnosticCode::LayoutInvalid:         return "UI_LAYOUT_INVALID";
        case TextDiagnosticCode::LayoutCycle:           return "UI_LAYOUT_CYCLE";
        case TextDiagnosticCode::ReferenceInvalid:      return "UI_REFERENCE_INVALID";
        case TextDiagnosticCode::TextInputUnavailable:  return "TEXT_INPUT_UNAVAILABLE";
        case TextDiagnosticCode::TextInputRangeClamped: return "TEXT_INPUT_RANGE_CLAMPED";
        case TextDiagnosticCode::ReflowDeferred:        return "UI_REFLOW_DEFERRED";
        case TextDiagnosticCode::PackageValidationFailed: return "PACKAGE_VALIDATION_FAILED";
    }
    return kUnknownStableCode;
}

std::optional<TextDiagnosticCode> ParseStableTextDiagnosticCode(
    std::string_view stable) noexcept {
    for (const TextDiagnosticCode code : kAllCodes) {
        if (stable == StableTextDiagnosticCode(code)) return code;
    }
    return std::nullopt;
}

std::string TextDiagnosticRateLimitKey(const TextDiagnostic& d) {
    return std::string(StableTextDiagnosticCode(d.code)) + "\x1f" + d.assetGuid +
        "\x1f" + std::to_string(d.sceneObjectId) + "\x1f" + d.componentType +
        "\x1f" + std::to_string(d.sourceByteRange.begin) + ":" +
        std::to_string(d.sourceByteRange.end);
}

void VectorTextDiagnosticSink::Report(TextDiagnostic diagnostic) {
    diagnostics_.push_back(std::move(diagnostic));
}

const std::vector<TextDiagnostic>& VectorTextDiagnosticSink::Diagnostics() const noexcept {
    return diagnostics_;
}

LoggerTextDiagnosticSink::LoggerTextDiagnosticSink(std::size_t maxRememberedKeys)
    : maxRememberedKeys_(maxRememberedKeys) {}

void LoggerTextDiagnosticSink::Report(TextDiagnostic diagnostic) {
    if (maxRememberedKeys_ > 0) {
        std::string key = TextDiagnosticRateLimitKey(diagnostic);
        // 지금 기억 중인 동일 key만 억제한다. 밀려난 key는 다시 보고될 수 있다.
        if (rememberedKeys_.find(key) != rememberedKeys_.end()) return;
        while (insertionOrder_.size() >= maxRememberedKeys_) {
            rememberedKeys_.erase(insertionOrder_.front());
            insertionOrder_.pop_front();
        }
        insertionOrder_.push_back(key);
        rememberedKeys_.insert(std::move(key));
    }

    Log::LogMessage m;
    m.severity = ToLogSeverity(diagnostic.severity);
    m.context  = Log::LogContext::Runtime;
    m.category = diagnostic.subsystem;
    m.message  = HumanReadableMessage(diagnostic);
    Log::Emit(m);
}

void LoggerTextDiagnosticSink::ResetRateLimit() {
    insertionOrder_.clear();
    rememberedKeys_.clear();
}

} // namespace molga::text
