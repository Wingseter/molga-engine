#pragma once

#include "Scripting/ScriptInvocationBoundary.h"

#include <functional>
#include <vector>

class World;

// 지연/반복 콜백(Invoke)과 프레임 단위 코루틴을 관리하는 스케줄러.
// World가 소유하며 매 프레임 Tick(dt)로 구동된다.
//
// 각 항목은 owner(보통 Script*)와 goId(소유 GameObject id)로 태깅된다.
//   - owner 기준: 스크립트별 CancelInvoke/StopCoroutines/IsInvoking
//   - goId 기준: 오브젝트 파괴 시 안전한 일괄 정리(CancelByGameObject)
class Scheduler {
public:
    using Action = std::function<void()>;
    // 매 프레임 dt와 함께 호출되며, true를 반환하면 계속, false면 종료한다.
    using CoroutineStep = std::function<bool(float)>;

    explicit Scheduler(World* world = nullptr) : world_(world) {}
    void SetWorld(World* world) { world_ = world; }

    // delay초 뒤 fn을 1회 호출.
    void Invoke(const void* owner, unsigned int goId, Action fn, float delay);
    // delay초 뒤 처음 호출하고, 이후 interval초마다 반복 호출.
    void InvokeRepeating(const void* owner, unsigned int goId, Action fn, float delay, float interval);
    // 매 프레임 step(dt)를 호출(false 반환 시 종료).
    void StartCoroutine(const void* owner, unsigned int goId, CoroutineStep step);

    // Script-owned overloads never retain a Script pointer. The handle is
    // resolved by ScriptInvocationBoundary immediately before each callback.
    void Invoke(const ScriptHandle& owner, Action fn, float delay);
    void InvokeRepeating(const ScriptHandle& owner, Action fn,
                         float delay, float interval);
    void StartCoroutine(const ScriptHandle& owner, CoroutineStep step);

    void CancelInvoke(const void* owner);        // owner의 모든 Invoke 취소
    void StopCoroutines(const void* owner);       // owner의 모든 코루틴 종료
    bool IsInvoking(const void* owner) const;     // owner에 대기 중인 Invoke가 있는가

    void CancelInvoke(const ScriptHandle& owner);
    void StopCoroutines(const ScriptHandle& owner);
    bool IsInvoking(const ScriptHandle& owner) const;

    void CancelByGameObject(unsigned int goId);   // 오브젝트 파괴 시 전체 취소

    void Tick(float dt);
    void Clear();

    // 디버그/테스트용 카운트
    std::size_t ActiveTimerCount() const;
    std::size_t ActiveCoroutineCount() const;

private:
    struct Timer {
        const void* owner = nullptr;
        ScriptHandle scriptOwner;
        unsigned int goId = 0;
        Action fn;
        float remaining = 0.0f;
        float interval = 0.0f;
        bool repeating = false;
        bool active = true;
        bool scriptOwned = false;
    };
    struct Coro {
        const void* owner = nullptr;
        ScriptHandle scriptOwner;
        unsigned int goId = 0;
        CoroutineStep step;
        bool active = true;
        bool scriptOwned = false;
    };

    void FinishTick() noexcept;

    std::vector<Timer> timers_;
    std::vector<Coro> coros_;
    // Tick 도중 추가된 항목은 다음 Tick에 합류(반복 중 컨테이너 변형 방지).
    std::vector<Timer> pendingTimers_;
    std::vector<Coro> pendingCoros_;
    bool ticking_ = false;
    bool clearRequested_ = false;
    World* world_ = nullptr;
};
