#include "Rendering/FontFace.h"

#include <algorithm>
#include <fstream>
#include <limits>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "imstb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace molga {
namespace {

constexpr std::uintmax_t kMaximumFontBytes = 256U * 1024U * 1024U;

std::uint16_t ReadU16(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::uint32_t ReadU32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

bool RangeFits(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

bool ValidateSfntDirectory(const std::vector<unsigned char>& bytes,
                           std::size_t offset,
                           std::string& error) {
    if (!RangeFits(offset, 12U, bytes.size())) {
        error = "font header is truncated";
        return false;
    }

    const std::uint32_t signature = ReadU32(bytes, offset);
    const bool supportedSignature =
        signature == 0x00010000U || // TrueType outlines
        signature == 0x4F54544FU || // OTTO (OpenType/CFF)
        signature == 0x74727565U || // true
        signature == 0x74797031U;   // typ1
    if (!supportedSignature) {
        error = "unsupported TTF/OTF signature";
        return false;
    }

    const std::size_t tableCount = ReadU16(bytes, offset + 4U);
    if (tableCount == 0U || tableCount > 4096U ||
        !RangeFits(offset + 12U, tableCount * 16U, bytes.size())) {
        error = "font table directory is invalid";
        return false;
    }

    for (std::size_t index = 0; index < tableCount; ++index) {
        const std::size_t record = offset + 12U + index * 16U;
        const std::size_t tableOffset = ReadU32(bytes, record + 8U);
        const std::size_t tableLength = ReadU32(bytes, record + 12U);
        if (!RangeFits(tableOffset, tableLength, bytes.size())) {
            error = "font table extends beyond the file";
            return false;
        }
    }
    return true;
}

bool ValidateContainer(const std::vector<unsigned char>& bytes,
                       std::size_t& fontOffset,
                       std::string& error) {
    if (bytes.size() < 12U) {
        error = "font file is too small";
        return false;
    }

    if (ReadU32(bytes, 0U) == 0x74746366U) { // ttcf
        const std::size_t count = ReadU32(bytes, 8U);
        if (count == 0U || count > 4096U ||
            !RangeFits(12U, count * 4U, bytes.size())) {
            error = "TrueType collection header is invalid";
            return false;
        }
        fontOffset = ReadU32(bytes, 12U);
    } else {
        fontOffset = 0U;
    }
    return ValidateSfntDirectory(bytes, fontOffset, error);
}

float SafePixelHeight(float pixelHeight) {
    return std::max(1.0f, std::min(pixelHeight, 512.0f));
}

} // namespace

struct FontFace::Impl {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool valid = false;
};

FontFace::FontFace() : impl_(std::make_unique<Impl>()) {}
FontFace::~FontFace() = default;
FontFace::FontFace(FontFace&&) noexcept = default;
FontFace& FontFace::operator=(FontFace&&) noexcept = default;

bool FontFace::LoadFromFile(const std::filesystem::path& path, std::string* error) {
    auto fail = [&](const std::string& message) {
        impl_->bytes.clear();
        impl_->valid = false;
        if (error) *error = message;
        return false;
    };

    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError)) {
        return fail("font source not found: " + path.string());
    }
    const std::uintmax_t size = std::filesystem::file_size(path, filesystemError);
    if (filesystemError || size == 0U || size > kMaximumFontBytes ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return fail("font source has an invalid size: " + path.string());
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail("could not open font source: " + path.string());
    }
    impl_->bytes.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(impl_->bytes.data()),
               static_cast<std::streamsize>(impl_->bytes.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != impl_->bytes.size()) {
        return fail("could not read the complete font source: " + path.string());
    }

    std::size_t checkedOffset = 0U;
    std::string validationError;
    if (!ValidateContainer(impl_->bytes, checkedOffset, validationError)) {
        return fail(validationError + ": " + path.string());
    }

    const int fontCount = stbtt_GetNumberOfFonts(impl_->bytes.data());
    const int stbOffset = stbtt_GetFontOffsetForIndex(impl_->bytes.data(), 0);
    if (fontCount < 1 || stbOffset < 0 ||
        static_cast<std::size_t>(stbOffset) != checkedOffset ||
        !stbtt_InitFont(&impl_->info, impl_->bytes.data(), stbOffset)) {
        return fail("stb_truetype rejected the font: " + path.string());
    }

    impl_->valid = true;
    if (error) error->clear();
    return true;
}

bool FontFace::IsValid() const {
    return impl_ && impl_->valid;
}

FontFaceMetrics FontFace::Metrics(float pixelHeight) const {
    FontFaceMetrics result;
    if (!IsValid()) return result;

    const float scale = stbtt_ScaleForPixelHeight(&impl_->info, SafePixelHeight(pixelHeight));
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&impl_->info, &ascent, &descent, &lineGap);
    result.ascent = static_cast<float>(ascent) * scale;
    result.descent = static_cast<float>(descent) * scale;
    result.lineGap = static_cast<float>(lineGap) * scale;
    result.lineHeight = static_cast<float>(ascent - descent + lineGap) * scale;
    return result;
}

FontGlyphBitmap FontFace::Rasterize(std::uint32_t codepoint, float pixelHeight) const {
    FontGlyphBitmap result;
    if (!IsValid() || codepoint > 0x10FFFFU) return result;

    const float scale = stbtt_ScaleForPixelHeight(&impl_->info, SafePixelHeight(pixelHeight));
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&impl_->info, static_cast<int>(codepoint), &advance, &bearing);
    result.xAdvance = static_cast<float>(advance) * scale;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&impl_->info, static_cast<int>(codepoint),
                                scale, scale, &x0, &y0, &x1, &y1);
    result.width = std::max(0, x1 - x0);
    result.height = std::max(0, y1 - y0);
    result.xOffset = x0;
    result.yOffset = y0;
    if (result.width == 0 || result.height == 0) return result;

    result.coverage.resize(static_cast<std::size_t>(result.width) *
                           static_cast<std::size_t>(result.height));
    stbtt_MakeCodepointBitmap(&impl_->info, result.coverage.data(),
                              result.width, result.height, result.width,
                              scale, scale, static_cast<int>(codepoint));
    return result;
}

float FontFace::Advance(std::uint32_t codepoint, float pixelHeight) const {
    if (!IsValid() || codepoint > 0x10FFFFU) return 0.0f;
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&impl_->info, static_cast<int>(codepoint), &advance, &bearing);
    const float scale = stbtt_ScaleForPixelHeight(&impl_->info, SafePixelHeight(pixelHeight));
    return static_cast<float>(advance) * scale;
}

float FontFace::Kerning(std::uint32_t left, std::uint32_t right, float pixelHeight) const {
    if (!IsValid() || left > 0x10FFFFU || right > 0x10FFFFU) return 0.0f;
    const float scale = stbtt_ScaleForPixelHeight(&impl_->info, SafePixelHeight(pixelHeight));
    return static_cast<float>(stbtt_GetCodepointKernAdvance(
        &impl_->info, static_cast<int>(left), static_cast<int>(right))) * scale;
}

bool FontFace::HasGlyph(std::uint32_t codepoint) const {
    return IsValid() && codepoint <= 0x10FFFFU &&
           stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(codepoint)) != 0;
}

} // namespace molga
