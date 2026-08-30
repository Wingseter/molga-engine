#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Common/Sha256.h"

extern char** environ;

namespace {

std::filesystem::path SourceRoot() { return std::filesystem::path(MOLGA_SOURCE_DIR); }
std::filesystem::path BinaryRoot() { return std::filesystem::path(MOLGA_BINARY_DIR); }

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE_MESSAGE(input.good(), path.string());
    return nlohmann::json::parse(input);
}

// The portable contract must carry no absolute path and no checkout/build root.
void CheckPortable(const nlohmann::json& node) {
    if (node.is_string()) {
        const std::string value = node.get<std::string>();
        CHECK_FALSE(std::filesystem::path(value).is_absolute());
        CHECK(value.find(MOLGA_SOURCE_DIR) == std::string::npos);
        CHECK(value.find(MOLGA_BINARY_DIR) == std::string::npos);
    } else if (node.is_array() || node.is_object()) {
        for (const auto& child : node) CheckPortable(child);
    }
}

// Component-wise containment, never a string-prefix test: /a/bc must not count
// as living under /a/b.
bool PathIsUnder(const std::filesystem::path& candidate,
                 const std::filesystem::path& root) {
    const auto a = std::filesystem::weakly_canonical(candidate);
    const auto b = std::filesystem::weakly_canonical(root);
    auto ai = a.begin();
    auto bi = b.begin();
    for (; bi != b.end(); ++ai, ++bi) {
        if (ai == a.end() || *ai != *bi) return false;
    }
    return true;
}

void CheckCanonicalPathEquals(const nlohmann::json& recorded,
                              const std::filesystem::path& expected) {
    const auto actual = std::filesystem::weakly_canonical(
        std::filesystem::path(recorded.get<std::string>()));
    CHECK(actual == std::filesystem::weakly_canonical(expected));
}

void CheckCanonicalPathUnder(const nlohmann::json& recorded,
                             const std::filesystem::path& root) {
    CHECK(PathIsUnder(std::filesystem::path(recorded.get<std::string>()), root));
}

std::filesystem::path BuildPath(const std::string& relative) {
    REQUIRE_FALSE(relative.empty());
    REQUIRE_FALSE(std::filesystem::path(relative).is_absolute());
    const auto resolved =
        std::filesystem::weakly_canonical(BinaryRoot() / relative);
    REQUIRE(PathIsUnder(resolved, BinaryRoot()));
    return resolved;
}

std::string RequiredSha256File(const std::filesystem::path& path) {
    std::string error;
    const std::string digest = molga::Sha256File(path, &error);
    // Parenthesized: doctest streams the first message operand with operator*,
    // which binds tighter than the string concatenation.
    REQUIRE_MESSAGE(error.empty(), (path.string() + ": " + error));
    REQUIRE(digest.size() == 64);
    REQUIRE(digest.find_first_not_of("0123456789abcdef") == std::string::npos);
    return digest;
}

// Recursive case-sensitive substring search over every key and string value, so
// `libharfbuzz-icu.a` cannot slip past a check for `harfbuzz-icu`.
bool JsonContainsSubstring(const nlohmann::json& node, std::string_view token) {
    if (node.is_string()) {
        return node.get<std::string>().find(token) != std::string::npos;
    }
    if (node.is_object()) {
        for (const auto& [key, value] : node.items()) {
            if (key.find(token) != std::string::npos) return true;
            if (JsonContainsSubstring(value, token)) return true;
        }
        return false;
    }
    if (node.is_array()) {
        for (const auto& child : node) {
            if (JsonContainsSubstring(child, token)) return true;
        }
    }
    return false;
}

void CheckExactObjectKeys(const nlohmann::json& actual,
                          const std::set<std::string>& expectedKeys) {
    REQUIRE(actual.is_object());
    std::set<std::string> actualKeys;
    for (const auto& [key, value] : actual.items()) actualKeys.insert(key);
    CHECK(actualKeys == expectedKeys);
}

void CheckExactPortableObjectKeys(const nlohmann::json& actual,
                                  const nlohmann::json& input,
                                  const std::set<std::string>& generatedKeys) {
    std::set<std::string> expected = generatedKeys;
    for (const auto& [key, value] : input.items()) expected.insert(key);
    CheckExactObjectKeys(actual, expected);
}

void CheckEveryDependencyArchiveIsStatic(const nlohmann::json& lock) {
    for (const char* archive : {"harfbuzz", "icu"}) {
        CHECK_FALSE(JsonContainsSubstring(lock.at(archive), ".dylib"));
        CHECK_FALSE(JsonContainsSubstring(lock.at(archive), ".so"));
    }
}

