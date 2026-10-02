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

#include <nlohmann/json.hpp>

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

// A coupling worth stating plainly: the cases below match strings that
// cmake/RepairHarfBuzzRawInstall.cmake writes — its three repair announcements
// ("reconfiguring"/"rebuilding"/"repairing the nested HarfBuzz ..."), the
// reasons it gives for them, and its closing "verified" line. Rewording any of
// them, however innocuous the edit looks, turns this into a five-minute red
// run. That is the price of asserting on behaviour the boundary only exposes by
// printing it, and it is paid deliberately; the script carries the matching
// note. The lines this matches from CMake and Make themselves — "Build files
// have been written to", "Built target X", and Step 4e's "static library
// libharfbuzz.a" / "libharfbuzz-icu.a" — are not part of that coupling, and
// were checked against CLICOLOR_FORCE, MAKEFLAGS=-s, --no-print-directory and
// -j4. That list is exhaustive: a new tool-owned line matched anywhere below
// belongs in it, or the hardening claim starts covering strings it never saw.
bool LogHasLineContaining(const std::vector<std::string>& lines,
                          const std::string& token) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(token) != std::string::npos;
    });
}

// Whole-line suffix matching, never a bare substring test: "Built target
// harfbuzz" is a substring of "Built target harfbuzz-icu", so a substring test
// would let one rebuilt target stand in for the proof that both were invoked.
bool LineEndsWith(const std::string& line, const std::string& suffix) {
    return line.size() >= suffix.size() &&
           line.compare(line.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool LogHasLineEndingWith(const std::vector<std::string>& lines,
                          const std::string& suffix) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return LineEndsWith(line, suffix);
    });
}

