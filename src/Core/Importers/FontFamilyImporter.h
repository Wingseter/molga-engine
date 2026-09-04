#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

class FontFamilyImporter : public IImporter {
public:
    std::string Name() const override { return "FontFamilyImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& extension) const override;
    ImportResult Import(const std::string& absoluteSourcePath) const override;
};

} // namespace molga
