#include "Common/Log.h"
#include "Common/LogMessage.h"
#include "Common/LogSink.h"
#include "Common/RingBufferSink.h"
#include "Text/TextDiagnostic.h"
#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// 전역 로그 구성을 건드리지 않고 한 스코프 동안만 링버퍼로 가로챈다.
// 생성 시 기존 sink 목록을 스냅샷해 두고, 소멸 시 그대로 되돌린다.
class ScopedRingLogSink {
public:
    ScopedRingLogSink()
        : previous_(Log::SnapshotSinks()),
          ring_(std::make_shared<Log::RingBufferSink>(/*capacity=*/1024)) {
        Log::ClearSinks();
        Log::AddSink(ring_);
    }

    ScopedRingLogSink(const ScopedRingLogSink&)            = delete;
    ScopedRingLogSink& operator=(const ScopedRingLogSink&) = delete;

    // 소멸자는 암묵적으로 noexcept다. by-value 파라미터에 lvalue를 넘기면 호출 지점에서
    // vector를 복사하며 할당이 일어나므로, 유일한 throw 지점을 없애려고 move 한다.
    ~ScopedRingLogSink() { Log::RestoreSinks(std::move(previous_)); }

    std::vector<Log::LogMessage> Messages() const { return ring_->Snapshot(); }

private:
    std::vector<std::shared_ptr<Log::ILogSink>> previous_;
    std::shared_ptr<Log::RingBufferSink>        ring_;
};

// 전역 sink 목록에 sentinel을 잠깐 얹는다. doctest의 REQUIRE는 예외를 던지므로,
// 실패 경로에서도 sink가 다른 TEST_CASE로 새지 않도록 해제를 소멸자에 맡긴다.
class ScopedSentinelSink {
public:
    explicit ScopedSentinelSink(std::shared_ptr<Log::ILogSink> sink)
        : sink_(std::move(sink)) {
        Log::AddSink(sink_);
    }

    ScopedSentinelSink(const ScopedSentinelSink&)            = delete;
    ScopedSentinelSink& operator=(const ScopedSentinelSink&) = delete;

    ~ScopedSentinelSink() { Log::RemoveSink(sink_); }

private:
    std::shared_ptr<Log::ILogSink> sink_;
};

