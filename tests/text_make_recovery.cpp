// Generator-recovery proofs for the vendored text dependencies.
//
// BUILD_BYPRODUCTS/INSTALL_BYPRODUCTS give Ninja file-level rules, so a deleted
// ExternalProject byproduct there is simply rebuilt. Under Unix Makefiles they
// only feed the clean list and inter-target ordering — there is no per-file
// rule — so a deleted byproduct stays deleted and every downstream consumer
// reads a hole. The molga_text_*_raw_install wrappers are the always-checked
// targets that close that hole, and these cases are the evidence that they do.
// Running any of them under Ninja would prove nothing.
//
// Split out of test_text_dependencies.cpp: same executable, same six
// target-scoped provenance macros, but the minutes-long cases are kept away
// from the seconds-long ones.

#include "text_dependency_test_support.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

using namespace molga::text_test;

namespace {

// ── Make-generator recovery harness ───────────────────────────────────────────
// The nested ICU install flattens both public header directories into one, and
// the pinned submodule fixes how many files that is; TextDependencies.cmake
// fails the configure if the count ever drifts from this.
constexpr int kIcuInstalledHeaderCount = 203;

std::string CMakeCacheEntry(const std::filesystem::path& tree,
                            const std::string& key) {
    std::ifstream cache(tree / "CMakeCache.txt");
    REQUIRE_MESSAGE(cache.good(), (tree / "CMakeCache.txt").string());
    const std::string prefix = key + "=";
    std::string line;
    while (std::getline(cache, line)) {
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    }
    return {};
}

// A run killed by a ctest timeout, a Ctrl-C, or a cancelled CI job never
// reaches its own cleanup, and nothing else collects what it left: `make clean`
// does not descend into a nested tree, and each one carries a complete nested
// ICU build. Sweeping every sibling of this slug also clears this pid's own
// slot, so the caller does not need to.
//
// This assumes one recovery run at a time per slug. RUN_SERIAL covers
// ctest-against-ctest, but a developer starting this binary by hand beside a
// running ctest will have their sweep delete the other run's in-flight tree and
// kill it mid-build. Accepted deliberately: the casualty is a throwaway tree,
// and pid-liveness checking would cost more than the leak it prevents.
void RemoveStaleTrees(const std::string& slug) {
    const std::string prefix = slug + "-";
    // Every match is collected before any is removed: unlinking entries while
    // readdir is still walking the same directory can skip siblings, which
    // would silently leave exactly the leak this is here to collect.
    std::vector<std::filesystem::path> stale;
    for (const auto& entry : std::filesystem::directory_iterator(BinaryRoot())) {
        if (entry.path().filename().string().rfind(prefix, 0) == 0) {
            stale.push_back(entry.path());
        }
    }
    for (const auto& leaked : stale) {
        // Guarded on the resolved path but removed by the entry itself.
        // remove_all follows a symlink, so a link named with this prefix would
        // resolve to something that passes both guards and then destroy the
        // real directory it points at; unlinking the entry removes the link.
        // directory_iterator already guarantees it is a direct child here, so
        // resolution is what introduces the escape, not what prevents it.
        const auto resolved = std::filesystem::weakly_canonical(leaked);
        REQUIRE_MESSAGE(PathIsUnder(resolved, BinaryRoot()), resolved.string());
        REQUIRE(resolved != std::filesystem::weakly_canonical(BinaryRoot()));
        std::filesystem::remove_all(leaked);
    }
}

// A throwaway full build tree pinned to the generator whose byproduct handling
// this file has to prove things about. The slug is a parameter so a later
// recovery proof can name its own tree rather than collide with this one; it is
// not a sharing mechanism, and every distinct slug pays the configure again.
std::filesystem::path ConfigureUnixMakefilesTree(const std::string& slug) {
    const auto tree = BinaryRoot() / (slug + "-" + std::to_string(::getpid()));
    REQUIRE(PathIsUnder(tree, BinaryRoot()));
    RemoveStaleTrees(slug);
    std::filesystem::create_directories(tree);

    REQUIRE(RunWithoutShell({MOLGA_CMAKE_COMMAND,
                             "-S", MOLGA_SOURCE_DIR,
                             "-B", tree.string(),
                             "-G", "Unix Makefiles",
                             "-DCMAKE_BUILD_TYPE=Debug",
                             // box2d is the one dependency the project fetches
                             // over the network, and a virgin binary dir would
                             // re-clone it. Point it at the checkout this build
                             // tree already has so an offline run — or a GitHub
                             // outage — cannot fail a proof about ICU.
                             "-DFETCHCONTENT_SOURCE_DIR_BOX2D=" +
                                 (BinaryRoot() / "_deps/box2d-src").string()}) == 0);

    // Ninja gives every declared byproduct a real file-level rule, so the
    // repair path under test would never be reached. A tree that silently came
    // up on another generator voids the proof rather than weakening it.
    REQUIRE(CMakeCacheEntry(tree, "CMAKE_GENERATOR:INTERNAL") == "Unix Makefiles");
    return tree;
}

int BuildTreeTarget(const std::filesystem::path& tree, const std::string& target) {
    return RunWithoutShell(
        {MOLGA_CMAKE_COMMAND, "--build", tree.string(), "--target", target});
}

// Deleting build output is the whole method here, so every path is resolved and
// component-checked against the throwaway tree before it goes. A containment
// failure aborts the case; it never degrades into skipping the delete.
void RemoveFileUnder(const std::filesystem::path& victim,
                     const std::filesystem::path& tree) {
    const auto resolved = std::filesystem::weakly_canonical(victim);
    REQUIRE_MESSAGE(PathIsUnder(resolved, tree), resolved.string());
    REQUIRE_MESSAGE(std::filesystem::is_regular_file(resolved), resolved.string());
    std::filesystem::remove(resolved);
    REQUIRE_FALSE(std::filesystem::exists(resolved));
}

// The wrapper's own postcondition: every consumed installed file equals the
// authority it was copied from — archives from the build tree, headers from the
// clean pinned source. Re-checked after each repair so restoring the deleted
// file by disturbing a sibling still fails.
void RequireRawInstallMatchesAuthorities(const std::filesystem::path& tree) {
    const auto icuBuild = tree / "text-dependencies/icu-build";
    const auto icuRaw = tree / "text-dependencies/icu-raw";
    for (const char* archive : {"libicuuc.a", "libicui18n.a"}) {
        REQUIRE_MESSAGE(RequiredSha256File(icuRaw / "lib" / archive) ==
                            RequiredSha256File(icuBuild / "lib" / archive),
                        archive);
    }
    REQUIRE(RequiredSha256File(icuRaw / "include/unicode/utypes.h") ==
            RequiredSha256File(SourceRoot() /
                               "external/icu/icu4c/source/common/unicode/utypes.h"));

    int installed = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(icuRaw / "include/unicode")) {
        if (entry.is_regular_file()) ++installed;
    }
    REQUIRE(installed == kIcuInstalledHeaderCount);
}

}  // namespace

