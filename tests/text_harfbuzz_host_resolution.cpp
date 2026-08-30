// Proof that the nested HarfBuzz configure resolved no host installation.
//
// Deliberately not a recovery proof: it deletes nothing, corrupts nothing, and
// would hold identically under Ninja. It reads what the nested configure
// actually decided — its cache, and the compile and link arguments it generated
// for both archive targets — rather than what the build was asked for. It runs
// against the shared recovery tree only because that tree is already
// configured, not because the generator matters to what it asserts.

#include "text_dependency_authorities.h"
#include "text_dependency_test_support.h"
#include "text_recovery_tree.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace molga::text_test;

namespace {

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

// Declared into the same suite as the recovery proofs because it shares their
// tree and therefore their cost; the ctest entry selects the suite by name.
TEST_SUITE("text-make-recovery") {

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
