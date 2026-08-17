#include "Core/Importers/PostProcessProfileImporter.h"

#include "Rendering/PostProcessProfile2D.h"

namespace molga {

ImportResult PostProcessProfileImporter::Import(
    const std::string& absoluteSourcePath) const {
    ImportResult result;
    PostProcessProfile2D profile;
    if (!PostProcessProfile2D::LoadFromFile(
            absoluteSourcePath, profile, &result.error)) {
        return result;
    }
    result.success = true;
    result.metadata = {
        {"schemaVersion", PostProcessProfile2D::kSchemaVersion},
        {"effectCount", profile.effects.size()},
        {"activeEffectCount", profile.ActiveEffectCount()}};
    return result;
}

} // namespace molga
