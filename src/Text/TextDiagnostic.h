#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace molga::text {

// 텍스트/UI 계층의 닫힌 심각도와 안정 코드 집합. 두 enum 모두 외부 계약이므로
// 값 추가는 설계 개정을 거쳐야 하고, 문자열 매핑은 영구적으로 안정이다.
enum class TextSeverity : std::uint8_t { Info, Warning, Error, Blocker };
enum class TextDiagnosticCode : std::uint16_t {
    DependencyInvalid, Utf8Invalid, FontInvalid, FontFamilyInvalid,
    MissingGlyph, AtlasExhausted, LayoutInvalid, LayoutCycle,
    ReferenceInvalid, TextInputUnavailable, TextInputRangeClamped,
    ReflowDeferred, PackageValidationFailed,
};
const char* StableTextDiagnosticCode(TextDiagnosticCode) noexcept;
std::optional<TextDiagnosticCode> ParseStableTextDiagnosticCode(
    std::string_view) noexcept;

struct SourceByteRange {
    std::uint32_t begin = 0, end = 0;
    friend constexpr bool operator==(SourceByteRange a,
                                     SourceByteRange b) noexcept {
        return a.begin == b.begin && a.end == b.end;
    }
};
struct TextDiagnostic {
    TextDiagnosticCode code = TextDiagnosticCode::DependencyInvalid;
    TextSeverity severity = TextSeverity::Error;
    std::string subsystem, message, remediation, assetGuid;
    unsigned int sceneObjectId = 0;
    std::string componentType;
    SourceByteRange sourceByteRange;
};
std::string TextDiagnosticRateLimitKey(const TextDiagnostic&);
class TextDiagnosticSink {
public:
    virtual ~TextDiagnosticSink() = default;
    virtual void Report(TextDiagnostic diagnostic) = 0;
};

// 보고된 record를 순서대로 전부 보관하는 수집용 sink(중복 제거 없음).
// 스레드 계약: Log::ILogSink와 달리 "어느 thread에서도 안전"을 물려받지 않는다.
// 내부 동기화가 없으므로 하나의 thread에서만 Report()/Diagnostics()를 호출한다.
class VectorTextDiagnosticSink final : public TextDiagnosticSink {
public:
    void Report(TextDiagnostic diagnostic) override;
    const std::vector<TextDiagnostic>& Diagnostics() const noexcept;
private:
    std::vector<TextDiagnostic> diagnostics_;
};

// 기존 사람이 읽는 logger 위의 adapter. 동일한 context key(TextDiagnosticRateLimitKey)의
// 반복 진단을 최근 maxRememberedKeys개까지 기억해 억제하고, 한도를 넘기기 전에 가장
// 오래된 key부터 밀어낸다. maxRememberedKeys가 0이면 아무것도 억제하지 않는다.
//
// 스레드 계약: Report()/ResetRateLimit()는 단일 thread 전용이다. 설계상 텍스트 처리는
// main-thread deterministic CPU phase로 유지하므로(승인 전까지 background shaping 없음)
// 아래 rate-limit 상태는 의도적으로 잠그지 않는다. Log::Emit 자체는 어느 thread에서도
// 안전하지만, 이 클래스는 그 보장을 물려받지 않는다. background 처리가 승인되어 계약이
// 바뀌면 그때 RingBufferSink처럼 내부 mutex를 추가해야 한다.
class LoggerTextDiagnosticSink final : public TextDiagnosticSink {
public:
    explicit LoggerTextDiagnosticSink(std::size_t maxRememberedKeys);
    void Report(TextDiagnostic diagnostic) override;
    void ResetRateLimit();
private:
    std::size_t maxRememberedKeys_ = 0;
    std::deque<std::string> insertionOrder_;
    std::unordered_set<std::string> rememberedKeys_;
};

} // namespace molga::text
