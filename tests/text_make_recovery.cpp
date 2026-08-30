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

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
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
// this file has to prove things about. The slug is a parameter only so the
// sweep above and the tree name cannot disagree; every case in this file shares
// one tree through SharedRecoveryTree() below, because a second slug would mean
// paying the configure and the whole nested ICU build again.
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

std::vector<std::string> ReadLines(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE_MESSAGE(input.good(), path.string());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        // Trailing whitespace would defeat the suffix matching below, and a
        // build tool's progress line is not a place to be precious about it.
        while (!line.empty() &&
               std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
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

// ── One shared tree for every case in this file ──────────────────────────────
constexpr char kRecoveryTreeSlug[] = "text-make-recovery";

// The configure is about a minute and the nested ICU build about three more, so
// a tree per case would multiply four minutes by the number of proofs. One tree
// is shared instead, which imposes the rule that every case must leave it
// verified-good — which is also what makes the cases order-independent.
struct SharedTreeHolder {
    std::filesystem::path path;
    // Pessimistic. A case that trips a REQUIRE never reaches its own
    // MarkSharedTreeVerified, so the tree survives for inspection; minutes to
    // reproduce a failure is exactly the cost this is here to avoid paying
    // twice. Every assertion in these cases is therefore a REQUIRE: a bare
    // CHECK would fail the run and still let the case fall through to the mark.
    bool retain = true;

    ~SharedTreeHolder() {
        if (path.empty()) return;
        if (retain) {
            std::cerr << "retained make-recovery tree for inspection: "
                      << path.string() << "\n";
            return;
        }
        std::filesystem::remove_all(path);
    }
};

SharedTreeHolder& SharedTreeState() {
    static SharedTreeHolder holder;
    return holder;
}

const std::filesystem::path& SharedRecoveryTree() {
    SharedTreeHolder& holder = SharedTreeState();
    if (holder.path.empty()) {
        holder.path = ConfigureUnixMakefilesTree(kRecoveryTreeSlug);
        MESSAGE("Make-generator recovery tree: " << holder.path.string());
        // Bring it to the state every case starts from. The HarfBuzz wrapper
        // pulls the whole ICU chain behind it, so this one build also leaves
        // the ICU case's first build a no-op.
        REQUIRE(BuildTreeTarget(holder.path, "molga_text_harfbuzz_raw_install") == 0);
    }
    holder.retain = true;
    return holder.path;
}

void MarkSharedTreeVerified() { SharedTreeState().retain = false; }

// The wrapper's own postcondition for ICU: every consumed installed file equals
// the authority it was copied from — archives from the build tree, headers from
// the clean pinned source. Re-checked after each repair so restoring the deleted
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

// ── Step 4a.1: the raw HarfBuzz install ──────────────────────────────────────
// hb-features.h is written by the nested configure, so unlike the other 33
// public headers it has no source-tree authority to answer to.
// RepairHarfBuzzRawInstall.cmake pins its bytes independently, and this is the
// same pin stated where the build cannot supply it: a boundary checked only
// against the values the build hands it agrees with any build at all.
constexpr char kGeneratedHeaderSha256[] =
    "b9f5b0184edfab48fa3953f3b5fe8f72f72db0c08ebf6ccfc2541451c8bbc597";

constexpr int kHarfBuzzInstalledHeaderCount = 34;

std::filesystem::path HbBuildDir(const std::filesystem::path& tree) {
    return tree / "text-dependencies/harfbuzz-build";
}
std::filesystem::path HbRawDir(const std::filesystem::path& tree) {
    return tree / "text-dependencies/harfbuzz-raw";
}
std::filesystem::path HbGeneratedHeader(const std::filesystem::path& tree) {
    return HbBuildDir(tree) / "src/hb-features.h";
}
std::filesystem::path HbInstalledHeader(const std::filesystem::path& tree,
                                        const std::string& name) {
    return HbRawDir(tree) / "include/harfbuzz" / name;
}
std::filesystem::path HbLogDir(const std::filesystem::path& tree) {
    return tree / "make-recovery-logs";
}

// The two build-tree archives, in the order the repair boundary names them.
const std::vector<std::string>& HbArchiveNames() {
    static const std::vector<std::string> names = {"libharfbuzz.a", "libharfbuzz-icu.a"};
    return names;
}

std::vector<std::string> HbBuildArchiveDigests(const std::filesystem::path& tree) {
    std::vector<std::string> digests;
    for (const auto& name : HbArchiveNames()) {
        digests.push_back(RequiredSha256File(HbBuildDir(tree) / name));
    }
    return digests;
}

// The complete Step 4 cache matrix, plus the three configure values that are
// part of the same one argument vector without being matrix entries.
// TextDependencies.cmake hands the boundary that matrix, so the boundary alone
// can only ever prove "the cache matches what I was given"; stating it here,
// independently of the build, is what makes it "the cache matches what Step 4
// pins". The toolchain half of the vector is deliberately absent — compiler,
// SDK and deployment target are host provenance, not something to pin.
std::vector<std::string> PinnedNestedCacheLines(const std::filesystem::path& tree) {
    const std::string deps = (tree / "text-dependencies").string();
    return {
        "BUILD_SHARED_LIBS:BOOL=OFF",
        "BUILD_FRAMEWORK:BOOL=OFF",
        "HB_HAVE_ICU:BOOL=ON",
        "HB_HAVE_CORETEXT:BOOL=OFF",
        "HB_HAVE_CAIRO:BOOL=OFF",
        "HB_HAVE_FREETYPE:BOOL=OFF",
        "HB_HAVE_GRAPHITE2:BOOL=OFF",
        "HB_HAVE_GLIB:BOOL=OFF",
        "HB_HAVE_GOBJECT:BOOL=OFF",
        "HB_HAVE_INTROSPECTION:BOOL=OFF",
        "HB_BUILD_UTILS:BOOL=OFF",
        "HB_BUILD_SUBSET:BOOL=OFF",
        "HB_BUILD_RASTER:BOOL=OFF",
        "HB_BUILD_VECTOR:BOOL=OFF",
        "HB_BUILD_GPU:BOOL=OFF",
        // Upstream declares this one STRING rather than BOOL, and the pin is on
        // the whole typed line, so a silent retype is a mismatch here too.
        "HB_BUILD_GPU_DEMO:STRING=OFF",
        "CMAKE_DISABLE_FIND_PACKAGE_Python3:BOOL=ON",
        "ICU_INCLUDE_DIR:PATH=" + deps + "/icu-raw/include",
        "ICU_UC_LIBRARY_RELEASE:FILEPATH=" + deps + "/icu/lib/libicuuc.a",
        "ICU_UC_LIBRARY_DEBUG:FILEPATH=" + deps + "/icu/lib/libicuuc.a",
        "CMAKE_BUILD_TYPE:STRING=Debug",
        "CMAKE_INSTALL_PREFIX:PATH=" + deps + "/harfbuzz-raw",
        // Pinned HarfBuzz propagates neither the nested ICU include root nor
        // U_STATIC_IMPLEMENTATION to its harfbuzz-icu target, which is the whole
        // reason this flag string is part of the configure command.
        "CMAKE_CXX_FLAGS:STRING=-I" + deps +
            "/icu-raw/include -DU_STATIC_IMPLEMENTATION",
    };
}

void RequireNestedCacheMatrix(const std::filesystem::path& tree) {
    const auto lines = ReadLines(HbBuildDir(tree) / "CMakeCache.txt");
    REQUIRE_FALSE(lines.empty());
    for (const auto& expected : PinnedNestedCacheLines(tree)) {
        INFO("pinned nested cache line " << expected);
        REQUIRE(std::find(lines.begin(), lines.end(), expected) != lines.end());
    }
}

// The wrapper's postcondition for HarfBuzz: the two installed archives equal
// the build-tree archives beside them, the 33 verbatim headers equal the clean
// pinned submodule, and the generated header equals the independent pin in both
// of its copies. Nothing here is ever compared against another installed copy.
void RequireHarfBuzzInstallMatchesAuthorities(const std::filesystem::path& tree) {
    const auto build = HbBuildDir(tree);
    const auto raw = HbRawDir(tree);
    for (const auto& name : HbArchiveNames()) {
        INFO("installed archive " << name);
        REQUIRE(std::filesystem::is_regular_file(raw / "lib" / name));
        REQUIRE(RequiredSha256File(raw / "lib" / name) ==
                RequiredSha256File(build / name));
    }
    REQUIRE(RequiredSha256File(HbGeneratedHeader(tree)) == kGeneratedHeaderSha256);

    int headers = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(raw / "include/harfbuzz")) {
        const std::string name = entry.path().filename().string();
        INFO("installed header " << name);
        // A symlink would hash-match its authority while pointing consumers at
        // bytes outside the nested prefix, so it is not an installed header.
        REQUIRE_FALSE(entry.is_symlink());
        REQUIRE(entry.is_regular_file());
        const std::string expected =
            name == "hb-features.h"
                ? std::string(kGeneratedHeaderSha256)
                : RequiredSha256File(SourceRoot() / "external/harfbuzz/src" / name);
        REQUIRE(RequiredSha256File(entry.path()) == expected);
        ++headers;
    }
    REQUIRE(headers == kHarfBuzzInstalledHeaderCount);
}

// What every case must be able to say about the tree when it hands it on.
void RequireHarfBuzzTreeVerified(const std::filesystem::path& tree) {
    RequireNestedCacheMatrix(tree);
    RequireHarfBuzzInstallMatchesAuthorities(tree);
}

// ── No host dependency resolution ────────────────────────────────────────────
// The plan's ruling, implemented literally: the configured CMake, compiler,
// SDK, ar, ranlib and nm paths are how the host toolchain is recorded, not a
// dependency that leaked in. So a Homebrew CMake is provenance while a Homebrew
// include directory is a finding. This set is the exemption, so a new entry
// nobody anticipated fails loudly rather than slipping through.
const std::set<std::string>& ToolchainProvenanceCacheEntries() {
    static const std::set<std::string> entries = {
        "CMAKE_ADDR2LINE", "CMAKE_AR", "CMAKE_COMMAND", "CMAKE_CPACK_COMMAND",
        "CMAKE_CTEST_COMMAND", "CMAKE_C_COMPILER", "CMAKE_C_COMPILER_AR",
        "CMAKE_C_COMPILER_RANLIB", "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_AR",
        "CMAKE_CXX_COMPILER_RANLIB", "CMAKE_DLLTOOL", "CMAKE_EDIT_COMMAND",
        "CMAKE_INSTALL_NAME_TOOL", "CMAKE_LINKER", "CMAKE_MAKE_PROGRAM",
        "CMAKE_MT", "CMAKE_NM", "CMAKE_OBJCOPY", "CMAKE_OBJDUMP",
        "CMAKE_OSX_SYSROOT", "CMAKE_RANLIB", "CMAKE_READELF", "CMAKE_ROOT",
        "CMAKE_STRIP", "CMAKE_TAPI", "CMAKE_UNAME"};
    return entries;
}

std::string ToLower(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

// ';' separates CMake list values and '[' ']' bracket FindPackage's recorded
// details; without them two adjacent absolute paths arrive as one word.
std::vector<std::string> SplitWords(const std::string& text) {
    static const std::string kDelimiters = " \t\r\n;[]\"'";
    std::vector<std::string> words;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find_first_of(kDelimiters, start);
        if (end == std::string::npos) {
            words.push_back(text.substr(start));
            break;
        }
        if (end > start) words.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return words;
}

// One word naming an absolute path: either the word itself starts with '/', or
// it is a compiler flag with the path glued on. A word such as
// CMakeFiles/harfbuzz-icu.dir/src/hb-icu.cc.o is deliberately not one — taking
// everything from its first '/' would manufacture "/harfbuzz-icu.dir/..." and
// the ICU containment rule below would then reject a relative object path.
bool AbsolutePathToken(const std::string& word, std::string* out) {
    if (!word.empty() && word.front() == '/') {
        *out = word;
        return true;
    }
    for (const char* flag : {"-isystem", "-iframework", "-isysroot",
                             "--sysroot=", "-I", "-L", "-F"}) {
        const std::string prefix(flag);
        if (word.size() > prefix.size() &&
            word.compare(0, prefix.size(), prefix) == 0 &&
            word[prefix.size()] == '/') {
            *out = word.substr(prefix.size());
            return true;
        }
    }
    return false;
}

// The five things the plan forbids the nested build from resolving, applied one
// word at a time so a single exempt word — the archiver at the head of a link
// line, say — does not exempt everything beside it.
void CollectHostResolution(const std::string& origin, const std::string& word,
                           bool toolchainProvenance,
                           const std::filesystem::path& tree,
                           std::vector<std::string>& findings) {
    const std::string lowered = ToLower(word);
    // CoreText and FreeType are optional backends pinned OFF. Their names
    // appear in cache entry names and in upstream's doc comments, neither of
    // which is scanned; a *value* naming either is a backend that resolved.
    for (const char* forbidden : {"coretext", "freetype"}) {
        if (lowered.find(forbidden) != std::string::npos) {
            findings.push_back(origin + ": " + forbidden + " in " + word);
        }
    }
    if (!toolchainProvenance) {
        for (const char* forbidden : {"/opt/homebrew", "/usr/local"}) {
            if (lowered.find(forbidden) != std::string::npos) {
                findings.push_back(origin + ": " + forbidden + " in " + word);
            }
        }
    }
    std::string token;
    if (!AbsolutePathToken(word, &token)) return;
    if (ToLower(token).find("icu") == std::string::npos) return;
    // A resolved ICU path is expected — HB_HAVE_ICU is ON. A resolved ICU path
    // outside this tree is the system installation the vendoring exists to
    // avoid, which is what "no system ICU" actually means here.
    if (!PathIsUnder(token, tree)) {
        findings.push_back(origin + ": ICU outside the tree: " + token);
    }
}

void CollectFromNestedCache(const std::filesystem::path& tree,
                            std::vector<std::string>& findings) {
    for (const auto& line : ReadLines(HbBuildDir(tree) / "CMakeCache.txt")) {
        if (line.empty() || line.front() == '#' || line.rfind("//", 0) == 0) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string typedName = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        const std::string name = typedName.substr(0, typedName.find(':'));
        const bool provenance = ToolchainProvenanceCacheEntries().count(name) != 0;
        for (const auto& word : SplitWords(value)) {
            CollectHostResolution("cache " + typedName, word, provenance, tree,
                                  findings);
        }
    }
}

void CollectFromGeneratedArguments(const std::filesystem::path& tree,
                                   std::vector<std::string>& findings) {
    for (const char* target : {"harfbuzz.dir", "harfbuzz-icu.dir"}) {
        for (const char* file : {"flags.make", "link.txt"}) {
            const auto path = HbBuildDir(tree) / "CMakeFiles" / target / file;
            // A generator layout change that moved these would otherwise leave
            // this case scanning nothing and reporting success.
            REQUIRE_MESSAGE(std::filesystem::is_regular_file(path), path.string());
            const bool linkLine = std::string(file) == "link.txt";
            for (const auto& line : ReadLines(path)) {
                if (line.empty() || line.front() == '#') continue;
                const auto words = SplitWords(line);
                for (std::size_t i = 0; i < words.size(); ++i) {
                    // The archiver invoked at the head of a link line, and the
                    // SDK that follows -isysroot, are the host toolchain the
                    // plan rules legitimate. Everything else on the line is a
                    // dependency the nested configure resolved.
                    const bool provenance =
                        (linkLine && i == 0) ||
                        (i > 0 && (words[i - 1] == "-isysroot" ||
                                   words[i - 1] == "--sysroot"));
                    CollectHostResolution(path.string(), words[i], provenance,
                                          tree, findings);
                }
            }
        }
    }
}

std::string JoinLines(const std::vector<std::string>& lines) {
    std::string joined;
    for (const auto& line : lines) joined += "\n  " + line;
    return joined;
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

// Step 4a.1 case D. The vendoring exists so that nothing in the text stack
// resolves a host installation, and the nested HarfBuzz configure is the one
// place with a find_package call that could. This reads what that configure
// actually decided — its cache, and the compile and link arguments it generated
// for both archive targets — rather than what the build was asked for.
TEST_CASE("nested HarfBuzz resolves no host dependency" * doctest::skip()) {
    const auto& tree = SharedRecoveryTree();
    RequireHarfBuzzTreeVerified(tree);

    // The plan's Expected list: both raw archives and all 34 headers below the
    // nested prefix. RequireHarfBuzzInstallMatchesAuthorities already proved
    // their bytes; this is the containment half of the same claim.
    for (const auto& name : HbArchiveNames()) {
        INFO("installed archive " << name);
        REQUIRE(PathIsUnder(HbRawDir(tree) / "lib" / name, tree));
    }
    int headers = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(HbRawDir(tree) / "include/harfbuzz")) {
        INFO("installed header " << entry.path().string());
        REQUIRE(PathIsUnder(entry.path(), tree));
        ++headers;
    }
    REQUIRE(headers == kHarfBuzzInstalledHeaderCount);

    // Collected rather than asserted one at a time: a configuration that
    // resolved a host installation usually does it in several places at once,
    // and finding them one rerun at a time costs minutes each.
    std::vector<std::string> findings;
    CollectFromNestedCache(tree, findings);
    CollectFromGeneratedArguments(tree, findings);
    REQUIRE_MESSAGE(findings.empty(), JoinLines(findings));

    MarkSharedTreeVerified();
}

}  // TEST_SUITE("text-make-recovery")
