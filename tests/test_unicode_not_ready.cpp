// A whole executable whose only job is to watch the analyzer refuse before any
// ICU lifetime exists.
//
// It is deliberately not session-backed: it defines no staged Engine/Text root,
// links the ordinary doctest main, and never calls Initialize, Shutdown or
// u_cleanup. TextRuntimeDependencies therefore stays in NeverInitialized for
// the whole process, and process exit — not a stop/restore seam — is the
// isolation. That matters because an ICU lifetime is terminal: there is no
// in-process way to get back to this state once a session has published one,
// which is exactly why the observation needs a process of its own.
#include "Text/TextDiagnostic.h"
#include "Text/TextRuntimeDependencies.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"
#include "UnicodeTextAnalyzerTestAccess.h"

#include "doctest.h"

namespace {

using molga::text_test::IcuObjectCreationCountForTest;
using molga::text_test::ResetIcuObjectCreationCountForTest;

bool HasDiagnostic(const molga::text::VectorTextDiagnosticSink& sink,
                   molga::text::TextDiagnosticCode code) {
    for (const auto& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

}  // namespace

// Without this the case below could pass in a process that had already run and
// cleaned up an ICU lifetime, which is a different refusal for a different
// reason. IsReady() is false on both sides of a lifetime, so only
// WasTerminallyCleaned() separates "never initialized" from "already gone".
TEST_CASE("this process never initialized a text runtime") {
    CHECK_FALSE(molga::text::TextRuntimeDependencies::Get().IsReady());
    CHECK_FALSE(
        molga::text::TextRuntimeDependencies::Get().WasTerminallyCleaned());
    CHECK(molga::text::TextRuntimeDependencies::Get()
              .OutstandingClientHandleCount() == 0);
}

TEST_CASE("Unicode analyzer creates no ICU handle before runtime ready") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build("A", sink);
    REQUIRE(buffer);
    ResetIcuObjectCreationCountForTest();
    CHECK_FALSE(molga::text::UnicodeTextAnalyzer::Analyze(
        *buffer, {}, sink));
    CHECK(IcuObjectCreationCountForTest() == 0);
    CHECK(HasDiagnostic(sink,
          molga::text::TextDiagnosticCode::DependencyInvalid));
}

// The refusal has to be the ready gate and nothing else. A malformed locale or
// an exhausted generation would also return nullopt with zero ICU objects, but
// would report LayoutInvalid; the case above accepts any diagnostic set that
// merely contains DependencyInvalid.
TEST_CASE("the not-ready refusal reports exactly one DependencyInvalid") {
    molga::text::VectorTextDiagnosticSink sink;
    auto buffer = molga::text::UnicodeTextBuffer::Build("A", sink);
    REQUIRE(buffer);
    CHECK_FALSE(molga::text::UnicodeTextAnalyzer::Analyze(
        *buffer, {"th-TH", molga::text::BaseDirection::RightToLeft}, sink));
    REQUIRE(sink.Diagnostics().size() == 1);
    CHECK(sink.Diagnostics().front().code ==
          molga::text::TextDiagnosticCode::DependencyInvalid);
    // Severity, not just code. The design has the caller promote Error to
    // Blocker; a refusal reported as Info would let an authoring path treat a
    // hard analysis failure as advice and carry on with no analysis at all.
    CHECK(sink.Diagnostics().front().severity ==
          molga::text::TextSeverity::Error);
    CHECK_FALSE(sink.Diagnostics().front().message.empty());
    CHECK_FALSE(sink.Diagnostics().front().remediation.empty());
}
