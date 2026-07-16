#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace molga {

struct FontFaceMetrics {
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    float lineHeight = 0.0f;
};

struct FontGlyphBitmap {
    int width = 0;
    int height = 0;
    int xOffset = 0;
    int yOffset = 0;
    float xAdvance = 0.0f;
    std::vector<unsigned char> coverage;
};

// Thin, renderer-independent wrapper around the stb_truetype copy vendored by
// Dear ImGui. The font byte buffer remains owned for stbtt_fontinfo's lifetime.
class FontFace {
public:
    FontFace();
    ~FontFace();

    FontFace(FontFace&&) noexcept;
    FontFace& operator=(FontFace&&) noexcept;

    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    bool LoadFromFile(const std::filesystem::path& path, std::string* error = nullptr);
    bool IsValid() const;

    FontFaceMetrics Metrics(float pixelHeight) const;
    FontGlyphBitmap Rasterize(std::uint32_t codepoint, float pixelHeight) const;
    float Advance(std::uint32_t codepoint, float pixelHeight) const;
    float Kerning(std::uint32_t left, std::uint32_t right, float pixelHeight) const;
    bool HasGlyph(std::uint32_t codepoint) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace molga
