#include "Texture.h"
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Texture::Texture(const char* imagePath)
    : Texture(imagePath, molga::TextureImportSettings::LegacyDefaults()) {}

Texture::Texture(const char* imagePath, const molga::TextureImportSettings& settings)
    : textureID(0), width(0), height(0), channels(0) {
    Reload(imagePath, settings, nullptr);
}

bool Texture::Reload(const char* imagePath, const molga::TextureImportSettings& settings,
                     std::string* errorOut) {
    stbi_set_flip_vertically_on_load(true);

    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    unsigned char* data = stbi_load(imagePath, &decodedWidth, &decodedHeight, &decodedChannels, 0);
    if (!data) {
        if (errorOut) *errorOut = std::string("stbi_load failed: ") +
            (stbi_failure_reason() ? stbi_failure_reason() : imagePath);
        return false;
    }

    const bool success = CreateFromData(decodedWidth, decodedHeight, data, decodedChannels,
                                        settings, errorOut);
    stbi_image_free(data);
    return success;
}

Texture::Texture(int w, int h, unsigned char* data, int ch)
    : textureID(0), width(0), height(0), channels(0) {
    CreateFromData(w, h, data, ch, molga::TextureImportSettings::LegacyDefaults());
}

bool Texture::CreateFromData(int w, int h, const unsigned char* data, int ch,
                             const molga::TextureImportSettings& settings,
                             std::string* errorOut) {
    if (!data || w <= 0 || h <= 0 || (ch != 1 && ch != 3 && ch != 4)) {
        if (errorOut) *errorOut = "invalid decoded texture data";
        return false;
    }

    GLenum format = GL_RGBA;
    GLenum internalFormat = GL_RGBA;
    if (ch == 1) {
        format = GL_RED;
        internalFormat = GL_RED;
    } else if (ch == 3) {
        format = GL_RGB;
        internalFormat = settings.colorSpace == molga::TextureColorSpace::SRGB
            ? GL_SRGB8 : GL_RGB8;
    } else if (ch == 4) {
        format = GL_RGBA;
        internalFormat = settings.colorSpace == molga::TextureColorSpace::SRGB
            ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    }

    unsigned int candidate = 0;
    glGenTextures(1, &candidate);
    if (!candidate) {
        if (errorOut) *errorOut = "could not allocate OpenGL texture";
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, candidate);

    auto toWrap = [](molga::TextureWrapMode mode) {
        if (mode == molga::TextureWrapMode::Repeat) return GL_REPEAT;
        if (mode == molga::TextureWrapMode::MirroredRepeat) return GL_MIRRORED_REPEAT;
        return GL_CLAMP_TO_EDGE;
    };
    const GLint magFilter = settings.filter == molga::TextureFilterMode::Nearest
        ? GL_NEAREST : GL_LINEAR;
    GLint minFilter = magFilter;
    if (settings.mipmaps) {
        minFilter = settings.filter == molga::TextureFilterMode::Nearest
            ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, toWrap(settings.wrapU));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, toWrap(settings.wrapV));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    if (settings.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glBindTexture(GL_TEXTURE_2D, 0);
    if (textureID) glDeleteTextures(1, &textureID);
    textureID = candidate;
    width = w;
    height = h;
    channels = ch;
    settings_ = settings;
    if (errorOut) errorOut->clear();
    return true;
}

Texture::~Texture() {
    if (textureID) {
        glDeleteTextures(1, &textureID);
    }
}

void Texture::Bind(unsigned int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, textureID);
}

void Texture::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Texture::UpdateSubData(int x, int y, int updateWidth, int updateHeight,
                            const unsigned char* data, int updateChannels) {
    if (!textureID || !data || updateWidth <= 0 || updateHeight <= 0 ||
        x < 0 || y < 0 || x + updateWidth > width || y + updateHeight > height) {
        return false;
    }

    GLenum format = GL_RGBA;
    if (updateChannels == 1) format = GL_RED;
    else if (updateChannels == 3) format = GL_RGB;
    else if (updateChannels != 4) return false;

    glBindTexture(GL_TEXTURE_2D, textureID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, updateWidth, updateHeight,
                    format, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}
