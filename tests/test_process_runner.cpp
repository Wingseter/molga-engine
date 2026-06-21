#include "Platform/Process.h"
#include "doctest.h"
#include <vector>
#include <string>

using molga::IProcessRunner;
using molga::ProcessResult;

namespace {
// 정해진 줄을 콜백으로 흘리고 정해진 exit code를 돌려주는 가짜 러너.
struct FakeProcessRunner : IProcessRunner {
    std::vector<std::string> lines;
    int exitCode = 0;
    ProcessResult Run(const std::string& /*cmd*/, const std::string& /*workdir*/,
                      const std::function<void(const std::string&)>& onLine,
                      const std::function<bool()>& isCancelled) override {
        for (auto& l : lines) {
            if (isCancelled && isCancelled()) return { -1, true };
            onLine(l);
        }
        return { exitCode, false };
    }
};
}

TEST_CASE("runner streams each line then reports exit code") {
    FakeProcessRunner r;
    r.lines = { "configuring\n", "building\n", "done\n" };
    r.exitCode = 0;

    std::string captured;
    ProcessResult res = r.Run("cmake --build build", "/proj/Scripts",
        [&](const std::string& l){ captured += l; },
        []{ return false; });

    CHECK(res.exitCode == 0);
    CHECK_FALSE(res.cancelled);
    CHECK(captured == "configuring\nbuilding\ndone\n");
}

TEST_CASE("cancellation stops streaming and marks cancelled") {
    FakeProcessRunner r;
    r.lines = { "a\n", "b\n", "c\n" };

    int seen = 0;
    ProcessResult res = r.Run("cmd", "/wd",
        [&](const std::string&){ ++seen; },
        [&]{ return seen >= 1; });   // 첫 줄 뒤 취소

    CHECK(res.cancelled);
    CHECK(seen == 1);
}
