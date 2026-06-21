#include "Editor/EditorTaskService.h"
#include "Common/Log.h"
#include "Common/LogSink.h"
#include "doctest.h"
#include <memory>
#include <vector>
#include <thread>

using molga::EditorTaskService;
using molga::TaskCategory;
using molga::TaskState;
using molga::TaskId;

namespace {
struct CapturingSink : Log::ILogSink {
    std::vector<Log::LogMessage> received;
    void Write(const Log::LogMessage& m) override { received.push_back(m); }
};
}

TEST_CASE("Begin/Update/Finish drives a task through its state machine") {
    EditorTaskService svc;
    auto id = svc.Begin("Compile Scripts", TaskCategory::ScriptCompile);
    svc.MarkRunning(id);
    CHECK(svc.GetState(id) == TaskState::Running);

    svc.Update(id, 0.5f, "compiling Player.cpp");
    CHECK(svc.GetProgress(id) == doctest::Approx(0.5f));

    svc.Finish(id, TaskState::Succeeded);
    CHECK(svc.GetState(id) == TaskState::Succeeded);
}

TEST_CASE("Update routes a line into the log pipeline with the task's context") {
    Log::ClearSinks();
    auto sink = std::make_shared<CapturingSink>();
    Log::AddSink(sink);

    EditorTaskService svc;
    auto id = svc.Begin("Build Game", TaskCategory::Build);
    svc.Update(id, 0.25f, "copying assets");
    svc.Finish(id, TaskState::Succeeded);

    bool routed = false;
    for (auto& m : sink->received)
        if (m.context == Log::LogContext::Build && m.message.find("copying assets") != std::string::npos)
            routed = true;
    CHECK(routed);
    Log::ClearSinks();
}

TEST_CASE("ParseDiagnostic extracts path and line from a compiler error line") {
    Log::LogMessage m;
    // gcc/clang 형식: "Scripts/Player.cpp:42:10: error: expected ';'"
    bool ok = EditorTaskService::ParseDiagnostic(
        "Scripts/Player.cpp:42:10: error: expected ';'", m);
    CHECK(ok);
    CHECK(m.externalPath == "Scripts/Player.cpp");
    CHECK(m.externalLine == 42);
    CHECK(m.severity == Log::Severity::Error);
}

TEST_CASE("queued task is observable and transitions to running then succeeded") {
    EditorTaskService svc;
    TaskId id = svc.Begin("Compile UserScripts", TaskCategory::ScriptCompile);
    CHECK(svc.Get(id).state == TaskState::Queued);

    svc.MarkRunning(id);
    CHECK(svc.Get(id).state == TaskState::Running);

    svc.AppendLog(id, "=== CMake Build ===\n");
    svc.Complete(id, TaskState::Succeeded);

    auto info = svc.Get(id);
    CHECK(info.state == TaskState::Succeeded);
    CHECK(info.category == TaskCategory::ScriptCompile);
    CHECK(info.log.find("CMake Build") != std::string::npos);
}

TEST_CASE("logs appended from a worker thread are visible on the main thread") {
    EditorTaskService svc;
    TaskId id = svc.Begin("Compile", TaskCategory::ScriptCompile);
    svc.MarkRunning(id);

    std::thread worker([&]{
        for (int i = 0; i < 100; ++i) svc.AppendLog(id, "line\n");
        svc.Complete(id, TaskState::Failed);
    });
    worker.join();

    auto info = svc.Get(id);
    CHECK(info.state == TaskState::Failed);
    CHECK(info.log.size() >= 500);   // 100 * "line\n"
}

TEST_CASE("cancel before run yields Cancelled and skips work") {
    EditorTaskService svc;
    TaskId id = svc.Begin("Compile", TaskCategory::ScriptCompile);
    svc.RequestCancel(id);
    CHECK(svc.IsCancelRequested(id));
    svc.Complete(id, TaskState::Cancelled);
    CHECK(svc.Get(id).state == TaskState::Cancelled);
}
