#include "Common/Log.h"
#include "Common/LogMessage.h"
#include "Common/LogSink.h"
#include "Common/RingBufferSink.h"
#include "Text/TextDiagnostic.h"
#include "Common/Sha256.h"
#include "Text/TextRuntimeDependencies.h"
#include "doctest.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

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

// ── Fresh-process ICU lifetime probes ────────────────────────────────────────
// This executable never initializes ICU or HarfBuzz. A successful ICU lifetime
// is terminal — hb_icu_get_unicode_funcs() caches ICU normalizer pointers
// process-statically, so nothing can restart one — which is why every
// not-ready, post-cleanup and tamper observation below is made by spawning a
// fresh molga_text_runtime_probe child instead of stopping and restoring ICU
// here. This parent owns the probe executable and both immutable roots; the
// child receives them only as literal argv and derives nothing from its own
// path or from the working directory.

namespace {

namespace fs = std::filesystem;

// The exact schema-1 record molga_text_runtime_probe writes. A missing, extra
// or wrongly typed key is a parse failure, never a silently defaulted field.
struct TextRuntimeProbeReport {
    std::string              mode;
    bool                     firstInitialize           = false;
    bool                     readyBeforeShutdown       = false;
    bool                     harfbuzzIcuProbe          = false;
    bool                     secondInitializeAttempted = false;
    bool                     secondInitialize          = false;
    int                      icuCallsBeforePublish     = -1;
    int                      icuCallsAfterTerminal     = -1;
    bool                     terminallyCleaned         = false;
    std::vector<std::string> diagnosticCodes;
};

// The closed mode set. The parent rejects anything else before spawning, so a
// typo cannot reach the child and be reported as a generic nonzero exit.
const std::vector<std::string>& ProbeModes() {
    static const std::vector<std::string> modes = {
        "tampered-data", "nonportable-contract", "terminal-nonrestart",
        "staged-valid",  "dev-missing-contract", "dev-tampered-data"};
    return modes;
}

std::string ReadAllBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE_MESSAGE(input.good(), path.string());
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

// One unique canonical directory per probe run, removed — and only it —
// through RAII so a failing CHECK cannot leak a 33 MB copy into the temp root.
class CallerTempRoot {
public:
    CallerTempRoot() {
        static unsigned counter = 0;
        const fs::path base = fs::canonical(fs::temp_directory_path());
        fs::path       candidate;
        do {
            candidate = base / ("molga-text-runtime-probe-" +
                                std::to_string(static_cast<long>(::getpid())) +
                                "-" + std::to_string(counter++));
        } while (fs::exists(candidate));
        REQUIRE(fs::create_directory(candidate));
        path_ = fs::canonical(candidate);
    }

    CallerTempRoot(const CallerTempRoot&)            = delete;
    CallerTempRoot& operator=(const CallerTempRoot&) = delete;

