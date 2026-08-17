#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

class PostProcessProfileImporter final : public IImporter {
public:
    std::string Name() const override { return "PostProcessProfileImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& extension) const override {
        return extension == ".postfx";
    }
    ImportResult Import(const std::string& absoluteSourcePath) const override;
};

} // namespace molga
