#include "Common/Log.h"
#include "Common/RingBufferSink.h"
#include "doctest.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <fstream>
#include <iterator>

using Log::RingBufferSink;

TEST_CASE("RingBufferSink retains only the most recent N messages (memory cap)") {
    RingBufferSink ring(/*capacity=*/100);
    for (int i = 0; i < 1000; ++i) {
        Log::LogMessage m; m.sequence = static_cast<std::uint64_t>(i);
        m.message = std::to_string(i);
        ring.Write(m);
    }
    auto snapshot = ring.Snapshot();
    CHECK(snapshot.size() == 100);              // 상한을 넘지 않음
    CHECK(snapshot.front().message == "900");   // 가장 오래된 것이 밀려남
    CHECK(snapshot.back().message == "999");
}

TEST_CASE("RingBufferSink survives concurrent writers without data race") {
    RingBufferSink ring(/*capacity=*/4096);
    constexpr int kThreads = 8;
    constexpr int kPer = 5000;
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&ring, t] {
            for (int i = 0; i < kPer; ++i) {
                Log::LogMessage m; m.message = std::to_string(t) + ":" + std::to_string(i);
                ring.Write(m);   // 동시 Write — TSan/ASan 하에서 깨끗해야 함
            }
        });
    }
    for (auto& w : workers) w.join();
    CHECK(ring.Snapshot().size() == 4096);       // 용량 유지, 크래시 없음
}

TEST_CASE("Emit fans out to a ring sink from multiple threads") {
    Log::ClearSinks();
    auto ring = std::make_shared<RingBufferSink>(8192);
    Log::AddSink(ring);

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([] {
            for (int i = 0; i < 1000; ++i) {
                Log::LogMessage m; m.message = "x";
                Log::Emit(m);
            }
        });
    }
    for (auto& w : workers) w.join();
    CHECK(ring->Snapshot().size() == 4000);
    Log::ClearSinks();
}

#include "Common/FileSink.h"
#include "Common/SmokeReportSink.h"
#include "SmokeTestSupport.h"

TEST_CASE("FileSink writes one structured line per message to disk") {
    test_support::TempDirectory dir("logsink");
    auto path = dir.Path() / "session.log";
    {
        Log::FileSink file(path.string());
        Log::LogMessage m; m.category = "Build"; m.severity = Log::Severity::Error;
        m.message = "link failed";
        file.Write(m);
    } // flush on destruction
    std::ifstream in(path);
    std::string contents((std::istreambuf_iterator<char>(in)), {});
    CHECK(contents.find("link failed") != std::string::npos);
    CHECK(contents.find("ERROR") != std::string::npos);
}

TEST_CASE("SmokeReportSink collects errors and writes them to the report file") {
    test_support::TempDirectory dir("smokesink");
    auto report = dir.Path() / "smoke_log.txt";
    Log::SmokeReportSink sink(report.string());

    Log::LogMessage ok; ok.severity = Log::Severity::Info;  ok.message = "frame ok";
    Log::LogMessage bad; bad.severity = Log::Severity::Error; bad.category = "Runtime";
    bad.message = "missing asset texture.png";
    sink.Write(ok);
    sink.Write(bad);
    sink.Flush();

    std::ifstream in(report);
    std::string contents((std::istreambuf_iterator<char>(in)), {});
    CHECK(contents.find("missing asset texture.png") != std::string::npos);
    CHECK(contents.find("frame ok") == std::string::npos);  // 실패만 기록
}
