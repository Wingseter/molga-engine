#pragma once

#include "Common/Fixed26_6.h"

#include <cstdint>
#include <string>
#include <vector>

namespace molga::ui {

// ── UI 시간의 유일한 형태 ───────────────────────────────────────────────────
// UI 하위 시스템 안에서는 벽시계를 읽지 않는다. 관성도 caret 깜빡임도 이
// 값에서만 나아간다 — 런타임, 에디터에 박힌 Game View, 정규 리플레이가 모두
// 같은 값 계약을 먹이고, 그래서 같은 입력 열은 언제나 같은 상태를 만든다.
// 경과 시간이 필요하면 인자로 도착한다.
//
// tickIndex는 0이 아닌 엄격 증가 정수다. 0을 허용하면 "아직 아무 tick도 없음"과
// 첫 tick이 구별되지 않는다. deltaSeconds는 26.6 초이고 1 <= raw <= 64이다:
// raw 0은 시간이 흐르지 않은 tick이라 상태를 바꿀 수 없고, raw 64(1초)를 넘는
// 한 걸음은 감속/탄성 재귀가 한 프레임 안에서 발산하는 구간이다.
struct UIDeterministicTick {
    std::uint64_t tickIndex = 0;
    molga::Fixed26_6 deltaSeconds = molga::Fixed26_6::FromRaw(0);
    bool operator==(const UIDeterministicTick& other) const noexcept {
        return tickIndex == other.tickIndex &&
               deltaSeconds == other.deltaSeconds;
    }
    bool operator!=(const UIDeterministicTick& other) const noexcept {
        return !(*this == other);
    }
};

inline constexpr std::int32_t kUIDeterministicTickMaxDeltaRaw = 64;

// 값 하나의 검사. AdvanceTick이 이것을 다시 부르므로, 프레임 orchestrator가
// 검사를 잊어도 스크롤이 잘못된 tick을 소비하지는 않는다.
bool UIDeterministicTickIsValid(const UIDeterministicTick&) noexcept;

// 배치 전체의 검사. 프레임 orchestrator(Task 12.3의 UISystem)는 **묶음 전체**를
// 밟기 전에 거절한다 — 나쁜 tick 하나를 건너뛰고 나머지를 밟으면 리플레이와
// 런타임이 서로 다른 걸음 수를 걷고, 그 차이는 몇 프레임 뒤 오프셋으로만
// 드러난다. 여기서는 그 술어만 제공한다; 부르는 쪽은 Task 12.3의 것이다.
bool UIDeterministicTickBatchIsValid(
    const std::vector<UIDeterministicTick>&) noexcept;

// ── 정규 트레이스 ───────────────────────────────────────────────────────────
// 정확한 정수 tickIndex와 deltaSecondsRaw만 나간다. 타임스탬프도 부동소수
// 지속시간도 받지 않는다 — 그 둘 중 하나라도 있으면 리플레이가 원본과 다른
// 걸음을 밟을 수 있고, 그 순간 "정규 리플레이는 런타임과 같은 값을 먹는다"가
// 깨진다.
std::string EncodeCanonicalTicks(const std::vector<UIDeterministicTick>&);
// 묶음 전체가 유효할 때만 그 묶음을 돌려준다. 한 원소라도 어긋나면 빈
// 벡터다(fail-closed) — 절반만 돌려주면 리플레이가 조용히 짧아진다.
std::vector<UIDeterministicTick> DecodeCanonicalTicks(const std::string&);

} // namespace molga::ui
