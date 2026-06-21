#include "Core/Importers/PrefabImporter.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace molga {

std::string PrefabImporter::ReadEmbeddedGuid(const std::string& absSourcePath) {
    std::ifstream in(absSourcePath);
    if (!in.is_open()) return {};
    nlohmann::json j;
    try { in >> j; } catch (...) { return {}; }
    return j.value("guid", std::string());
}

ImportResult PrefabImporter::Import(const std::string& absSourcePath) const {
    ImportResult r;
    if (!std::filesystem::exists(absSourcePath)) {
        r.error = "source not found: " + absSourcePath;
        return r;
    }
    r.success = !ReadEmbeddedGuid(absSourcePath).empty();
    if (!r.success) r.error = "prefab has no embedded guid";
    return r;
}

} // namespace molga
