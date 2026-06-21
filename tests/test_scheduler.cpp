#include "doctest.h"
#include "Core/Scheduler.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include "Scripting/Script.h"
#include <memory>

// ── Scheduler 단위 테스트 ──────────────────────────────────────────────────

TEST_CASE("Scheduler: Invoke fires once after the delay elapses") {
    Scheduler s;
    int calls = 0;
    const void* owner = &calls;
    s.Invoke(owner, 1, [&] { calls++; }, 1.0f);

    s.Tick(0.5f); CHECK(calls == 0);   // 누적 0.5
    s.Tick(0.5f); CHECK(calls == 1);   // 누적 1.0 -> 발화
    s.Tick(1.0f); CHECK(calls == 1);   // 1회성, 반복 없음
    CHECK(s.ActiveTimerCount() == 0);
}

TEST_CASE("Scheduler: InvokeRepeating fires every interval") {
    Scheduler s;
    int calls = 0;
    s.InvokeRepeating(&calls, 1, [&] { calls++; }, 1.0f, 1.0f);

    s.Tick(1.0f); CHECK(calls == 1);
    s.Tick(1.0f); CHECK(calls == 2);
    s.Tick(1.0f); CHECK(calls == 3);
    CHECK(s.ActiveTimerCount() == 1);  // 여전히 활성
}

TEST_CASE("Scheduler: CancelInvoke and IsInvoking") {
    Scheduler s;
    int calls = 0;
    const void* owner = &calls;
    s.InvokeRepeating(owner, 1, [&] { calls++; }, 1.0f, 1.0f);

    CHECK(s.IsInvoking(owner));
    s.CancelInvoke(owner);
    CHECK_FALSE(s.IsInvoking(owner));

    s.Tick(2.0f);
    CHECK(calls == 0);
}

TEST_CASE("Scheduler: coroutine ticks each frame until it returns false") {
    Scheduler s;
    int ticks = 0;
    float accum = 0.0f;
    s.StartCoroutine(&ticks, 1, [&](float dt) {
        ticks++;
        accum += dt;
        return accum < 1.0f;  // 누적 1초까지 유지
    });

    s.Tick(0.4f); CHECK(ticks == 1);  // 0.4 유지
    s.Tick(0.4f); CHECK(ticks == 2);  // 0.8 유지
    s.Tick(0.4f); CHECK(ticks == 3);  // 1.2 -> 종료
    CHECK(s.ActiveCoroutineCount() == 0);
    s.Tick(0.4f); CHECK(ticks == 3);  // 더 이상 호출 안 됨
}

TEST_CASE("Scheduler: CancelByGameObject cancels everything for that object") {
    Scheduler s;
    int a = 0, b = 0;
    s.Invoke(&a, 100, [&] { a++; }, 1.0f);
    s.StartCoroutine(&b, 100, [&](float) { b++; return true; });
    s.Invoke(&a, 200, [&] { a++; }, 1.0f);  // 다른 goId

    s.CancelByGameObject(100);
    s.Tick(1.0f);

    CHECK(a == 1);  // goId 200만 발화
    CHECK(b == 0);  // 코루틴 취소됨
}

TEST_CASE("Scheduler: scheduling inside a callback defers to the next tick") {
    Scheduler s;
    int outer = 0, inner = 0;
    s.Invoke(&outer, 1, [&] {
        outer++;
        s.Invoke(&inner, 1, [&] { inner++; }, 0.0f);
    }, 1.0f);

    s.Tick(1.0f);
    CHECK(outer == 1);
    CHECK(inner == 0);  // 같은 tick에서 발화하지 않음

    s.Tick(0.0f);
    CHECK(inner == 1);  // 다음 tick에 발화(delay 0)
}

// ── World/Script 통합 테스트 ───────────────────────────────────────────────

namespace {
class TimerScript : public Script {
public:
    SCRIPT_CLASS(TimerScript)
    int fired = 0;
    void Start() override { Invoke([this] { fired++; }, 1.0f); }
};
} // namespace

TEST_CASE("World::Update drives Script::Invoke") {
    World w;
    auto go = std::make_shared<GameObject>("GO");
    auto* s = static_cast<TimerScript*>(go->AddComponentRaw(new TimerScript()));
    w.Add(go);
    s->Start();  // 평소엔 StartPending이 호출; 테스트에선 직접 호출

    w.Update(0.5f); CHECK(s->fired == 0);
    w.Update(0.5f); CHECK(s->fired == 1);
    w.Update(1.0f); CHECK(s->fired == 1);  // 1회성
}

TEST_CASE("Destroying an object cancels its pending invokes") {
    World w;
    auto go = std::make_shared<GameObject>("GO");
    auto* s = static_cast<TimerScript*>(go->AddComponentRaw(new TimerScript()));
    w.Add(go);
    s->Start();

    w.Destroy(go.get(), 0.0f);
    w.FlushDeferred(0.0f);  // 실제 파괴 -> CancelByGameObject

    CHECK(w.GetScheduler()->ActiveTimerCount() == 0);

    // go 로컬 shared_ptr가 살아 있어 콜백이 발화했다면 fired가 증가할 것이나,
    // 취소되었으므로 0으로 유지된다.
    w.Update(2.0f);
    CHECK(s->fired == 0);
}
