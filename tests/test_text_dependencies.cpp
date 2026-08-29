#include "doctest.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
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
