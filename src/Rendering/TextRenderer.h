#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include "../Common/Types.h"
#include "Rendering/FontAtlas.h"

class Shader;
class Renderer;
class Texture;
namespace molga { class RenderQueue; }

enum class TextHorizontalAlignment {
    Left,
    Center,
    Right
};

struct TextMetrics {
    float width = 0.0f;
    float height = 0.0f;
    float lineHeight = 0.0f;
    std::size_t lineCount = 1;
};

struct TextDrawParams {
    std::string text;
    std::string fontGuid;
    float x = 0.0f;
    float y = 0.0f;
    float fontSizePx = 16.0f;
    float scale = 1.0f;       // legacy/source-compatible multiplier
    float lineSpacing = 1.2f;
    Color color = Color::White();
    TextHorizontalAlignment alignment = TextHorizontalAlignment::Left;
    int cameraPass = 0;
    int sortingLayer = 0;
    int sortingOrder = 0;
    float depthOrYSort = 0.0f;
};

// Character info for bitmap font
struct CharInfo {
    float u0, v0, u1, v1;  // UV coordinates in texture
    float width, height;    // Size of character
    float xOffset, yOffset; // Offset from cursor position
    float xAdvance;         // How much to advance cursor after this char
};

// Simple bitmap font text renderer
class TextRenderer {
public:
    class GlyphCollectionScope;

    TextRenderer();
    ~TextRenderer();

    // Initialize with built-in font
    bool Init();

    // Shutdown and cleanup
    void Shutdown();

    // Render text at position
    void RenderText(Renderer* renderer, Shader* shader,
                    const std::string& text, float x, float y,
                    float scale = 1.0f, const Color& color = Color::White());

    // Get text dimensions
    float GetTextWidth(const std::string& text, float scale = 1.0f) const;
    float GetTextHeight(float scale = 1.0f) const;

    // UTF-8/codepoint-aware measurement and batched RenderQueue submission.
    // A missing/unreadable font GUID falls back to the built-in ASCII font.
    TextMetrics MeasureText(const std::string& text,
                            const std::string& fontGuid,
                            float fontSizePx,
                            float scale = 1.0f,
                            float requestedLineSpacing = 1.2f);
    void CollectText(molga::RenderQueue& queue, const TextDrawParams& params);

    // ── Task 6.3: 한 프레임의 glyph 수집 ────────────────────────────────────
    // 프레임 번호가 정해진 다음, 월드/UI 텍스트를 큐에 담기 전에 연다. 이
    // scope가 살아 있는 동안 atlas가 내주는 page는 현재 수집의 pin을 받아
    // 축출되지 않고, scope가 닫히는 순간 밖으로 나간 page는 쓰기 봉인된다.
    // 그러므로 어휘적 범위는 모든 이른 return을 지나 "모든 명령이 page 토큰을
    // 복사한 뒤"까지 덮어야 한다.
    //
    // 중첩은 std::logic_error다. 두 번째 BeginFrame은 첫 수집의 pin 집합을
    // 통째로 지우므로, 이미 큐에 들어간 명령이 가리키는 page가 그 자리에서
    // 축출 대상이 된다 — 중첩을 조용히 무시하면 그 손실이 진단 없이 화면에만
    // 나타난다.
    GlyphCollectionScope BeginGlyphCollection(std::uint64_t frameIndex);

    // 이 renderer가 소유한 단 하나의 glyph 캐시. 두 접근자는 같은 객체를
    // 돌려주며, 프로세스 전역의 두 번째 캐시를 만들지 않는다.
    //
    // 가변 접근자는 시작 시 예산 설정과 초점 픽스처의 것이다. 보통의 렌더링은
    // CollectLayout을 통해서만 atlas에 닿는다. 자격/성능 쪽은 const 쪽으로
    // 텔레메트리만 읽는다.
    molga::GlyphAtlasCache& GlyphAtlas() noexcept;
    const molga::GlyphAtlasCache& GlyphAtlas() const noexcept;

