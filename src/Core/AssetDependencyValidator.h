#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace molga {

class AssetDatabase;

enum class DependencyIssueCode {
    Missing,
    TypeMismatch,
    UsageMismatch,
    ImportFailed,
    Cycle,
    InvalidDocument
};

struct DependencyIssue {
    DependencyIssueCode code = DependencyIssueCode::Missing;
    std::string owner;
    std::string guid;
    std::string expectedImporter;
    std::string actualImporter;
    std::string message;
};

struct DependencyValidationResult {
    std::vector<DependencyIssue> issues;
    bool Ok() const { return issues.empty(); }
    explicit operator bool() const { return Ok(); }
    std::string Summary() const;
};

class AssetDependencyValidator {
public:
    static DependencyValidationResult ValidateScenes(
        const std::vector<std::filesystem::path>& scenePaths,
        const AssetDatabase& database);
    static DependencyValidationResult ValidateAssetRoots(
        const std::vector<std::string>& rootGuids,
        const AssetDatabase& database);
};

} // namespace molga
