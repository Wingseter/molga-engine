#ifndef MOLGA_TEXTURE_H
#define MOLGA_TEXTURE_H

#include <glad/glad.h>

class Texture {
public:
    Texture(const char* imagePath);
    ~Texture();

    void Bind(unsigned int slot = 0) const;
    void Unbind() const;

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    unsigned int GetID() const { return textureID; }

private:
    unsigned int textureID;
    int width;
    int height;
    int channels;
};

#endif // MOLGA_TEXTURE_H
