#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace test_support {
namespace fs = std::filesystem;

class TempDirectory {
public:
    explicit TempDirectory(const std::string& prefix) {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = fs::temp_directory_path() /
                ("molga-" + prefix + "-" + std::to_string(stamp));
        fs::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& Path() const { return path_; }

private:
    fs::path path_;
};

inline void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not write " + path.string());
    }
    output << text;
}

}  // namespace test_support
