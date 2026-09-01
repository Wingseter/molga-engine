#include "TextRuntimeTestSession.h"

#include "Text/TextRuntimeDependencies.h"
#include "doctest.h"

#include <filesystem>

TEST_CASE("text test session owns one exact staged runtime") {
    auto& session = TextRuntimeTestSession::Current();
    CHECK(session.EngineTextRoot() ==
          std::filesystem::path(MOLGA_TEXT_TEST_ENGINE_TEXT_ROOT));
    CHECK(molga::text::TextRuntimeDependencies::Get().IsReady());
    CHECK_FALSE(session.ShutdownWasRequested());
}
