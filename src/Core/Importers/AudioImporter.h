#pragma once

#include "Core/Importers/Importer.h"

namespace molga {

class AudioImporter : public IImporter {
public:
    std::string Name() const override { return "AudioImporter"; }
    int Version() const override { return 1; }
    bool CanImport(const std::string& ext) const override {
        return ext == ".wav" || ext == ".mp3" || ext == ".ogg";
    }
    ImportResult Import(const std::string& absSourcePath) const override;
};

} // namespace molga