// The one line carrying ${token}, so several claims about a single command are
// claims about that command rather than about lines that merely coexist in the
// log. Requiring exactly one match is the point: two would mean the repair ran
// twice and the caller's reasoning about which invocation it is reading no
// longer holds.
std::string TheLineContaining(const std::vector<std::string>& lines,
                              const std::string& token) {
    std::vector<std::string> matches;
    for (const auto& line : lines) {
        if (line.find(token) != std::string::npos) matches.push_back(line);
    }
    REQUIRE_MESSAGE(matches.size() == 1,
                    (token + ": " + std::to_string(matches.size()) + " lines"));
    return matches.front();
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

// ── Step 4e support ───────────────────────────────────────────────────────────

// The line harfbuzz_rerun_build prints before it runs. It is the only place the
// boundary states the command it is about to execute, and the script builds the
// announcement from the same list it then passes to execute_process.
constexpr char kArchiveCommandAnnouncement[] = "nested HarfBuzz archive command: ";

// The five values Step 4e compares, in the order it names them.
const std::vector<std::string>& CompositeProvenanceKeys() {
    static const std::vector<std::string> keys = {
        "rawCoreSha256", "rawAdapterSha256", "adapterObjectSha256",
        "filteredMemberDigest", "finalCompositeSha256"};
    return keys;
}

// Re-derive the composite provenance from the files on disk through
// VerifyTextDependencies' closed read-only mode, which is the same function the
// publishing barrier runs. Deriving it here instead — parsing `ar -t` and
// filtering pseudo-members in the test — would be a second definition of
// "filtered member digest" free to drift from the one the build lock records.
//
// The mode reads only paths and tool identities out of the lock and recomputes
// every digest, so the lock this tree published before the deletions below is a
// valid input to the run that follows them.
nlohmann::json ReadOnlyCompositeProvenance(const std::filesystem::path& tree,
                                           const std::filesystem::path& resultRoot,
                                           const std::string& resultName) {
    // Runs twice per case, so every REQUIRE below needs to say which of the two
    // it belongs to.
    INFO("read-only provenance " << resultName);
    const auto resultFile = resultRoot / resultName;
    REQUIRE(PathIsUnder(resultFile, tree));
    REQUIRE(std::filesystem::is_directory(resultRoot));
    REQUIRE_FALSE(std::filesystem::exists(resultFile));

    REQUIRE_MESSAGE(
        RunWithoutShell({
            MOLGA_CMAKE_COMMAND,
            "-DMODE=HARFBUZZ_READ_ONLY",
            "-DBUILD_LOCK=" +
                (tree / "generated/text_dependency_build_lock.json").string(),
            std::string("-DSOURCE_ROOT=") + MOLGA_SOURCE_DIR,
            "-DBINARY_ROOT=" + tree.string(),
            "-DRESULT_ROOT=" + resultRoot.string(),
            "-DRESULT_FILE=" + resultFile.string(),
            "-P",
            MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT,
        }) == 0,
        resultFile.string());
    REQUIRE_MESSAGE(std::filesystem::is_regular_file(resultFile),
                    resultFile.string());

    std::ifstream input(resultFile);
    REQUIRE_MESSAGE(input.good(), resultFile.string());
    const auto result = nlohmann::json::parse(input);

    // A value that went missing or empty would make every comparison against it
    // trivially true, which is the one way this proof could pass while proving
    // nothing. Checked here rather than at the comparison, so it covers the
    // recording and the re-derivation alike.
    for (const auto& key : CompositeProvenanceKeys()) {
        INFO("recorded " << key);
        REQUIRE(result.contains(key));
        REQUIRE(result.at(key).is_string());
        const std::string value = result.at(key).get<std::string>();
        REQUIRE(value.size() == 64);
        REQUIRE(value.find_first_not_of("0123456789abcdef") == std::string::npos);
    }
    return result;
}

// The archiver's own ordered member listing. This is the raw table, not the
// filtered one — filtering is a pure function of it, so identical raw listings
// imply identical filtered listings without this file owning a second copy of
// what "filtered" means.
//
// filteredMemberDigest already carries the proof, so diagnosis is this
// helper's whole warrant: doctest has no insertion operator for a vector of
// strings and prints the comparison as `{?} == {?}`. The listing therefore
// stays on disk in the retained tree, and the caller has to name both files in
// its own INFO — a listing diff is what makes a member-order change readable.
std::vector<std::string> ArchiveMemberListing(const std::string& ar,
                                              const std::filesystem::path& archive,
                                              const std::filesystem::path& listingPath) {
    INFO("member listing of " << archive.string() << " in " << listingPath.string());
    REQUIRE(RunCapturingOutput({ar, "-t", archive.string()}, listingPath) == 0);
    auto listing = ReadLines(listingPath);
    REQUIRE_FALSE(listing.empty());
    return listing;
}

// Every compiled object behind the two nested archive targets, by content.
// Step 4e leaves these in place deliberately, and "they were not touched" is
// the load-bearing claim that makes the archives' byte identity a statement
// about `ar` rather than about a recompile that happened to agree.
//
// Hashed rather than timestamped. There are two objects, so the digests are
// nearly free, and a modification time is only a proxy: a recompile landing
// inside one timestamp tick — plausible on a coarse-resolution volume, and
// this test deliberately does its work in seconds — would pass a mtime check
// while changing the bytes that went into the archive.
std::vector<std::pair<std::string, std::string>>
NestedObjectDigests(const std::filesystem::path& tree) {
    std::vector<std::pair<std::string, std::string>> digests;
    for (const char* target : {"harfbuzz.dir", "harfbuzz-icu.dir"}) {
        const auto root = HbBuildDir(tree) / "CMakeFiles" / target;
        REQUIRE_MESSAGE(std::filesystem::is_directory(root), root.string());
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".o") continue;
            digests.emplace_back(entry.path().string(),
                                 RequiredSha256File(entry.path()));
        }
    }
    // A layout change that moved the objects would otherwise leave this
    // comparing two empty vectors and reporting success.
    REQUIRE_FALSE(digests.empty());
    std::sort(digests.begin(), digests.end());
    return digests;
}

// Compared entry by entry rather than vector against vector, for the same
// reason the listings above are written to disk: doctest would print the whole
// comparison as `{?} == {?}` and name neither the object nor the phase it
// failed in.
void RequireNestedObjectsUnchanged(
    const std::filesystem::path& tree, const std::string& phase,
    const std::vector<std::pair<std::string, std::string>>& expected) {
    INFO("nested objects " << phase);
    const auto actual = NestedObjectDigests(tree);
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        INFO("object " << actual[i].first);
        REQUIRE(actual[i].first == expected[i].first);
        REQUIRE(actual[i].second == expected[i].second);
    }
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
    RequireSharedTreeEntry(tree);

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

    // The exit check covers the nested HarfBuzz build as well as ICU, which is
    // the claim that matters here: the HarfBuzz cases run against this same
    // tree, so these seven repairs must have left it exactly as they found it.
    MarkSharedTreeVerified(tree);
}

