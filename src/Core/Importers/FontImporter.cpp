#include "Core/Importers/FontImporter.h"

#include "Rendering/FontFace.h"

namespace molga {

ImportResult FontImporter::Import(const std::string& absSourcePath) const {
    ImportResult result;
    FontFace face;
    if (!face.LoadFromFile(absSourcePath, &result.error)) {
        return result;
    }
    result.success = true;
    return result;
}

} // namespace molga