void CheckEveryIncludeAndArchiveUnderNestedPrefix(
    const nlohmann::json& lock, const std::filesystem::path& nestedPrefix) {
    CheckCanonicalPathUnder(lock.at("icu").at("includeRoot"), nestedPrefix);
    const auto& hb = lock.at("harfbuzz").at("icuComposite");
    for (const char* key : {"rawCorePath", "rawAdapterPath", "finalCompositePath"}) {
        CheckCanonicalPathUnder(hb.at(key), nestedPrefix);
    }
    const auto& icu = lock.at("icu").at("commonComposite");
    for (const char* key : {"rawCommonPath", "finalCompositePath"}) {
        CheckCanonicalPathUnder(icu.at(key), nestedPrefix);
    }
}

void CheckSharedDependencyFieldsEqual(const nlohmann::json& lock,
                                      const nlohmann::json& portable) {
    CHECK(lock.at("harfbuzz").at("commit") == portable.at("harfbuzz").at("commit"));
    CHECK(lock.at("harfbuzz").at("libraries") == portable.at("harfbuzz").at("libraries"));
    CHECK(lock.at("harfbuzz").at("compositeSha256") ==
          portable.at("harfbuzz").at("compositeSha256"));
    CHECK(lock.at("icu").at("commit") == portable.at("icu").at("commit"));
    CHECK(lock.at("icu").at("libraries") == portable.at("icu").at("libraries"));
    CHECK(lock.at("icu").at("archiveSha256") == portable.at("icu").at("archiveSha256"));
}

std::string ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) return {};
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void WriteFileBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Any leftover staging sibling or transaction directory beside the records.
int CountTransactionResidue(const std::filesystem::path& directory) {
    int residue = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string name = entry.path().filename().string();
        if (name.find("molga-new-") != std::string::npos ||
            name.find("molga-backup-") != std::string::npos ||
            name.find("molga-text-txn-") != std::string::npos ||
            name.find("molga-stage-") != std::string::npos) {
            ++residue;
        }
    }
    return residue;
}

int RunWithoutShell(const std::vector<std::string>& argv) {
    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& argument : argv) raw.push_back(const_cast<char*>(argument.c_str()));
    raw.push_back(nullptr);
    pid_t pid = 0;
    if (posix_spawn(&pid, raw[0], nullptr, nullptr, raw.data(), environ) != 0) return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Re-derive the HarfBuzz composite provenance from the files on disk through
// the verifier's closed read-only mode and require it to match the build lock.
void CheckHarfBuzzCompositeProvenanceMatchesFiles(const nlohmann::json& lock) {
    const auto resultRoot =
        BinaryRoot() / ("text-harfbuzz-readonly-" + std::to_string(::getpid()));
    std::filesystem::remove_all(resultRoot);
    std::filesystem::create_directories(resultRoot);
    const auto resultFile = resultRoot / "harfbuzz-read-only-result.json";
    REQUIRE_FALSE(std::filesystem::exists(resultFile));

    const int exitCode = RunWithoutShell({
        MOLGA_CMAKE_COMMAND,
        "-DMODE=HARFBUZZ_READ_ONLY",
        std::string("-DBUILD_LOCK=") + MOLGA_TEXT_DEPENDENCY_BUILD_LOCK,
        std::string("-DSOURCE_ROOT=") + MOLGA_SOURCE_DIR,
        std::string("-DBINARY_ROOT=") + MOLGA_BINARY_DIR,
        std::string("-DRESULT_ROOT=") + resultRoot.string(),
        std::string("-DRESULT_FILE=") + resultFile.string(),
        "-P",
        MOLGA_TEXT_VERIFY_DEPENDENCIES_SCRIPT,
    });
    CHECK(exitCode == 0);
    REQUIRE(std::filesystem::is_regular_file(resultFile));

    const auto result = ReadJson(resultFile);
    CheckExactObjectKeys(result, {"schemaVersion", "mode", "rawCoreSha256",
                                  "rawAdapterSha256", "adapterObjectSha256",
                                  "finalCompositeSha256", "filteredMemberDigest",
                                  "archiverFamily", "arPath", "ranlibPath",
                                  "nmPath", "arAppendFlags", "ranlibFlags",
                                  "zeroArDate", "rawCoreDefinedAdapterSymbols",
                                  "rawIcuAdapterDefinedAdapterSymbols",
                                  "compositeDefinedAdapterSymbols"});
    CHECK(result.at("schemaVersion").is_number_integer());
    CHECK(result.at("schemaVersion") == 1);
    CHECK(result.at("mode") == "HARFBUZZ_READ_ONLY");

    const auto& recorded = lock.at("harfbuzz").at("icuComposite");
    for (const auto& [key, value] : result.items()) {
        if (key == "schemaVersion" || key == "mode") continue;
        CHECK_MESSAGE(recorded.at(key) == value, key);
    }
    std::filesystem::remove_all(resultRoot);
}

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

