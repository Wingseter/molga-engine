#include "UI/UIFixedTickClock.h"

#include "Text/TextDiagnostic.h"

#include <limits>
#include <utility>

namespace molga::ui {

namespace {

void Report(molga::text::TextDiagnosticSink& sink, molga::text::TextSeverity severity,
            std::string message, std::string remediation) {
    molga::text::TextDiagnostic diagnostic;
    diagnostic.code = molga::text::TextDiagnosticCode::LayoutInvalid;
    diagnostic.severity = severity;
    diagnostic.subsystem = "ui.frame";
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    sink.Report(std::move(diagnostic));
}

}  // namespace

std::vector<UIDeterministicTick> UIFixedTickClock::AdvanceElapsedNanoseconds(
    std::uint64_t elapsedNanoseconds, molga::text::TextDiagnosticSink& sink) {
    std::vector<UIDeterministicTick> ticks;
    // 합이 넘치면 값을 지어내지 않는다: 아무것도 내지 않고 이월도 그대로다.
    if (elapsedNanoseconds > std::numeric_limits<std::uint64_t>::max() - carryNanoseconds_) {
        Report(sink, molga::text::TextSeverity::Error,
               "the elapsed time overflowed the UI tick clock carry",
               "sample the monotonic clock once per frame");
        return ticks;
    }
    const std::uint64_t total = carryNanoseconds_ + elapsedNanoseconds;
    std::uint64_t remaining = total / StepNanoseconds;
    std::uint64_t carry = total % StepNanoseconds;

    while (remaining > 0) {
        // 다음 인덱스가 UINT64_MAX이면 그 다음은 감긴다. 감기기 전에 멈춘다.
        if (nextTickIndex_ == std::numeric_limits<std::uint64_t>::max()) {
            if (!exhaustionReported_) {
                exhaustionReported_ = true;
                Report(sink, molga::text::TextSeverity::Blocker,
                       "the UI tick index is exhausted and no further ticks "
                       "are produced",
                       "restart the process; the index never wraps");
            }
            // 내지 못한 시간은 이월에 남긴다.
            carry += remaining * StepNanoseconds;
            break;
        }
        UIDeterministicTick tick;
        tick.tickIndex = nextTickIndex_++;
        tick.deltaSeconds = molga::Fixed26_6::FromRaw(1);
        ticks.push_back(tick);
        --remaining;
    }
    carryNanoseconds_ = carry;
    return ticks;
}

UIFixedTickClock UIFixedTickClock::WithNextTickIndexForTesting(std::uint64_t nextTickIndex) {
    UIFixedTickClock clock;
    clock.nextTickIndex_ = nextTickIndex;
    return clock;
}

}  // namespace molga::ui
