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

    // 검증된 불변 바이트 위에서 정확히 하나의 face를 연다. 소유권 지분을
    // 값으로 받아 face 수명 내내 붙들기 때문에, 호출자가 자기 지분을 놓아도
    // stbtt_fontinfo가 가리키는 버퍼는 살아 있다. 리소스 계층은 이 진입점만
    // 쓴다 — 저작 원본 경로에서 face를 여는 일은 없다.
    bool LoadFromBytes(
        std::shared_ptr<const std::vector<std::uint8_t>> bytes,
        std::uint32_t faceIndex, std::string* errorOut = nullptr);
    bool IsValid() const;
    std::uint32_t FaceIndex() const noexcept;

    // cmap 조회용 비-fallback 검사 유틸리티. 0은 .notdef이다. 셰이핑의 face
    // 선택은 이 두 함수가 아니라 임포트된 coverage와 대상 범위로 한정한
    // HarfBuzz probe로 결정한다.
    std::uint32_t GlyphId(char32_t codepoint) const noexcept;
    bool HasCodepoint(char32_t codepoint) const noexcept;

    // 셰이핑된 glyph ID 하나를 그린다. cmap을 거치지 않는 것이 요점이다:
    // HarfBuzz가 고른 glyph는 codepoint 하나로 되돌릴 수 없다(합자, 문맥 이형,
    // 조합된 자모). glyph atlas는 오직 이 진입점만 쓴다.
    //
    // 돌려주는 것은 비트맵 경계/bearing/coverage뿐이고 xAdvance는 0으로 남는다.
    // 논리 advance와 offset은 이미 ShapedGlyph에 들어 있는 HarfBuzz 값이며,
    // 래스터라이저의 근사로 그것을 덮어쓰면 같은 문자열이 폰트마다 다른 자리에
    // 놓인다.
    //
    // rasterScaleKey는 pixelHeight에 곱해지는 26.6 배율이다(64가 1.0). 이
    // 함수가 const인 것은 GlyphAtlasCache::GetGlyph가 const FontFace&를 받기
    // 때문이다 — atlas는 face를 소유하지 않고, face의 글자 모양도 바꾸지
    // 않는다. 다만 완전히 무변이는 아니다: 아래 LastRasterizedGlyphId의
    // 관찰용 기록 하나만은 이 함수가 쓴다(mutable atomic, relaxed).
    FontGlyphBitmap RasterizeGlyph(std::uint32_t glyphId,
                                   std::uint16_t pixelHeight,
                                   std::uint16_t rasterScaleKey) const;

    // 마지막으로 RasterizeGlyph에 넘어온 glyph ID. 0은 "아직 아무것도 묻지
    // 않았다"이다(0 자체는 .notdef의 적법한 요청이므로, 이 값 하나로 "묻지
    // 않았다"를 단정할 수는 없다).
    //
    // 아래 detail의 레거시 계수기와 같은 성격의 seam이고 같은 이유로 출하되는
    // 빌드에 들어 있다. 시험할 가치가 있는 주장 — "atlas가 키가 지목한 face에
    // 키가 지목한 glyph를 물었는가" — 이 프로덕션 경로에 대한 것이므로,
    // 테스트에만 컴파일되는 관찰은 다른 프로그램을 재게 된다. 요청 직전에
    // 기록하므로 유효하지 않은 face나 범위 밖 ID에 대한 요청도 남는다.
    //
    // 레거시 계수기와 달리 face마다 따로 기록한다 — 세고 싶은 것이 "누가
    // 물었는가"라서, 전역 하나로는 어느 face에 물었는지가 사라진다. 그래서
    // FontFaceResource가 shared_ptr<const FontFace>로 공유하는 그 객체 안에
    // 상태가 하나 생기고, 값은 관찰용일 뿐 래스터 결과에 영향을 주지 않지만,
    // 그 하나 때문에 race가 되지 않도록 atomic이다.
    std::uint32_t LastRasterizedGlyphId() const noexcept;

    // 아래 codepoint 기반 헬퍼는 아직 이관되지 않은 레거시 렌더러 전용이며,
    // Task 8.2에서 함께 사라진다. 새 셰이핑/레이아웃 코드는 부르지 않는다:
    // 이들은 셰이핑도 커닝도 fallback도 대신하지 못한다.
    FontFaceMetrics Metrics(float pixelHeight) const;
    FontGlyphBitmap Rasterize(std::uint32_t codepoint, float pixelHeight) const;
    float Advance(std::uint32_t codepoint, float pixelHeight) const;
    float Kerning(std::uint32_t left, std::uint32_t right, float pixelHeight) const;
    bool HasGlyph(std::uint32_t codepoint) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

namespace detail {

// stb_truetype이 셰이핑/커닝/fallback/줄바꿈에 쓰이지 않는다는 주장은 코드를
// 읽어서는 증명되지 않는다 — 새 호출 하나가 조용히 늘어나도 결과는 그럴듯하게
// 나오기 때문이다. 그래서 위 두 레거시 헬퍼가 불린 횟수를 호출 직전에 센다.
//
// UnicodeAnalysis.h의 ICU 계수기와 같은 이유로 출하되는 빌드에 들어 있다.
// 시험할 가치가 있는 주장이 프로덕션 경로에 대한 것이므로, 테스트에만
// 컴파일되는 계수기는 다른 프로그램을 재게 된다. Task 8.2가 두 헬퍼를 지울 때
// 이 계수기도 함께 사라진다.
std::uint64_t LegacyFontFaceMetricCallCount() noexcept;
void          ResetLegacyFontFaceMetricCallCount() noexcept;

} // namespace detail

} // namespace molga
