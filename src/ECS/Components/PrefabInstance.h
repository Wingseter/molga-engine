#pragma once

#include "../Component.h"
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

class PrefabInstance : public Component {
public:
    COMPONENT_TYPE(PrefabInstance)

    PrefabInstance() = default;
    PrefabInstance(const std::string& guid) : prefabGuid(guid) {}

    const std::string& GetPrefabGuid() const { return prefabGuid; }
    void SetPrefabGuid(const std::string& guid) { prefabGuid = guid; }

    nlohmann::json& GetModifications() { return modifications; }
    const nlohmann::json& GetModifications() const { return modifications; }
    void SetModifications(const nlohmann::json& mods) { modifications = mods; }

    std::unordered_map<unsigned int, unsigned int>& GetIdRemap() { return idRemap; }
    const std::unordered_map<unsigned int, unsigned int>& GetIdRemap() const { return idRemap; }
    void SetIdRemap(const std::unordered_map<unsigned int, unsigned int>& remap) { idRemap = remap; }

    // Serialization
    void Serialize(nlohmann::json& j) const override {
        j["prefabGuid"] = prefabGuid;
        j["modifications"] = modifications;
    }

    void Deserialize(const nlohmann::json& j) override {
        if (j.contains("prefabGuid")) prefabGuid = j["prefabGuid"].get<std::string>();
        if (j.contains("modifications")) modifications = j["modifications"];
    }

private:
    std::string prefabGuid;
    nlohmann::json modifications = nlohmann::json::array();
    
    // Transient mapping from prefab local ID to scene runtime ID
    std::unordered_map<unsigned int, unsigned int> idRemap;
};
