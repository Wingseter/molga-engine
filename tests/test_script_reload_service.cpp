#include "Scripting/ScriptReloadService.h"
#include "doctest.h"

using molga::ScriptReloadService;
using molga::ReloadOutcome;

namespace {
// 검증 성공/실패와 스왑 횟수를 추적하는 가짜 라이브러리 포트.
struct FakeLibraryPort : molga::ILibraryPort {
    bool validateOk = true;
    int  swaps = 0;
    std::string active = "lib.v1";
    bool Validate(const std::string&, std::string& err) override {
        if (!validateOk) { err = "bad library"; return false; }
        return true;
    }
    void Swap(const std::string& path) override { active = path; ++swaps; }
    std::string Active() const override { return active; }
};
}

TEST_CASE("successful validation swaps to the new library") {
    FakeLibraryPort lib;
    ScriptReloadService svc(&lib);
    ReloadOutcome out = svc.PerformReload("lib.v2");
    CHECK(out == ReloadOutcome::Reloaded);
    CHECK(lib.active == "lib.v2");
    CHECK(lib.swaps == 1);
}

TEST_CASE("failed validation keeps the last-good library (no swap)") {
    FakeLibraryPort lib;
    lib.validateOk = false;
    ScriptReloadService svc(&lib);
    ReloadOutcome out = svc.PerformReload("lib.broken");
    CHECK(out == ReloadOutcome::ValidationFailed);
    CHECK(lib.active == "lib.v1");   // 변하지 않음 = last-good 유지
    CHECK(lib.swaps == 0);
}

TEST_CASE("reload requested during play mode is deferred until edit mode") {
    FakeLibraryPort lib;
    ScriptReloadService svc(&lib);
    svc.RequestReload("lib.v2");

    // play 중: pump는 아무 것도 하지 않는다
    CHECK(svc.PumpPendingReload(/*isEditMode=*/false) == ReloadOutcome::Deferred);
    CHECK(lib.swaps == 0);

    // stop 후 edit 모드: 큐된 reload가 수행된다
    CHECK(svc.PumpPendingReload(/*isEditMode=*/true) == ReloadOutcome::Reloaded);
    CHECK(lib.active == "lib.v2");
}

TEST_CASE("pump with no pending reload is a no-op") {
    FakeLibraryPort lib;
    ScriptReloadService svc(&lib);
    CHECK(svc.PumpPendingReload(true) == ReloadOutcome::Idle);
}
