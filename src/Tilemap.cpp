#include "Tilemap.h"
#include "SpriteSheet.h"
#include "Shader.h"
#include "Camera2D.h"
#include "Texture.h"
#include <glad/glad.h>

Tilemap::Tilemap(int width, int height, int tileSize)
    : width(width), height(height), tileSize(tileSize), spriteSheet(nullptr), VAO(0), VBO(0) {
    tiles.resize(width * height, -1);  // -1 = empty
    solidTiles.resize(256, false);  // Support up to 256 tile types
    SetupBuffers();
}

Tilemap::~Tilemap() {
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
}

void Tilemap::SetupBuffers() {
    float vertices[] = {
        // pos      // tex
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,

        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
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

void Tilemap::Render(Shader* shader, Camera2D* camera) {
    if (!spriteSheet || !shader) return;

    shader->Use();

    // Set projection
    mat4x4 projection, view, projView;
    if (camera) {
        camera->GetProjectionMatrix(projection);
        camera->GetViewMatrix(view);
    } else {
        mat4x4_identity(projection);
        mat4x4_identity(view);
    }
    mat4x4_mul(projView, projection, view);
    shader->SetMat4("projection", (float*)projView);

    spriteSheet->GetTexture()->Bind(0);
    shader->SetInt("uTexture", 0);
    shader->SetBool("useTexture", true);
    shader->SetVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);

    glBindVertexArray(VAO);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int tileId = tiles[y * width + x];
            if (tileId < 0) continue;

            Frame frame = spriteSheet->GetFrame(tileId);

            mat4x4 model;
            mat4x4_identity(model);
            mat4x4_translate_in_place(model, static_cast<float>(x * tileSize), static_cast<float>(y * tileSize), 0.0f);
            mat4x4_scale_aniso(model, model, static_cast<float>(tileSize), static_cast<float>(tileSize), 1.0f);

            shader->SetMat4("model", (float*)model);
            shader->SetVec4("uUV", frame.u0, frame.v0, frame.u1, frame.v1);

            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    glBindVertexArray(0);
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
