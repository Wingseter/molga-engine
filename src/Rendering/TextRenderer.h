#pragma once

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
    float lineHeight = 16.0f;
    float lineSpacing = 1.2f;
    bool initialized = false;
};
