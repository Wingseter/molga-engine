#pragma once

#include "Rendering/SpriteRef.h"

namespace molga {

class AssetDatabase;

class SpriteResolver {
public:
    // Resolves slice geometry without allocating or touching a GPU texture.
    // This is also useful to author native-size/pivot data in headless tools.
    static ResolvedSprite ResolveMetadata(const SpriteRef& reference,
                                          const AssetDatabase& database);
    static ResolvedSprite Resolve(const SpriteRef& reference,
                                  AssetDatabase& database);
    static ResolvedSprite Resolve(const SpriteRef& reference);
};

} // namespace molga