// Step 4a.1 case A. src/hb-features.h is written by the nested configure, not
// by its build command, so it cannot honestly be a BUILD_BYPRODUCTS entry;
// under Make a deleted one therefore has no producer to schedule. This is the
// proof that molga_text_harfbuzz_raw_install replays the pinned configure to
// put it back, and that doing so disturbs nothing else.
TEST_CASE("make generator recovery regenerates the deleted HarfBuzz feature header"
          * doctest::skip()) {
    const auto& tree = SharedRecoveryTree();
    RequireSharedTreeEntry(tree);

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
    INFO("build log " << logPath.string());
    REQUIRE(RunCapturingOutput({MOLGA_CMAKE_COMMAND, "--build", tree.string(),
                                "--target", "molga_text_harfbuzz_raw_install"},
                               logPath) == 0);
    const auto log = ReadLines(logPath);

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
    REQUIRE(LogHasLineContaining(log, HarfBuzzVerifiedLogLine()));

    // Both copies of the generated header are back at the independent pin.
    REQUIRE(RequiredSha256File(generated) == kGeneratedHeaderSha256);
    REQUIRE(RequiredSha256File(installedHeader) == kGeneratedHeaderSha256);

    // The repair rebuilt the archives, so the interesting claim is that it did
    // not perturb them: ZERO_AR_DATE=1 wraps both the original build and the
    // replay precisely so the restored bytes are the bytes that were there.
    REQUIRE(HbBuildArchiveDigests(tree) == archivesBefore);

    MarkSharedTreeVerified(tree);
}

// Step 4a.1 case B, and the reason the boundary pins hb-features.h by SHA at
// all. The header is generated, so it has no source-tree authority; a boundary
// that compared the installed copy against the build-tree copy alone would let
// two identically wrong copies agree their way past it. Corrupting both with
// the same byte is that false agreement, made on purpose.
TEST_CASE("make generator recovery rejects two identically wrong HarfBuzz headers"
          * doctest::skip()) {
    const auto& tree = SharedRecoveryTree();
    RequireSharedTreeEntry(tree);

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
    INFO("build log " << logPath.string());
    REQUIRE(RunCapturingOutput({MOLGA_CMAKE_COMMAND, "--build", tree.string(),
                                "--target", "molga_text_harfbuzz_raw_install"},
                               logPath) == 0);
    const auto log = ReadLines(logPath);

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
    REQUIRE(LogHasLineContaining(log, HarfBuzzVerifiedLogLine()));

    REQUIRE(RequiredSha256File(generated) == kGeneratedHeaderSha256);
    REQUIRE(RequiredSha256File(installedHeader) == kGeneratedHeaderSha256);
    REQUIRE(HbBuildArchiveDigests(tree) == archivesBefore);

    MarkSharedTreeVerified(tree);
}

// Step 4a.1 case C. hb-blob.h is a transitive include of hb.h that no Molga
// source names directly, so a deleted one is exactly the kind of hole that
// surfaces as an incomprehensible compile error in a consumer rather than as a
// missing file. The wrapper is what every consumer's build order goes through,
// so it has to restore it before any of them compiles.
TEST_CASE("make generator recovery restores a deleted transitive HarfBuzz header"
          * doctest::skip()) {
    const auto& tree = SharedRecoveryTree();
    RequireSharedTreeEntry(tree);

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
    INFO("build log " << logPath.string());
    REQUIRE(RunCapturingOutput({MOLGA_CMAKE_COMMAND, "--build", tree.string(),
                                "--target", "molga_text_harfbuzz_raw_install"},
                               logPath) == 0);
    const auto log = ReadLines(logPath);

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
    REQUIRE(LogHasLineContaining(log, HarfBuzzVerifiedLogLine()));

    // Byte-identical to the clean pinned source, not to some other installed
    // copy, and restored before the target that every consumer depends on
    // reports success.
    REQUIRE(std::filesystem::is_regular_file(installedBlob));
    REQUIRE(RequiredSha256File(installedBlob) == pinned);
    REQUIRE(HbBuildArchiveDigests(tree) == archivesBefore);

    MarkSharedTreeVerified(tree);
}

