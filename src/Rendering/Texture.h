#pragma once

#include <glad/glad.h>

class Texture {
public:
    Texture(const char* imagePath);
    Texture(int width, int height, unsigned char* data, int channels = 4);
    ~Texture();

    void Bind(unsigned int slot = 0) const;
    void Unbind() const;

    // Updates a rectangular region without replacing the Texture object/GL
    // handle. Dynamic font atlases rely on pointer stability for batching.
    bool UpdateSubData(int x, int y, int updateWidth, int updateHeight,
                       const unsigned char* data, int updateChannels = 4);

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