    ~CallerTempRoot() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& Path() const noexcept { return path_; }

private:
    fs::path path_;
};

struct SpawnOutcome {
    int         exitCode = -1;
    std::string standardOutput;
    std::string standardError;
};

// posix_spawn with no shell: every argument reaches the child verbatim, so a
// path containing a space or a shell metacharacter cannot change the command.
SpawnOutcome SpawnCaptured(const std::vector<std::string>& argv,
                           const fs::path&                 scratchRoot) {
    const fs::path outPath = scratchRoot / "stdout.txt";
    const fs::path errPath = scratchRoot / "stderr.txt";

    // doctest's REQUIRE throws, so the file actions and any spawned child have
    // to be owned by something that unwinds. Only reachable when the harness
    // itself is broken, but this harness gets copied for later milestones.
    struct FileActions {
        posix_spawn_file_actions_t value{};
        bool                       initialized = false;
        FileActions() { initialized = posix_spawn_file_actions_init(&value) == 0; }
        FileActions(const FileActions&)            = delete;
        FileActions& operator=(const FileActions&) = delete;
        ~FileActions() {
            if (initialized) posix_spawn_file_actions_destroy(&value);
        }
    } actions;
    REQUIRE(actions.initialized);
    REQUIRE(posix_spawn_file_actions_addopen(
                &actions.value, STDOUT_FILENO, outPath.c_str(),
                O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0);
    REQUIRE(posix_spawn_file_actions_addopen(
                &actions.value, STDERR_FILENO, errPath.c_str(),
                O_WRONLY | O_CREAT | O_TRUNC, 0644) == 0);

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string& argument : argv) {
        raw.push_back(const_cast<char*>(argument.c_str()));
    }
    raw.push_back(nullptr);

    SpawnOutcome outcome;
    pid_t        pid     = 0;
    const int    spawned = posix_spawn(&pid, raw[0], &actions.value, nullptr,
                                       raw.data(), environ);
    REQUIRE(spawned == 0);

    // Reaps the child even if the wait assertion below throws.
    struct ChildReaper {
        pid_t pid    = 0;
        bool  reaped = false;
        ~ChildReaper() {
            if (!reaped) {
                int discarded = 0;
                ::waitpid(pid, &discarded, 0);
            }
        }
    } reaper{pid, false};

    int status = 0;
    REQUIRE(::waitpid(pid, &status, 0) == pid);
    reaper.reaped = true;
    outcome.exitCode       = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    outcome.standardOutput = ReadAllBytes(outPath);
    outcome.standardError  = ReadAllBytes(errPath);
    return outcome;
}

TextRuntimeProbeReport ParseProbeReport(const fs::path& path,
                                        const std::string& expectedMode) {
    const std::string bytes = ReadAllBytes(path);
    nlohmann::json    document = nlohmann::json::parse(bytes, nullptr, false);
    REQUIRE_MESSAGE(!document.is_discarded(), bytes);
    REQUIRE(document.is_object());
    // Exact schema: the ten declared keys and nothing else.
    REQUIRE(document.size() == 10);
    for (const char* key : {"mode", "firstInitialize", "readyBeforeShutdown",
                            "harfbuzzIcuProbe", "secondInitializeAttempted",
                            "secondInitialize", "icuCallsBeforePublish",
                            "icuCallsAfterTerminal", "terminallyCleaned",
                            "diagnosticCodes"}) {
        REQUIRE_MESSAGE(document.contains(key), key);
    }

    TextRuntimeProbeReport report;
    REQUIRE(document.at("mode").is_string());
    report.mode = document.at("mode").get<std::string>();
    REQUIRE(report.mode == expectedMode);
    const auto readBool = [&document](const char* key) {
        REQUIRE_MESSAGE(document.at(key).is_boolean(), key);
        return document.at(key).get<bool>();
    };
    const auto readCount = [&document](const char* key) {
        REQUIRE_MESSAGE(document.at(key).is_number_unsigned(), key);
        return document.at(key).get<int>();
    };
    report.firstInitialize           = readBool("firstInitialize");
    report.readyBeforeShutdown       = readBool("readyBeforeShutdown");
    report.harfbuzzIcuProbe          = readBool("harfbuzzIcuProbe");
    report.secondInitializeAttempted = readBool("secondInitializeAttempted");
    report.secondInitialize          = readBool("secondInitialize");
    report.icuCallsBeforePublish     = readCount("icuCallsBeforePublish");
    report.icuCallsAfterTerminal     = readCount("icuCallsAfterTerminal");
    report.terminallyCleaned         = readBool("terminallyCleaned");
    REQUIRE(document.at("diagnosticCodes").is_array());
    for (const auto& entry : document.at("diagnosticCodes")) {
        REQUIRE(entry.is_string());
        report.diagnosticCodes.push_back(entry.get<std::string>());
    }
    return report;
}

// The child's single stdout line, without its prefix; empty when it said
// nothing. Not part of the schema-1 report: the record's ten keys are fixed.
std::string RoutedFileAccessName(const std::string& standardOutput) {
    static constexpr std::string_view kPrefix = "MOLGA_TEXT_ICU_FILE_ACCESS ";
    std::size_t                       begin   = 0;
    while (begin < standardOutput.size()) {
        std::size_t end = standardOutput.find('\n', begin);
        if (end == std::string::npos) end = standardOutput.size();
        const std::string line = standardOutput.substr(begin, end - begin);
        if (line.rfind(kPrefix, 0) == 0) return line.substr(kPrefix.size());
        begin = end + 1;
    }
    return {};
}

// fileAccessName, when given, receives the UDataFileAccess enumerator the child
// actually handed the routed udata_setFileAccess.
TextRuntimeProbeReport RunTextRuntimeProbe(const std::string& mode,
                                           std::string* fileAccessName = nullptr) {
    // The single portable-contract read this process performs. Requiring the
    // barrier's contract bytes to equal the fixture root's staged copy is what
    // makes the child's --fixture-root the verified pair and not just some
    // directory that happens to hold two files with the right names.
    const fs::path contract = fs::canonical(MOLGA_TEXT_DEPENDENCY_CONTRACT);
    const fs::path fixtureRoot =
        fs::canonical(MOLGA_TEXT_RUNTIME_FIXTURE_ROOT);
    const fs::path developmentRoot =
        fs::canonical(MOLGA_TEXT_ENGINE_DEV_TEXT_ROOT);
    REQUIRE(ReadAllBytes(contract) ==
            ReadAllBytes(fixtureRoot / "text_dependency_contract.json"));
    REQUIRE_MESSAGE(std::find(ProbeModes().begin(), ProbeModes().end(), mode) !=
                        ProbeModes().end(),
                    mode);

    const CallerTempRoot temp;
    const fs::path       report = temp.Path() / "probe-report.json";
    REQUIRE_FALSE(fs::exists(report));

    const SpawnOutcome outcome = SpawnCaptured(
        {MOLGA_TEXT_RUNTIME_PROBE, "--mode", mode, "--fixture-root",
         fixtureRoot.string(), "--development-root", developmentRoot.string(),
         "--report", report.string()},
        temp.Path());
    REQUIRE_MESSAGE(outcome.exitCode == 0,
                    (mode + " exited " + std::to_string(outcome.exitCode) +
                     "\nstdout:\n" + outcome.standardOutput + "\nstderr:\n" +
                     outcome.standardError));
    if (fileAccessName != nullptr) {
        *fileAccessName = RoutedFileAccessName(outcome.standardOutput);
    }
    return ParseProbeReport(report, mode);
}

// ── Application startup-seam unwind order ────────────────────────────────────

// $<TARGET_FILE_DIR:molga_engine>/Engine/Text is the one development root this
// target is given; the two development executables are its grandparent's
// direct children. Deriving them here keeps the compile-definition set on this
// target exactly the three the plan names.
fs::path DevelopmentExecutableDir() {
    return fs::path(MOLGA_TEXT_ENGINE_DEV_TEXT_ROOT).parent_path().parent_path();
}

std::vector<std::string> LifetimeEvents(const std::string& standardOutput) {
    static constexpr std::string_view kPrefix = "MOLGA_TEXT_LIFETIME ";
    std::vector<std::string>          events;
    std::size_t                       begin = 0;
    while (begin < standardOutput.size()) {
        std::size_t end = standardOutput.find('\n', begin);
        if (end == std::string::npos) end = standardOutput.size();
        const std::string line = standardOutput.substr(begin, end - begin);
        if (line.rfind(kPrefix, 0) == 0) events.push_back(line.substr(kPrefix.size()));
        begin = end + 1;
    }
    return events;
}

}  // namespace

