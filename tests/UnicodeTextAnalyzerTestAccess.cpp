#include "UnicodeTextAnalyzerTestAccess.h"

#include "Text/UnicodeAnalysis.h"

#include <utility>

namespace molga::text_test {

std::uint64_t IcuObjectCreationCountForTest() noexcept {
    return molga::text::detail::IcuObjectCreationCount();
}

void ResetIcuObjectCreationCountForTest() noexcept {
    molga::text::detail::ResetIcuObjectCreationCount();
}

ScopedAnalysisGenerationOverride::ScopedAnalysisGenerationOverride(
    std::uint64_t previousNext, bool previousExhausted) noexcept
    : previousNext_(previousNext),
      previousExhausted_(previousExhausted),
      active_(true) {}

ScopedAnalysisGenerationOverride::ScopedAnalysisGenerationOverride(
    ScopedAnalysisGenerationOverride&& other) noexcept
    : previousNext_(other.previousNext_),
      previousExhausted_(other.previousExhausted_),
      active_(other.active_) {
    other.active_ = false;
}

ScopedAnalysisGenerationOverride& ScopedAnalysisGenerationOverride::operator=(
    ScopedAnalysisGenerationOverride&& other) noexcept {
    if (this != &other) {
        if (active_) {
            molga::text::detail::ExchangeAnalysisGenerationState(
                {previousNext_, previousExhausted_});
        }
        previousNext_      = other.previousNext_;
        previousExhausted_ = other.previousExhausted_;
        active_            = other.active_;
        other.active_      = false;
    }
    return *this;
}

ScopedAnalysisGenerationOverride::~ScopedAnalysisGenerationOverride() {
    if (!active_) return;
    active_ = false;
    molga::text::detail::ExchangeAnalysisGenerationState(
        {previousNext_, previousExhausted_});
}

ScopedAnalysisGenerationOverride
UnicodeTextAnalyzerTestAccess::SetNextGeneration(std::uint64_t next) noexcept {
    const molga::text::detail::AnalysisGenerationState previous =
        molga::text::detail::ExchangeAnalysisGenerationState({next, false});
    return ScopedAnalysisGenerationOverride(previous.next, previous.exhausted);
}

}  // namespace molga::text_test
