#include "Rendering/GpuRetirementQueue.h"

#include <algorithm>
#include <utility>

namespace molga {

void GpuRetirementQueue::Enqueue(
    std::unique_ptr<IGpuCompletionFence> fence,
    std::vector<std::shared_ptr<const void>> lifetimes) {
    // fence 없이 불리는 것은 호출자의 오류다. 그래도 여기서 토큰을 놓아 버리면
    // 제출된 명령 밑에서 텍스처가 사라지므로, 증거가 없는 제출은 idle drain이
    // 증명해 줄 때까지 붙든다.
    if (!fence) {
        RetainWithoutFence(std::move(lifetimes));
        return;
    }
    submissions_.push_back(Submission{std::move(fence), std::move(lifetimes)});
}

void GpuRetirementQueue::RetainWithoutFence(
    std::vector<std::shared_ptr<const void>> lifetimes) {
    unfenced_.insert(unfenced_.end(),
                     std::make_move_iterator(lifetimes.begin()),
                     std::make_move_iterator(lifetimes.end()));
}

void GpuRetirementQueue::Poll() {
    // 항목마다 자기 fence에게 정확히 한 번 묻는다. 앞에서부터 훑다가 첫
    // 미신호에서 멈추면, 먼저 끝난 뒤쪽 제출이 앞 제출의 fence를 기다리며
    // page를 붙들고 있게 된다.
    const auto retired =
        std::remove_if(submissions_.begin(), submissions_.end(),
                       [](const Submission& submission) {
                           return submission.fence->IsSignaled();
                       });
    submissions_.erase(retired, submissions_.end());
}

void GpuRetirementQueue::DrainAfterGpuIdle() {
    // 호출자가 성공한 GPU idle wait를 증명한 뒤에만 불린다. 그 시점에는 신호
    // 여부를 다시 물을 이유가 없다 — 장치가 idle이면 아직 신호하지 않은
    // fence의 명령도 이미 끝나 있다. 여기서 놓아주지 않으면 fence 획득이
    // 실패했던 프로세스는 종료할 때까지 page를 영원히 붙든다.
    submissions_.clear();
    unfenced_.clear();
}

std::size_t GpuRetirementQueue::PendingSubmissionCount() const noexcept {
    return submissions_.size();
}

} // namespace molga
