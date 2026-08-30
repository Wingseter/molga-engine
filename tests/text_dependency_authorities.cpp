#include "text_dependency_authorities.h"

#include "text_dependency_test_support.h"

#include <algorithm>

namespace molga::text_test {

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

std::string HarfBuzzVerifiedLogLine() {
    // RepairHarfBuzzRawInstall.cmake computes this as the allowlist length plus
    // the two archives, so the same arithmetic here is what keeps the two from
    // drifting when the pinned submodule gains or loses a public header.
    return "nested HarfBuzz raw install verified: " +
           std::to_string(kHarfBuzzInstalledHeaderCount + 2) + " consumed outputs";
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

void RequireHarfBuzzTreeVerified(const std::filesystem::path& tree) {
    RequireNestedCacheMatrix(tree);
    RequireHarfBuzzInstallMatchesAuthorities(tree);
}

// The same rule for ICU: archives from the build tree, headers from the clean
// pinned source. Re-checked after each repair so restoring the deleted file by
// disturbing a sibling still fails.
void RequireIcuRawInstallMatchesAuthorities(const std::filesystem::path& tree) {
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

void RequireTextDependenciesVerified(const std::filesystem::path& tree) {
    // Both halves, not just the one a given case touched. The HarfBuzz cases do
    // not deliberately disturb ICU, but they do rebuild through the same target
    // graph, and a shared tree is only order-independent if what each case
    // hands on is the whole tree rather than its own corner of it.
    RequireIcuRawInstallMatchesAuthorities(tree);
    RequireHarfBuzzTreeVerified(tree);
}

}  // namespace molga::text_test
