#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace molga {

// ── The GPU completion boundary ─────────────────────────────────────────────
// 제출된 명령이 끝났는지 묻는 유일한 질문이고, SDL 타입은 이 경계를 넘어오지
// 않는다. 이유는 둘이다. 반납 큐는 장치 없이 시험할 수 있어야 하고, 진짜
// fence로는 "아직 신호하지 않았다"를 결정적으로 관찰할 수 없다 — GPU가 먼저
// 끝내 버리면 그 단언은 통과하지 않는 것이 아니라 사라진다.
class IGpuCompletionFence {
public:
    virtual ~IGpuCompletionFence() = default;
    virtual bool IsSignaled() const noexcept = 0;
};

// ── The retirement queue ────────────────────────────────────────────────────
// 제출 하나가 붙들고 있던 수명 토큰들을, 그 제출의 fence가 신호한 뒤에만
// 놓아준다. 프레임 경계도 참조 계수도 반납의 근거가 아니다: CPU가 프레임을
// 넘겼다는 것은 GPU가 그 프레임의 명령을 다 읽었다는 뜻이 아니고, 마지막
// 참조가 사라졌다는 것도 마찬가지다.
//
// 증거가 없을 때의 안전한 쪽은 언제나 "붙들고 있기"다. fence를 얻지 못한
// 제출은 RetainWithoutFence로 들어가 Poll이 건드리지 않으며, 장치 idle이
// 증명된 DrainAfterGpuIdle에서만 풀린다.
//
// 단일 thread 전용이다. 렌더러의 프레임 경계에서만 불리고 아래 상태에는
// 잠금이 없다.
class GpuRetirementQueue {
public:
    void Enqueue(std::unique_ptr<IGpuCompletionFence>,
                 std::vector<std::shared_ptr<const void>> lifetimes);
    void RetainWithoutFence(
        std::vector<std::shared_ptr<const void>> lifetimes);
    void Poll();
    void DrainAfterGpuIdle();
    std::size_t PendingSubmissionCount() const noexcept;

private:
    struct Submission {
        std::unique_ptr<IGpuCompletionFence> fence;
        std::vector<std::shared_ptr<const void>> lifetimes;
    };

    // 삽입 순서를 유지하는 벡터다. fence는 제출 순서대로 신호하지 않으므로
    // 앞에서부터 하나씩 버리는 큐로는 만들 수 없고(가운데만 신호할 수 있다),
    // 남는 항목들의 순서는 결정적인 감사 로그를 위해 보존한다.
    std::vector<Submission> submissions_;
    std::vector<std::shared_ptr<const void>> unfenced_;
};

} // namespace molga
