#include "Rendering/RenderPassState.h"

namespace molga {

bool RenderPassState::TryBegin() {
    if (phase_ != Phase::Idle) {
        ++violations_;
        return false;
    }
    phase_ = Phase::Drawing;
    return true;
}

bool RenderPassState::TryEnd() {
    if (phase_ != Phase::Drawing) {
        ++violations_;
        return false;
    }
    phase_ = Phase::Idle;
    return true;
}

} // namespace molga
