#pragma once

namespace molga {

// GL과 분리된, Renderer Begin/Draw/End 계약을 강제하는 상태 머신.
// OpenGL 컨텍스트 없이 단위 테스트할 수 있도록 molga_core에 둔다.
class RenderPassState {
public:
    enum class Phase { Idle, Drawing };

    // 패스를 연다. 이미 Drawing이면 계약 위반(중첩 Begin)으로 false를 반환하고
    // 상태를 바꾸지 않는다.
    bool TryBegin();

    // 패스가 열려 있는 동안에만 true.
    bool CanDraw() const { return phase_ == Phase::Drawing; }

    // 패스를 닫는다. Drawing이 아니면 계약 위반으로 false를 반환한다.
    bool TryEnd();

    Phase phase() const { return phase_; }
    int violations() const { return violations_; }

private:
    Phase phase_ = Phase::Idle;
    int violations_ = 0;
};

} // namespace molga
