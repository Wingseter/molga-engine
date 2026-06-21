#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

class TextureImporter : public IImporter {
public:
    std::string Name() const override { return "TextureImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& ext) const override {
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg";
    }
    ImportResult Import(const std::string& absSourcePath) const override;
};

} // namespace molga
