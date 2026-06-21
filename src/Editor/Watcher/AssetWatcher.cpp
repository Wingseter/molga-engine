#include "Editor/Watcher/AssetWatcher.h"

namespace molga {

namespace fs = std::filesystem;

void AssetWatcher::Snapshot(const fs::path& root,
                            std::unordered_map<std::string, long long>& out) {
    out.clear();
    if (!fs::exists(root)) return;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() == ".meta") continue;
        
        // .trash 내부 파일 건너뜀
        auto relPath = fs::relative(e.path(), root);
        bool inTrash = false;
        for (const auto& p : relPath) {
            if (p == ".trash") {
                inTrash = true;
                break;
            }
        }
        if (inTrash) continue;

        std::string rel = relPath.generic_string();
        auto t = fs::last_write_time(e.path()).time_since_epoch().count();
        out[rel] = static_cast<long long>(t);
    }
}

void AssetWatcher::Prime(const fs::path& root) { Snapshot(root, mtimes_); }

AssetWatcher::Changes AssetWatcher::Poll(const fs::path& root) {
    std::unordered_map<std::string, long long> now;
    Snapshot(root, now);
    Changes c;
    for (auto& [rel, t] : now) {
        auto it = mtimes_.find(rel);
        if (it == mtimes_.end()) c.added.push_back(rel);
        else if (it->second != t) c.modified.push_back(rel);
    }
    for (auto& [rel, t] : mtimes_) {
        (void)t;
        if (now.find(rel) == now.end()) c.removed.push_back(rel);
    }
    mtimes_.swap(now);
    return c;
}

} // namespace molga
