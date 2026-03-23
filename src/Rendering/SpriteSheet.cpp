#include "SpriteSheet.h"

SpriteSheet::SpriteSheet(Texture* texture, int frameWidth, int frameHeight)
    : texture(texture), frameWidth(frameWidth), frameHeight(frameHeight) {
    cols = texture->GetWidth() / frameWidth;
    rows = texture->GetHeight() / frameHeight;
    frameU = static_cast<float>(frameWidth) / texture->GetWidth();
    frameV = static_cast<float>(frameHeight) / texture->GetHeight();
}

SpriteSheet::SpriteSheet(Texture* texture, int cols, int rows, int frameWidth, int frameHeight)
    : texture(texture), cols(cols), rows(rows), frameWidth(frameWidth), frameHeight(frameHeight) {
    frameU = static_cast<float>(frameWidth) / texture->GetWidth();
    frameV = static_cast<float>(frameHeight) / texture->GetHeight();
}

Frame SpriteSheet::GetFrame(int index) const {
    int col = index % cols;
    int row = index / cols;
    return GetFrame(col, row);
}

Frame SpriteSheet::GetFrame(int col, int row) const {
    Frame frame;
    frame.u0 = col * frameU;
    frame.v0 = row * frameV;
    frame.u1 = frame.u0 + frameU;
    frame.v1 = frame.v0 + frameV;
    return frame;
}