// Step 4e. Every other case here asks whether a repair restores a file; this
// one asks whether it restores the same bytes after the tools that produce them
// have genuinely re-run.
//
// Five files go: both build-tree raw archives, both installed copies of them,
// and the final composite. The compiled objects stay. That combination is what
// makes the sequence a rebuild rather than a reinstall — with no archive left
// anywhere in the tree there is nothing to copy back, so the only thing that
// can produce `harfbuzz-build/libharfbuzz.a` again is upstream's own `ar`
// followed by `ranlib`, running over objects that were never touched.
//
// Identity is claimed inside this one tree only. A Debug object file can carry
// the build root in its debug strings, so two trees at different paths are not
// expected to agree and asserting that they do would be a flaky test rather
// than a stronger one.
TEST_CASE("make generator recovery rebuilds a byte-identical HarfBuzz composite"
          * doctest::skip()) {
    const auto& tree = SharedRecoveryTree();
    RequireSharedTreeEntry(tree);

    const auto hbBuild = HbBuildDir(tree);
    const auto rawCore = hbBuild / "libharfbuzz.a";
    const auto rawAdapter = hbBuild / "libharfbuzz-icu.a";
    const auto installedCore = HbRawDir(tree) / "lib/libharfbuzz.a";
    const auto installedAdapter = HbRawDir(tree) / "lib/libharfbuzz-icu.a";
    const auto composite = HbCompositeArchive(tree);
    std::filesystem::create_directories(HbLogDir(tree));

    // ── 1. Record what the tree currently holds ──────────────────────────────
    const auto resultRoot = tree / "harfbuzz-determinism";
    REQUIRE(PathIsUnder(resultRoot, tree));
    std::filesystem::remove_all(resultRoot);
    std::filesystem::create_directories(resultRoot);

    const auto before = ReadOnlyCompositeProvenance(tree, resultRoot, "before.json");
    const std::string ar = before.at("arPath").get<std::string>();
    const std::vector<std::filesystem::path> listed = {rawCore, rawAdapter, composite};
    std::vector<std::filesystem::path> listingPathsBefore;
    std::vector<std::vector<std::string>> listingsBefore;
    for (std::size_t i = 0; i < listed.size(); ++i) {
        listingPathsBefore.push_back(
            resultRoot / ("members-before-" + std::to_string(i) + ".txt"));
        listingsBefore.push_back(
            ArchiveMemberListing(ar, listed[i], listingPathsBefore.back()));
    }
    const auto objectsBefore = NestedObjectDigests(tree);
    const auto archivesBefore = HbBuildArchiveDigests(tree);

    // ── 2. Delete exactly five files, and no object ──────────────────────────
    for (const auto& victim : {rawCore, rawAdapter, installedCore,
                               installedAdapter, composite}) {
        RemoveFileUnder(victim, tree);
    }
    RequireNestedObjectsUnchanged(tree, "after the deletions", objectsBefore);

    // ── 3. The raw-install boundary alone, then the composite producer alone ─
    const auto boundaryLog = HbLogDir(tree) / "determinism-raw-install.log";
    REQUIRE(PathIsUnder(boundaryLog, tree));
    INFO("boundary log " << boundaryLog.string());
    REQUIRE(RunCapturingOutput({MOLGA_CMAKE_COMMAND, "--build", tree.string(),
                                "--target", "molga_text_harfbuzz_raw_install"},
                               boundaryLog) == 0);
    const auto boundary = ReadLines(boundaryLog);

    // ── 4. The boundary rebuilt, and rebuilt the two nested targets ──────────
    // The reason names the deleted build-tree archive, so the rebuild was
    // triggered by the missing archive rather than by a reconfigure — and no
    // reconfigure happened, which is what leaves the objects reusable.
    REQUIRE(LogHasLineContaining(
        boundary, "rebuilding the nested HarfBuzz archives: " + rawCore.string() +
                      " is missing"));
    REQUIRE_FALSE(
        LogHasLineContaining(boundary, "reconfiguring the nested HarfBuzz build:"));

    // The exact command, read off the single line that announces it: the two
    // nested targets by name, under ZERO_AR_DATE=1, against this build
    // directory. Three claims about one line rather than three lines, because
    // the environment and the targets only mean anything together.
    const std::string announced =
        TheLineContaining(boundary, kArchiveCommandAnnouncement);
    INFO("announced " << announced);
    REQUIRE(announced.find(" -E env ZERO_AR_DATE=1 ") != std::string::npos);
    REQUIRE(LineEndsWith(announced, " --build " + hbBuild.string() +
                                        " --target harfbuzz harfbuzz-icu"));

    // The crux of the whole case: Make prints this only when it executes the
    // archive recipe, and that recipe is CMakeFiles/<target>.dir/link.txt —
    // `ar` and then `ranlib`. A sequence that merely reinstalled unchanged
    // build-tree archives cannot produce either line. The language word is
    // deliberately outside the match: upstream compiles the core from a C
    // amalgamation and the adapter from C++, so Make says "Linking C" for one
    // and "Linking CXX" for the other.
    REQUIRE(LogHasLineEndingWith(boundary, "static library libharfbuzz.a"));
    REQUIRE(LogHasLineEndingWith(boundary, "static library libharfbuzz-icu.a"));
    REQUIRE(LogHasLineEndingWith(boundary, "Built target harfbuzz"));
    REQUIRE(LogHasLineEndingWith(boundary, "Built target harfbuzz-icu"));

    // Only then the install side, and only because the copies were deleted.
    REQUIRE(LogHasLineContaining(
        boundary, "repairing the nested HarfBuzz install: installed " +
                      installedCore.string() + " is missing"));
    REQUIRE(LogHasLineContaining(boundary, HarfBuzzVerifiedLogLine()));

    // Nothing was recompiled, so the archives above were assembled from exactly
    // the objects that were already there.
    RequireNestedObjectsUnchanged(tree, "after the rebuild", objectsBefore);
    // HbBuildArchiveDigests hashes exactly the two archives and names the one
    // that is missing, so it is also the existence check.
    REQUIRE(HbBuildArchiveDigests(tree) == archivesBefore);

    // The composite producer on its own. Its output was deleted, so the
    // file-level rule the custom command declares is what runs; the boundary
    // ahead of it in the graph now has nothing left to repair.
    const auto producerLog = HbLogDir(tree) / "determinism-composite.log";
    REQUIRE(PathIsUnder(producerLog, tree));
    INFO("producer log " << producerLog.string());
    REQUIRE(RunCapturingOutput({MOLGA_CMAKE_COMMAND, "--build", tree.string(),
                                "--target", "molga_text_harfbuzz_composite"},
                               producerLog) == 0);
    const auto producer = ReadLines(producerLog);
    REQUIRE_FALSE(
        LogHasLineContaining(producer, "rebuilding the nested HarfBuzz archives:"));
    REQUIRE_FALSE(
        LogHasLineContaining(producer, "repairing the nested HarfBuzz install:"));
    REQUIRE(std::filesystem::is_regular_file(composite));

    // ── 5. Every recorded value, re-derived the same way, unchanged ──────────
    const auto after = ReadOnlyCompositeProvenance(tree, resultRoot, "after.json");
    for (const auto& key : CompositeProvenanceKeys()) {
        INFO("provenance value " << key);
        REQUIRE(after.at(key) == before.at(key));
    }
    // And nothing else the mode reports moved either — the archiver family, its
    // append flags, the three tool paths, and the three adapter symbol counts.
    //
    // Known scope: archiverFamily is whatever this host resolved, so the
    // determinism claim is proved on one family per run. ZERO_AR_DATE=1 is
    // Apple ar's variable and VerifyTextDependencies branches apple -> "qcs"
    // against "qcsD" elsewhere, so a GNU or LLVM ar takes a path no proof in
    // this suite has yet exercised.
    REQUIRE(after == before);

    for (std::size_t i = 0; i < listed.size(); ++i) {
        const auto listingPathAfter =
            resultRoot / ("members-after-" + std::to_string(i) + ".txt");
        // Both files by name: doctest prints the vectors as `{?} == {?}`, so
        // these two paths in the retained tree are the whole diagnosis.
        INFO("member listing of " << listed[i].string() << ": "
                                  << listingPathsBefore[i].string() << " vs "
                                  << listingPathAfter.string());
        REQUIRE(ArchiveMemberListing(ar, listed[i], listingPathAfter) ==
                listingsBefore[i]);
    }

    std::filesystem::remove_all(resultRoot);
    MarkSharedTreeVerified(tree);
}

}  // TEST_SUITE("text-make-recovery")