TEST_CASE("ICU tamper fails before publishing ready state") {
    const auto report = RunTextRuntimeProbe("tampered-data");
    CHECK_FALSE(report.firstInitialize);
    CHECK_FALSE(report.readyBeforeShutdown);
    CHECK(report.icuCallsBeforePublish == 0);
    CHECK(report.diagnosticCodes == std::vector<std::string>{
        "TEXT_DEPENDENCY_INVALID"});
}

TEST_CASE("nonportable dependency contract fails before every ICU call") {
    const auto report = RunTextRuntimeProbe("nonportable-contract");
    CHECK_FALSE(report.firstInitialize);
    CHECK(report.icuCallsBeforePublish == 0);
    CHECK(report.diagnosticCodes == std::vector<std::string>{
        "TEXT_DEPENDENCY_INVALID"});
}

TEST_CASE("successful ICU and HarfBuzz lifetime cannot restart after cleanup") {
    const auto report = RunTextRuntimeProbe("terminal-nonrestart");
    CHECK(report.firstInitialize);
    CHECK(report.readyBeforeShutdown);
    CHECK(report.harfbuzzIcuProbe);
    CHECK(report.secondInitializeAttempted);
    CHECK_FALSE(report.secondInitialize);
    CHECK(report.icuCallsAfterTerminal == 0);
    CHECK(report.terminallyCleaned);
    CHECK(report.diagnosticCodes.back() == "TEXT_DEPENDENCY_INVALID");
}

