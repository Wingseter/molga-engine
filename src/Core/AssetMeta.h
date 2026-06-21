#pragma once

#include "Core/Guid.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace molga {

// 소스 애셋 옆 "<파일>.meta" sidecar. guid가 애셋의 안정된 정체성이다.
struct AssetMeta {
    std::string guid;
    std::string importer;        // 예: "TextureImporter"
    int importerVersion = 1;

    static std::filesystem::path MetaPathFor(const std::filesystem::path& asset) {
        std::filesystem::path p = asset;
        p += ".meta";
        return p;
    }

    // .meta가 있으면 로드, 없으면 새 guid로 생성·기록한다.
    static AssetMeta CreateOrLoad(const std::filesystem::path& asset,
                                  const std::string& importer, int version) {
        std::filesystem::path metaPath = MetaPathFor(asset);
        AssetMeta m;
        if (std::filesystem::exists(metaPath)) {
            std::ifstream in(metaPath);
            nlohmann::json j;
            try { in >> j; } catch (...) {}
            m.guid = j.value("guid", std::string());
            m.importer = j.value("importer", importer);
            m.importerVersion = j.value("importerVersion", version);
        }
        if (!Guid::IsValid(m.guid)) {
            m.guid = Guid::Generate();
            m.importer = importer;
            m.importerVersion = version;
            Write(asset, m);
        }
        return m;
    }

    static void Write(const std::filesystem::path& asset, const AssetMeta& m) {
        nlohmann::json j;
        j["guid"] = m.guid;
        j["importer"] = m.importer;
        j["importerVersion"] = m.importerVersion;
        std::ofstream out(MetaPathFor(asset));
        out << j.dump(2);
    }
};

} // namespace molga
