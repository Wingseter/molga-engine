#pragma once

// What the installed text dependencies in a build tree must equal, and where
// those files live.
//
// The rule this file exists to hold in one place: every consumed installed file
// answers to an authority that is not another copy of itself. An archive
// answers to the build-tree archive beside it, one of the 33 verbatim HarfBuzz
// headers and every ICU header to the clean pinned submodule, and the generated
// hb-features.h — which has no source-tree authority at all — to a SHA-256 pin
// stated here rather than taken from the build.
//
// Shared by text_make_recovery.cpp, text_harfbuzz_host_resolution.cpp and
// text_recovery_tree.cpp, which is why it is a translation unit of its own
// rather than a block inside whichever case file happened to need it first.

#include <filesystem>
#include <string>
#include <vector>

namespace molga::text_test {

// The nested ICU install flattens both public header directories into one, and
// the pinned submodule fixes how many files that is; TextDependencies.cmake
// fails the configure if the count ever drifts from this.
inline constexpr int kIcuInstalledHeaderCount = 203;

// hb-features.h is written by the nested configure, so unlike the other 33
// public headers it has no source-tree authority to answer to.
// RepairHarfBuzzRawInstall.cmake pins its bytes independently, and this is the
// same pin stated where the build cannot supply it: a boundary checked only
// against the values the build hands it agrees with any build at all.
inline constexpr char kGeneratedHeaderSha256[] =
    "b9f5b0184edfab48fa3953f3b5fe8f72f72db0c08ebf6ccfc2541451c8bbc597";

inline constexpr int kHarfBuzzInstalledHeaderCount = 34;

std::filesystem::path HbBuildDir(const std::filesystem::path& tree);
std::filesystem::path HbRawDir(const std::filesystem::path& tree);
std::filesystem::path HbGeneratedHeader(const std::filesystem::path& tree);
std::filesystem::path HbInstalledHeader(const std::filesystem::path& tree,
                                        const std::string& name);

// The two build-tree archives, in the order the repair boundary names them.
const std::vector<std::string>& HbArchiveNames();
std::vector<std::string> HbBuildArchiveDigests(const std::filesystem::path& tree);

std::vector<std::string> PinnedNestedCacheLines(const std::filesystem::path& tree);
void RequireNestedCacheMatrix(const std::filesystem::path& tree);
void RequireHarfBuzzInstallMatchesAuthorities(const std::filesystem::path& tree);
void RequireHarfBuzzTreeVerified(const std::filesystem::path& tree);
void RequireIcuRawInstallMatchesAuthorities(const std::filesystem::path& tree);

}  // namespace molga::text_test