TEST_CASE("built development Engine Text root initializes exact staged pair") {
    const auto report = RunTextRuntimeProbe("staged-valid");
    CHECK(report.firstInitialize);
    CHECK(report.readyBeforeShutdown);
    CHECK(report.terminallyCleaned);
}

// Deliberately a separate case rather than a CHECK inside the verbatim
// staged-valid block, so those blocks stay byte-for-byte as planned.
//
// Every other ICU-count assertion in this file is that a counter equals zero,
// and a counting table that was never reached also reads zero. If the runtime
// ever called ICU directly instead of through detail::IcuRuntimeApi, all six
// modes would still pass and this task's central claim — no ICU call before
// validation — would be vacuous. Requiring the three calls a successful
// lifetime must make (udata_setCommonData, udata_setFileAccess, u_init) is
// what keeps the seam provably live.
//
// The argument is pinned for the same reason the count is. UDATA_NO_FILES is
// the whole of what makes the staged package the only source of ICU data:
// UDATA_ONLY_PACKAGES still permits a .dat package found on the filesystem —
// through ICU_DATA, say — and every count in this file reads exactly the same
// when the mode is weakened that way. This is the only assertion that sees it.
TEST_CASE("a successful lifetime routes exactly three calls through the counted table") {
    std::string fileAccessName;
    const auto  report = RunTextRuntimeProbe("staged-valid", &fileAccessName);
    REQUIRE(report.firstInitialize);
    CHECK(report.icuCallsBeforePublish == 3);
    CHECK(fileAccessName == "UDATA_NO_FILES");
}

TEST_CASE("development Engine Text root fails closed when missing or tampered") {
    for (const std::string mode :
         {"dev-missing-contract", "dev-tampered-data"}) {
        const auto report = RunTextRuntimeProbe(mode);
        CHECK_FALSE(report.firstInitialize);
        CHECK(report.icuCallsBeforePublish == 0);
        CHECK(report.diagnosticCodes == std::vector<std::string>{
            "TEXT_DEPENDENCY_INVALID"});
    }
}

TEST_CASE("editor and runtime startup seams unwind text state in order") {
    // The seam returns a nonzero code through the real scoped startup function.
    // Because the guard is declared ahead of every text handle, ordinary stack
    // unwinding destroys the last client handle first and begins guard shutdown
    // next; by the time that function has returned the terminal u_cleanup has
    // run, which main observes as a lifecycle that actually reached
    // TerminallyCleaned, and only then does the process return. A text owner
    // declared outside the guard's scope reorders these four lines.
    const std::vector<std::string> expected = {
        "last_text_handle_destroyed", "runtime_guard_shutdown_begins",
        "u_cleanup", "process_return"};
    const fs::path executableDir = DevelopmentExecutableDir();
    for (const char* executable : {"molga_engine", "molga_runtime"}) {
        for (const int code : {17, 18}) {
            const CallerTempRoot temp;
            const SpawnOutcome   outcome = SpawnCaptured(
                  {(executableDir / executable).string(),
                   "--text-test-return-after-services=" + std::to_string(code)},
                  temp.Path());
            const std::string context = std::string(executable) + " " +
                                        std::to_string(code) + "\nstdout:\n" +
                                        outcome.standardOutput + "\nstderr:\n" +
                                        outcome.standardError;
            REQUIRE_MESSAGE(outcome.exitCode == code, context);
            CHECK_MESSAGE(LifetimeEvents(outcome.standardOutput) == expected,
                          context);
        }
    }
}

