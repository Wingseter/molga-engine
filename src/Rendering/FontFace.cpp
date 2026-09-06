#include "Rendering/FontFace.h"

#include <algorithm>
#include <atomic>
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
#include "text/rasterizer/imstb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace molga {
namespace {

constexpr std::uintmax_t kMaximumFontBytes = 256U * 1024U * 1024U;

std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]));
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

bool RangeFits(std::size_t offset, std::size_t length, std::size_t size) {
    return offset <= size && length <= size - offset;
}

bool ValidateSfntDirectory(const std::vector<std::uint8_t>& bytes,
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

bool ValidateContainer(const std::vector<std::uint8_t>& bytes,
                       std::uint32_t faceIndex,
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
        if (static_cast<std::size_t>(faceIndex) >= count) {
            error = "font face index is outside the collection";
            return false;
        }
        fontOffset = ReadU32(bytes, 12U + static_cast<std::size_t>(faceIndex) * 4U);
    } else {
        // 컬렉션이 아닌 파일에는 face 0밖에 없다. 여기서 거절하지 않으면
        // stb가 컬렉션 기본 face로 되돌아가 요청하지 않은 face를 열어 준다.
        if (faceIndex != 0U) {
            error = "font face index is outside the file";
            return false;
        }
        fontOffset = 0U;
    }
    return ValidateSfntDirectory(bytes, fontOffset, error);
}

float SafePixelHeight(float pixelHeight) {
    return std::max(1.0f, std::min(pixelHeight, 512.0f));
}

} // namespace

struct FontFace::Impl {
    // 바이트 소유권이 stb 상태보다 먼저 선언된다. 멤버는 선언 역순으로
    // 파괴되므로, stbtt_fontinfo가 가리키던 버퍼가 그 정보보다 먼저 사라지는
    // 순서는 이 배치에서 만들어질 수 없다.
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    stbtt_fontinfo info{};
    std::uint32_t faceIndex = 0;
    // const RasterizeGlyph이 유일하게 쓰는 멤버다. pimpl이 unique_ptr이라 const가
    // 전파되지 않아 mutable 없이도 컴파일되지만, 그러면 "여기만 예외"라는 사실이
    // 선언 어디에도 남지 않는다. atomic인 이유는 이 face가
    // shared_ptr<const FontFace>로 여러 소유자에게 공유되기 때문이다: 관찰용
    // 기록 하나가 그 공유를 data race로 바꾸어서는 안 된다. 순서 보장은 필요
    // 없으므로 relaxed다.
    mutable std::atomic<std::uint32_t> lastRasterizedGlyphId{0};
    // Task 8.2: 같은 이유, 같은 규칙(mutable atomic, relaxed)의 두 번째 관찰
    // 기록. 위와 달리 값이 아니라 횟수를 센다.
    mutable std::atomic<std::uint64_t> rasterizeCalls{0};
    bool valid = false;
};

FontFace::FontFace() : impl_(std::make_unique<Impl>()) {}
FontFace::~FontFace() = default;
FontFace::FontFace(FontFace&&) noexcept = default;
FontFace& FontFace::operator=(FontFace&&) noexcept = default;

bool FontFace::LoadFromBytes(
    std::shared_ptr<const std::vector<std::uint8_t>> bytes,
    std::uint32_t faceIndex, std::string* errorOut) {
    // 두 진입점 모두 impl_를 곧바로 쓴다. move된 FontFace는 impl_가 비어 있고,
    // 그 위에서 적재를 시도하면 접근자들과 달리 널 역참조가 된다.
    if (!impl_) {
        if (errorOut) *errorOut = "font face has no state to load into";
        return false;
    }
    auto fail = [&](const std::string& message) {
        // stb 상태를 먼저 무효화하고 나서 바이트 소유권을 놓는다. 반대 순서면
        // 잠깐이지만 죽은 버퍼를 가리키는 유효한 face가 존재하게 된다.
        impl_->valid = false;
        impl_->info = stbtt_fontinfo{};
        impl_->faceIndex = 0U;
        impl_->lastRasterizedGlyphId.store(0U, std::memory_order_relaxed);
        impl_->bytes.reset();
        if (errorOut) *errorOut = message;
        return false;
    };

    if (!bytes || bytes->empty() || bytes->size() > kMaximumFontBytes) {
        return fail("font bytes are missing or outside the supported size");
    }

    std::size_t checkedOffset = 0U;
    std::string validationError;
    if (!ValidateContainer(*bytes, faceIndex, checkedOffset, validationError)) {
        return fail(validationError);
    }

    const int fontCount = stbtt_GetNumberOfFonts(bytes->data());
    const int stbOffset = stbtt_GetFontOffsetForIndex(
        bytes->data(), static_cast<int>(faceIndex));
    if (fontCount < 1 ||
        static_cast<std::uint32_t>(fontCount) <= faceIndex || stbOffset < 0 ||
        static_cast<std::size_t>(stbOffset) != checkedOffset) {
        return fail("stb_truetype rejected the requested font face");
    }

    // 성공 경로도 실패 경로와 같은 순서를 지킨다: stb 상태를 먼저 무효화한
    // 다음에야 이전 바이트 지분을 놓는다. 이미 적재된 face를 다시 적재하면서
    // 순서를 뒤집으면, 옛 버퍼가 여기서 해제되는데 impl_->info는 다음 줄까지
    // 여전히 그 버퍼를 가리키므로 valid한 face가 죽은 메모리를 가리키게 된다.
    impl_->valid = false;
    impl_->info = stbtt_fontinfo{};
    impl_->faceIndex = 0U;
    impl_->lastRasterizedGlyphId.store(0U, std::memory_order_relaxed);
    // 그다음 소유권을 세우고 나서 stbtt_fontinfo를 그 버퍼 위에 만든다. 성공한
    // face가 게시되는 순간에는 이미 지분을 들고 있어야 한다.
    impl_->bytes = std::move(bytes);
    if (!stbtt_InitFont(&impl_->info, impl_->bytes->data(), stbOffset)) {
        return fail("stb_truetype rejected the font");
    }

    impl_->faceIndex = faceIndex;
    impl_->valid = true;
    if (errorOut) errorOut->clear();
    return true;
}