TEST_CASE("immutable text inputs match the approved bytes") {
    const std::filesystem::path root = MOLGA_SOURCE_DIR;
    CHECK(std::filesystem::file_size(root / "resources/text/icudt78l.dat") ==
          33107232ULL);
    CHECK(molga::Sha256File(root / "resources/text/icudt78l.dat") ==
          "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b");
    CHECK(molga::Sha256File(root / "external/text/rasterizer/imstb_truetype.h") ==
          "c51a0f7e7ea760f2366bd3752635ec58e21fccfec4a832501639990ba6ce0528");
    CHECK(molga::Sha256File(root / "tests/fixtures/text/fonts/NotoSansKR-Regular.otf") ==
          "69975a0ac8472717870aefeab0a4d52739308d90856b9955313b2ad5e0148d68");
}

TEST_CASE("build provenance and portable dependency contract agree") {
    const auto lock = ReadJson(MOLGA_TEXT_DEPENDENCY_BUILD_LOCK);
    const auto portable = ReadJson(MOLGA_TEXT_DEPENDENCY_CONTRACT);
    const auto input = ReadJson(SourceRoot() /
                                "resources/text/dependency-contract.input.json");

    CHECK(lock.at("harfbuzz").at("commit") == portable.at("harfbuzz").at("commit"));
    CHECK(lock.at("icu").at("commit") == portable.at("icu").at("commit"));
    CHECK(lock.at("icuData").at("sha256") == input.at("icuData").at("sha256"));
    CHECK(lock.at("harfbuzz").at("options").at("icu") == true);
    CHECK(lock.at("harfbuzz").at("options").at("coretext") == false);
    CHECK(lock.at("icu").at("libraries") ==
          nlohmann::json::array({"icui18n", "icuuc"}));
    CHECK(portable.at("icu").at("libraries") ==
          nlohmann::json::array({"icui18n", "icuuc"}));

    CheckExactPortableObjectKeys(portable.at("icu"), input.at("icu"), {"archiveSha256"});
    CheckExactObjectKeys(portable.at("icu").at("archiveSha256"), {"icui18n", "icuuc"});
    CHECK(portable.at("icu").at("archiveSha256") == lock.at("icu").at("archiveSha256"));
    CHECK(portable.at("icu").at("archiveSha256").at("icuuc") ==
          lock.at("icu").at("commonComposite").at("finalCompositeSha256"));

    // Composite internals stay build-lock-only.
    for (const char* token : {"commonComposite", "rawCommon", "rawI18n", "stubSource",
                              "stubObject", "libicudata", "icudt78_dat"}) {
        CHECK_FALSE(JsonContainsSubstring(portable.at("icu"), token));
    }

    CHECK(lock.at("harfbuzz").at("libraries") == nlohmann::json::array({"harfbuzz"}));
    CHECK(portable.at("harfbuzz").at("libraries") == nlohmann::json::array({"harfbuzz"}));
    CHECK(lock.at("harfbuzz").at("compositeSha256") ==
          portable.at("harfbuzz").at("compositeSha256"));
    CHECK(lock.at("harfbuzz").at("compositeSha256") ==
          RequiredSha256File(BuildPath("text-dependencies/harfbuzz/lib/libharfbuzz.a")));

    const auto& hbComposite = lock.at("harfbuzz").at("icuComposite");
    CHECK(hbComposite.at("finalCompositeSha256") ==
          lock.at("harfbuzz").at("compositeSha256"));
    CHECK(hbComposite.at("rawCoreDefinedAdapterSymbols") == 0);
    CHECK(hbComposite.at("rawIcuAdapterDefinedAdapterSymbols") == 1);
    CHECK(hbComposite.at("compositeDefinedAdapterSymbols") == 1);
    CheckHarfBuzzCompositeProvenanceMatchesFiles(lock);

    CheckExactPortableObjectKeys(portable.at("harfbuzz"), input.at("harfbuzz"),
                                 {"compositeSha256"});
    for (const char* token : {"harfbuzz-icu", "icuComposite", "rawCore",
                              "rawAdapter", "adapterObject"}) {
        CHECK_FALSE(JsonContainsSubstring(portable, token));
    }

    CHECK(std::filesystem::path(
              lock.at("harfbuzz").at("sourcePath").get<std::string>()).is_absolute());
    CheckCanonicalPathEquals(lock.at("harfbuzz").at("sourcePath"),
                             SourceRoot() / "external/harfbuzz");
    CheckCanonicalPathEquals(lock.at("icu").at("sourcePath"),
                             SourceRoot() / "external/icu");
    CheckEveryIncludeAndArchiveUnderNestedPrefix(lock, BinaryRoot() / "text-dependencies");
    CheckEveryDependencyArchiveIsStatic(lock);
    CheckSharedDependencyFieldsEqual(lock, portable);
    CheckPortable(portable);
}

