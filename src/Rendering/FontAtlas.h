#pragma once

#include "Rendering/FontFace.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class Texture;

namespace molga {

struct FontAtlasGlyph {
    Texture* texture = nullptr;
    int pageIndex = -1;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    float xAdvance = 0.0f;
    bool drawable = false;
};

// Lazy per-(font GUID, pixel size) glyph atlas. Pages retain their Texture
// object for the cache lifetime so RenderQueue texture pointers stay stable.
class FontAtlasCache {
public:
    static constexpr int kDefaultPageSize = 1024;

    explicit FontAtlasCache(int pageSize = kDefaultPageSize);
    ~FontAtlasCache();

    FontAtlasCache(FontAtlasCache&&) noexcept;
    FontAtlasCache& operator=(FontAtlasCache&&) noexcept;

    FontAtlasCache(const FontAtlasCache&) = delete;
    FontAtlasCache& operator=(const FontAtlasCache&) = delete;

    bool GetMetrics(const std::string& fontGuid, int pixelSize,
                    FontFaceMetrics& outMetrics);
    bool GetGlyph(const std::string& fontGuid, int pixelSize,
                  std::uint32_t codepoint, FontAtlasGlyph& outGlyph);
    float GetKerning(const std::string& fontGuid, int pixelSize,
                     std::uint32_t left, std::uint32_t right);

    void Invalidate(const std::string& fontGuid);
    void Clear();

    std::size_t PageCount(const std::string& fontGuid, int pixelSize) const;
    std::size_t GlyphCount(const std::string& fontGuid, int pixelSize) const;
    std::size_t CachedFontSizeCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace molga