    void InvalidateFont(const std::string& fontGuid);
    void InvalidateAllFonts();
    std::size_t GetAtlasPageCount(const std::string& fontGuid, int pixelSize) const;
    std::size_t GetCachedFontSizeCount() const;

    // Set line height multiplier
    void SetLineSpacing(float spacing) { lineSpacing = spacing; }

    // Singleton access
    static TextRenderer& Get();

private:
    void GenerateBuiltinFont();

    std::unique_ptr<Texture> fontTexture;
    std::unordered_map<char, CharInfo> characters;
    molga::FontAtlasCache fontAtlas;
    // Task 6.3: 이 renderer가 소유하는 단 하나의 glyph-ID atlas. 위
    // fontAtlas는 Task 8.2가 지우는 레거시 codepoint 어댑터이고, 그때까지
    // 둘은 서로를 호출하지도 채우지도 않는다.
    molga::GlyphAtlasCache atlas_;
    bool glyphCollectionActive_ = false;
    float lineHeight = 16.0f;
    float lineSpacing = 1.2f;
    bool initialized = false;
};

// 한 프레임의 glyph 수집을 여는 RAII 손잡이. 옮길 수 있고 복사할 수 없다:
// 지분이 둘로 늘어나는 순간 EndCollection이 두 번 불리고, 그 두 번째가 *다음*
// 수집의 pin을 지운다.
class TextRenderer::GlyphCollectionScope {
public:
    GlyphCollectionScope(GlyphCollectionScope&&) noexcept;
    ~GlyphCollectionScope();
    GlyphCollectionScope(const GlyphCollectionScope&) = delete;
    GlyphCollectionScope& operator=(const GlyphCollectionScope&) = delete;
private:
    friend class TextRenderer;
    GlyphCollectionScope(TextRenderer&, std::uint64_t);
    TextRenderer* owner_ = nullptr;
    std::uint64_t frameIndex_ = 0;
};

// ── Task 6.3: 프로덕션 GPU teardown 순서는 여기 한 곳에만 있다 ──────────────
// 제출된 명령이 아직 읽고 있을지 모르는 텍스처를 부수지 않으려면 순서가
// 하나뿐이다: renderer가 GPU idle을 증명하고(Task 6.2 Step 8a) 반납 큐를
// 비운 다음에(Step 8b) 텍스트/atlas GPU 자원을 부수고, GraphicsDevice는 그
// 뒤에 죽는다.
//
// 두 진입점(src/main.cpp, src/runtime_main.cpp)이 이 함수만 부른다. 순서를
// 각자 적어 두면 한쪽만 되돌아가도 아무도 알아채지 못하지만, 여기 한 벌만
// 있으면 그 변경은 test_rendering_sdlgpu의 순서 케이스에서 실패한다.
void ShutdownRendererThenTextGpuResources(Renderer& renderer,
                                          TextRenderer& textRenderer);

namespace molga {
namespace detail {

// ── Task 6.3: scope가 수집을 정확히 한 번 닫는지 밖에서 관찰한다 ────────────
// GlyphAtlasCache::EndCollection은 멱등이다 — 두 번 부른 캐시와 한 번 부른
// 캐시는 상태가 같다. 그런데 scope가 지켜야 하는 계약은 바로 그 횟수다:
// 옮겨진 scope가 소유권을 놓지 않으면 두 번째 EndCollection이 *다음* 수집의
// pin을 지워, 그 프레임이 이미 가리키는 page가 축출 가능해진다. 그 차이는
// 캐시 상태로 드러나지 않으므로 호출 자체를 세야 한다.
//
// Renderer.h의 종료 단계 hook과 같은 이유로 출하되는 빌드에 남는다: 관찰
// 대상이 프로덕션 경로 그 자체다. 프로세스 전역이고 되돌릴 수 있으며,
// 기본값은 널이고 널이면 아무 일도 하지 않는다.
using GlyphCollectionEndHook = void (*)(std::uint64_t frameIndex);
void SetGlyphCollectionEndHookForTest(GlyphCollectionEndHook hook) noexcept;

} // namespace detail
} // namespace molga
