#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

class FontImporter : public IImporter {
public:
    std::string Name() const override { return "FontImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& ext) const override {
        return ext == ".ttf" || ext == ".otf";
    }
    ImportResult Import(const std::string& absSourcePath) const override;
};

} // namespace molga
