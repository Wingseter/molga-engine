#include "Rendering/FontAtlas.h"

#include "Core/AssetDatabase.h"
#include "Rendering/Texture.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>
#include <vector>

namespace molga {
namespace {

struct CacheKey {
    std::string guid;
    int pixelSize = 0;

    bool operator==(const CacheKey& other) const {
        return pixelSize == other.pixelSize && guid == other.guid;
    }
};

struct CacheKeyHash {
    std::size_t operator()(const CacheKey& key) const {
        const std::size_t guidHash = std::hash<std::string>{}(key.guid);
        const std::size_t sizeHash = std::hash<int>{}(key.pixelSize);
        return guidHash ^ (sizeHash + 0x9e3779b9U + (guidHash << 6U) + (guidHash >> 2U));
    }
};

int NormalizePixelSize(int pixelSize) {
    return std::max(1, std::min(pixelSize, 512));
}

bool CanCreateTextures() {
    return GraphicsDevice::Current() != nullptr;
}

struct AtlasPage {
    explicit AtlasPage(int requestedSize)
        : size(requestedSize), pixels(static_cast<std::size_t>(size) *
                                      static_cast<std::size_t>(size) * 4U, 0U) {}

    bool TryPlace(int width, int height, int& outX, int& outY) {
        constexpr int padding = 1;
        if (width + padding * 2 > size || height + padding * 2 > size) {
            return false;
        }
        if (cursorX + width + padding > size) {
            cursorX = padding;
            cursorY += rowHeight + padding;
            rowHeight = 0;
        }
        if (cursorY + height + padding > size) return false;

        outX = cursorX;
        outY = cursorY;
        cursorX += width + padding;
        rowHeight = std::max(rowHeight, height);
        return true;
    }

    void CopyCoverage(int x, int y, const FontGlyphBitmap& bitmap) {
        for (int row = 0; row < bitmap.height; ++row) {
            for (int column = 0; column < bitmap.width; ++column) {
                const std::size_t source = static_cast<std::size_t>(row) *
                                           static_cast<std::size_t>(bitmap.width) +
                                           static_cast<std::size_t>(column);
                const std::size_t destination =
                    (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(size) +
                     static_cast<std::size_t>(x + column)) * 4U;
                pixels[destination + 0U] = 255U;
                pixels[destination + 1U] = 255U;
                pixels[destination + 2U] = 255U;
                pixels[destination + 3U] = bitmap.coverage[source];
            }
        }

        if (texture) {
            std::vector<unsigned char> rgba(
                static_cast<std::size_t>(bitmap.width) *
                static_cast<std::size_t>(bitmap.height) * 4U);
            for (std::size_t index = 0; index < bitmap.coverage.size(); ++index) {
                rgba[index * 4U + 0U] = 255U;
                rgba[index * 4U + 1U] = 255U;
                rgba[index * 4U + 2U] = 255U;
                rgba[index * 4U + 3U] = bitmap.coverage[index];
            }
            texture->UpdateSubData(x, y, bitmap.width, bitmap.height, rgba.data(), 4);
        }
    }

    Texture* EnsureTexture() {
        if (!texture && CanCreateTextures()) {
            texture = std::make_unique<Texture>(size, size, pixels.data(), 4);
        }
        return texture.get();
    }

    int size = 0;
    int cursorX = 1;
    int cursorY = 1;
    int rowHeight = 0;
    std::vector<unsigned char> pixels;
    std::unique_ptr<Texture> texture;
};

struct CachedFontSize {
    FontFace face;
    FontFaceMetrics metrics;
    std::vector<AtlasPage> pages;
    std::unordered_map<std::uint32_t, FontAtlasGlyph> glyphs;
};

} // namespace

struct FontAtlasCache::Impl {
    explicit Impl(int requestedPageSize)
        : pageSize(std::max(16, requestedPageSize)) {}

    CachedFontSize* FindOrLoad(const std::string& guid, int requestedPixelSize) {
        if (guid.empty()) return nullptr;
        const CacheKey key{guid, NormalizePixelSize(requestedPixelSize)};
        auto found = caches.find(key);
        if (found != caches.end()) return found->second.get();

        const std::filesystem::path path = AssetDatabase::Get().AbsoluteSourcePath(guid);
        if (path.empty()) {
            // Cache failures as well: a broken scene must not hit the file
            // system twice per label on every frame. Asset reimport/project
            // scans invalidate the renderer cache before retrying.
            caches.emplace(key, nullptr);
            return nullptr;
        }

        auto cached = std::make_unique<CachedFontSize>();
        if (!cached->face.LoadFromFile(path)) {
            caches.emplace(key, nullptr);
            return nullptr;
        }
        cached->metrics = cached->face.Metrics(static_cast<float>(key.pixelSize));
        CachedFontSize* result = cached.get();
        caches.emplace(key, std::move(cached));
        return result;
    }

