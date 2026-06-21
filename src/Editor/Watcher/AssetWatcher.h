#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace molga {

// 폴링 기반 증분 watcher. 전체 재스캔 대신 added/removed/modified만 보고한다.
class AssetWatcher {
public:
    struct Changes {
        std::vector<std::string> added;
        std::vector<std::string> removed;
        std::vector<std::string> modified;
        bool Any() const { return !added.empty() || !removed.empty() || !modified.empty(); }
    };

    void Prime(const std::filesystem::path& root);   // 최초 스냅샷(변화 보고 없음)
    Changes Poll(const std::filesystem::path& root); // 직전 스냅샷과 비교 후 갱신

private:
    std::unordered_map<std::string, long long> mtimes_;  // relPath -> mtime ticks
    static void Snapshot(const std::filesystem::path& root,
                         std::unordered_map<std::string, long long>& out);
};

} // namespace molga
