#ifndef MOLGA_TEXT_RENDERER_H
#define MOLGA_TEXT_RENDERER_H

#include <string>
#include <unordered_map>
#include "Common/Types.h"

class Shader;
class Renderer;
class Texture;

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

    // Set line height multiplier
    void SetLineSpacing(float spacing) { lineSpacing = spacing; }

    // Singleton access
    static TextRenderer& Get();

private:
    void GenerateBuiltinFont();

    Texture* fontTexture = nullptr;
    std::unordered_map<char, CharInfo> characters;
    float lineHeight = 16.0f;
    float lineSpacing = 1.2f;
    bool initialized = false;
};

#endif // MOLGA_TEXT_RENDERER_H
