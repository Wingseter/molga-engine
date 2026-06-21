#include "Core/Importers/AudioImporter.h"
#include <filesystem>

namespace molga {

ImportResult AudioImporter::Import(const std::string& absSourcePath) const {
    ImportResult r;
    if (!std::filesystem::exists(absSourcePath)) {
        r.error = "source not found: " + absSourcePath;
        return r;
    }
    r.success = true;   // 디코드는 런타임 miniaudio가 담당; 임포트는 존재 검증만
    return r;
}

} // namespace molga
