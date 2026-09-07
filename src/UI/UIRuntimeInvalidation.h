#pragma once

#include <cstdint>
#include <optional>

namespace molga::ui {

// UI 스냅샷 캐시가 "무엇이 달라졌는가"를 묻는 네 축. 닫힌 열거이고, 축을
// 늘리는 것은 캐시 정체성 변경이므로 설계 개정을 거쳐야 한다.
enum class UIRuntimeGenerationKind : std::uint8_t {
    SemanticDirty, ScrollDisplacement, TextureBinding, Device
};

// 한 시점의 네 세대 값 묶음. 0은 어느 축에서도 발행되지 않으므로 "아직 아무
// 것도 없음"과 실제 세대를 혼동할 수 없다.
struct UIRuntimeGenerationSnapshot {
    std::uint64_t semanticDirtyGeneration = 1;
    std::uint64_t scrollDisplacementGeneration = 1;
    std::uint64_t textureBindingGeneration = 1;
    std::uint64_t deviceGeneration = 1;
    bool cacheable = true;
};

// 프로세스 전역 집계 무효화 시계.
//
// 감기지 않는다. fetch_add였다면 UINT64_MAX 다음이 0으로 감기고, 그 0은 이미
// 발행된 적 있는 값이라 옛 세대가 새 상태를 가리키게 된다 — 캐시가 지난
// 프레임의 스냅샷을 새 것으로 착각하는 정확한 경로다. 그래서 증가 전에
// 0/UINT64_MAX를 확인하고, 소진되면 그 자리에 멈춘 채 cacheable을 내린다.
//
// Advance는 저장한 값과 같은 값을 돌려준다. "증가시킨 뒤 다시 읽기"였다면
// 동시에 취득한 두 호출이 나중 값을 함께 읽어 서로 다른 자원에 같은 정체성을
// 붙일 수 있다(Task 11의 texture/device 바인딩이 이 반환값을 그대로 발행된
// 정체성으로 쓴다).
class UIRuntimeInvalidationClock {
public:
    static UIRuntimeGenerationSnapshot Current() noexcept;
    static std::optional<std::uint64_t> Advance(
        UIRuntimeGenerationKind) noexcept;
};

// 테스트 전용 훅. 소진 경계는 전역 원자값을 직접 세워 놓아야만 재현되고, 그
// 원자값은 .cpp의 익명 이름공간에 산다. 스코프를 벗어나면 직전 값으로
// 되돌리므로 다른 케이스가 소진된 시계를 물려받지 않는다.
class ScopedUIRuntimeGenerationForTesting {
public:
    ScopedUIRuntimeGenerationForTesting(UIRuntimeGenerationKind kind,
                                        std::uint64_t value) noexcept;
    ~ScopedUIRuntimeGenerationForTesting();

    ScopedUIRuntimeGenerationForTesting(
        const ScopedUIRuntimeGenerationForTesting&) = delete;
    ScopedUIRuntimeGenerationForTesting& operator=(
        const ScopedUIRuntimeGenerationForTesting&) = delete;

private:
    UIRuntimeGenerationKind kind_;
    std::uint64_t previous_;
};

} // namespace molga::ui
