#include "Platform/NativeInputEvent.h"

#include <atomic>
#include <limits>

namespace molga::platform {

namespace {
// 마지막으로 낸 값. 0에서 시작하므로 첫 값은 1이다.
std::atomic<std::uint64_t> g_lastSequence{0};
}  // namespace

const NativeSurfaceBatchStart* NativeInputBatch::FindSurface(
    molga::WindowId windowId) const noexcept {
    const NativeSurfaceBatchStart* found = nullptr;
    for (const NativeSurfaceBatchStart& start : surfaceStarts) {
        if (start.windowId != windowId) continue;
        if (found != nullptr) return nullptr;  // 중복
        found = &start;
    }
    return found;
}

std::optional<std::uint64_t> AcquireNativeInputSequence() noexcept {
    std::uint64_t current = g_lastSequence.load(std::memory_order_relaxed);
    for (;;) {
        // 소진: 증가시키지 않는다. 감기면 0이나 이미 쓴 값이 나온다.
        if (current == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        if (g_lastSequence.compare_exchange_weak(current, current + 1,
                                                 std::memory_order_relaxed)) {
            return current + 1;
        }
    }
}

ScopedNativeInputSequenceForTesting::ScopedNativeInputSequenceForTesting(
    std::uint64_t value) noexcept
    : previous_(g_lastSequence.exchange(value)) {}

ScopedNativeInputSequenceForTesting::~ScopedNativeInputSequenceForTesting() {
    g_lastSequence.store(previous_);
}

std::uint64_t PeekNativeInputSequenceForTesting() noexcept {
    return g_lastSequence.load();
}

}  // namespace molga::platform
