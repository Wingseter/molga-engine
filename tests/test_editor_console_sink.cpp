#include "Editor/EditorConsoleSink.h"
#include "Common/Log.h"
#include "doctest.h"
#include <thread>
#include <vector>

using molga::EditorConsoleSink;

TEST_CASE("Write enqueues and Drain transfers ownership to the caller") {
    EditorConsoleSink sink;
    Log::LogMessage a; a.message = "a";
    Log::LogMessage b; b.message = "b";
    sink.Write(a);
    sink.Write(b);

    auto drained = sink.Drain();
    CHECK(drained.size() == 2);
    CHECK(sink.Drain().empty());          // 두 번째 Drain은 비어 있어야 함
}

TEST_CASE("concurrent producers, single Drain consumer, no loss") {
    EditorConsoleSink sink;
    constexpr int kThreads = 6;
    constexpr int kPer = 2000;
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&sink] {
            for (int i = 0; i < kPer; ++i) {
                Log::LogMessage m; m.message = "x"; sink.Write(m);
            }
        });
    }
    std::size_t total = 0;
    // producer가 도는 동안 main이 주기적으로 Drain(에디터 프레임을 모사)
    while (total < static_cast<std::size_t>(kThreads * kPer)) {
        total += sink.Drain().size();
    }
    for (auto& p : producers) p.join();
    total += sink.Drain().size();
    CHECK(total == static_cast<std::size_t>(kThreads * kPer));  // 유실 없음
}