// Steps 9a/9b: the generated record pair is published through the same
// transactional publisher as every other text artifact, so an interrupted
// publication must restore both previous documents rather than leave the
// build lock and the portable contract disagreeing with each other.
TEST_CASE("generated dependency records roll back and republish atomically") {
    const std::filesystem::path lockPath = MOLGA_TEXT_DEPENDENCY_BUILD_LOCK;
    const std::filesystem::path contractPath = MOLGA_TEXT_DEPENDENCY_CONTRACT;
    const std::string originalLock = ReadFileBytes(lockPath);
    const std::string originalContract = ReadFileBytes(contractPath);
    REQUIRE_FALSE(originalLock.empty());
    REQUIRE_FALSE(originalContract.empty());

    const auto scratch =
        BinaryRoot() / ("text-record-rollback-" + std::to_string(::getpid()));
    std::filesystem::remove_all(scratch);
    std::filesystem::create_directories(scratch);

    const auto stagedLock = scratch / "staged-lock.json";
    const auto stagedContract = scratch / "staged-contract.json";
    const std::string newLock = originalLock + "\n";
    const std::string newContract = originalContract + "\n";
    WriteFileBytes(stagedLock, newLock);
    WriteFileBytes(stagedContract, newContract);

    const auto script = scratch / "publish.cmake";
    const auto journal = scratch / "record-journal.json";
    WriteFileBytes(script,
        std::string("cmake_minimum_required(VERSION 3.27)\n") +
        "include(\"" + MOLGA_SOURCE_DIR + "/cmake/TextArtifactTransaction.cmake\")\n"
        "text_publish_artifact_set(ok \"" + journal.string() + "\" ${INJECT} \"" +
        (BinaryRoot() / "generated").string() + ";" + scratch.string() + "\"\n"
        "  \"" + stagedLock.string() + "\" \"" + lockPath.string() + "\"\n"
        "  \"" + stagedContract.string() + "\" \"" + contractPath.string() + "\")\n"
        "if(ok)\n  message(STATUS \"PUBLISHED\")\nelse()\n  message(STATUS \"ROLLED_BACK\")\nendif()\n");

    for (const char* injection : {"2", "-2"}) {
        // Injected failure: both destinations must return to their old bytes.
        CHECK(RunWithoutShell({MOLGA_CMAKE_COMMAND,
                               std::string("-DINJECT=") + injection,
                               "-P", script.string()}) == 0);
        CHECK(ReadFileBytes(lockPath) == originalLock);
        CHECK(ReadFileBytes(contractPath) == originalContract);
        CHECK_FALSE(std::filesystem::exists(journal));
        CHECK(CountTransactionResidue(lockPath.parent_path()) == 0);

        // Clean rerun: both destinations must carry the new bytes together.
        CHECK(RunWithoutShell({MOLGA_CMAKE_COMMAND, "-DINJECT=0",
                               "-P", script.string()}) == 0);
        CHECK(ReadFileBytes(lockPath) == newLock);
        CHECK(ReadFileBytes(contractPath) == newContract);
        CHECK(CountTransactionResidue(lockPath.parent_path()) == 0);

        // Restore the real records for the next iteration and later tests.
        WriteFileBytes(lockPath, originalLock);
        WriteFileBytes(contractPath, originalContract);
    }

    std::filesystem::remove_all(scratch);
    CHECK(ReadFileBytes(lockPath) == originalLock);
    CHECK(ReadFileBytes(contractPath) == originalContract);
}

// Step 3.1b: the raw-ICU repair wrapper, proved under the generator it exists
// for. BUILD_BYPRODUCTS/INSTALL_BYPRODUCTS give Ninja file-level rules, so a
// deleted ExternalProject byproduct there is simply rebuilt. Under Unix
// Makefiles they only feed the clean list and inter-target ordering — there is
// no per-file rule — so a deleted byproduct stays deleted and every downstream
// consumer reads a hole. molga_text_icu_raw_install is the always-checked
// wrapper that closes that hole, and this case is the evidence that it does.
// Running the same case under Ninja would prove nothing.
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
