#include "Texture.h"
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Texture::Texture(const char* imagePath) : textureID(0), width(0), height(0), channels(0) {
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(imagePath, &width, &height, &channels, 0);
    if (!data) {
        std::cerr << "ERROR::TEXTURE::FILE_NOT_FOUND: " << imagePath << std::endl;
        return;
    }

    CreateFromData(width, height, data, channels);
    stbi_image_free(data);
}

Texture::Texture(int w, int h, unsigned char* data, int ch)
    : textureID(0), width(0), height(0), channels(0) {
    CreateFromData(w, h, data, ch);
}

void Texture::CreateFromData(int w, int h, unsigned char* data, int ch) {
    width = w;
    height = h;
    channels = ch;

    GLenum format = GL_RGBA;
    GLenum internalFormat = GL_RGBA;
    if (channels == 1) {
        format = GL_RED;
        internalFormat = GL_RED;
    } else if (channels == 3) {
        format = GL_RGB;
        internalFormat = GL_RGB;
    } else if (channels == 4) {
        format = GL_RGBA;
        internalFormat = GL_RGBA;
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    glBindTexture(GL_TEXTURE_2D, 0);
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
