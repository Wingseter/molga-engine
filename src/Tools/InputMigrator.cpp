#include "Systems/Input.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string UtcTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return stream.str();
}

bool ReadJson(const fs::path& path, nlohmann::json& document,
              std::string& error) {
    std::ifstream input(path);
    if (!input.is_open()) {
        error = "could not open " + path.string();
        return false;
    }
    try {
        input >> document;
        return true;
    } catch (const std::exception& exception) {
        error = "could not parse " + path.string() + ": " + exception.what();
        return false;
    }
}

int Usage() {
    std::cerr << "Usage: molga_migrate input --project <path> [--apply]\n";
    return 2;
}

bool ReplaceFileAtomically(const fs::path& replacement,
                           const fs::path& destination,
                           std::error_code& error) {
#ifdef _WIN32
    // std::filesystem::rename does not replace an existing destination on
    // Windows. ReplaceFileW preserves the all-or-nothing contract and keeps
    // the original file untouched when replacement fails.
    if (ReplaceFileW(destination.c_str(), replacement.c_str(), nullptr,
                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
        error.clear();
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    fs::rename(replacement, destination, error);
    return !error;
#endif
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4 || std::string(argv[1]) != "input") return Usage();

    fs::path projectPath;
    bool apply = false;
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--project" && i + 1 < argc) {
            projectPath = argv[++i];
        } else if (argument == "--apply") {
            apply = true;
        } else {
            return Usage();
        }
    }
    if (projectPath.empty()) return Usage();

    const fs::path inputPath = projectPath / "ProjectSettings" /
                               "input_actions.json";
    nlohmann::json legacy;
    std::string error;
    if (!ReadJson(inputPath, legacy, error)) {
        std::cerr << error << '\n';
        return 3;
    }

    nlohmann::json migrated;
    if (!Input::MigrateLegacyDocument(legacy, migrated, error)) {
        std::cerr << "Input migration failed: " << error << '\n';
        return 4;
    }

    if (legacy == migrated) {
        std::cout << "Input actions already use schema v2; no changes required.\n";
        return 0;
    }

    if (!apply) {
        std::cout << "Dry run: " << inputPath << " can be migrated to schema v2.\n";
        std::cout << migrated.dump(2) << '\n';
        std::cout << "Re-run with --apply to write the migration.\n";
        return 0;
    }

    const fs::path temporary = inputPath.string() + ".tmp";
    const fs::path backup = inputPath.string() + ".bak." + UtcTimestamp();
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output.is_open()) {
            std::cerr << "Could not create temporary file: " << temporary << '\n';
            return 5;
        }
        output << migrated.dump(4) << '\n';
        if (!output.good()) {
            std::cerr << "Could not write temporary file: " << temporary << '\n';
            return 5;
        }
    }

    nlohmann::json verification;
    if (!ReadJson(temporary, verification, error) ||
        !Input::DeserializeActions(verification, &error)) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        std::cerr << "Generated schema verification failed: " << error << '\n';
        return 5;
    }

    std::error_code filesystemError;
    fs::copy_file(inputPath, backup, fs::copy_options::none, filesystemError);
    if (filesystemError) {
        fs::remove(temporary, filesystemError);
        std::cerr << "Could not create backup: " << backup << '\n';
        return 5;
    }

    filesystemError.clear();
    if (!ReplaceFileAtomically(temporary, inputPath, filesystemError)) {
        std::cerr << "Could not atomically replace input file; original and backup are intact: "
                  << filesystemError.message() << '\n';
        return 5;
    }

    std::cout << "Migrated " << inputPath << " to schema v2.\n"
              << "Backup: " << backup << '\n';
    return 0;
}
