#ifndef MOLGA_TILEMAP_H
#define MOLGA_TILEMAP_H

#include <vector>
#include "Collision.h"

class Texture;
class SpriteSheet;
class Shader;
class Camera2D;

class Tilemap {
public:
    Tilemap(int width, int height, int tileSize);
    ~Tilemap();

    void SetTile(int x, int y, int tileId);
    int GetTile(int x, int y) const;

    void SetSpriteSheet(SpriteSheet* sheet);
    void SetCollisionTile(int tileId, bool solid);

    void Render(Shader* shader, Camera2D* camera = nullptr);

    // Collision
    bool IsSolid(int x, int y) const;
    bool CheckCollision(const AABB& box) const;
    std::vector<AABB> GetCollidingTiles(const AABB& box) const;

    // World to tile conversion
    int WorldToTileX(float worldX) const;
    int WorldToTileY(float worldY) const;
    AABB GetTileAABB(int x, int y) const;

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    int GetTileSize() const { return tileSize; }
    float GetWorldWidth() const { return static_cast<float>(width * tileSize); }
    float GetWorldHeight() const { return static_cast<float>(height * tileSize); }

private:
    int width, height;
    int tileSize;
    std::vector<int> tiles;
    std::vector<bool> solidTiles;
    SpriteSheet* spriteSheet;

    unsigned int VAO, VBO;
    void SetupBuffers();
};

#endif // MOLGA_TILEMAP_H
