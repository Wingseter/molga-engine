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
// Every case here deletes or corrupts something and then requires the wrapper
// to put it back. The one case that asserts about the nested build without
// breaking anything lives in text_harfbuzz_host_resolution.cpp, because it is
// generator-independent and does not belong to this claim.

#include "text_dependency_authorities.h"
#include "text_dependency_test_support.h"
#include "text_recovery_tree.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using namespace molga::text_test;

namespace {

// RunWithoutShell inherits this process's stdout, which is what puts a failing
// build's output straight into the ctest log, and several callers depend on
// that signature. Capture is therefore a second entry point rather than an
// extra parameter: the HarfBuzz cases have to assert on what the repair
// boundary printed, and reading it back from a file is the only way to.
int RunCapturingOutput(const std::vector<std::string>& argv,
                       const std::filesystem::path& logPath) {
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return -1;
    // Both streams into the same file: CMake sends message(STATUS) to stdout
    // and its warnings to stderr, and the assertions below have to see the
    // boundary's own lines and the nested build's progress in one pass.
    // O_TRUNC so each capture is one run's log rather than an accumulation.
    int result = posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, logPath.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (result == 0) {
        result = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO,
                                                  STDERR_FILENO);
    }
    if (result != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }

    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& argument : argv) raw.push_back(const_cast<char*>(argument.c_str()));
    raw.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, raw[0], &actions, nullptr, raw.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool LogHasLineContaining(const std::vector<std::string>& lines,
                          const std::string& token) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(token) != std::string::npos;
    });
}

// Whole-line suffix matching, never a bare substring test: "Built target
// harfbuzz" is a substring of "Built target harfbuzz-icu", so a substring test
// would let one rebuilt target stand in for the proof that both were invoked.
bool LogHasLineEndingWith(const std::vector<std::string>& lines,
                          const std::string& suffix) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.size() >= suffix.size() &&
               line.compare(line.size() - suffix.size(), suffix.size(), suffix) == 0;
    });
}

std::filesystem::path HbLogDir(const std::filesystem::path& tree) {
    return tree / "make-recovery-logs";
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

// Overwriting build output needs the same containment proof as deleting it: a
// symlinked hop out of the throwaway tree would otherwise let a mutation land
// on the checkout, and the source template external/harfbuzz/src/hb-features.h.in
// is exactly the kind of file that is one resolution away.
void WriteFileUnder(const std::filesystem::path& target,
                    const std::filesystem::path& tree,
                    const std::string& bytes) {
    REQUIRE_MESSAGE(!std::filesystem::is_symlink(target), target.string());
    const auto resolved = std::filesystem::weakly_canonical(target);
    REQUIRE_MESSAGE(PathIsUnder(resolved, tree), resolved.string());
    REQUIRE_MESSAGE(std::filesystem::is_regular_file(resolved), resolved.string());
    WriteFileBytes(resolved, bytes);
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
    const auto& tree = SharedRecoveryTree();

    // Only the wrapper and its ExternalProject dependency, never `all`: the
    // point is what one target's rebuild restores on its own.
    REQUIRE(BuildTreeTarget(tree, "molga_text_icu_raw_install") == 0);
    RequireIcuRawInstallMatchesAuthorities(tree);

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
        // RequireIcuRawInstallMatchesAuthorities runs at eight call sites and
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
        RequireIcuRawInstallMatchesAuthorities(tree);
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
        RequireIcuRawInstallMatchesAuthorities(tree);
    }

    // The HarfBuzz cases run against this same tree, so the ICU repairs must
    // have left the nested HarfBuzz build exactly as they found it.
    RequireHarfBuzzTreeVerified(tree);
    MarkSharedTreeVerified();
}

