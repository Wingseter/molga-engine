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

// 저작 컴포넌트 세터가 아닌 곳에서 의미 세대를 올리는 단 하나의 입구.
// 계층 변경(활성/추가/삭제/재부모/형제 순서)과 프로세스 전역 불변 자원의
// 교체(폰트 face 바이트, family fallback/style map, 확정된 텍스트 레이아웃,
// 검증된 텍스처 내용 SHA)가 전부 여기로 온다. UIComponent::Invalidate는
// 자기 revision도 함께 올려야 하므로 이 함수를 쓰지 않는다.
//
// 반드시 "성공한 변경 뒤에 정확히 한 번"이다. 실패했거나 아무것도 바뀌지
// 않은 재로드에서 부르면 캐시가 매 프레임 미스가 되고, 그 미스는 진단 하나
// 없이 성능으로만 드러난다 — 그래서 호출부가 먼저 실제 변경 여부를 판정한다.
//
// 반환값은 세대가 아직 소진되지 않았는가이다. 소진되면 false이며, 그 뒤로는
// 옛 세대가 새 상태를 가리키지 않도록 캐시가 통째로 우회된다.
bool NotifyUISemanticMutation() noexcept;

// ── 은퇴한 월드 세대 회수 ───────────────────────────────────────────────────
// UI 런타임 캐시는 월드 세대로 칸을 나눈다. 세대가 은퇴하면(파괴, Clear,
// 씬 로드, 이동 대입, 외부 교체) 그 칸을 지울 사람이 필요하다. 지우지 않으면
// 죽은 월드마다 기하 LRU 한 벌과 살아 있는 스냅샷 하나가 영원히 남는다 —
// 에디터의 Play/Stop과 Open Scene, 그리고 패키징된 런타임의 씬 전환이 전부
// 이 경로다.
//
// 핸들러를 두는 이유는 방향 때문이다. World는 Core에 있고 캐시는 UI에 있으므로,
// UI 쪽이 자신을 등록하고 Core는 이름만 부른다.
using UIWorldReleaseHandler = void (*)(std::uint64_t);
// 이전 핸들러를 돌려준다. 테스트가 잠시 가로챈 뒤 되돌려 놓을 수 있어야
// 한 번 설치되고 마는 프로덕션 배선이 그 테스트 이후로 죽지 않는다.
UIWorldReleaseHandler SetUIWorldReleaseHandler(
    UIWorldReleaseHandler handler) noexcept;
void NotifyUIWorldReleased(std::uint64_t worldGeneration) noexcept;

// ── 은퇴한 장치 세대 회수 ───────────────────────────────────────────────────
// 월드 축과 같은 모양의 나머지 절반이다. 월드 축에는 다섯 개의 프로덕션
// 알림이 있고 장치 축에는 **생성 하나**뿐이었다 — 그리고 생성 시점의 캐시는
// 반드시 비어 있으므로 그 알림은 관찰될 수 있는 일을 하지 않는다. 실제로
// 장치에 묶인 스냅샷을 놓아야 하는 순간은 장치가 **은퇴할 때**이고, 그때
// GraphicsDevice는 아무에게도 알리지 않았다.
//
// 알리지 않으면 UILayoutSystem이 죽은 세대의 텍스처 핸들과 바인딩 수명
// 토큰을 담은 lastSnapshot/fullSlots를 계속 든다. 그 상태로 다음 장치가
// 서면 그 토큰들이 "외부 소유자"로 잡혀 다음 종료를 막는다.
using UIDeviceRetireHandler = void (*)(std::uint64_t);
UIDeviceRetireHandler SetUIDeviceRetireHandler(
    UIDeviceRetireHandler handler) noexcept;
void NotifyUIDeviceRetired(std::uint64_t deviceGeneration) noexcept;

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
