#pragma once

#include "Core/Guid.h"
#include "Core/PersistentStorage.h"
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
    nlohmann::json settings = nlohmann::json::object();
    // The complete sidecar is retained so a newer/third-party importer can add
    // fields without an older editor deleting them on its next write.
    nlohmann::json preserved = nlohmann::json::object();

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
            nlohmann::json j = nlohmann::json::object();
            try {
                in >> j;
                if (!j.is_object()) j = nlohmann::json::object();
            } catch (...) {
                j = nlohmann::json::object();
            }
            m.preserved = j;
            const auto guid = j.find("guid");
            if (guid != j.end() && guid->is_string()) m.guid = guid->get<std::string>();
            const auto importerValue = j.find("importer");
            m.importer = importerValue != j.end() && importerValue->is_string()
                ? importerValue->get<std::string>() : importer;
            const auto importerVersion = j.find("importerVersion");
            if (importerVersion != j.end() && importerVersion->is_number_integer()) {
                try {
                    m.importerVersion = importerVersion->get<int>();
                } catch (...) {
                    m.importerVersion = version;
                }
            } else {
                m.importerVersion = version;
            }
            if (m.importerVersion <= 0) m.importerVersion = version;
            const auto settings = j.find("settings");
            if (settings != j.end() && settings->is_object()) {
                m.settings = *settings;
            }
        }
        if (!Guid::IsValid(m.guid)) {
            m.guid = Guid::Generate();
            m.importer = importer;
            m.importerVersion = version;
            Write(asset, m);
        }
        return m;
    }

    static bool Write(const std::filesystem::path& asset, const AssetMeta& m) {
        nlohmann::json j = m.preserved.is_object()
            ? m.preserved : nlohmann::json::object();
        j["guid"] = m.guid;
        j["importer"] = m.importer;
        j["importerVersion"] = m.importerVersion;
        j["settings"] = m.settings.is_object() ? m.settings : nlohmann::json::object();
        return PersistentStorage::AtomicWriteText(MetaPathFor(asset), j.dump(2));
    }
};

} // namespace molga
