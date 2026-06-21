#include "Editor/AssetReferenceScan.h"
#include <fstream>
#include <sstream>

namespace molga {

static bool FileContainsGuid(const std::filesystem::path& p, const std::string& guid) {
    std::ifstream in(p);
    if (!in.is_open()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str().find(guid) != std::string::npos;
}

std::vector<std::filesystem::path> AssetReferenceScan::FindReferencers(
    const std::filesystem::path& assetRoot, const std::string& guid) {
    std::vector<std::filesystem::path> out;
    if (guid.empty() || !std::filesystem::exists(assetRoot)) return out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(assetRoot)) {
        if (!e.is_regular_file()) continue;
        
        // .trash 디렉터리 내부 파일은 건너뜀
        auto rel = std::filesystem::relative(e.path(), assetRoot);
        bool inTrash = false;
        for (const auto& p : rel) {
            if (p == ".trash") {
                inTrash = true;
                break;
            }
        }
        if (inTrash) continue;

        std::string ext = e.path().extension().string();
        if (ext == ".json" || ext == ".prefab" || ext == ".mat" || ext == ".scene") {
            if (FileContainsGuid(e.path(), guid)) out.push_back(e.path());
        }
    }
    return out;
}

} // namespace molga
