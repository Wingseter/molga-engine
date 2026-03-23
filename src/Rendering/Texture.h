#ifndef MOLGA_TEXTURE_H
#define MOLGA_TEXTURE_H

#include <glad/glad.h>

class Texture {
public:
    Texture(const char* imagePath);
    Texture(int width, int height, unsigned char* data, int channels = 4);
    ~Texture();

    void Bind(unsigned int slot = 0) const;
    void Unbind() const;

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    unsigned int GetID() const { return textureID; }

private:
    void CreateFromData(int w, int h, unsigned char* data, int ch);

    unsigned int textureID;
    int width;
    int height;
    int channels;
};

#endif // MOLGA_TEXTURE_H
