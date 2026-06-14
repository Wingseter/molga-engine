#include "Scheduler.h"
#include <algorithm>

void Scheduler::Invoke(const void* owner, unsigned int goId, Action fn, float delay) {
    Timer t{ owner, goId, std::move(fn), delay, 0.0f, false, true };
    (ticking_ ? pendingTimers_ : timers_).push_back(std::move(t));
}

void Scheduler::InvokeRepeating(const void* owner, unsigned int goId, Action fn,
                                float delay, float interval) {
    Timer t{ owner, goId, std::move(fn), delay, interval, true, true };
    (ticking_ ? pendingTimers_ : timers_).push_back(std::move(t));
}

void Scheduler::StartCoroutine(const void* owner, unsigned int goId, CoroutineStep step) {
    Coro c{ owner, goId, std::move(step), true };
    (ticking_ ? pendingCoros_ : coros_).push_back(std::move(c));
}

void Scheduler::CancelInvoke(const void* owner) {
    for (auto& t : timers_)        if (t.owner == owner) t.active = false;
    for (auto& t : pendingTimers_) if (t.owner == owner) t.active = false;
}

void Scheduler::StopCoroutines(const void* owner) {
    for (auto& c : coros_)        if (c.owner == owner) c.active = false;
    for (auto& c : pendingCoros_) if (c.owner == owner) c.active = false;
}

bool Scheduler::IsInvoking(const void* owner) const {
    for (const auto& t : timers_)        if (t.active && t.owner == owner) return true;
    for (const auto& t : pendingTimers_) if (t.active && t.owner == owner) return true;
    return false;
}

void Scheduler::CancelByGameObject(unsigned int goId) {
    for (auto& t : timers_)        if (t.goId == goId) t.active = false;
    for (auto& t : pendingTimers_) if (t.goId == goId) t.active = false;
    for (auto& c : coros_)        if (c.goId == goId) c.active = false;
    for (auto& c : pendingCoros_) if (c.goId == goId) c.active = false;
}

void Scheduler::Tick(float dt) {
    ticking_ = true;

    // 타이머: 새 추가는 pending으로 가므로 timers_는 이 루프 동안 크기가 변하지 않는다.
    for (std::size_t i = 0; i < timers_.size(); ++i) {
        Timer& t = timers_[i];
        if (!t.active) continue;
        t.remaining -= dt;
        if (t.remaining <= 0.0f) {
            if (t.repeating) {
                t.remaining += t.interval;
                // interval이 0 이하라면 매 프레임 1회로 제한(프레임 내 무한 호출 방지).
                if (t.remaining <= 0.0f) t.remaining = 0.0f;
            } else {
                t.active = false;
            }
            t.fn();  // 콜백이 Invoke/Cancel을 호출해도 안전(아래 참고).
        }
    }

    // 코루틴
    for (std::size_t i = 0; i < coros_.size(); ++i) {
        Coro& c = coros_[i];
        if (!c.active) continue;
        if (!c.step(dt)) c.active = false;
    }

    ticking_ = false;

    // Tick 중 추가된 항목 합류
    if (!pendingTimers_.empty()) {
        for (auto& t : pendingTimers_) timers_.push_back(std::move(t));
        pendingTimers_.clear();
    }
    if (!pendingCoros_.empty()) {
        for (auto& c : pendingCoros_) coros_.push_back(std::move(c));
        pendingCoros_.clear();
    }

    // 비활성 항목 제거
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                  [](const Timer& t) { return !t.active; }), timers_.end());
    coros_.erase(std::remove_if(coros_.begin(), coros_.end(),
                  [](const Coro& c) { return !c.active; }), coros_.end());
}

void Scheduler::Clear() {
    timers_.clear();
    coros_.clear();
    pendingTimers_.clear();
    pendingCoros_.clear();
}

std::size_t Scheduler::ActiveTimerCount() const {
    std::size_t n = 0;
    for (const auto& t : timers_) if (t.active) ++n;
    for (const auto& t : pendingTimers_) if (t.active) ++n;
    return n;
}

std::size_t Scheduler::ActiveCoroutineCount() const {
    std::size_t n = 0;
    for (const auto& c : coros_) if (c.active) ++n;
    for (const auto& c : pendingCoros_) if (c.active) ++n;
    return n;
}
