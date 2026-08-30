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
//
// The two LAUNCHER entries are here because CMake initializes
// CMAKE_<LANG>_COMPILER_LAUNCHER from the environment: a colleague with
// CMAKE_CXX_COMPILER_LAUNCHER=/opt/homebrew/bin/ccache exported lands that
// value verbatim in the nested cache. A compiler launcher is the compiler's
// provenance by any reading, and failing that developer's run for it would be
// this check crying wolf on the first day it met a real machine.
const std::set<std::string>& ToolchainProvenanceCacheEntries() {
    static const std::set<std::string> entries = {
        "CMAKE_ADDR2LINE", "CMAKE_AR", "CMAKE_COMMAND", "CMAKE_CPACK_COMMAND",
        "CMAKE_CTEST_COMMAND", "CMAKE_C_COMPILER", "CMAKE_C_COMPILER_AR",
        "CMAKE_C_COMPILER_LAUNCHER", "CMAKE_C_COMPILER_RANLIB",
        "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_AR",
        "CMAKE_CXX_COMPILER_LAUNCHER", "CMAKE_CXX_COMPILER_RANLIB",
        "CMAKE_DLLTOOL", "CMAKE_EDIT_COMMAND",
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
//
// Quotes group rather than separate. A path containing a space — a sysroot
// under "/Applications/Xcode 16.app", which is an ordinary thing to have —
// arrives quoted in a generated argument line, and splitting on the space would
// leave the tail as a bare relative word that every rule below ignores: a
// silent pass, which is worse than a noisy failure. The residual limit is an
// unquoted space inside a path in a STRING cache value, which nothing here can
// tell from two arguments; the only such value this scans is CMAKE_CXX_FLAGS,
// which this project composes itself.
std::vector<std::string> SplitWords(const std::string& text) {
    static const std::string kDelimiters = " \t\r\n;[]";
    std::vector<std::string> words;
    std::string current;
    char quote = '\0';
    for (const char character : text) {
        if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else {
                current.push_back(character);
            }
        } else if (character == '"' || character == '\'') {
            quote = character;
        } else if (kDelimiters.find(character) != std::string::npos) {
            if (!current.empty()) words.push_back(current);
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    // An unterminated quote is malformed input, not a reason to drop the rest
    // of the line unscanned.
    if (!current.empty()) words.push_back(current);
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

// A word that is itself the sysroot flag with its path glued on. The two
// spellings have to agree with AbsolutePathToken above, which already accepts
// "-isysroot/x" and "--sysroot=/x": treating only the space-separated form as
// provenance would leave the glued form a path token nothing ever exempts, so
// the same SDK would pass written one way and fail written the other.
bool IsGluedSysroot(const std::string& word) {
    return word.rfind("-isysroot/", 0) == 0 || word.rfind("--sysroot=/", 0) == 0;
}

bool IsSysrootFlag(const std::string& word) {
    return word == "-isysroot" || word == "--sysroot";
}

// The five things the plan forbids the nested build from resolving, applied one
// word at a time so a single exempt word — the archiver at the head of a link
// line, say — does not exempt everything beside it.
void CollectHostResolution(const std::string& origin, const std::string& word,
                           bool toolchainProvenance,
                           const std::filesystem::path& tree,
                           std::vector<std::string>& findings) {
    const std::string lowered = ToLower(word);
    // CoreText and FreeType are optional backends pinned OFF, and that is a
    // claim about what resolved rather than about where it lives, so it holds
    // even for a path inside the tree. Their names appear in cache entry names
    // and in upstream's doc comments, neither of which is scanned; a *value*
    // naming either is a backend that resolved.
    for (const char* forbidden : {"coretext", "freetype"}) {
        if (lowered.find(forbidden) != std::string::npos) {
            findings.push_back(origin + ": " + forbidden + " in " + word);
        }
    }

    std::string token;
    const bool absolute = AbsolutePathToken(word, &token);

    // Containment first, and this ordering is the whole point. A word that
    // resolves inside the throwaway tree is this build's own output, wherever
    // the tree happens to live, so Homebrew and /usr/local are forbidden only
    // *outside* it. Without this there is no APPLE gate and no escape: a
    // container that builds under /usr/local/src turns ICU_INCLUDE_DIR,
    // CMAKE_INSTALL_PREFIX and every path in both flags.make files into
    // findings — hundreds of them, on a completely correct build.
    if (absolute && PathIsUnder(token, tree)) return;

    if (!toolchainProvenance) {
        for (const char* forbidden : {"/opt/homebrew", "/usr/local"}) {
            if (lowered.find(forbidden) != std::string::npos) {
                findings.push_back(origin + ": " + forbidden + " in " + word);
            }
        }
    }
    // Reached only for a path outside the tree, by the early return above. A
    // resolved ICU path is expected — HB_HAVE_ICU is ON — but one outside this
    // tree is the system installation the vendoring exists to avoid, which is
    // what "no system ICU" actually means here.
    if (absolute && ToLower(token).find("icu") != std::string::npos) {
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
        const auto colon = typedName.find(':');
        const std::string name = typedName.substr(0, colon);
        const std::string type =
            colon == std::string::npos ? std::string() : typedName.substr(colon + 1);
        const bool provenance = ToolchainProvenanceCacheEntries().count(name) != 0;
        // A PATH or FILEPATH value is exactly one path and CMake does not quote
        // it in the cache, so splitting it would cut an install prefix under
        // "/Applications/Xcode 16.app" in half and hide the tail from every
        // rule. Only the entries that are genuinely word lists — STRING flags,
        // INTERNAL details — get split.
        const std::vector<std::string> words =
            (type == "PATH" || type == "FILEPATH") ? std::vector<std::string>{value}
                                                   : SplitWords(value);
        for (const auto& word : words) {
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
                    // SDK named by -isysroot in either spelling, are the host
                    // toolchain the plan rules legitimate. Everything else on
                    // the line is a dependency the nested configure resolved.
                    const bool provenance =
                        (linkLine && i == 0) || IsGluedSysroot(words[i]) ||
                        (i > 0 && IsSysrootFlag(words[i - 1]));
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
    RequireSharedTreeEntry(tree);

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

    MarkSharedTreeVerified(tree);
}

}  // TEST_SUITE("text-make-recovery")
