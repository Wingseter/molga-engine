#include "Scheduler.h"
#include "Core/World.h"
#include <algorithm>

void Scheduler::Invoke(const void* owner, unsigned int goId, Action fn, float delay) {
    Timer t;
    t.owner = owner;
    t.goId = goId;
    t.fn = std::move(fn);
    t.remaining = delay;
    (ticking_ ? pendingTimers_ : timers_).push_back(std::move(t));
}

void Scheduler::InvokeRepeating(const void* owner, unsigned int goId, Action fn,
                                float delay, float interval) {
    Timer t;
    t.owner = owner;
    t.goId = goId;
    t.fn = std::move(fn);
    t.remaining = delay;
    t.interval = interval;
    t.repeating = true;
    (ticking_ ? pendingTimers_ : timers_).push_back(std::move(t));
}

void Scheduler::StartCoroutine(const void* owner, unsigned int goId, CoroutineStep step) {
    Coro c;
    c.owner = owner;
    c.goId = goId;
    c.step = std::move(step);
    (ticking_ ? pendingCoros_ : coros_).push_back(std::move(c));
}

void Scheduler::Invoke(const ScriptHandle& owner, Action fn, float delay) {
    Timer timer;
    timer.scriptOwner = owner;
    timer.goId = owner.objectId;
    timer.fn = std::move(fn);
    timer.remaining = delay;
    timer.scriptOwned = true;
    (ticking_ ? pendingTimers_ : timers_).push_back(std::move(timer));
}

void Scheduler::InvokeRepeating(const ScriptHandle& owner, Action fn,
                                float delay, float interval) {
    Timer timer;
    timer.scriptOwner = owner;
    timer.goId = owner.objectId;
    timer.fn = std::move(fn);
    timer.remaining = delay;
    timer.interval = interval;
    timer.repeating = true;
    timer.scriptOwned = true;
    (ticking_ ? pendingTimers_ : timers_).push_back(std::move(timer));
}

void Scheduler::StartCoroutine(const ScriptHandle& owner, CoroutineStep step) {
    Coro coroutine;
    coroutine.scriptOwner = owner;
    coroutine.goId = owner.objectId;
    coroutine.step = std::move(step);
    coroutine.scriptOwned = true;
    (ticking_ ? pendingCoros_ : coros_).push_back(std::move(coroutine));
}

void Scheduler::CancelInvoke(const void* owner) {
    for (auto& t : timers_) {
        if (!t.scriptOwned && t.owner == owner) t.active = false;
    }
    for (auto& t : pendingTimers_) {
        if (!t.scriptOwned && t.owner == owner) t.active = false;
    }
}

void Scheduler::StopCoroutines(const void* owner) {
    for (auto& c : coros_) {
        if (!c.scriptOwned && c.owner == owner) c.active = false;
    }
    for (auto& c : pendingCoros_) {
        if (!c.scriptOwned && c.owner == owner) c.active = false;
    }
}

bool Scheduler::IsInvoking(const void* owner) const {
    for (const auto& t : timers_) {
        if (t.active && !t.scriptOwned && t.owner == owner) return true;
    }
    for (const auto& t : pendingTimers_) {
        if (t.active && !t.scriptOwned && t.owner == owner) return true;
    }
    return false;
}

void Scheduler::CancelInvoke(const ScriptHandle& owner) {
    for (auto& timer : timers_) {
        if (timer.scriptOwned && timer.scriptOwner == owner) timer.active = false;
    }
    for (auto& timer : pendingTimers_) {
        if (timer.scriptOwned && timer.scriptOwner == owner) timer.active = false;
    }
}

void Scheduler::StopCoroutines(const ScriptHandle& owner) {
    for (auto& coroutine : coros_) {
        if (coroutine.scriptOwned && coroutine.scriptOwner == owner) coroutine.active = false;
    }
    for (auto& coroutine : pendingCoros_) {
        if (coroutine.scriptOwned && coroutine.scriptOwner == owner) coroutine.active = false;
    }
}

bool Scheduler::IsInvoking(const ScriptHandle& owner) const {
    for (const auto& timer : timers_) {
        if (timer.active && timer.scriptOwned && timer.scriptOwner == owner) return true;
    }
    for (const auto& timer : pendingTimers_) {
        if (timer.active && timer.scriptOwned && timer.scriptOwner == owner) return true;
    }
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
    struct TickCleanup {
        Scheduler& scheduler;
        ~TickCleanup() { scheduler.FinishTick(); }
    } cleanup{*this};

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
            if (t.scriptOwned) {
                if (!world_ || !ScriptInvocationBoundary::Invoke(
                        *world_, t.scriptOwner, ScriptPhase::Invoke,
                        [&](Script&) { t.fn(); })) {
                    t.active = false;
                }
            } else {
                // Generic engine callbacks intentionally remain fail-loud.
                // TickCleanup still restores scheduler invariants on unwind.
                try {
                    t.fn();
                } catch (...) {
                    t.active = false;
                    throw;
                }
            }
        }
    }

    // 코루틴
    for (std::size_t i = 0; i < coros_.size(); ++i) {
        Coro& c = coros_[i];
        if (!c.active) continue;
        if (c.scriptOwned) {
            bool keepRunning = false;
            const bool completed = world_ && ScriptInvocationBoundary::Invoke(
                *world_, c.scriptOwner, ScriptPhase::Coroutine,
                [&](Script&) { keepRunning = c.step(dt); });
            if (!completed || !keepRunning) c.active = false;
        } else {
            try {
                if (!c.step(dt)) c.active = false;
            } catch (...) {
                c.active = false;
                throw;
            }
        }
    }
}

void Scheduler::FinishTick() noexcept {
    ticking_ = false;

    if (clearRequested_) {
        timers_.clear();
        coros_.clear();
        pendingTimers_.clear();
        pendingCoros_.clear();
        clearRequested_ = false;
        return;
    }

    // Tick 중 추가된 항목 합류
    try {
        if (!pendingTimers_.empty()) {
            timers_.reserve(timers_.size() + pendingTimers_.size());
            for (auto& timer : pendingTimers_) timers_.push_back(std::move(timer));
            pendingTimers_.clear();
        }
        if (!pendingCoros_.empty()) {
            coros_.reserve(coros_.size() + pendingCoros_.size());
            for (auto& coroutine : pendingCoros_) coros_.push_back(std::move(coroutine));
            pendingCoros_.clear();
        }
    } catch (...) {
        // Cleanup runs from a destructor, including during exception unwind.
        // Preserve the original callback exception and leave no deferred work
        // stranded behind a stale ticking_ flag.
        pendingTimers_.clear();
        pendingCoros_.clear();
    }

    // 비활성 항목 제거
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                  [](const Timer& t) { return !t.active; }), timers_.end());
    coros_.erase(std::remove_if(coros_.begin(), coros_.end(),
                  [](const Coro& c) { return !c.active; }), coros_.end());
}

void Scheduler::Clear() {
    if (ticking_) {
        for (auto& timer : timers_) timer.active = false;
        for (auto& coroutine : coros_) coroutine.active = false;
        pendingTimers_.clear();
        pendingCoros_.clear();
        clearRequested_ = true;
        return;
    }
    timers_.clear();
    coros_.clear();
    pendingTimers_.clear();
    pendingCoros_.clear();
    clearRequested_ = false;
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