    const CachedFontSize* Find(const std::string& guid, int requestedPixelSize) const {
        const CacheKey key{guid, NormalizePixelSize(requestedPixelSize)};
        const auto found = caches.find(key);
        return found == caches.end() ? nullptr : found->second.get();
    }

    int pageSize = FontAtlasCache::kDefaultPageSize;
    std::unordered_map<CacheKey, std::unique_ptr<CachedFontSize>, CacheKeyHash> caches;
};

FontAtlasCache::FontAtlasCache(int pageSize)
    : impl_(std::make_unique<Impl>(pageSize)) {}
FontAtlasCache::~FontAtlasCache() = default;
FontAtlasCache::FontAtlasCache(FontAtlasCache&&) noexcept = default;
FontAtlasCache& FontAtlasCache::operator=(FontAtlasCache&&) noexcept = default;

bool FontAtlasCache::GetMetrics(const std::string& fontGuid, int pixelSize,
                                FontFaceMetrics& outMetrics) {
    CachedFontSize* cached = impl_->FindOrLoad(fontGuid, pixelSize);
    if (!cached) return false;
    outMetrics = cached->metrics;
    return true;
}

bool FontAtlasCache::GetGlyph(const std::string& fontGuid, int pixelSize,
                              std::uint32_t codepoint, FontAtlasGlyph& outGlyph) {
    CachedFontSize* cached = impl_->FindOrLoad(fontGuid, pixelSize);
    if (!cached) return false;

    auto found = cached->glyphs.find(codepoint);
    if (found != cached->glyphs.end()) {
        outGlyph = found->second;
        if (outGlyph.pageIndex >= 0) {
            outGlyph.texture = cached->pages[static_cast<std::size_t>(outGlyph.pageIndex)].EnsureTexture();
            found->second.texture = outGlyph.texture;
        }
        return true;
    }

    const FontGlyphBitmap bitmap = cached->face.Rasterize(
        codepoint, static_cast<float>(NormalizePixelSize(pixelSize)));
    FontAtlasGlyph glyph;
    glyph.width = static_cast<float>(bitmap.width);
    glyph.height = static_cast<float>(bitmap.height);
    glyph.xOffset = static_cast<float>(bitmap.xOffset);
    glyph.yOffset = static_cast<float>(bitmap.yOffset);
    glyph.xAdvance = bitmap.xAdvance;

    if (bitmap.width > 0 && bitmap.height > 0 && !bitmap.coverage.empty()) {
        if (cached->pages.empty()) cached->pages.emplace_back(impl_->pageSize);
        int x = 0;
        int y = 0;
        if (!cached->pages.back().TryPlace(bitmap.width, bitmap.height, x, y)) {
            cached->pages.emplace_back(impl_->pageSize);
            if (!cached->pages.back().TryPlace(bitmap.width, bitmap.height, x, y)) {
                cached->pages.pop_back();
                cached->glyphs.emplace(codepoint, glyph);
                outGlyph = glyph;
                return true;
            }
        }

        AtlasPage& page = cached->pages.back();
        page.CopyCoverage(x, y, bitmap);
        glyph.pageIndex = static_cast<int>(cached->pages.size() - 1U);
        glyph.u0 = static_cast<float>(x) / static_cast<float>(page.size);
        glyph.v0 = static_cast<float>(y) / static_cast<float>(page.size);
        glyph.u1 = static_cast<float>(x + bitmap.width) / static_cast<float>(page.size);
        glyph.v1 = static_cast<float>(y + bitmap.height) / static_cast<float>(page.size);
        glyph.texture = page.EnsureTexture();
        glyph.drawable = true;
    }

    cached->glyphs.emplace(codepoint, glyph);
    outGlyph = glyph;
    return true;
}

float FontAtlasCache::GetKerning(const std::string& fontGuid, int pixelSize,
                                 std::uint32_t left, std::uint32_t right) {
    CachedFontSize* cached = impl_->FindOrLoad(fontGuid, pixelSize);
    return cached ? cached->face.Kerning(
                        left, right, static_cast<float>(NormalizePixelSize(pixelSize)))
                  : 0.0f;
}

void FontAtlasCache::Invalidate(const std::string& fontGuid) {
    for (auto iterator = impl_->caches.begin(); iterator != impl_->caches.end();) {
        if (iterator->first.guid == fontGuid) {
            iterator = impl_->caches.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void FontAtlasCache::Clear() {
    impl_->caches.clear();
}

std::size_t FontAtlasCache::PageCount(const std::string& fontGuid, int pixelSize) const {
    const CachedFontSize* cached = impl_->Find(fontGuid, pixelSize);
    return cached ? cached->pages.size() : 0U;
}

std::size_t FontAtlasCache::GlyphCount(const std::string& fontGuid, int pixelSize) const {
    const CachedFontSize* cached = impl_->Find(fontGuid, pixelSize);
    return cached ? cached->glyphs.size() : 0U;
}

std::size_t FontAtlasCache::CachedFontSizeCount() const {
    return impl_->caches.size();
}

} // namespace molga
