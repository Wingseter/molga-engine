#ifndef MOLGA_SPRITESHEET_H
#define MOLGA_SPRITESHEET_H

#include "Texture.h"

struct Frame {
    float u0, v0;  // top-left UV
    float u1, v1;  // bottom-right UV
};

class SpriteSheet {
public:
    SpriteSheet(Texture* texture, int frameWidth, int frameHeight);
    SpriteSheet(Texture* texture, int cols, int rows, int frameWidth, int frameHeight);

    Frame GetFrame(int index) const;
    Frame GetFrame(int col, int row) const;

    int GetFrameCount() const { return cols * rows; }
    int GetCols() const { return cols; }
    int GetRows() const { return rows; }
    int GetFrameWidth() const { return frameWidth; }
    int GetFrameHeight() const { return frameHeight; }
    Texture* GetTexture() const { return texture; }

private:
    Texture* texture;
    int cols, rows;
    int frameWidth, frameHeight;
    float frameU, frameV;  // UV size per frame
};

#endif // MOLGA_SPRITESHEET_H