bool FontFace::LoadFromFile(const std::filesystem::path& path, std::string* error) {
    if (!impl_) {
        if (error) *error = "font face has no state to load into";
        return false;
    }
    auto fail = [&](const std::string& message) {
        impl_->valid = false;
        impl_->info = stbtt_fontinfo{};
        impl_->faceIndex = 0U;
        impl_->lastRasterizedGlyphId.store(0U, std::memory_order_relaxed);
        impl_->bytes.reset();
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
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(raw.data()),
               static_cast<std::streamsize>(raw.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != raw.size()) {
        return fail("could not read the complete font source: " + path.string());
    }

    // 파일 경로 진입점도 결국 같은 불변 바이트 위에서 face를 연다. 검증과
    // 소유권 규칙이 한 곳에만 있어야 두 경로가 갈라지지 않는다.
    std::string loadError;
    if (!LoadFromBytes(
            std::make_shared<const std::vector<std::uint8_t>>(std::move(raw)),
            0U, &loadError)) {
        return fail(loadError + ": " + path.string());
    }
    if (error) error->clear();
    return true;
}

bool FontFace::IsValid() const {
    return impl_ && impl_->valid;
}

std::uint32_t FontFace::FaceIndex() const noexcept {
    return IsValid() ? impl_->faceIndex : 0U;
}

std::uint32_t FontFace::GlyphId(char32_t codepoint) const noexcept {
    if (!IsValid() || static_cast<std::uint32_t>(codepoint) > 0x10FFFFU) {
        return 0U;
    }
    const int glyph =
        stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(codepoint));
    return glyph > 0 ? static_cast<std::uint32_t>(glyph) : 0U;
}

bool FontFace::HasCodepoint(char32_t codepoint) const noexcept {
    return GlyphId(codepoint) != 0U;
}

FontGlyphBitmap FontFace::RasterizeGlyph(std::uint32_t glyphId,
                                         std::uint16_t pixelHeight,
                                         std::uint16_t rasterScaleKey) const {
    FontGlyphBitmap result;
    if (!impl_) return result;
    // ── const가 실제로 const가 되게 한다 (Task 6.2) ──────────────────────────
    // pimpl이 unique_ptr이라 이 함수의 const는 Impl로 전파되지 않는다:
    // `impl_->`로는 무엇이든 쓸 수 있고, 그러면 이 함수가 face를 바꾸지
    // 않는다는 것은 주석의 주장일 뿐이다. const 참조 하나를 끼워 넣어 그
    // 주장을 컴파일러의 것으로 만든다 — 아래에서 lastRasterizedGlyphId
    // (mutable atomic) 외의 멤버에 쓰면 컴파일되지 않는다.
    const Impl& state = *impl_;
    // 물어본 사실을 먼저 남긴다. 성공한 요청만 세면 "잘못된 face에 물어서
    // 빈 비트맵이 나왔다"는 가장 위험한 경우가 흔적 없이 사라진다.
    state.lastRasterizedGlyphId.store(glyphId, std::memory_order_relaxed);
    state.rasterizeCalls.fetch_add(1U, std::memory_order_relaxed);
    if (!IsValid()) return result;
    // 범위 밖 glyph ID는 여기서 닫는다. stb도 loca 경계를 검사하지만, 그
    // 방어가 우리 것이 아니면 벤더 사본을 갱신하는 날 조용히 사라진다.
    if (state.info.numGlyphs <= 0 ||
        glyphId >= static_cast<std::uint32_t>(state.info.numGlyphs)) {
        return result;
    }

    // 26.6 배율. 곱은 uint64로 한다: 65535 * 65535는 uint32의 상한에 붙어
    // 있어서, 나중에 한 자리만 넓혀도 조용히 넘친다.
    const float requestedHeight =
        static_cast<float>(static_cast<std::uint64_t>(pixelHeight) *
                           static_cast<std::uint64_t>(rasterScaleKey)) /
        64.0f;
    const float scale =
        stbtt_ScaleForPixelHeight(&state.info, SafePixelHeight(requestedHeight));

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphBitmapBox(&state.info, static_cast<int>(glyphId),
                            scale, scale, &x0, &y0, &x1, &y1);
    result.width = std::max(0, x1 - x0);
    result.height = std::max(0, y1 - y0);
    result.xOffset = x0;
    result.yOffset = y0;
    if (result.width == 0 || result.height == 0) return result;

    result.coverage.resize(static_cast<std::size_t>(result.width) *
                           static_cast<std::size_t>(result.height));
    stbtt_MakeGlyphBitmap(&state.info, result.coverage.data(),
                          result.width, result.height, result.width,
                          scale, scale, static_cast<int>(glyphId));
    return result;
}

std::uint32_t FontFace::LastRasterizedGlyphId() const noexcept {
    return impl_ ? impl_->lastRasterizedGlyphId.load(std::memory_order_relaxed)
                 : 0U;
}

std::uint64_t FontFace::RasterizeCallCountForTest() const noexcept {
    return impl_ ? impl_->rasterizeCalls.load(std::memory_order_relaxed) : 0U;
}

} // namespace molga
