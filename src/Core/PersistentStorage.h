#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// Resolves the per-user writable data directory shared by PlayerPrefs and
// SaveSystem. Production code configures it once after loading game.json;
// tests may install an exact root override.
class PersistentStorage {
public:
    static bool ConfigureRuntime(const std::string& companyName,
                                 const std::string& gameName);
    static bool ConfigureEditor(const std::filesystem::path& projectPath,
                                const std::string& companyName,
                                const std::string& gameName);

    static const std::filesystem::path& Root();
    static bool IsConfigured();

    static bool IsSafePathSegment(const std::string& value);
    static std::uint64_t StableProjectHash(const std::filesystem::path& projectPath);

    // Writes to a sibling temporary file, flushes it, and only then replaces
    // the destination. On failure the existing destination is left intact.
    static bool AtomicWriteText(const std::filesystem::path& destination,
                                const std::string& contents,
                                std::string* errorOut = nullptr);

    // Test support. The override is the exact storage root (company/game are
    // intentionally not appended), making tests independent of the host OS.
    static void SetRootOverrideForTesting(const std::filesystem::path& root);
    static void ClearRootOverrideForTesting();
    static void FailNextAtomicReplaceForTesting();

private:
    static std::filesystem::path PlatformDataRoot();
};
