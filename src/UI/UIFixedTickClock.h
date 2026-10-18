#pragma once

#include "UI/UIDeterministicTick.h"

#include <cstdint>
#include <vector>

namespace molga::text {
class TextDiagnosticSink;
}

namespace molga::ui {

// 프로덕션 UI 시간의 유일한 공급자. 경과 나노초를 받아 1/64초 tick으로 바꾼다.
//
// 벽시계를 읽지 않는다: 프레임 소유자가 한 프레임에 한 번 표본을 재고(Time::
// GetElapsedNanoseconds) 여기 넘긴다. 그래서 같은 경과 열은 어느 인스턴스에서나
// 같은 tick 열이 된다. 한 걸음에 못 미치는 나머지는 이월되고, 이월은 버려지지
// 않는다. 시계는 표면마다 하나씩 두는 값이다(공유하면 두 표면이 같은 tick을
// 나눠 가진다).
class UIFixedTickClock {
public:
    // 1/64초. 모든 tick의 deltaSeconds raw는 1이다.
    static constexpr std::uint64_t StepNanoseconds = 15'625'000;

    // 이월 + 경과 안의 완전한 걸음마다 tick 하나. 인덱스가 감기기 직전이거나
    // 이월 합이 넘치면 tick을 내지 않고 진단을 낸다(Blocker/Error, "ui.frame").
    std::vector<UIDeterministicTick> AdvanceElapsedNanoseconds(
        std::uint64_t elapsedNanoseconds, molga::text::TextDiagnosticSink& sink);

    static UIFixedTickClock WithNextTickIndexForTesting(std::uint64_t nextTickIndex);

private:
    std::uint64_t carryNanoseconds_ = 0;
    // 0은 "아직 tick 없음"과 구별되지 않으므로 1에서 시작한다.
    std::uint64_t nextTickIndex_ = 1;
    bool exhaustionReported_ = false;
};

}  // namespace molga::ui
