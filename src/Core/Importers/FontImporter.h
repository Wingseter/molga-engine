#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

class FontImporter : public IImporter {
public:
    std::string Name() const override { return "FontImporter"; }
    int Version() const override { return 2; }
    bool CanImport(const std::string& extension) const override;
    ImportResult Import(const std::string& absoluteSourcePath) const override;
    ImportResult Import(const std::string& absoluteSourcePath,
                        const nlohmann::json& settings) const override;
};

} // namespace molga
