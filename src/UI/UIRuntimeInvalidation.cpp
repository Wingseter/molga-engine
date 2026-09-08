#include "UI/UIRuntimeInvalidation.h"

#include <atomic>
#include <cstddef>
#include <limits>

namespace molga::ui {

namespace {

constexpr std::uint64_t kExhausted = std::numeric_limits<std::uint64_t>::max();
constexpr std::size_t kGenerationCount = 4;

// 네 축이 한 배열에 산다. 축을 늘리면 kGenerationCount와 Current()가 함께
// 바뀌어야 하므로, 아래 static_assert가 둘 중 하나만 고친 변경을 컴파일에서
// 잡는다.
std::atomic<std::uint64_t>& Counter(UIRuntimeGenerationKind kind) noexcept {
    static std::atomic<std::uint64_t> counters[kGenerationCount] = {
        std::atomic<std::uint64_t>{1}, std::atomic<std::uint64_t>{1},
        std::atomic<std::uint64_t>{1}, std::atomic<std::uint64_t>{1}};
    return counters[static_cast<std::size_t>(kind)];
}

static_assert(static_cast<std::size_t>(UIRuntimeGenerationKind::Device) + 1 ==
                  kGenerationCount,
              "every UIRuntimeGenerationKind needs its own counter");

} // namespace

// 취득(acquire)/해제(release)로 읽고 쓴다. relaxed였다면 각 값이 한 번씩만
// 발행되는 것은 보장되지만 자원 쓰기와 세대 발행 사이에 happens-before가 없어,
// 새 세대를 관측한 스레드가 그 세대가 가리키는 자원을 아직 못 볼 수 있다.
// Task 11이 이 반환값을 텍스처/디바이스 바인딩의 정체성으로 그대로 발행한다.
UIRuntimeGenerationSnapshot UIRuntimeInvalidationClock::Current() noexcept {
    UIRuntimeGenerationSnapshot snapshot;
    snapshot.semanticDirtyGeneration =
        Counter(UIRuntimeGenerationKind::SemanticDirty)
            .load(std::memory_order_acquire);
    snapshot.scrollDisplacementGeneration =
        Counter(UIRuntimeGenerationKind::ScrollDisplacement)
            .load(std::memory_order_acquire);
    snapshot.textureBindingGeneration =
        Counter(UIRuntimeGenerationKind::TextureBinding)
            .load(std::memory_order_acquire);
    snapshot.deviceGeneration = Counter(UIRuntimeGenerationKind::Device)
                                    .load(std::memory_order_acquire);
    // 축 하나만 소진돼도 캐시 정체성 전체가 더 이상 상태를 구분하지 못한다.
    snapshot.cacheable = snapshot.semanticDirtyGeneration != kExhausted &&
                         snapshot.scrollDisplacementGeneration != kExhausted &&
                         snapshot.textureBindingGeneration != kExhausted &&
                         snapshot.deviceGeneration != kExhausted;
    return snapshot;
}

std::optional<std::uint64_t> UIRuntimeInvalidationClock::Advance(
    UIRuntimeGenerationKind kind) noexcept {
    auto& counter = Counter(kind);
    auto candidate = counter.load(std::memory_order_acquire);
    for (;;) {
        // 증가 전에 검사한다. 0은 애초에 발행되지 않는 값이고 UINT64_MAX는
        // 마지막으로 발행된 값이므로, 둘 중 하나면 더 발행할 수 없다.
        if (candidate == 0 || candidate == kExhausted) return std::nullopt;
        if (counter.compare_exchange_weak(candidate, candidate + 1,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            return candidate + 1;
        }
    }
}

namespace {
// 프로세스 하나에 UI 런타임은 하나다. 등록되지 않았다면 UI가 한 번도 만들어진
// 적이 없다는 뜻이고, 그때는 비울 캐시도 없다.
UIWorldReleaseHandler g_worldReleaseHandler = nullptr;
} // namespace

UIWorldReleaseHandler SetUIWorldReleaseHandler(
    UIWorldReleaseHandler handler) noexcept {
    UIWorldReleaseHandler previous = g_worldReleaseHandler;
    g_worldReleaseHandler = handler;
    return previous;
}

void NotifyUIWorldReleased(std::uint64_t worldGeneration) noexcept {
    if (worldGeneration == 0 || !g_worldReleaseHandler) return;
    g_worldReleaseHandler(worldGeneration);
}

bool NotifyUISemanticMutation() noexcept {
    return UIRuntimeInvalidationClock::Advance(
               UIRuntimeGenerationKind::SemanticDirty)
        .has_value();
}

ScopedUIRuntimeGenerationForTesting::ScopedUIRuntimeGenerationForTesting(
    UIRuntimeGenerationKind kind, std::uint64_t value) noexcept
    : kind_(kind),
      previous_(Counter(kind).exchange(value, std::memory_order_relaxed)) {}

ScopedUIRuntimeGenerationForTesting::~ScopedUIRuntimeGenerationForTesting() {
    // 훅이 세운 값은 프로덕션이 발행한 적 없는 값이므로 통째로 되돌린다.
    // 되돌리지 않으면 소진된 시계가 프로세스 남은 수명 내내 남아, 뒤따르는
    // 케이스가 전부 cacheable=false를 본다.
    Counter(kind_).store(previous_, std::memory_order_relaxed);
}

} // namespace molga::ui
