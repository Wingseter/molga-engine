#include "Core/SpriteResolver.h"

#include "Core/AssetDatabase.h"
#include "Core/TextureImportSettings.h"
#include "Core/TextureManager.h"
#include "Rendering/Texture.h"

#include <algorithm>

namespace molga {

ResolvedSprite SpriteResolver::ResolveMetadata(const SpriteRef& reference,
                                               const AssetDatabase& database) {
    ResolvedSprite result;
    const AssetRecord* record = database.Find(reference.textureGuid);
    if (!record || record->importer != "TextureImporter" || record->importFailed ||
        record->textureWidth <= 0 || record->textureHeight <= 0) {
        return result;
    }

    TextureImportSettings settings =
        DeserializeTextureImportSettings(record->settings, true);
    Rect pixels{0, 0, record->textureWidth, record->textureHeight};
    Vector2 pivot = settings.defaultPivot;
    if (!reference.sliceId.empty()) {
        const auto slice = std::find_if(settings.slices.begin(), settings.slices.end(),
            [&](const SpriteSlice& value) { return value.id == reference.sliceId; });
        if (slice == settings.slices.end() || !slice->IsValid()) return result;
        pixels = slice->pixelRect;
        pivot = slice->pivot;
    } else if (settings.spriteMode == SpriteImportMode::Multiple) {
        // Multiple-mode references are intentionally stable-ID based. An empty
        // ID must not silently switch to a different slice after re-slicing.
        return result;
    }

    if (pixels.x < 0 || pixels.y < 0 || pixels.Right() > record->textureWidth ||
        pixels.Bottom() > record->textureHeight || pixels.width <= 0 || pixels.height <= 0) {
        return result;
    }

    const float width = static_cast<float>(record->textureWidth);
    const float height = static_cast<float>(record->textureHeight);
    result.uv.u0 = static_cast<float>(pixels.x) / width;
    result.uv.u1 = static_cast<float>(pixels.x + pixels.width) / width;
    // Slice rectangles use source-image top-left coordinates. Texture loading
    // keeps the legacy vertical flip, so top-left pixels map to high V values.
    result.uv.v0 = 1.0f - static_cast<float>(pixels.y + pixels.height) / height;
    result.uv.v1 = 1.0f - static_cast<float>(pixels.y) / height;
    result.pivot = pivot;
    result.pixelRect = pixels;
    result.nativeSize = {static_cast<float>(pixels.width) / settings.pixelsPerUnit,
                         static_cast<float>(pixels.height) / settings.pixelsPerUnit};
    result.valid = true;
    return result;
}

ResolvedSprite SpriteResolver::Resolve(const SpriteRef& reference,
                                       AssetDatabase& database) {
    ResolvedSprite result = ResolveMetadata(reference, database);
    if (!result.valid) return result;

    const AssetRecord* record = database.Find(reference.textureGuid);
    if (!record) {
        result.valid = false;
        return result;
    }
    const TextureImportSettings settings =
        DeserializeTextureImportSettings(record->settings, true);
    const std::filesystem::path source = database.AbsoluteSourcePath(reference.textureGuid);
    if (source.empty()) {
        result.valid = false;
        return result;
    }
    result.texture = TextureManager::Get().LoadWithSettings(source.string(), settings,
                                                            "SpriteResolver");
    if (!result.texture) result.valid = false;
    return result;
}

ResolvedSprite SpriteResolver::Resolve(const SpriteRef& reference) {
    return Resolve(reference, AssetDatabase::Get());
}

} // namespace molga