// rate-limit key만 다르고 나머지 문맥은 동일한 진단을 만든다.
molga::text::TextDiagnostic DiagnosticAtRange(molga::text::SourceByteRange range) {
    return molga::text::TextDiagnostic{
        molga::text::TextDiagnosticCode::Utf8Invalid, molga::text::TextSeverity::Error,
        "unicode", "invalid sequence", "replace authored bytes", "font-a", 7,
        "UILabel", range};
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("text diagnostic codes and context keys are stable") {
    using namespace molga::text;
    const std::pair<TextDiagnosticCode, std::string_view> expected[] = {
        {TextDiagnosticCode::DependencyInvalid, "TEXT_DEPENDENCY_INVALID"},
        {TextDiagnosticCode::Utf8Invalid, "TEXT_UTF8_INVALID"},
        {TextDiagnosticCode::FontInvalid, "TEXT_FONT_INVALID"},
        {TextDiagnosticCode::FontFamilyInvalid, "TEXT_FONT_FAMILY_INVALID"},
        {TextDiagnosticCode::MissingGlyph, "TEXT_MISSING_GLYPH"},
        {TextDiagnosticCode::AtlasExhausted, "TEXT_ATLAS_EXHAUSTED"},
        {TextDiagnosticCode::LayoutInvalid, "UI_LAYOUT_INVALID"},
        {TextDiagnosticCode::LayoutCycle, "UI_LAYOUT_CYCLE"},
        {TextDiagnosticCode::ReferenceInvalid, "UI_REFERENCE_INVALID"},
        {TextDiagnosticCode::TextInputUnavailable, "TEXT_INPUT_UNAVAILABLE"},
        {TextDiagnosticCode::TextInputRangeClamped, "TEXT_INPUT_RANGE_CLAMPED"},
        {TextDiagnosticCode::ReflowDeferred, "UI_REFLOW_DEFERRED"},
        {TextDiagnosticCode::PackageValidationFailed, "PACKAGE_VALIDATION_FAILED"},
    };
    for (const auto& [code, stable] : expected) {
        CHECK(std::string_view(StableTextDiagnosticCode(code)) == stable);
        CHECK(ParseStableTextDiagnosticCode(stable) == code);
    }
    CHECK_FALSE(ParseStableTextDiagnosticCode("TEXT_UNKNOWN_FROM_FUTURE"));
    CHECK_FALSE(ParseStableTextDiagnosticCode("0"));
}

TEST_CASE("vector sink retains records and source range changes identity") {
    using namespace molga::text;
    VectorTextDiagnosticSink sink;
    TextDiagnostic first{TextDiagnosticCode::Utf8Invalid, TextSeverity::Error,
        "unicode", "invalid sequence", "replace authored bytes", "font-a", 7,
        "UILabel", {3, 4}};
    sink.Report(first);
    sink.Report(first);
    first.sourceByteRange = {4, 5};
    sink.Report(first);
    CHECK(sink.Diagnostics().size() == 3);
    CHECK(TextDiagnosticRateLimitKey(sink.Diagnostics()[0]) !=
          TextDiagnosticRateLimitKey(sink.Diagnostics()[2]));
}

TEST_CASE("logger sink suppresses only a remembered complete context key") {
    using namespace molga::text;
    TextDiagnostic first{TextDiagnosticCode::Utf8Invalid, TextSeverity::Error,
        "unicode", "invalid sequence", "replace authored bytes", "font-a", 7,
        "UILabel", {3, 4}};
    ScopedRingLogSink log;
    LoggerTextDiagnosticSink logger(/*maxRememberedKeys=*/256);
    logger.Report(first);
    logger.Report(first);
    first.sourceByteRange = {4, 5};
    logger.Report(first);
    CHECK(log.Messages().size() == 2);
}

TEST_CASE("logger sink evicts oldest keys and zero capacity never suppresses") {
    using namespace molga::text;
    const auto a = DiagnosticAtRange({1, 2});
    const auto b = DiagnosticAtRange({2, 3});
    {
        ScopedRingLogSink log;
        LoggerTextDiagnosticSink one(/*maxRememberedKeys=*/1);
        one.Report(a); one.Report(b); one.Report(a);
        CHECK(log.Messages().size() == 3);
    }
    {
        ScopedRingLogSink log;
        LoggerTextDiagnosticSink zero(/*maxRememberedKeys=*/0);
        zero.Report(a); zero.Report(a);
        CHECK(log.Messages().size() == 2);
    }
}

// 아래 TEST_CASE들은 계획서의 네 블록이 덮지 않는 계약을 고정한다. 계획서 블록 자체는
// 한 글자도 바꾸지 않고, 빠진 검증만 별도 케이스로 추가한다.

TEST_CASE("out-of-range diagnostic codes fall back without round-tripping") {
    using namespace molga::text;
    // 손상된 직렬화나 미래 빌드가 만든 값. 고정 underlying type(uint16_t) 범위 안이라
    // 캐스팅 자체는 정의된 동작이고, 매핑은 예외 없이 대체 코드를 돌려줘야 한다.
    const auto unknown = static_cast<TextDiagnosticCode>(9999);
    CHECK(std::string_view(StableTextDiagnosticCode(unknown)) == "TEXT_DIAGNOSTIC_UNKNOWN");
    // 대체 코드는 의도적으로 왕복하지 않는다(13개 안정 코드의 별칭이 아니다).
    CHECK_FALSE(ParseStableTextDiagnosticCode("TEXT_DIAGNOSTIC_UNKNOWN"));

    // 대체 코드는 rate-limit key 경로에서도 유효한 접두사여야 한다. 빈 문자열이나
    // nullptr이면 여기서 key가 무너진다.
    TextDiagnostic corrupted = DiagnosticAtRange({0, 0});
    corrupted.code = unknown;
    const std::string key = TextDiagnosticRateLimitKey(corrupted);
    CHECK(key.rfind("TEXT_DIAGNOSTIC_UNKNOWN\x1f", 0) == 0);
}

TEST_CASE("logger sink evicts the oldest remembered key first") {
    using namespace molga::text;
    // 용량 1에서는 FIFO와 LIFO 축출이 구분되지 않으므로 용량 2로 순서를 고정한다.
    const auto a = DiagnosticAtRange({1, 2});
    const auto b = DiagnosticAtRange({2, 3});
    const auto c = DiagnosticAtRange({3, 4});
    ScopedRingLogSink log;
    LoggerTextDiagnosticSink two(/*maxRememberedKeys=*/2);
    two.Report(a);
    two.Report(b);
    two.Report(c);  // 한도 초과: 가장 오래된 a가 밀려나고 {b, c}만 남는다.
    two.Report(a);  // 밀려났으므로 다시 발행된다. 최신부터 축출했다면 여기서 억제된다.
    CHECK(log.Messages().size() == 4);
    two.Report(c);  // 아직 기억 중이므로 억제된다.
    CHECK(log.Messages().size() == 4);
}

TEST_CASE("logger sink forgets every remembered key after ResetRateLimit") {
    using namespace molga::text;
    const auto a = DiagnosticAtRange({1, 2});
    ScopedRingLogSink log;
    LoggerTextDiagnosticSink logger(/*maxRememberedKeys=*/256);
    logger.Report(a);
    logger.Report(a);
    CHECK(log.Messages().size() == 1);
    logger.ResetRateLimit();
    logger.Report(a);
    CHECK(log.Messages().size() == 2);
}

TEST_CASE("scoped ring log sink leaves the global sink list unchanged") {
    // ScopedRingLogSink는 Log::SnapshotSinks/RestoreSinks 위에서 동작한다. 두 API가
    // 실제로 이전 목록을(순서까지) 되돌리는지 여기서만 검증한다.
    const auto sentinel = std::make_shared<Log::RingBufferSink>(/*capacity=*/8);
    std::vector<std::shared_ptr<Log::ILogSink>> before;
    {
        const ScopedSentinelSink guard(sentinel);
        before = Log::SnapshotSinks();
        REQUIRE_FALSE(before.empty());
        REQUIRE(before.back() == sentinel);

        {
            ScopedRingLogSink log;
            const auto during = Log::SnapshotSinks();
            CHECK(during.size() == 1);
            CHECK(during[0] != sentinel);

            Log::LogMessage inside;
            inside.category = "text-diagnostic-test";
            inside.message  = "inside scope";
            Log::Emit(inside);
            CHECK(log.Messages().size() == 1);
        }

        const auto restored = Log::SnapshotSinks();
        REQUIRE(restored.size() == before.size());
        CHECK(std::equal(restored.begin(), restored.end(), before.begin()));
        // scope 안의 메시지는 가로채였으므로 sentinel에는 닿지 않았다.
        CHECK(sentinel->Snapshot().empty());

        Log::LogMessage after;
        after.category = "text-diagnostic-test";
        after.message  = "after scope";
        Log::Emit(after);
        CHECK(sentinel->Snapshot().size() == 1);
    }
    // guard가 소멸하며 sentinel을 떼어냈다.
    CHECK(Log::SnapshotSinks().size() == before.size() - 1);
}

TEST_CASE("every declared code is reachable through the reverse parser") {
    using namespace molga::text;
    // enum은 0부터 연속이므로 대체 코드를 만날 때까지 훑으면 switch가 아는 코드 전부다.
    // switch에만 코드를 추가하고 역파서의 닫힌 목록을 빠뜨리면 여기서 걸린다.
    int declared = 0;
    for (std::uint16_t raw = 0; raw < 1024; ++raw) {
        const auto code = static_cast<TextDiagnosticCode>(raw);
        const std::string_view stable(StableTextDiagnosticCode(code));
        if (stable == "TEXT_DIAGNOSTIC_UNKNOWN") break;
        ++declared;
        CHECK(ParseStableTextDiagnosticCode(stable) == code);
    }
    CHECK(declared == 13);
}

TEST_CASE("reverse parser rejects aliases and near misses") {
    using namespace molga::text;
    // Step 3a가 명시한 거부 부류 중 '별칭'. 다섯 코드는 열거자 이름과 접두사가 달라서
    // (UI_/PACKAGE_) 그럴듯한 오타가 실재한다. 정확한 전체 문자열 비교만 통과해야 한다.
    CHECK_FALSE(ParseStableTextDiagnosticCode("TEXT_LAYOUT_INVALID"));
    CHECK_FALSE(ParseStableTextDiagnosticCode("TEXT_PACKAGE_VALIDATION_FAILED"));
    CHECK_FALSE(ParseStableTextDiagnosticCode("UI_REFLOW_DEFERRED "));
    CHECK_FALSE(ParseStableTextDiagnosticCode("text_utf8_invalid"));
    CHECK_FALSE(ParseStableTextDiagnosticCode("TEXT_UTF8_INVALID_EXTRA"));
    CHECK_FALSE(ParseStableTextDiagnosticCode("TEXT_UTF8"));
    CHECK_FALSE(ParseStableTextDiagnosticCode(""));
}

TEST_CASE("source byte ranges compare by both endpoints") {
    using namespace molga::text;
    // 선언된 public operator==의 두 끝점을 모두 고정한다. 중괄호 안의 쉼표는 매크로
    // 인자를 쪼개므로 CHECK에 직접 넣지 않고 이름 붙인 값으로 비교한다.
    constexpr SourceByteRange base{3, 4};
    constexpr SourceByteRange sameAsBase{3, 4};
    constexpr SourceByteRange endDiffers{3, 5};
    constexpr SourceByteRange beginDiffers{2, 4};
    CHECK(base == sameAsBase);
    CHECK_FALSE(base == endDiffers);
    CHECK_FALSE(base == beginDiffers);
    static_assert(base == sameAsBase, "constexpr 비교여야 한다");
    static_assert(!(base == endDiffers), "end가 다르면 같지 않다");
}

TEST_CASE("rate limit key discriminates every context field") {
    using namespace molga::text;
    // 계획서의 케이스들은 sourceByteRange만 바꾸므로, key가 code+range로 축소돼도
    // 통과한다. 나머지 문맥 필드가 실제로 identity에 들어가는지 여기서 고정한다.
    const TextDiagnostic base    = DiagnosticAtRange({3, 4});
    const std::string    baseKey = TextDiagnosticRateLimitKey(base);

    TextDiagnostic otherAsset     = base;
    otherAsset.assetGuid          = "font-b";
    TextDiagnostic otherObject    = base;
    otherObject.sceneObjectId     = 8;
    TextDiagnostic otherComponent = base;
    otherComponent.componentType  = "UITextInput";
    TextDiagnostic otherCode      = base;
    otherCode.code                = TextDiagnosticCode::MissingGlyph;
    TextDiagnostic otherRange     = base;
    otherRange.sourceByteRange    = {4, 5};

    CHECK(TextDiagnosticRateLimitKey(otherAsset) != baseKey);
    CHECK(TextDiagnosticRateLimitKey(otherObject) != baseKey);
    CHECK(TextDiagnosticRateLimitKey(otherComponent) != baseKey);
    CHECK(TextDiagnosticRateLimitKey(otherCode) != baseKey);
    CHECK(TextDiagnosticRateLimitKey(otherRange) != baseKey);

    // 반대 방향도 고정한다. key는 record 전체가 아니라 code + context key다.
    // 문구나 심각도만 다른 반복 진단까지 새 key가 되면 unbounded dynamic string
    // 경로에서 rate limit이 사실상 사라진다.
    TextDiagnostic sameContext = base;
    sameContext.message        = "another invalid sequence";
    sameContext.remediation    = "another remediation";
    sameContext.severity       = TextSeverity::Warning;
    CHECK(TextDiagnosticRateLimitKey(sameContext) == baseKey);
}

TEST_CASE("logger sink keeps distinct context fields separately remembered") {
    using namespace molga::text;
    // key 수준뿐 아니라 억제 동작 수준에서도 문맥 분리를 고정한다. 문맥 필드를 하나라도
    // 빠뜨린 key는 서로 다른 asset/scene object/component를 한 건으로 접어 삼킨다.
    const TextDiagnostic base      = DiagnosticAtRange({3, 4});
    TextDiagnostic otherAsset      = base;
    otherAsset.assetGuid           = "font-b";
    TextDiagnostic otherObject     = base;
    otherObject.sceneObjectId      = 8;
    TextDiagnostic otherComponent  = base;
    otherComponent.componentType   = "UITextInput";

    ScopedRingLogSink        log;
    LoggerTextDiagnosticSink logger(/*maxRememberedKeys=*/256);
    logger.Report(base);
    logger.Report(otherAsset);
    logger.Report(otherObject);
    logger.Report(otherComponent);
    CHECK(log.Messages().size() == 4);

    // 같은 네 건을 다시 보고하면 전부 기억 중이므로 하나도 추가되지 않는다.
    logger.Report(base);
    logger.Report(otherAsset);
    logger.Report(otherObject);
    logger.Report(otherComponent);
    CHECK(log.Messages().size() == 4);
}

TEST_CASE("logger sink emits the whole diagnostic context in one readable line") {
    using namespace molga::text;
    // 설계 11절은 현재 logger를 typed record의 유일한 사람이 읽는 전달 경로로 못박는다.
    // 건수만 세면 빈 메시지도 통과하므로 발행된 record 자체를 읽는다.
    TextDiagnostic d  = DiagnosticAtRange({117, 133});
    d.code            = TextDiagnosticCode::MissingGlyph;
    d.subsystem       = "text-shaping";
    d.message         = "no glyph for U+1F600";
    d.remediation     = "add an emoji fallback family";
    d.assetGuid       = "family-guid-42";
    d.sceneObjectId   = 4242;
    d.componentType   = "UITextInput";

    ScopedRingLogSink        log;
    LoggerTextDiagnosticSink logger(/*maxRememberedKeys=*/256);
    logger.Report(d);

    const std::vector<Log::LogMessage> messages = log.Messages();
    REQUIRE(messages.size() == 1);
    const Log::LogMessage& m = messages[0];

    CHECK(m.category == d.subsystem);
    CHECK(m.severity == Log::Severity::Error);
    CHECK(Contains(m.message, "TEXT_MISSING_GLYPH"));
    CHECK(Contains(m.message, d.message));
    CHECK(Contains(m.message, d.subsystem));
    CHECK(Contains(m.message, d.remediation));
    CHECK(Contains(m.message, d.assetGuid));
    CHECK(Contains(m.message, "4242"));
    CHECK(Contains(m.message, d.componentType));
    CHECK(Contains(m.message, "117:133"));
}

TEST_CASE("logger sink maps every text severity onto the logger severity") {
    using namespace molga::text;
    const std::pair<TextSeverity, Log::Severity> expected[] = {
        {TextSeverity::Info, Log::Severity::Info},
        {TextSeverity::Warning, Log::Severity::Warning},
        {TextSeverity::Error, Log::Severity::Error},
        // Log::Severity에는 Blocker가 없으므로 가장 가까운 최상위 값으로 접는다.
        // 이 접기는 SmokeReportSink의 실패 목록(severity >= Error)에 영향을 주므로
        // 조용히 바뀌면 안 된다.
        {TextSeverity::Blocker, Log::Severity::Fatal},
    };
    for (const auto& [textSeverity, logSeverity] : expected) {
        // 용량 0이면 아무것도 억제하지 않으므로 문맥이 같아도 매번 발행된다.
        ScopedRingLogSink        log;
        LoggerTextDiagnosticSink logger(/*maxRememberedKeys=*/0);
        TextDiagnostic           d = DiagnosticAtRange({1, 2});
        d.severity                 = textSeverity;
        logger.Report(d);

        const std::vector<Log::LogMessage> messages = log.Messages();
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].severity == logSeverity);
    }
}
