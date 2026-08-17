#pragma once

#include <filesystem>
#include <string>

namespace PackageFinalizer {

struct Result {
    bool ok = false;
    std::string error;

    explicit operator bool() const { return ok; }
};

Result FinalizeStagedPackage(const std::filesystem::path& stagingOutput,
                             const std::filesystem::path& finalOutput);

// Deterministic rollback-path coverage across platforms whose directory
// permission and rename semantics differ.
void FailNextStagingRenameForTesting();

}  // namespace PackageFinalizer
