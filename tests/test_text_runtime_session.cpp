#include "TextRuntimeTestSession.h"

#include "Common/Sha256.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextRuntimeDependencies.h"
#include "doctest.h"

#include <filesystem>
#include <optional>
#include <utility>

TEST_CASE("text test session owns one exact staged runtime") {
    auto& session = TextRuntimeTestSession::Current();
    CHECK(session.EngineTextRoot() ==
          std::filesystem::path(MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT));
    CHECK(molga::text::TextRuntimeDependencies::Get().IsReady());
    CHECK_FALSE(session.ShutdownWasRequested());
}

TEST_CASE("the published contract SHA is the SHA of the contract that loaded") {
    // The value later caches key on. Nothing else in the tree reads it yet, so
    // without this the first wrong answer would surface a milestone away.
    auto& session = TextRuntimeTestSession::Current();
    const std::filesystem::path contract =
        session.EngineTextRoot() / "text_dependency_contract.json";
    std::string error;
    const std::string expected = molga::Sha256File(contract, &error);
    REQUIRE(error.empty());
    REQUIRE(expected.size() == 64);
    CHECK(molga::text::TextRuntimeDependencies::Get().DependencyContractSha256() ==
          expected);
}

TEST_CASE("client handle moves transfer exactly one lease") {
    // Regression cover for the move-assignment bug review finding 3 caught: a
    // std::swap here handed the destination's obligation back to the source, so
    // the lease was discharged at the wrong scope exit. OutstandingClientHandleCount
    // is the observer that makes each step of that visible.
    using molga::text::TextRuntimeClientHandle;
    using molga::text::TextRuntimeDependencies;
    TextRuntimeDependencies& runtime = TextRuntimeDependencies::Get();
    REQUIRE(runtime.IsReady());
    REQUIRE(runtime.OutstandingClientHandleCount() == 0);

    {
        std::optional<TextRuntimeClientHandle> first = TextRuntimeClientHandle::Acquire();
        REQUIRE(first);
        CHECK(runtime.OutstandingClientHandleCount() == 1);

        // Move-construction transfers the one lease rather than duplicating it.
        TextRuntimeClientHandle moved(std::move(*first));
        CHECK(runtime.OutstandingClientHandleCount() == 1);

        // Self-move keeps the lease. Routed through a pointer so this is not the
        // syntactically self-assigning form the compiler warns about.
        TextRuntimeClientHandle* self = &moved;
        moved                         = std::move(*self);
        CHECK(runtime.OutstandingClientHandleCount() == 1);

        {
            std::optional<TextRuntimeClientHandle> second =
                TextRuntimeClientHandle::Acquire();
            REQUIRE(second);
            CHECK(runtime.OutstandingClientHandleCount() == 2);

            // Assigning over a live handle must release the destination's own
            // lease and deactivate the source: two live, one assignment, one
            // left. A swap would leave both alive and count 2.
            moved = std::move(*second);
            CHECK(runtime.OutstandingClientHandleCount() == 1);
        }
        // The moved-from optional still holds an object, but it owns nothing,
        // so leaving this scope must not double-release.
        CHECK(runtime.OutstandingClientHandleCount() == 1);
    }
    CHECK(runtime.OutstandingClientHandleCount() == 0);
}

TEST_CASE("a second lifetime guard is refused while this process is ready") {
    // The guard's own move-assignment cannot be exercised in-process: Create is
    // its only constructor and it refuses once a lifetime exists, which is
    // itself the property under test here. Its Release()/take/clear shape is
    // identical to the client handle's above, and the app-unwind case proves
    // exactly one cleanup runs.
    molga::text::VectorTextDiagnosticSink sink;
    auto& session = TextRuntimeTestSession::Current();
    std::optional<molga::text::TextRuntimeLifetimeGuard> second =
        molga::text::TextRuntimeLifetimeGuard::Create(
            molga::text::TextDependencyConfig::FromEngineTextRoot(
                session.EngineTextRoot(), /*packagedRuntime=*/false),
            sink);
    CHECK_FALSE(second);
    REQUIRE(sink.Diagnostics().size() == 1);
    CHECK(sink.Diagnostics()[0].code ==
          molga::text::TextDiagnosticCode::DependencyInvalid);
    // Still ready: a refused second guard must not have disturbed the lifetime.
    CHECK(molga::text::TextRuntimeDependencies::Get().IsReady());
}
