#include "TextRenderer.h"
#include "Texture.h"
#include "Renderer.h"
#include "Shader.h"
#include "Sprite.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/Utf8.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Simple 8x8 bitmap font data (ASCII 32-126)
// Each character is 8 pixels wide and 8 pixels tall
static const unsigned char BUILTIN_FONT_DATA[] = {
    // Space (32)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ! (33)
    0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00,
    // " (34)
    0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00,
    // # (35)
    0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00,
    // $ (36)
    0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00,
    // % (37)
    0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00,
    // & (38)
    0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00,
    // ' (39)
    0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ( (40)
    0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00,
    // ) (41)
    0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00,
    // * (42)
    0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00,
    // + (43)
    0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00,
    // , (44)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30,
    // - (45)
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00,
    // . (46)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00,
    // / (47)
    0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00,
    // 0 (48)
    0x7C, 0xCE, 0xDE, 0xF6, 0xE6, 0xC6, 0x7C, 0x00,
    // 1 (49)
    0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00,
    // 2 (50)
    0x7C, 0xC6, 0x0E, 0x3C, 0x78, 0xE0, 0xFE, 0x00,
    // 3 (51)
    0x7E, 0x0C, 0x18, 0x3C, 0x06, 0xC6, 0x7C, 0x00,
    // 4 (52)
    0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00,
    // 5 (53)
    0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00,
    // 6 (54)
    0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00,
    // 7 (55)
    0xFE, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00,
    // 8 (56)
    0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00,
    // 9 (57)
    0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00,
    // : (58)
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00,
    // ; (59)
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30,
    // < (60)
    0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00,
    // = (61)
    0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00,
    // > (62)
    0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00,
    // ? (63)
    0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00,
    // @ (64)
    0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7C, 0x00,
    // A (65)
    0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00,
    // B (66)
    0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xFC, 0x00,
    // C (67)
    0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00,
    // D (68)
    0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8, 0x00,
    // E (69)
    0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xFE, 0x00,
    // F (70)
    0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0x00,
    // G (71)
    0x7C, 0xC6, 0xC0, 0xCE, 0xC6, 0xC6, 0x7E, 0x00,
    // H (72)
    0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00,
    // I (73)
    0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00,
    // J (74)
    0x06, 0x06, 0x06, 0x06, 0x06, 0xC6, 0x7C, 0x00,
    // K (75)
    0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0x00,
    // L (76)
    0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00,
    // M (77)
    0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00,
    // N (78)
    0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00,
    // O (79)
    0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    // P (80)
    0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0x00,
    // Q (81)
    0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x06,
    // R (82)
    0xFC, 0xC6, 0xC6, 0xFC, 0xD8, 0xCC, 0xC6, 0x00,
    // S (83)
    0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0xC6, 0x7C, 0x00,
    // T (84)
    0xFE, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00,
    // U (85)
    0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    // V (86)
    0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00,
    // W (87)
    0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00,
    // X (88)
    0xC6, 0x6C, 0x38, 0x38, 0x6C, 0xC6, 0xC6, 0x00,
    // Y (89)
    0xC6, 0xC6, 0x6C, 0x38, 0x18, 0x18, 0x18, 0x00,
    // Z (90)
    0xFE, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xFE, 0x00,
    // [ (91)
    0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00,
    // \ (92)
    0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00,
    // ] (93)
    0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00,
    // ^ (94)
    0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00,
    // _ (95)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE,
    // ` (96)
    0x18, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00,
    // a (97)
    0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0x7E, 0x00,
    // b (98)
    0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0x00,
    // c (99)
    0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00,
    // d (100)
    0x06, 0x06, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x00,
    // e (101)
    0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00,
    // f (102)
    0x1C, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00,
    // g (103)
    0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x7C,
    // h (104)
    0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00,
    // i (105)
    0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00,
    // j (106)
    0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0xCC, 0x78,
    // k (107)
    0xC0, 0xC0, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0x00,
    // l (108)
    0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00,
    // m (109)
    0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xC6, 0xC6, 0x00,
    // n (110)
    0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00,
    // o (111)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00,
    // p (112)
    0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0,
    // q (113)
    0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x06,
    // r (114)
    0x00, 0x00, 0xDC, 0xE6, 0xC0, 0xC0, 0xC0, 0x00,
    // s (115)
    0x00, 0x00, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x00,
    // t (116)
    0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00,
    // u (117)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00,
    // v (118)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00,
    // w (119)
    0x00, 0x00, 0xC6, 0xC6, 0xD6, 0xFE, 0x6C, 0x00,
    // x (120)
    0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00,
    // y (121)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x7C,
    // z (122)
    0x00, 0x00, 0xFE, 0x0C, 0x38, 0x60, 0xFE, 0x00,
    // { (123)
    0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00,
    // | (124)
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00,
    // } (125)
    0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00,
    // ~ (126)
    0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

namespace {

bool CanCreateBuiltinTexture() {
    return molga::GraphicsDevice::Current() != nullptr;
}

} // namespace

TextRenderer& TextRenderer::Get() {
    static TextRenderer instance;
    return instance;
}

TextRenderer::TextRenderer() {
}

TextRenderer::~TextRenderer() {
    Shutdown();
}

bool TextRenderer::Init() {
    if (initialized) return true;

    GenerateBuiltinFont();
    initialized = true;
    return true;
}

void TextRenderer::Shutdown() {
    fontAtlas.Clear();
    fontTexture.reset();
    characters.clear();
    initialized = false;
}

void TextRenderer::GenerateBuiltinFont() {
    // Create a texture atlas for the font (16x6 characters = 96 characters)
    const int CHAR_WIDTH = 8;
    const int CHAR_HEIGHT = 8;
    const int CHARS_PER_ROW = 16;
    const int NUM_CHARS = 95;  // ASCII 32-126
    const int ROWS = (NUM_CHARS + CHARS_PER_ROW - 1) / CHARS_PER_ROW;

    int texWidth = CHARS_PER_ROW * CHAR_WIDTH;
    int texHeight = ROWS * CHAR_HEIGHT;

    // Create RGBA texture data
    unsigned char* textureData = new unsigned char[texWidth * texHeight * 4];
    memset(textureData, 0, texWidth * texHeight * 4);

    // Fill texture with font data
    for (int charIdx = 0; charIdx < NUM_CHARS; charIdx++) {
        int col = charIdx % CHARS_PER_ROW;
        int row = charIdx / CHARS_PER_ROW;

        for (int y = 0; y < CHAR_HEIGHT; y++) {
            unsigned char rowBits = BUILTIN_FONT_DATA[charIdx * CHAR_HEIGHT + y];
            for (int x = 0; x < CHAR_WIDTH; x++) {
                int texX = col * CHAR_WIDTH + x;
                int texY = row * CHAR_HEIGHT + y;
                int pixelIdx = (texY * texWidth + texX) * 4;

                // Check if bit is set (MSB first)
                bool set = (rowBits & (0x80 >> x)) != 0;
                unsigned char value = set ? 255 : 0;

                textureData[pixelIdx + 0] = 255;  // R
                textureData[pixelIdx + 1] = 255;  // G
                textureData[pixelIdx + 2] = 255;  // B
                textureData[pixelIdx + 3] = value; // A (glyph)
            }
        }

        // Store character info
        char c = static_cast<char>(32 + charIdx);
        CharInfo info;
        info.u0 = static_cast<float>(col * CHAR_WIDTH) / texWidth;
        info.v0 = static_cast<float>(row * CHAR_HEIGHT) / texHeight;
        info.u1 = static_cast<float>((col + 1) * CHAR_WIDTH) / texWidth;
        info.v1 = static_cast<float>((row + 1) * CHAR_HEIGHT) / texHeight;
        info.width = static_cast<float>(CHAR_WIDTH);
        info.height = static_cast<float>(CHAR_HEIGHT);
        info.xOffset = 0;
        info.yOffset = 0;
        info.xAdvance = static_cast<float>(CHAR_WIDTH);
        characters[c] = info;
    }

    // Unit tests and import validation can run without a graphics device.
    // Metrics remain usable there; editor/runtime initialization creates the
    // actual fallback atlas after SDL_GPU is ready.
    if (CanCreateBuiltinTexture()) {
        fontTexture = std::make_unique<Texture>(texWidth, texHeight, textureData, 4);
    }
    lineHeight = static_cast<float>(CHAR_HEIGHT);

    delete[] textureData;
}

void TextRenderer::RenderText(Renderer* renderer, Shader* shader,
                               const std::string& text, float x, float y,
                               float scale, const Color& color) {
    if (!initialized || !fontTexture) return;

    float cursorX = x;
    float cursorY = y;

    bool wasDrawing = renderer->IsDrawing();
    if (!wasDrawing) {
        renderer->Begin(shader, nullptr);
    }

    for (std::uint32_t codepoint : molga::DecodeUtf8(text)) {
        if (codepoint == '\n') {
            cursorX = x;
            cursorY += lineHeight * scale * lineSpacing;
            continue;
        }

        const char c = (codepoint >= 32U && codepoint <= 126U)
            ? static_cast<char>(codepoint) : '?';
        auto it = characters.find(c);
        if (it == characters.end()) {
            cursorX += 8.0f * scale;
            continue;
        }

        const CharInfo& info = it->second;

        Sprite sprite;
        sprite.SetTexture(fontTexture.get());
        sprite.SetPosition(cursorX + info.xOffset * scale, cursorY + info.yOffset * scale);
        sprite.SetSize(info.width * scale, info.height * scale);
        sprite.SetColor(color.r, color.g, color.b, color.a);
        sprite.SetUV(info.u0, info.v0, info.u1, info.v1);

        renderer->DrawSprite(&sprite);

        cursorX += info.xAdvance * scale;
    }

    if (!wasDrawing) {
        renderer->End();
    }
}

float TextRenderer::GetTextWidth(const std::string& text, float scale) const {
    float width = 0.0f;
    float maxWidth = 0.0f;

    for (std::uint32_t codepoint : molga::DecodeUtf8(text)) {
        if (codepoint == '\n') {
            maxWidth = std::max(maxWidth, width);
            width = 0.0f;
            continue;
        }

        const char c = (codepoint >= 32U && codepoint <= 126U)
            ? static_cast<char>(codepoint) : '?';
        auto it = characters.find(c);
        if (it != characters.end()) {
            width += it->second.xAdvance * scale;
        } else {
            width += 8.0f * scale;
        }
    }

    return std::max(maxWidth, width);
}

float TextRenderer::GetTextHeight(float scale) const {
    return lineHeight * scale;
}

namespace {

int AtlasPixelSize(float fontSizePx) {
    return std::max(1, std::min(static_cast<int>(std::lround(fontSizePx)), 512));
}

float SafeScale(float scale) {
    return std::max(0.0f, scale);
}

float SafeLineSpacing(float spacing) {
    return std::max(0.1f, std::min(spacing, 10.0f));
}

float AlignmentOffset(TextHorizontalAlignment alignment, float width) {
    if (alignment == TextHorizontalAlignment::Center) return -width * 0.5f;
    if (alignment == TextHorizontalAlignment::Right) return -width;
    return 0.0f;
}

void SetVertex(molga::Vertex2D& vertex, float x, float y, float u, float v,
               const Color& color) {
    vertex = {x, y, u, v, color.r, color.g, color.b, color.a};
}

} // namespace

TextMetrics TextRenderer::MeasureText(const std::string& text,
                                      const std::string& fontGuid,
                                      float fontSizePx,
                                      float scale,
                                      float requestedLineSpacing) {
    const int pixelSize = AtlasPixelSize(fontSizePx);
    const float sizeScale = SafeScale(scale);
    const float spacing = SafeLineSpacing(requestedLineSpacing);
    molga::FontFaceMetrics fontMetrics;
    const bool useFont = fontAtlas.GetMetrics(fontGuid, pixelSize, fontMetrics);

    TextMetrics result;
    result.lineHeight = (useFont && fontMetrics.lineHeight > 0.0f)
        ? fontMetrics.lineHeight * sizeScale
        : static_cast<float>(pixelSize) * sizeScale;

    float lineWidth = 0.0f;
    std::uint32_t previous = 0U;
    bool hasPrevious = false;
    for (std::uint32_t codepoint : molga::DecodeUtf8(text)) {
        if (codepoint == '\n') {
            result.width = std::max(result.width, lineWidth);
            lineWidth = 0.0f;
            previous = 0U;
            hasPrevious = false;
            ++result.lineCount;
            continue;
        }

        if (useFont) {
            if (hasPrevious) {
                lineWidth += fontAtlas.GetKerning(
                    fontGuid, pixelSize, previous, codepoint) * sizeScale;
            }
            molga::FontAtlasGlyph glyph;
            if (fontAtlas.GetGlyph(fontGuid, pixelSize, codepoint, glyph)) {
                lineWidth += glyph.xAdvance * sizeScale;
            }
        } else {
            lineWidth += static_cast<float>(pixelSize) * sizeScale;
        }
        previous = codepoint;
        hasPrevious = true;
    }
    result.width = std::max(result.width, lineWidth);
    result.height = result.lineHeight;
    if (result.lineCount > 1U) {
        result.height += static_cast<float>(result.lineCount - 1U) *
                         result.lineHeight * spacing;
    }
    return result;
}

void TextRenderer::CollectText(molga::RenderQueue& queue, const TextDrawParams& params) {
    if (params.text.empty()) return;

    const std::vector<std::uint32_t> codepoints = molga::DecodeUtf8(params.text);
    const int pixelSize = AtlasPixelSize(params.fontSizePx);
    const float sizeScale = SafeScale(params.scale);
    const float spacing = SafeLineSpacing(params.lineSpacing);
    molga::FontFaceMetrics fontMetrics;
    const bool useFont = fontAtlas.GetMetrics(params.fontGuid, pixelSize, fontMetrics);

    std::vector<float> lineWidths(1U, 0.0f);
    std::uint32_t previous = 0U;
    bool hasPrevious = false;
    for (std::uint32_t codepoint : codepoints) {
        if (codepoint == '\n') {
            lineWidths.push_back(0.0f);
            previous = 0U;
            hasPrevious = false;
            continue;
        }
        if (useFont) {
            if (hasPrevious) {
                lineWidths.back() += fontAtlas.GetKerning(
                    params.fontGuid, pixelSize, previous, codepoint) * sizeScale;
            }
            molga::FontAtlasGlyph glyph;
            if (fontAtlas.GetGlyph(params.fontGuid, pixelSize, codepoint, glyph)) {
                lineWidths.back() += glyph.xAdvance * sizeScale;
            }
        } else {
            lineWidths.back() += static_cast<float>(pixelSize) * sizeScale;
        }
        previous = codepoint;
        hasPrevious = true;
    }

    const float resolvedLineHeight =
        (useFont && fontMetrics.lineHeight > 0.0f)
            ? fontMetrics.lineHeight * sizeScale
            : static_cast<float>(pixelSize) * sizeScale;
    float cursorY = params.y;
    std::size_t lineIndex = 0U;
    float cursorX = params.x + AlignmentOffset(params.alignment, lineWidths.front());
    float baseline = cursorY + (useFont ? fontMetrics.ascent * sizeScale : 0.0f);
    previous = 0U;
    hasPrevious = false;

    for (std::uint32_t codepoint : codepoints) {
        if (codepoint == '\n') {
            ++lineIndex;
            cursorY += resolvedLineHeight * spacing;
            cursorX = params.x + AlignmentOffset(params.alignment, lineWidths[lineIndex]);
            baseline = cursorY + (useFont ? fontMetrics.ascent * sizeScale : 0.0f);
            previous = 0U;
            hasPrevious = false;
            continue;
        }

        if (useFont) {
            if (hasPrevious) {
                cursorX += fontAtlas.GetKerning(
                    params.fontGuid, pixelSize, previous, codepoint) * sizeScale;
            }
            molga::FontAtlasGlyph glyph;
            if (fontAtlas.GetGlyph(params.fontGuid, pixelSize, codepoint, glyph)) {
                if (glyph.drawable) {
                    const float x = cursorX + glyph.xOffset * sizeScale;
                    const float y = baseline + glyph.yOffset * sizeScale;
                    const float width = glyph.width * sizeScale;
                    const float height = glyph.height * sizeScale;

                    molga::RenderCommand command;
                    command.sortKey.cameraPass = params.cameraPass;
                    command.sortKey.sortingLayer = params.sortingLayer;
                    command.sortKey.sortingOrder = params.sortingOrder;
                    command.sortKey.depthOrYSort = params.depthOrYSort;
                    command.batchKey.shaderName = "batch";
                    if (glyph.texture && glyph.texture->IsValid()) {
                        command.batchKey.texture = glyph.texture->Handle();
                        command.batchKey.textureSampler = glyph.texture->Sampler();
                        command.batchKey.textureStableId = glyph.texture->StableId();
                    }
                    command.batchKey.isBatchable = true;
                    command.isBatchableSprite = true;
                    // stb bitmaps and atlas CPU pages use a top-to-bottom row
                    // order, hence top vertices sample v0 and bottom vertices v1.
                    SetVertex(command.vertices[0], x, y, glyph.u0, glyph.v0, params.color);
                    SetVertex(command.vertices[1], x + width, y, glyph.u1, glyph.v0, params.color);
                    SetVertex(command.vertices[2], x + width, y + height,
                              glyph.u1, glyph.v1, params.color);
                    SetVertex(command.vertices[3], x, y + height,
                              glyph.u0, glyph.v1, params.color);
                    queue.Submit(command);
                }
                cursorX += glyph.xAdvance * sizeScale;
            }
        } else {
            const std::uint32_t displayCodepoint =
                (codepoint >= 32U && codepoint <= 126U) ? codepoint : '?';
            const char displayCharacter = static_cast<char>(displayCodepoint);
            const auto found = characters.find(displayCharacter);
            const float bitmapScale = static_cast<float>(pixelSize) / 8.0f * sizeScale;
            if (found != characters.end()) {
                const CharInfo& glyph = found->second;
                if (displayCharacter != ' ') {
                    const float x = cursorX + glyph.xOffset * bitmapScale;
                    const float y = cursorY + glyph.yOffset * bitmapScale;
                    const float width = glyph.width * bitmapScale;
                    const float height = glyph.height * bitmapScale;
                    molga::RenderCommand command;
                    command.sortKey.cameraPass = params.cameraPass;
                    command.sortKey.sortingLayer = params.sortingLayer;
                    command.sortKey.sortingOrder = params.sortingOrder;
                    command.sortKey.depthOrYSort = params.depthOrYSort;
                    command.batchKey.shaderName = "batch";
                    if (fontTexture && fontTexture->IsValid()) {
                        command.batchKey.texture = fontTexture->Handle();
                        command.batchKey.textureSampler = fontTexture->Sampler();
                        command.batchKey.textureStableId = fontTexture->StableId();
                    }
                    command.batchKey.isBatchable = true;
                    command.isBatchableSprite = true;
                    SetVertex(command.vertices[0], x, y, glyph.u0, glyph.v0, params.color);
                    SetVertex(command.vertices[1], x + width, y, glyph.u1, glyph.v0, params.color);
                    SetVertex(command.vertices[2], x + width, y + height,
                              glyph.u1, glyph.v1, params.color);
                    SetVertex(command.vertices[3], x, y + height,
                              glyph.u0, glyph.v1, params.color);
                    queue.Submit(command);
                }
            }
            cursorX += static_cast<float>(pixelSize) * sizeScale;
        }
        previous = codepoint;
        hasPrevious = true;
    }
}

void TextRenderer::InvalidateFont(const std::string& fontGuid) {
    fontAtlas.Invalidate(fontGuid);
}

void TextRenderer::InvalidateAllFonts() {
    fontAtlas.Clear();
}

std::size_t TextRenderer::GetAtlasPageCount(const std::string& fontGuid,
                                            int pixelSize) const {
    return fontAtlas.PageCount(fontGuid, pixelSize);
}

std::size_t TextRenderer::GetCachedFontSizeCount() const {
    return fontAtlas.CachedFontSizeCount();
}
