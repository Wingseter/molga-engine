#pragma once

#include <cstdint>

namespace molga::text_test {

// The test-only view of UnicodeAnalysis.cpp's two private seams.
//
// This translation unit links into test_unicode_text and
// test_unicode_not_ready and into nothing else; tests/CMakeLists.txt asserts
// exactly that at configure time, because molga::text::detail is an ordinary
// exported namespace and nothing else would stop a product target from listing
// this source.
//
// What that check proves is precisely this file's absence from the product
// targets it names — no more. The seams themselves live in molga_core, so a
// product translation unit that includes Text/UnicodeAnalysis.h can still call
// molga::text::detail::ExchangeAnalysisGenerationState without this wrapper.
// "No shipped code renumbers or resets an analysis generation" is therefore a
// reviewed rule, not a mechanically enforced one; see the header comment on
// those seams for why no runtime latch can enforce it either.

// Attempted ICU iterator/object creations this process has made, counted at
// the call site rather than after the fact, so a refused ubrk_open still
// registers. The fail-closed cases want this to stay put across a rejected
// Analyze.
std::uint64_t IcuObjectCreationCountForTest() noexcept;
void          ResetIcuObjectCreationCountForTest() noexcept;

// Restores whatever the process-wide generation allocator held when it was
// created. A case that latches exhaustion therefore cannot leak that latch
// into every later case in the same executable — which would turn them all
// into silent no-ops rather than failures.
class ScopedAnalysisGenerationOverride {
public:
    ScopedAnalysisGenerationOverride(const ScopedAnalysisGenerationOverride&) =
        delete;
    ScopedAnalysisGenerationOverride& operator=(
        const ScopedAnalysisGenerationOverride&) = delete;
    ScopedAnalysisGenerationOverride(
        ScopedAnalysisGenerationOverride&&) noexcept;
    ScopedAnalysisGenerationOverride& operator=(
        ScopedAnalysisGenerationOverride&&) noexcept;
    ~ScopedAnalysisGenerationOverride();

private:
    friend class UnicodeTextAnalyzerTestAccess;
    ScopedAnalysisGenerationOverride(std::uint64_t previousNext,
                                     bool          previousExhausted) noexcept;
    std::uint64_t previousNext_      = 1;
    bool          previousExhausted_ = false;
    bool          active_            = false;
};

class UnicodeTextAnalyzerTestAccess {
public:
    // Makes the next allocated generation exactly `next` and clears the
    // exhaustion latch, returning the RAII restore for the previous state.
    static ScopedAnalysisGenerationOverride SetNextGeneration(
        std::uint64_t next) noexcept;
};

}  // namespace molga::text_test
