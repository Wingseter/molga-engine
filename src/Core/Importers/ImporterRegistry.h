#pragma once

#include "Core/Importers/Importer.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace molga {

class ImporterRegistry {
public:
    static ImporterRegistry& Get();

    void Register(std::shared_ptr<IImporter> importer);
    const IImporter* FindByName(const std::string& name) const;
    const IImporter* FindForExtension(const std::string& extension) const;
    ImportResult Import(const std::string& importerName,
                        const std::string& absoluteSourcePath,
                        const nlohmann::json& settings = nlohmann::json::object()) const;
    void RegisterBuiltins();
    void ClearForTesting();

private:
    std::unordered_map<std::string, std::shared_ptr<IImporter>> byName_;
    std::vector<std::shared_ptr<IImporter>> ordered_;
    bool builtinsRegistered_ = false;
};

} // namespace molga
