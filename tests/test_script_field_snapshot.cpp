#include "Scripting/Script.h"
#include "doctest.h"
#include <nlohmann/json.hpp>

namespace {
// reload 시 "같은 클래스의 새 인스턴스"를 흉내내는 두 객체.
struct DummyScript : Script {
    float speed = 0.0f;
    int   lives = 0;
    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Float("Speed", &speed).Int("Lives", &lives);
    }
    const char* GetScriptName() const override { return "DummyScript"; }
};
}

TEST_CASE("field values survive a snapshot/restore round trip (reload simulation)") {
    DummyScript before;
    before.speed = 250.0f;
    before.lives = 3;

    nlohmann::json snap = before.SnapshotFields();   // unload 직전

    DummyScript after;                               // reload 후 새 인스턴스
    CHECK(after.speed == 0.0f);
    after.RestoreFields(snap);                       // 복원

    CHECK(after.speed == doctest::Approx(250.0f));
    CHECK(after.lives == 3);
}

TEST_CASE("snapshot of a script with no registered fields is empty but safe") {
    struct Bare : Script { const char* GetScriptName() const override { return "Bare"; } };
    Bare b;
    nlohmann::json snap = b.SnapshotFields();
    Bare b2;
    b2.RestoreFields(snap);   // no-op, must not throw
    CHECK(true);
}
