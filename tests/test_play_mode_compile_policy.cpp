#include "Scripting/ScriptReloadService.h"
#include "doctest.h"

using molga::ScriptReloadService;
using molga::ReloadOutcome;

namespace {
struct FakeLibraryPort : molga::ILibraryPort {
    int swaps = 0; std::string active = "v1";
    bool Validate(const std::string&, std::string&) override { return true; }
    void Swap(const std::string& p) override { active = p; ++swaps; }
    std::string Active() const override { return active; }
};
}

TEST_CASE("compile during play queues reload until Stop") {
    FakeLibraryPort lib;
    ScriptReloadService svc(&lib);

    // 컴파일 성공 → reload 요청(엔진은 Play 중)
    svc.RequestReload("v2");

    // Play 동안 여러 프레임 pump: swap 없음
    for (int frame = 0; frame < 5; ++frame)
        CHECK(svc.PumpPendingReload(/*isEditMode=*/false) == ReloadOutcome::Deferred);
    CHECK(lib.swaps == 0);
    CHECK(svc.HasPending());

    // Stop → Edit: 정확히 한 번 reload
    CHECK(svc.PumpPendingReload(true) == ReloadOutcome::Reloaded);
    CHECK(lib.swaps == 1);
    CHECK(lib.active == "v2");
    CHECK_FALSE(svc.HasPending());
}