// ── Validation that never reaches ICU ────────────────────────────────────────
// Everything below runs in this process, which is allowed precisely because
// each case hands Initialize a configuration rejected during pure validation,
// before the first ICU call. Every case re-asserts that the runtime is still
// not ready, so a regression that let one of them through would be caught here
// rather than by this executable quietly acquiring an ICU lifetime.

namespace {

nlohmann::json ValidContractDocument() {
    const std::string bytes = ReadAllBytes(
        fs::path(MOLGA_TEXT_RUNTIME_FIXTURE_ROOT) / "text_dependency_contract.json");
    nlohmann::json document = nlohmann::json::parse(bytes, nullptr, false);
    REQUIRE_FALSE(document.is_discarded());
    return document;
}

void WriteText(const fs::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    REQUIRE(output.good());
}

// Initializes from a root holding only the given contract text. Returns the one
// diagnostic message so a case can prove *which* check rejected it.
std::string InitializeRejectionMessage(const std::string& contractText) {
    const CallerTempRoot temp;
    WriteText(temp.Path() / "text_dependency_contract.json", contractText);
    molga::text::VectorTextDiagnosticSink sink;
    const bool initialized = molga::text::TextRuntimeDependencies::Get().Initialize(
        molga::text::TextDependencyConfig::FromEngineTextRoot(
            temp.Path(), /*packagedRuntime=*/false),
        sink);
    CHECK_FALSE(initialized);
    CHECK_FALSE(molga::text::TextRuntimeDependencies::Get().IsReady());
    REQUIRE(sink.Diagnostics().size() == 1);
    CHECK(sink.Diagnostics()[0].code ==
          molga::text::TextDiagnosticCode::DependencyInvalid);
    CHECK_FALSE(sink.Diagnostics()[0].remediation.empty());
    return sink.Diagnostics()[0].message;
}

bool Mentions(const std::string& message, const std::string& needle) {
    return message.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("the untampered contract passes verification, so rejections mean something") {
    // Non-vacuity guard for every case below. The temp root holds a byte-exact
    // copy of the staged contract and no icudt78l.dat, so verification must get
    // all the way past the contract and fail on the missing data instead.
    const std::string message =
        InitializeRejectionMessage(ReadAllBytes(
            fs::path(MOLGA_TEXT_RUNTIME_FIXTURE_ROOT) /
            "text_dependency_contract.json"));
    CHECK(Mentions(message, "ICU data"));
    CHECK_FALSE(Mentions(message, "contract"));
}

TEST_CASE("the portable contract is rejected at every locked level") {
    nlohmann::json document = ValidContractDocument();

    SUBCASE("unknown top-level field") {
        document["extra"] = 1;
        CHECK(Mentions(InitializeRejectionMessage(document.dump()),
                       "top-level"));
    }
    SUBCASE("missing top-level field") {
        document.erase("icu");
        CHECK(Mentions(InitializeRejectionMessage(document.dump()),
                       "top-level"));
    }
    SUBCASE("wrong schema") {
        document["schema"] = 2;
        CHECK(Mentions(InitializeRejectionMessage(document.dump()), "schema"));
    }
    SUBCASE("malformed JSON") {
        CHECK(Mentions(InitializeRejectionMessage("{ not json"), "valid JSON"));
    }
    SUBCASE("unknown harfbuzz field") {
        document["harfbuzz"]["extra"] = false;
        CHECK(Mentions(InitializeRejectionMessage(document.dump()),
                       "harfbuzz fields"));
    }
    // The three key sets finding 6 added. Without these the option objects were
    // accepted whatever they contained.
    SUBCASE("unknown harfbuzz option") {
        document["harfbuzz"]["options"]["newFlag"] = false;
        CHECK(Mentions(InitializeRejectionMessage(document.dump()),
                       "harfbuzz options"));
    }
    SUBCASE("missing icu option") {
        document["icu"]["options"].erase("tools");
        CHECK(Mentions(InitializeRejectionMessage(document.dump()),
                       "icu options"));
    }
    SUBCASE("unknown icu archive hash") {
        document["icu"]["archiveSha256"]["icuio"] = std::string(64, 'a');
        CHECK(Mentions(InitializeRejectionMessage(document.dump()),
                       "archive hashes"));
    }
    SUBCASE("absolute path anywhere in the record") {
        document["icu"]["sourcePath"] = "/checkout/external/icu";
        CHECK(Mentions(InitializeRejectionMessage(document.dump()),
                       "absolute path"));
    }
}

TEST_CASE("an empty or relative Engine Text root is refused before any ICU call") {
    // PathService::EngineResource yields the relative "Engine/Text" when the
    // executable directory was never resolved. Resolving that against the
    // caller's working directory is the fallback the milestone forbids, so the
    // rejection lives in Initialize where all three call sites are covered.
    using molga::text::TextDependencyConfig;
    using molga::text::TextRuntimeDependencies;
    for (const std::string root : {"", "Engine/Text", "./Engine/Text"}) {
        molga::text::VectorTextDiagnosticSink sink;
        CHECK_FALSE(TextRuntimeDependencies::Get().Initialize(
            TextDependencyConfig::FromEngineTextRoot(root, false), sink));
        CHECK_FALSE(TextRuntimeDependencies::Get().IsReady());
        REQUIRE(sink.Diagnostics().size() == 1);
        CHECK(Mentions(sink.Diagnostics()[0].message, "empty or relative"));
    }
    // FromEngineTextRoot's specified mapping is unchanged: an empty root still
    // yields all-empty paths and carries the packaged flag through.
    const TextDependencyConfig empty = TextDependencyConfig::FromEngineTextRoot("", true);
    CHECK(empty.dependencyContract.empty());
    CHECK(empty.icuData.empty());
    CHECK(empty.packagedRuntime);
}

TEST_CASE("packaged ICU data verification rejects each way a file can be wrong") {
    const CallerTempRoot temp;
    molga::text::VectorTextDiagnosticSink sink;

    const fs::path missing = temp.Path() / "absent.dat";
    CHECK_FALSE(molga::text::VerifyPackagedIcuDataFile(missing, sink));

    const fs::path tooSmall = temp.Path() / "small.dat";
    WriteText(tooSmall, "not 33107232 bytes");
    CHECK_FALSE(molga::text::VerifyPackagedIcuDataFile(tooSmall, sink));

    // Right size, wrong bytes: the size check alone cannot see this, which is
    // why the SHA exists. resize_file makes the 33 MB sparse and instant.
    const fs::path wrongBytes = temp.Path() / "zeros.dat";
    WriteText(wrongBytes, "");
    fs::resize_file(wrongBytes, molga::text::kPackagedIcuDataBytes);
    REQUIRE(fs::file_size(wrongBytes) == molga::text::kPackagedIcuDataBytes);
    CHECK_FALSE(molga::text::VerifyPackagedIcuDataFile(wrongBytes, sink));

    REQUIRE(sink.Diagnostics().size() == 3);
    for (const molga::text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        CHECK(diagnostic.code == molga::text::TextDiagnosticCode::DependencyInvalid);
        CHECK_FALSE(diagnostic.remediation.empty());
    }
    // And it accepts the staged file, so the three rejections above are not
    // just "this function always says no".
    CHECK(molga::text::VerifyPackagedIcuDataFile(
        fs::path(MOLGA_TEXT_RUNTIME_FIXTURE_ROOT) / "icudt78l.dat", sink));
    CHECK(sink.Diagnostics().size() == 3);
}

// The contract half of the same pair. Existence was all the packager asked of
// it, so a contract the packaged player rejects at startup — the nonportable
// one below is exactly what a staging mistake produces — copied cleanly into a
// game and surfaced only as that player's exit 4.
TEST_CASE("packaged contract verification rejects each way a contract can be wrong") {
    const CallerTempRoot                  temp;
    molga::text::VectorTextDiagnosticSink sink;

    const fs::path missing = temp.Path() / "absent.json";
    CHECK_FALSE(molga::text::VerifyPackagedDependencyContractFile(missing, sink));

    const fs::path malformed = temp.Path() / "malformed.json";
    WriteText(malformed, "{ not json");
    CHECK_FALSE(
        molga::text::VerifyPackagedDependencyContractFile(malformed, sink));

    // Well-formed JSON with every locked field, and still not portable. This is
    // the case fs::is_regular_file could never see and the one that matters:
    // the file exists, parses, and names a path off the build machine.
    nlohmann::json nonportable  = ValidContractDocument();
    nonportable["icu"]["sourcePath"] = "/checkout/external/icu";
    const fs::path nonportablePath = temp.Path() / "nonportable.json";
    WriteText(nonportablePath, nonportable.dump());
    CHECK_FALSE(molga::text::VerifyPackagedDependencyContractFile(nonportablePath,
                                                                  sink));

    REQUIRE(sink.Diagnostics().size() == 3);
    for (const molga::text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        CHECK(diagnostic.code == molga::text::TextDiagnosticCode::DependencyInvalid);
        CHECK_FALSE(diagnostic.remediation.empty());
    }
    // And it accepts the staged contract, so the three rejections above are not
    // just "this function always says no".
    CHECK(molga::text::VerifyPackagedDependencyContractFile(
        fs::path(MOLGA_TEXT_RUNTIME_FIXTURE_ROOT) / "text_dependency_contract.json",
        sink));
    CHECK(sink.Diagnostics().size() == 3);
}

TEST_CASE("the probe refuses every malformed command line") {
    // These were hand-verified during implementation; automating them is what
    // keeps the child's argv contract from eroding. Exit 2 is its argument
    // rejection, distinct from 3 for a failed run.
    const CallerTempRoot temp;
    const fs::path fixtureRoot = fs::canonical(MOLGA_TEXT_RUNTIME_FIXTURE_ROOT);
    const fs::path developmentRoot = fs::canonical(MOLGA_TEXT_ENGINE_DEV_TEXT_ROOT);
    const std::string probe = MOLGA_TEXT_RUNTIME_PROBE;
    const std::vector<std::string> base = {
        probe, "--mode", "staged-valid", "--fixture-root", fixtureRoot.string(),
        "--development-root", developmentRoot.string(), "--report",
        (temp.Path() / "unused.json").string()};

    const fs::path existing = temp.Path() / "already-there.json";
    WriteText(existing, "{}");

    std::vector<std::pair<std::string, std::vector<std::string>>> cases;
    auto with = [&base](std::size_t index, const std::string& value) {
        std::vector<std::string> argv = base;
        argv[index] = value;
        return argv;
    };
    cases.emplace_back("unknown mode", with(2, "bogus"));
    cases.emplace_back("noncanonical fixture root",
                       with(4, (fixtureRoot / ".." / "Text").string()));
    cases.emplace_back("relative development root", with(6, "Engine/Text"));
    cases.emplace_back("report is not a direct child",
                       with(8, (temp.Path() / "sub" / "r.json").string()));
    cases.emplace_back("report already exists", with(8, existing.string()));

    std::vector<std::string> duplicated = base;
    duplicated.insert(duplicated.begin() + 1, {"--mode", "staged-valid"});
    cases.emplace_back("duplicate switch", duplicated);

    std::vector<std::string> unknownSwitch = base;
    unknownSwitch.insert(unknownSwitch.begin() + 1, {"--bogus", "x"});
    cases.emplace_back("unknown switch", unknownSwitch);

    std::vector<std::string> missingValue = {probe, "--mode"};
    cases.emplace_back("missing value", missingValue);

    for (const auto& entry : cases) {
        // Plain locals, not a structured binding: doctest's message macro
        // captures them in a lambda, and capturing a binding is C++20.
        const std::string  name = entry.first;
        const CallerTempRoot scratch;
        const SpawnOutcome   outcome = SpawnCaptured(entry.second, scratch.Path());
        CHECK_MESSAGE(outcome.exitCode == 2, (name + ": " + outcome.standardError));
    }
}
