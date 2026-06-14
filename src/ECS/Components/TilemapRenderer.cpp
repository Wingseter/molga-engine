#include "TilemapRenderer.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"

REGISTER_COMPONENT(TilemapRenderer)

#include "../../Rendering/Renderer.h"
#include "../../Rendering/Sprite.h"
#include "../../Rendering/Texture.h"
#include "../../Core/TextureManager.h"
#include "../../Core/PathService.h"
#include "../../Common/Log.h"
#include "../../Physics/Collision.h"

#ifdef MOLGA_EDITOR
#include "../../Editor/Project.h"
#include <imgui.h>
#include <filesystem>
#endif

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

TilemapRenderer::TilemapRenderer() {
    tiles.resize(width * height, -1);
    solidTiles.resize(256, false);
}

void TilemapRenderer::SetTile(int x, int y, int tileId) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        tiles[y * width + x] = tileId;
    }
}

int TilemapRenderer::GetTile(int x, int y) const {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        return tiles[y * width + x];
    }
    return -1;
}

void TilemapRenderer::SetSolid(int tileId, bool solid) {
    if (tileId >= 0 && tileId < static_cast<int>(solidTiles.size())) {
        solidTiles[tileId] = solid;
    }
}

bool TilemapRenderer::IsSolid(int tileId) const {
    if (tileId >= 0 && tileId < static_cast<int>(solidTiles.size())) {
        return solidTiles[tileId];
    }
    return false;
}

bool TilemapRenderer::IsSolid(int x, int y) const {
    int tileId = GetTile(x, y);
    if (tileId < 0) return false;
    return IsSolid(tileId);
}

std::vector<AABB> TilemapRenderer::GetCollidingTiles(const AABB& worldBox) const {
    std::vector<AABB> result;
    if (!gameObject) return result;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return result;

    Vector2 transformPos = transform->GetWorldPosition();

    // Translate 'worldBox' to local space (subtract transform pos)
    AABB localBox = worldBox;
    localBox.x -= transformPos.x;
    localBox.y -= transformPos.y;

    // Query solid tiles using 'WorldToTileX/Y' logic
    int startX = static_cast<int>(std::floor(localBox.x / tileSize));
    int endX = static_cast<int>(std::floor((localBox.x + localBox.width) / tileSize));
    int startY = static_cast<int>(std::floor(localBox.y / tileSize));
    int endY = static_cast<int>(std::floor((localBox.y + localBox.height) / tileSize));

    for (int y = startY; y <= endY; y++) {
        for (int x = startX; x <= endX; x++) {
            if (IsSolid(x, y)) {
                AABB tileBox;
                tileBox.x = static_cast<float>(x * tileSize);
                tileBox.y = static_cast<float>(y * tileSize);
                tileBox.width = static_cast<float>(tileSize);
                tileBox.height = static_cast<float>(tileSize);

                if (Collision::CheckAABB(localBox, tileBox)) {
                    // Translate back to world space
                    tileBox.x += transformPos.x;
                    tileBox.y += transformPos.y;
                    result.push_back(tileBox);
                }
            }
        }
    }
    return result;
}

void TilemapRenderer::RenderSprite(Renderer* renderer) {
    if (!gameObject || !enabled || !spriteSheet) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    Vector2 worldPos = transform->GetWorldPosition();
    Vector2 worldScale = transform->GetWorldScale();

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int tileId = tiles[y * width + x];
            if (tileId < 0) continue;

            if (tileId >= spriteSheet->GetFrameCount()) continue;

            Frame frame = spriteSheet->GetFrame(tileId);

            Sprite sprite;
            float tileWorldX = worldPos.x + (x * tileSize) * worldScale.x;
            float tileWorldY = worldPos.y + (y * tileSize) * worldScale.y;

            sprite.SetPosition(tileWorldX, tileWorldY);
            sprite.SetSize(static_cast<float>(tileSize) * worldScale.x, static_cast<float>(tileSize) * worldScale.y);
            sprite.SetRotation(0.0f);
            sprite.SetColor(1.0f, 1.0f, 1.0f, 1.0f);
            sprite.SetTexture(spriteSheet->GetTexture());
            sprite.SetFrame(frame);

            renderer->DrawSprite(&sprite);
        }
    }
}

void TilemapRenderer::Serialize(nlohmann::json& j) const {
    j["spriteSheetPath"] = spriteSheetPath;
    j["width"] = width;
    j["height"] = height;
    j["tileSize"] = tileSize;
    j["tiles"] = tiles;
    j["solidTiles"] = solidTiles;
    j["sortingOrder"] = sortingOrder;
}

