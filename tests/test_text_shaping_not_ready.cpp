// Step 1g: the fresh-process not-ready shaping observation.
//
// This executable is deliberately generic — no text session main, no staged
// Engine/Text root, no initialization — so the process begins and stays in
// NeverInitialized and the shaper's ready gate is the only thing a case here
// can be observing. It never calls Initialize, Shutdown,
// hb_icu_get_unicode_funcs or u_cleanup; process exit supplies the isolation
// that an in-process stop/restore seam is not allowed to.
//
// The counter's positive witness lives in test_text_shaping, which shapes with
// a ready runtime and requires the count to move. Without that companion case
// a counter stubbed to return zero would satisfy everything here.

#include "Text/TextDiagnostic.h"
#include "Text/TextShapingService.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"
#include "doctest.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace molga::text {

// The definition of the friend class UnicodeAnalysis.h names, and the only one
// in the build. The header carries a friend declaration and nothing else, so
// molga_core exports no symbol that can assemble a UnicodeAnalysis from parts:
// a translation unit that wants that power has to write this class itself,
// which is a far louder act than calling an exported function and is exactly
// what makes the "test-only" claim reviewable.
//
// Why the seam is needed at all: Step 1g's case must call
// TextShapingService::ShapeAnalysisItem in a NeverInitialized process, its
// signature takes a const UnicodeAnalysis&, and UnicodeTextAnalyzer::Analyze —
// the only other way to obtain one — refuses to run there by construction.
class UnicodeAnalysisTestAccess {
public:
    static UnicodeAnalysis Assemble(
        std::vector<std::uint32_t> graphemeBoundaries,
        std::vector<std::uint32_t> lineBreakBoundaries,
        std::vector<AnalysisItem> items, UnicodeAnalysisIdentity identity) {
        UnicodeAnalysis analysis;
        analysis.graphemeBoundaries_  = std::move(graphemeBoundaries);
        analysis.lineBreakBoundaries_ = std::move(lineBreakBoundaries);
        analysis.items_               = std::move(items);
        analysis.identity_            = std::move(identity);
        return analysis;
    }
};

}  // namespace molga::text

namespace {

namespace text = molga::text;

// The test-access counter companion, compiled into this executable and no
// other. molga::text::detail is an ordinary exported namespace, so what keeps
// this wrapper out of other targets is the source list, not the namespace.
std::uint64_t HarfBuzzObjectCreationCountForTest() noexcept {
    return text::detail::HarfBuzzObjectCreationCount();
}
void ResetHarfBuzzObjectCreationCountForTest() noexcept {
    text::detail::ResetHarfBuzzObjectCreationCount();
}

bool HasDiagnostic(const text::VectorTextDiagnosticSink& sink,
                   text::TextDiagnosticCode code) {
    for (const text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

// Immutable request/resource values only: a byte-preserving buffer (which uses
// no ICU), a hand-assembled analysis, and an empty resolved family. Nothing
// here opens a font, a blob, a face or a HarfBuzz buffer.
class NotReadyShapingRequest {
public:
    NotReadyShapingRequest(text::UnicodeTextBuffer buffer,
                           text::UnicodeAnalysis analysis,
                           text::AnalysisItem item, std::string language)
        : buffer_(std::move(buffer)),
          analysis_(std::move(analysis)),
          item_(item) {
        style_.language = std::move(language);
    }

    text::VectorTextDiagnosticSink sink;

    std::optional<std::vector<text::ShapedRun>> ShapeFirstItem() {
        return service_.ShapeAnalysisItem(buffer_, analysis_, item_, family_,
                                          style_, {true, true}, sink);
    }

private:
    text::UnicodeTextBuffer buffer_;
    text::UnicodeAnalysis analysis_;
    text::AnalysisItem item_;
    text::ResolvedFamily family_;
    text::ShapeStyle style_;
    text::TextShapingService service_;
};

NotReadyShapingRequest BuildNotReadyShapingRequestWithoutHarfBuzz(
    const std::string& utf8, const std::string& language) {
    text::VectorTextDiagnosticSink buildSink;
    auto buffer = text::UnicodeTextBuffer::Build(utf8, buildSink);
    REQUIRE(buffer);
    REQUIRE(buffer->Scalars().size() == 1U);
    const std::uint32_t byteLength = static_cast<std::uint32_t>(utf8.size());
    const std::uint32_t unitLength =
        static_cast<std::uint32_t>(buffer->SanitizedUtf16().size());

    // A well-formed single-grapheme item, not a placeholder, so the counter is
    // reading a shaper that had real work to refuse rather than one that had
    // nothing to do.
    text::AnalysisItem item;
    item.sourceBytes = {0, byteLength};
    item.utf16Units = {0, unitLength};
    item.graphemes = {0, 1};
    item.paragraphStart = true;
    item.paragraphEnd = true;

    text::UnicodeAnalysisIdentity identity;
    identity.resolvedGraphemeLocale = language;
    identity.resolvedLineBreakLocale = language;
    identity.analysisGeneration = 1;
    text::UnicodeAnalysis analysis =
        text::UnicodeAnalysisTestAccess::Assemble({0, byteLength},
                                                  {0, byteLength}, {item},
                                                  identity);
    return NotReadyShapingRequest(std::move(*buffer), std::move(analysis), item,
                                  language);
}

} // namespace

TEST_CASE("shaper creates no HarfBuzz object before runtime ready") {
    auto fixture = BuildNotReadyShapingRequestWithoutHarfBuzz("A", "und");
    ResetHarfBuzzObjectCreationCountForTest();
    CHECK_FALSE(fixture.ShapeFirstItem());
    CHECK(HarfBuzzObjectCreationCountForTest() == 0);
    CHECK(HasDiagnostic(fixture.sink,
          molga::text::TextDiagnosticCode::DependencyInvalid));
}