// Step 4a.1 case A. src/hb-features.h is written by the nested configure, not
// by its build command, so it cannot honestly be a BUILD_BYPRODUCTS entry;
// under Make a deleted one therefore has no producer to schedule. This is the
// proof that molga_text_harfbuzz_raw_install replays the pinned configure to
// put it back, and that doing so disturbs nothing else.
TEST_CASE("make generator recovery regenerates the deleted HarfBuzz feature header"
          * doctest::skip()) {
    const auto& tree = SharedRecoveryTree();
    RequireHarfBuzzTreeVerified(tree);

    const auto generated = HbGeneratedHeader(tree);
    const auto installedHeader = HbInstalledHeader(tree, "hb-features.h");
    INFO("victim " << generated.string());

    // Recorded before the delete, so "unchanged" below is a claim about this
    // tree rather than about a constant that happens to be right.
    REQUIRE(RequiredSha256File(generated) == kGeneratedHeaderSha256);
    REQUIRE(RequiredSha256File(installedHeader) == kGeneratedHeaderSha256);
    const std::vector<std::string> archivesBefore = HbBuildArchiveDigests(tree);

    // Only the canonical build-tree copy. The installed copy is left correct on
    // purpose: a boundary that repaired by copying the installed file back
    // would pass this case and fail the next one.
    RemoveFileUnder(generated, tree);

    std::filesystem::create_directories(HbLogDir(tree));
    const auto logPath = HbLogDir(tree) / "deleted-generated-header.log";
    REQUIRE(PathIsUnder(logPath, tree));
    REQUIRE(RunCapturingOutput({MOLGA_CMAKE_COMMAND, "--build", tree.string(),
                                "--target", "molga_text_harfbuzz_raw_install"},
                               logPath) == 0);
    const auto log = ReadLines(logPath);
    INFO("build log " << logPath.string());

    // The pinned configure was re-run, and it was re-run against this nested
    // build directory: its argument vector is spliced verbatim from
    // TextDependencies.cmake, so what the log can show is that it completed
    // there, and the cache assertion below is where its content is checked.
    REQUIRE(LogHasLineContaining(
        log, "reconfiguring the nested HarfBuzz build: " + generated.string() +
                 " is missing"));
    REQUIRE(LogHasLineEndingWith(
        log, "Build files have been written to: " + HbBuildDir(tree).string()));

    // And both nested archive targets were invoked afterwards. Naming the two
    // targets is a narrowing of `cmake --build <dir>` that is only equivalent
    // because every HB_BUILD_* entry is OFF, so both names have to appear.
    REQUIRE(LogHasLineContaining(
        log, "rebuilding the nested HarfBuzz archives: the nested configuration "
             "was repaired"));
    REQUIRE(LogHasLineEndingWith(log, "Built target harfbuzz"));
    REQUIRE(LogHasLineEndingWith(log, "Built target harfbuzz-icu"));
    REQUIRE(LogHasLineContaining(
        log, "nested HarfBuzz raw install verified: 36 consumed outputs"));

    // Both copies of the generated header are back at the independent pin.
    REQUIRE(RequiredSha256File(generated) == kGeneratedHeaderSha256);
    REQUIRE(RequiredSha256File(installedHeader) == kGeneratedHeaderSha256);

    // The repair rebuilt the archives, so the interesting claim is that it did
    // not perturb them: ZERO_AR_DATE=1 wraps both the original build and the
    // replay precisely so the restored bytes are the bytes that were there.
    REQUIRE(HbBuildArchiveDigests(tree) == archivesBefore);

    RequireHarfBuzzTreeVerified(tree);
    MarkSharedTreeVerified();
}