// Step 3.1b: the raw-ICU repair wrapper, proved under the generator it exists
// for. molga_text_icu_raw_install is the always-checked wrapper that closes the
// byproduct hole, and this case is the evidence that it does.
//
// Skipped by default because it configures and builds a throwaway tree from
// scratch, which is minutes against the seconds the `unit` sweep budgets. The
// test_text_make_recovery ctest entry runs it with --no-skip, selecting this
// suite by name: a later recovery proof joins by declaring itself here rather
// than by its title happening to start with the right words.
TEST_SUITE("text-make-recovery") {

TEST_CASE("make generator recovery repairs every deleted raw ICU install output"
          * doctest::skip()) {
    const auto tree = ConfigureUnixMakefilesTree("text-make-recovery");
    MESSAGE("Make-generator recovery tree: " << tree.string());

    // Only the wrapper and its ExternalProject dependency, never `all`: the
    // point is what one target's rebuild restores on its own.
    REQUIRE(BuildTreeTarget(tree, "molga_text_icu_raw_install") == 0);
    RequireRawInstallMatchesAuthorities(tree);

    const auto icuBuild = tree / "text-dependencies/icu-build";
    const auto icuRaw = tree / "text-dependencies/icu-raw";
    const std::vector<std::string> buildOutputs = {
        "lib/libicuuc.a", "lib/libicui18n.a",
        "stubdata/libicudata.a", "stubdata/stubdata.ao"};

    // Phase A. These four are the archive authorities the installed copies are
    // compared against, so losing any one of them has to restore the whole set
    // before the wrapper can compare or install anything. ZERO_AR_DATE=1 is set
    // on both the original build and the repair precisely so the restored bytes
    // are the bytes that were there: a repair that rebuilds something different
    // would silently republish a different archive downstream.
    for (const auto& victim : buildOutputs) {
        // RequireRawInstallMatchesAuthorities runs at eight call sites and
        // compares bare 64-hex strings. doctest's context scopes are stack
        // based and propagate into callees, so this names the failing repair
        // instead of leaving CI pointing at a helper line.
        INFO("phase A victim " << victim);
        std::vector<std::string> before;
        for (const auto& output : buildOutputs) {
            before.push_back(RequiredSha256File(icuBuild / output));
        }

        RemoveFileUnder(icuBuild / victim, tree);
        REQUIRE_MESSAGE(BuildTreeTarget(tree, "molga_text_icu_raw_install") == 0,
                        victim);

        for (std::size_t i = 0; i < buildOutputs.size(); ++i) {
            const auto restored = icuBuild / buildOutputs[i];
            REQUIRE_MESSAGE(std::filesystem::is_regular_file(restored),
                            (victim + " -> " + buildOutputs[i]));
            REQUIRE_MESSAGE(RequiredSha256File(restored) == before[i],
                            (victim + " -> " + buildOutputs[i]));
        }
        RequireRawInstallMatchesAuthorities(tree);
    }

    // Phase B. The installed tree is what consumers actually read, and it is
    // never baselined against itself: a header answers to the clean pinned
    // source, an archive to the build tree beside it.
    const std::filesystem::path pinnedUtypes =
        SourceRoot() / "external/icu/icu4c/source/common/unicode/utypes.h";
    const std::vector<std::pair<std::string, std::filesystem::path>> installed = {
        {"include/unicode/utypes.h", pinnedUtypes},
        {"lib/libicuuc.a", icuBuild / "lib/libicuuc.a"},
        {"lib/libicui18n.a", icuBuild / "lib/libicui18n.a"},
    };
    for (const auto& entry : installed) {
        // Named rather than structured-bound: doctest's message macros capture
        // their operand in a lambda, which a structured binding cannot be.
        const std::string& relative = entry.first;
        INFO("phase B victim " << relative);
        const std::string expected = RequiredSha256File(entry.second);
        RemoveFileUnder(icuRaw / relative, tree);
        REQUIRE_MESSAGE(BuildTreeTarget(tree, "molga_text_icu_raw_install") == 0,
                        relative);

        const auto repaired = icuRaw / relative;
        REQUIRE_MESSAGE(std::filesystem::is_regular_file(repaired), relative);
        REQUIRE_MESSAGE(RequiredSha256File(repaired) == expected, relative);
        RequireRawInstallMatchesAuthorities(tree);
    }

    // Reached only when every assertion above held, so a failed run leaves the
    // tree in place for inspection; the next run's slug sweep collects it.
    std::filesystem::remove_all(tree);
}

}  // TEST_SUITE("text-make-recovery")
