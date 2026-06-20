#include "Core/PackageFinalizer.h"
#include "SmokeTestSupport.h"
#include "doctest.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("FinalizeStagedPackage replaces final output after staging is ready") {
    test_support::TempDirectory temp{"package-finalizer-replace"};
    const fs::path finalOutput = temp.Path() / "Game";
    const fs::path stagingOutput = temp.Path() / "Game.staging";
    const fs::path backupOutput = temp.Path() / "Game.previous";

    test_support::WriteText(finalOutput / "old.txt", "old build");
    test_support::WriteText(stagingOutput / "new.txt", "new build");

    const auto result = PackageFinalizer::FinalizeStagedPackage(stagingOutput, finalOutput);

    REQUIRE(result.ok);
    CHECK(fs::exists(finalOutput / "new.txt"));
    CHECK_FALSE(fs::exists(finalOutput / "old.txt"));
    CHECK_FALSE(fs::exists(stagingOutput));
    CHECK_FALSE(fs::exists(backupOutput));
}

TEST_CASE("FinalizeStagedPackage keeps final output when staging is missing") {
    test_support::TempDirectory temp{"package-finalizer-missing-staging"};
    const fs::path finalOutput = temp.Path() / "Game";
    const fs::path stagingOutput = temp.Path() / "Game.staging";

    test_support::WriteText(finalOutput / "old.txt", "old build");

    const auto result = PackageFinalizer::FinalizeStagedPackage(stagingOutput, finalOutput);

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("staging") != std::string::npos);
    CHECK(fs::exists(finalOutput / "old.txt"));
}

TEST_CASE("FinalizeStagedPackage restores final output when staging rename fails") {
    test_support::TempDirectory temp{"package-finalizer-rollback"};
    const fs::path stagingParent = temp.Path() / "staging_parent";
    const fs::path finalParent = temp.Path() / "final_parent";

    const fs::path finalOutput = finalParent / "Game";
    const fs::path stagingOutput = stagingParent / "Game.staging";
    const fs::path backupOutput = finalParent / "Game.previous";

    test_support::WriteText(finalOutput / "old.txt", "old build");
    test_support::WriteText(stagingOutput / "new.txt", "new build");

    // Make stagingParent read-only to prevent moving stagingOutput out of it
    fs::permissions(stagingParent, fs::perms::owner_read | fs::perms::owner_exec);

    const auto result = PackageFinalizer::FinalizeStagedPackage(stagingOutput, finalOutput);

    // Restore permissions so cleanup in TempDirectory destructor works
    fs::permissions(stagingParent, fs::perms::owner_all);

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("move staged output") != std::string::npos);
    CHECK(result.error.find("restored") != std::string::npos);
    CHECK(fs::exists(finalOutput / "old.txt"));
    CHECK_FALSE(fs::exists(backupOutput));
    CHECK(fs::exists(stagingOutput / "new.txt"));
}
