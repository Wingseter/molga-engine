#include "Core/Importers/TextureImporter.h"
#include <filesystem>
#include "stb_image.h"

namespace molga {

ImportResult TextureImporter::Import(const std::string& absSourcePath) const {
    ImportResult r;
    if (!std::filesystem::exists(absSourcePath)) {
        r.error = "source not found: " + absSourcePath;
        return r;
    }
    int w = 0, h = 0, ch = 0;
    if (stbi_info(absSourcePath.c_str(), &w, &h, &ch) == 1) {
        r.success = true;
        r.width = w;
        r.height = h;
    } else {
        r.error = "stbi_info failed for: " + absSourcePath;
    }
    return r;
}

} // namespace molga