// Step 4a.1 case B, and the reason the boundary pins hb-features.h by SHA at
// all. The header is generated, so it has no source-tree authority; a boundary
// that compared the installed copy against the build-tree copy alone would let
// two identically wrong copies agree their way past it. Corrupting both with
// the same byte is that false agreement, made on purpose.
TEST_CASE("make generator recovery rejects two identically wrong HarfBuzz headers"
          * doctest::skip()) {
    const auto& tree = SharedRecoveryTree();
    RequireHarfBuzzTreeVerified(tree);

    const auto generated = HbGeneratedHeader(tree);
    const auto installedHeader = HbInstalledHeader(tree, "hb-features.h");
    const std::vector<std::string> archivesBefore = HbBuildArchiveDigests(tree);

    const std::string original = ReadFileBytes(generated);
    REQUIRE_FALSE(original.empty());
    // One byte inside the leading licence comment, and the length is preserved,
    // so the corrupt file is still a plausible header rather than obvious
    // garbage. It is never compiled in this state — the boundary regenerates it
    // before the nested build step — but a mutation outside a comment would
    // make that an assumption instead of a fact.
    const std::size_t offset = original.find("This is part of HarfBuzz");
    REQUIRE(offset != std::string::npos);
    std::string corrupted = original;
    corrupted[offset] = 'X';
    REQUIRE(corrupted.size() == original.size());

    WriteFileUnder(generated, tree, corrupted);
    WriteFileUnder(installedHeader, tree, corrupted);

    // The trap is only set if the two copies really do agree with each other
    // and really do disagree with the pin.
    const std::string corruptSha = RequiredSha256File(generated);
    INFO("identically corrupted to " << corruptSha);
    REQUIRE(RequiredSha256File(installedHeader) == corruptSha);
    REQUIRE(corruptSha != kGeneratedHeaderSha256);

    std::filesystem::create_directories(HbLogDir(tree));
    const auto logPath = HbLogDir(tree) / "identical-corruption.log";
    REQUIRE(PathIsUnder(logPath, tree));
    REQUIRE(RunCapturingOutput({MOLGA_CMAKE_COMMAND, "--build", tree.string(),
                                "--target", "molga_text_harfbuzz_raw_install"},
                               logPath) == 0);
    const auto log = ReadLines(logPath);
    INFO("build log " << logPath.string());

    // The crux. The reason names the *build-tree* header and the digest it was
    // measured against, which is only possible if the check was the independent
    // pin rather than the installed copy sitting beside it.
    REQUIRE(LogHasLineContaining(
        log, "reconfiguring the nested HarfBuzz build: build-tree hb-features.h "
             "is " + corruptSha + ", expected " + kGeneratedHeaderSha256));
    REQUIRE(LogHasLineEndingWith(
        log, "Build files have been written to: " + HbBuildDir(tree).string()));
    REQUIRE(LogHasLineContaining(
        log, "rebuilding the nested HarfBuzz archives: the nested configuration "
             "was repaired"));
    REQUIRE(LogHasLineEndingWith(log, "Built target harfbuzz"));
    REQUIRE(LogHasLineEndingWith(log, "Built target harfbuzz-icu"));
    // Only after the build-tree copy is right does the installed one get
    // repaired, and it is repaired against that copy rather than reinstalled
    // over itself.
    REQUIRE(LogHasLineContaining(
        log, "repairing the nested HarfBuzz install: installed hb-features.h "
             "does not match " + generated.string()));

    REQUIRE(RequiredSha256File(generated) == kGeneratedHeaderSha256);
    REQUIRE(RequiredSha256File(installedHeader) == kGeneratedHeaderSha256);
    REQUIRE(HbBuildArchiveDigests(tree) == archivesBefore);

    RequireHarfBuzzTreeVerified(tree);
    MarkSharedTreeVerified();
}

// Step 4a.1 case C. hb-blob.h is a transitive include of hb.h that no Molga
// source names directly, so a deleted one is exactly the kind of hole that
// surfaces as an incomprehensible compile error in a consumer rather than as a
// missing file. The wrapper is what every consumer's build order goes through,
// so it has to restore it before any of them compiles.
TEST_CASE("make generator recovery restores a deleted transitive HarfBuzz header"
          * doctest::skip()) {
    const auto& tree = SharedRecoveryTree();
    RequireHarfBuzzTreeVerified(tree);

    const auto installedBlob = HbInstalledHeader(tree, "hb-blob.h");
    INFO("victim " << installedBlob.string());
    const std::string pinned =
        RequiredSha256File(SourceRoot() / "external/harfbuzz/src/hb-blob.h");
    REQUIRE(RequiredSha256File(installedBlob) == pinned);
    const std::vector<std::string> archivesBefore = HbBuildArchiveDigests(tree);

    RemoveFileUnder(installedBlob, tree);

    std::filesystem::create_directories(HbLogDir(tree));
    const auto logPath = HbLogDir(tree) / "deleted-transitive-header.log";
    REQUIRE(PathIsUnder(logPath, tree));
    REQUIRE(RunCapturingOutput({MOLGA_CMAKE_COMMAND, "--build", tree.string(),
                                "--target", "molga_text_harfbuzz_raw_install"},
                               logPath) == 0);
    const auto log = ReadLines(logPath);
    INFO("build log " << logPath.string());

    REQUIRE(LogHasLineContaining(
        log, "repairing the nested HarfBuzz install: installed " +
                 installedBlob.string() + " is missing"));
    // A missing installed header is an install-side defect, and the repair
    // stays that narrow: a nested reconfigure or archive rebuild here would
    // mean the boundary cannot tell the two failures apart.
    REQUIRE_FALSE(
        LogHasLineContaining(log, "reconfiguring the nested HarfBuzz build:"));
    REQUIRE_FALSE(
        LogHasLineContaining(log, "rebuilding the nested HarfBuzz archives:"));

    // Byte-identical to the clean pinned source, not to some other installed
    // copy, and restored before the target that every consumer depends on
    // reports success.
    REQUIRE(std::filesystem::is_regular_file(installedBlob));
    REQUIRE(RequiredSha256File(installedBlob) == pinned);
    REQUIRE(HbBuildArchiveDigests(tree) == archivesBefore);

    RequireHarfBuzzTreeVerified(tree);
    MarkSharedTreeVerified();
}

}  // TEST_SUITE("text-make-recovery")
