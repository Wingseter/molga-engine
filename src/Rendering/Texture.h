#pragma once

#include "Core/TextureImportSettings.h"
#include <glad/glad.h>
#include <string>

class Texture {
public:
    Texture(const char* imagePath);
    Texture(const char* imagePath, const molga::TextureImportSettings& settings);
    Texture(int width, int height, unsigned char* data, int channels = 4);
    ~Texture();

    void Bind(unsigned int slot = 0) const;
    void Unbind() const;

    // Updates a rectangular region without replacing the Texture object/GL
    // handle. Dynamic font atlases rely on pointer stability for batching.
    bool UpdateSubData(int x, int y, int updateWidth, int updateHeight,
                       const unsigned char* data, int updateChannels = 4);
    bool Reload(const char* imagePath, const molga::TextureImportSettings& settings,
                std::string* errorOut = nullptr);

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    unsigned int GetID() const { return textureID; }
    bool IsValid() const { return textureID != 0 && width > 0 && height > 0; }
    const molga::TextureImportSettings& GetImportSettings() const { return settings_; }

private:
    bool CreateFromData(int w, int h, const unsigned char* data, int ch,
                        const molga::TextureImportSettings& settings,
                        std::string* errorOut = nullptr);

    unsigned int textureID;
    int width;
    int height;
    int channels;
    molga::TextureImportSettings settings_ = molga::TextureImportSettings::LegacyDefaults();
};
