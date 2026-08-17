#include "Common/Log.h"
#include "Common/LogMessage.h"
#include "Common/LogSink.h"
#include "doctest.h"
#include <memory>
#include <vector>

namespace {
// 받은 메시지를 모으는 테스트용 sink.
struct CapturingSink : Log::ILogSink {
    std::vector<Log::LogMessage> received;
    void Write(const Log::LogMessage& m) override { received.push_back(m); }
};
}

TEST_CASE("Emit fans a structured message out to every registered sink") {
    Log::ClearSinks();
    auto sink = std::make_shared<CapturingSink>();
    Log::AddSink(sink);

    Log::LogMessage m;
    m.severity = Log::Severity::Error;
    m.context  = Log::LogContext::ScriptCompiler;
    m.category = "ScriptCompiler";
    m.message  = "expected ';'";
    m.externalPath = "Scripts/Player.cpp";
    m.externalLine = 42;
    Log::Emit(m);

    REQUIRE(sink->received.size() == 1);
    CHECK(sink->received[0].message == "expected ';'");
    CHECK(sink->received[0].externalLine == 42);
    CHECK(sink->received[0].severity == Log::Severity::Error);
    Log::ClearSinks();
}

TEST_CASE("Emit stamps a monotonically increasing sequence number") {
    Log::ClearSinks();
    auto sink = std::make_shared<CapturingSink>();
    Log::AddSink(sink);

    Log::LogMessage a; a.message = "first";
    Log::LogMessage b; b.message = "second";
    Log::Emit(a);
    Log::Emit(b);

    REQUIRE(sink->received.size() == 2);
    CHECK(sink->received[1].sequence > sink->received[0].sequence);
    Log::ClearSinks();
}

TEST_CASE("legacy Info/Warn/Error delegate to Emit with mapped severity") {
    Log::ClearSinks();
    auto sink = std::make_shared<CapturingSink>();
    Log::AddSink(sink);

    Log::Info("TagI", "info-line");
    Log::Warn("TagW", "warn-line");
    Log::Error("TagE", "err-line");

    REQUIRE(sink->received.size() == 3);
    CHECK(sink->received[0].severity == Log::Severity::Info);
    CHECK(sink->received[0].category == "TagI");
    CHECK(sink->received[1].severity == Log::Severity::Warning);
    CHECK(sink->received[2].severity == Log::Severity::Error);
    CHECK(sink->received[2].message == "err-line");
    Log::ClearSinks();
}
