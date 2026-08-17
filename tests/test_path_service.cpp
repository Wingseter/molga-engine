#include "Core/PathService.h"
#include "doctest.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("Resolve passes absolute through and joins relative under root") {
    CHECK(PathService::Resolve("/proj", "/abs/x.png") == "/abs/x.png");
    CHECK(fs::path(PathService::Resolve("/proj", "Assets/x.png")) ==
          fs::path("/proj") / "Assets/x.png");
    CHECK(PathService::Resolve("/proj", "") == "");
}

TEST_CASE("IsSafeOutputPath rejects dangerous targets") {
    std::string why;
    CHECK_FALSE(PathService::IsSafeOutputPath("", why));
    CHECK_FALSE(PathService::IsSafeOutputPath(fs::path("/"), why));
    CHECK_FALSE(PathService::IsSafeOutputPath(fs::path("."), why));
}

TEST_CASE("IsSafeOutputPath accepts a normal nested export dir") {
    std::string why;
    CHECK(PathService::IsSafeOutputPath(fs::path("/tmp/molga_export_abc/dist"), why));
}

TEST_CASE("IsSafeOutputPath rejects project root and engine root when provided") {
    std::string why;
    CHECK_FALSE(PathService::IsSafeOutputPath("/tmp/project", why, "/tmp/project", "/tmp/engine"));
    CHECK_FALSE(PathService::IsSafeOutputPath("/tmp/engine", why, "/tmp/project", "/tmp/engine"));
    CHECK(PathService::IsSafeOutputPath("/tmp/project/Builds/Game", why, "/tmp/project", "/tmp/engine"));
}
