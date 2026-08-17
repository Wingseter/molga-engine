#include "Tilemap.h"
#include "SpriteSheet.h"
#include "Shader.h"
#include "Camera2D.h"
#include "Texture.h"
#include "Renderer.h"
#include "RenderPass.h"
#include "RenderQueue.h"
#include "RenderSystem2D.h"

Tilemap::Tilemap(int width, int height, int tileSize)
    : width(width), height(height), tileSize(tileSize), spriteSheet(nullptr) {
    tiles.resize(width * height, -1);  // -1 = empty
    solidTiles.resize(256, false);  // Support up to 256 tile types
}

void Tilemap::SetTile(int x, int y, int tileId) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        tiles[y * width + x] = tileId;
    }
}

int Tilemap::GetTile(int x, int y) const {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        return tiles[y * width + x];
    }
    return -1;
}

void Tilemap::SetSpriteSheet(SpriteSheet* sheet) {
    spriteSheet = sheet;
}

void Tilemap::SetCollisionTile(int tileId, bool solid) {
    if (tileId >= 0 && tileId < static_cast<int>(solidTiles.size())) {
        solidTiles[tileId] = solid;
    }
}

void Tilemap::Render(Renderer* renderer, Shader* shader, Camera2D* camera) {
    if (!renderer || !spriteSheet || !spriteSheet->GetTexture() || !shader) return;
    molga::RenderQueue queue;
    Texture* texture = spriteSheet->GetTexture();
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int tileId = tiles[y * width + x];
            if (tileId < 0) continue;

            Frame frame = spriteSheet->GetFrame(tileId);

            const float left = static_cast<float>(x * tileSize);
            const float top = static_cast<float>(y * tileSize);
            const float right = left + static_cast<float>(tileSize);
            const float bottom = top + static_cast<float>(tileSize);
            molga::RenderCommand command;
            command.batchKey.shaderName = "batch";
            command.batchKey.isBatchable = true;
            command.batchKey.texture = texture->Handle();
            command.batchKey.textureSampler = texture->Sampler();
            command.batchKey.textureStableId = texture->StableId();
            command.isBatchableSprite = true;
            command.vertices = {{{left, top, frame.u0, frame.v0, 1, 1, 1, 1},
                                 {right, top, frame.u1, frame.v0, 1, 1, 1, 1},
                                 {right, bottom, frame.u1, frame.v1, 1, 1, 1, 1},
                                 {left, bottom, frame.u0, frame.v1, 1, 1, 1, 1}}};
            queue.Submit(command);
        }
    }
    molga::RenderPass pass(*renderer, shader, camera);
    molga::RenderSystem2D::Get().Render(queue, renderer, camera);
}

bool Tilemap::IsSolid(int x, int y) const {
    int tileId = GetTile(x, y);
    if (tileId < 0 || tileId >= static_cast<int>(solidTiles.size())) {
        return false;
    }
    return solidTiles[tileId];
}

int Tilemap::WorldToTileX(float worldX) const {
    return static_cast<int>(worldX / tileSize);
}

int Tilemap::WorldToTileY(float worldY) const {
    return static_cast<int>(worldY / tileSize);
}

AABB Tilemap::GetTileAABB(int x, int y) const {
    return {
        static_cast<float>(x * tileSize),
        static_cast<float>(y * tileSize),
        static_cast<float>(tileSize),
        static_cast<float>(tileSize)
    };
}

bool Tilemap::CheckCollision(const AABB& box) const {
    int startX = WorldToTileX(box.Left());
    int endX = WorldToTileX(box.Right());
    int startY = WorldToTileY(box.Top());
    int endY = WorldToTileY(box.Bottom());

    for (int y = startY; y <= endY; y++) {
        for (int x = startX; x <= endX; x++) {
            if (IsSolid(x, y)) {
                AABB tileBox = GetTileAABB(x, y);
                if (Collision::CheckAABB(box, tileBox)) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::vector<AABB> Tilemap::GetCollidingTiles(const AABB& box) const {
    std::vector<AABB> result;

    int startX = WorldToTileX(box.Left());
    int endX = WorldToTileX(box.Right());
    int startY = WorldToTileY(box.Top());
    int endY = WorldToTileY(box.Bottom());

    for (int y = startY; y <= endY; y++) {
        for (int x = startX; x <= endX; x++) {
            if (IsSolid(x, y)) {
                AABB tileBox = GetTileAABB(x, y);
                if (Collision::CheckAABB(box, tileBox)) {
                    result.push_back(tileBox);
                }
            }
        }
    }
    return result;
}