void TilemapRenderer::Deserialize(const nlohmann::json& j) {
    if (j.contains("spriteSheetPath")) {
        spriteSheetPath = j["spriteSheetPath"];
    }
    if (j.contains("width")) {
        width = j["width"];
    }
    if (j.contains("height")) {
        height = j["height"];
    }
    if (j.contains("tileSize")) {
        tileSize = j["tileSize"];
    }
    if (j.contains("tiles") && j["tiles"].is_array()) {
        tiles = j["tiles"].get<std::vector<int>>();
    } else {
        tiles.assign(width * height, -1);
    }
    if (tiles.size() != static_cast<size_t>(width * height)) {
        tiles.resize(width * height, -1);
    }
    if (j.contains("solidTiles") && j["solidTiles"].is_array()) {
        solidTiles = j["solidTiles"].get<std::vector<bool>>();
        if (solidTiles.size() < 256) {
            solidTiles.resize(256, false);
        }
    } else {
        solidTiles.assign(256, false);
    }
    if (j.contains("sortingOrder")) {
        sortingOrder = j["sortingOrder"];
    }
}

void TilemapRenderer::ResolveAssets() {
    if (spriteSheetPath.empty()) {
        spriteSheet.reset();
        return;
    }
    std::string abs = PathService::Get().ResolveAsset(spriteSheetPath);
    Texture* texture = TextureManager::Get().Load(abs);
    if (texture) {
        spriteSheet = std::make_unique<SpriteSheet>(texture, tileSize, tileSize);
    } else {
        spriteSheet.reset();
    }
}

void TilemapRenderer::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    namespace fs = std::filesystem;

    ImGui::Text("Tilemap Settings");
    ImGui::Separator();

    // SpriteSheet Path
    char pathBuffer[512];
    strncpy(pathBuffer, spriteSheetPath.c_str(), sizeof(pathBuffer) - 1);
    pathBuffer[sizeof(pathBuffer) - 1] = '\0';

    ImGui::SetNextItemWidth(-60);
    if (ImGui::InputText("SpriteSheet", pathBuffer, sizeof(pathBuffer))) {
        spriteSheetPath = pathBuffer;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        spriteSheetPath.clear();
        spriteSheet.reset();
    }

    // Drop target for texture/spritesheet
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PATH")) {
            const char* droppedPath = static_cast<const char*>(payload->Data);

            // Convert to relative path if in project
            std::string relativePath = droppedPath;
            if (Project::Get().IsOpen()) {
                relativePath = Project::Get().GetRelativePath(droppedPath);
            }
            spriteSheetPath = relativePath;
            ResolveAssets();
        }
        ImGui::EndDragDropTarget();
    }

    // Load button
    if (!spriteSheetPath.empty() && !spriteSheet) {
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            ResolveAssets();
        }
    }

    // Grid size input
    int currentWidth = width;
    int currentHeight = height;
    bool sizeChanged = false;
    if (ImGui::DragInt("Width", &currentWidth, 1.0f, 1, 1000)) {
        if (currentWidth < 1) currentWidth = 1;
        sizeChanged = true;
    }
    if (ImGui::DragInt("Height", &currentHeight, 1.0f, 1, 1000)) {
        if (currentHeight < 1) currentHeight = 1;
        sizeChanged = true;
    }

    if (sizeChanged && (currentWidth != width || currentHeight != height)) {
        std::vector<int> newTiles(currentWidth * currentHeight, -1);
        for (int y = 0; y < std::min(height, currentHeight); ++y) {
            for (int x = 0; x < std::min(width, currentWidth); ++x) {
                newTiles[y * currentWidth + x] = tiles[y * width + x];
            }
        }
        tiles = std::move(newTiles);
        width = currentWidth;
        height = currentHeight;
    }

    int size = tileSize;
    if (ImGui::DragInt("Tile Size", &size, 1.0f, 1, 256)) {
        if (size != tileSize) {
            tileSize = size;
            ResolveAssets();
        }
    }

    int order = sortingOrder;
    if (ImGui::InputInt("Sorting Order", &order)) {
        sortingOrder = order;
    }

    // Solid tiles list
    ImGui::Spacing();
    ImGui::Text("Solid Tiles (Collision Flags)");
    ImGui::Separator();

    int maxSolidTilesToShow = spriteSheet ? spriteSheet->GetFrameCount() : 64;
    maxSolidTilesToShow = std::min(maxSolidTilesToShow, 256);

    ImGui::BeginChild("SolidTilesList", ImVec2(0, 150), true);
    for (int i = 0; i < maxSolidTilesToShow; ++i) {
        bool solid = solidTiles[i];
        std::string label = "Tile " + std::to_string(i);
        if (ImGui::Checkbox(label.c_str(), &solid)) {
            solidTiles[i] = solid;
        }
        if ((i + 1) % 4 != 0 && i < maxSolidTilesToShow - 1) {
            ImGui::SameLine();
        }
    }
    ImGui::EndChild();
#endif
}
