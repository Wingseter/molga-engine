#pragma once

// Helpers shared by the two translation units of test_text_dependencies:
// test_text_dependencies.cpp (the fast provenance cases) and
// text_make_recovery.cpp (the heavy generator-recovery proofs).
//
// The six machine-local provenance macros are attached with
// target_compile_definitions, so they are target-scoped and both translation
// units see them; molga_attach_text_provenance_test stays the single caller
// and test_text_dependencies stays the single target it accepts.
//
// Only what both files need lives here. Anything one file alone uses stays in
// that file, so this header does not slowly become the place every helper
// goes.

#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <spawn.h>
#include <sys/wait.h>

#include "Common/Sha256.h"

extern char** environ;

namespace molga::text_test {

inline std::filesystem::path SourceRoot() {
    return std::filesystem::path(MOLGA_SOURCE_DIR);
}
inline std::filesystem::path BinaryRoot() {
    return std::filesystem::path(MOLGA_BINARY_DIR);
}

// Component-wise containment, never a string-prefix test: /a/bc must not count
// as living under /a/b.
inline bool PathIsUnder(const std::filesystem::path& candidate,
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

inline std::string RequiredSha256File(const std::filesystem::path& path) {
    std::string error;
    const std::string digest = molga::Sha256File(path, &error);
    // Parenthesized: doctest streams the first message operand with operator*,
    // which binds tighter than the string concatenation.
    REQUIRE_MESSAGE(error.empty(), (path.string() + ": " + error));
    REQUIRE(digest.size() == 64);
    REQUIRE(digest.find_first_not_of("0123456789abcdef") == std::string::npos);
    return digest;
}

inline std::string ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) return {};
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

inline void WriteFileBytes(const std::filesystem::path& path,
                           const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

inline int RunWithoutShell(const std::vector<std::string>& argv) {
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

}  // namespace molga::text_test
