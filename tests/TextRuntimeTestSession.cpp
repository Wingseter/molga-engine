#include "TextRuntimeTestSession.h"

#include "Text/TextDiagnostic.h"
#include "Text/TextRuntimeDependencies.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

TextRuntimeTestSession* g_current = nullptr;

}  // namespace

TextRuntimeTestSession::TextRuntimeTestSession(
    std::filesystem::path engineTextRoot)
    : engineTextRoot_(std::move(engineTextRoot)) {}

TextRuntimeTestSession::~TextRuntimeTestSession() {
    // Still installed means the explicit shutdown never ran — a doctest
    // exception escaped, or main forgot. Run the same checked terminal
    // shutdown so u_cleanup cannot be skipped; a successful explicit shutdown
    // has already cleared installed_ and this is a no-op.
    if (installed_) ShutdownAfterTests();
}

bool TextRuntimeTestSession::Initialize() {
    // Exactly one configuration, from the one root this session was handed.
    // Nothing here consults the working directory or an application target
    // directory.
    molga::text::LoggerTextDiagnosticSink sink(/*maxRememberedKeys=*/64);
    molga::text::TextRuntimeDependencies& runtime =
        molga::text::TextRuntimeDependencies::Get();
    if (!runtime.Initialize(
            molga::text::TextDependencyConfig::FromEngineTextRoot(
                engineTextRoot_, /*packagedRuntime=*/false),
            sink)) {
        return false;
    }
    return runtime.IsReady();
}

bool TextRuntimeTestSession::ShutdownAfterTests() {
    shutdownRequested_ = true;
    molga::text::TextRuntimeDependencies& runtime =
        molga::text::TextRuntimeDependencies::Get();
    if (runtime.OutstandingClientHandleCount() != 0) {
        std::fprintf(stderr,
                     "text runtime test session: %zu client handles outlived "
                     "the tests\n",
                     runtime.OutstandingClientHandleCount());
        return false;
    }
    runtime.Shutdown();
    if (installed_) Install(nullptr);
    // Terminal from here on. The executable's main returns immediately after
    // this, so nothing in the process can reach ICU again.
    return !runtime.IsReady();
}

const std::filesystem::path& TextRuntimeTestSession::EngineTextRoot()
    const noexcept {
    return engineTextRoot_;
}

bool TextRuntimeTestSession::ShutdownWasRequested() const noexcept {
    return shutdownRequested_;
}

TextRuntimeTestSession& TextRuntimeTestSession::Current() {
    if (g_current == nullptr) {
        std::fprintf(stderr,
                     "text runtime test session: no session is installed\n");
        std::abort();
    }
    return *g_current;
}

void TextRuntimeTestSession::Install(TextRuntimeTestSession* session) noexcept {
    if (g_current != nullptr && g_current != session) {
        g_current->installed_ = false;
    }
    g_current = session;
    if (session != nullptr) session->installed_ = true;
}
