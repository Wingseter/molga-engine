#include "Core/Importers/PrefabImporter.h"
#include "Core/Guid.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace molga {

std::string PrefabImporter::ReadEmbeddedGuid(const std::string& absSourcePath) {
    std::ifstream in(absSourcePath);
    if (!in.is_open()) return {};
    nlohmann::json j;
    try { in >> j; } catch (...) { return {}; }
    if (!j.is_object()) return {};
    const auto guid = j.find("guid");
    return guid != j.end() && guid->is_string()
        ? guid->get<std::string>() : std::string{};
}

ImportResult PrefabImporter::Import(const std::string& absSourcePath) const {
    ImportResult r;
    if (!std::filesystem::exists(absSourcePath)) {
        r.error = "source not found: " + absSourcePath;
        return r;
    }
    r.success = Guid::IsValid(ReadEmbeddedGuid(absSourcePath));
    if (!r.success) r.error = "prefab has no valid embedded guid";
    return r;
}

} // namespace molga
