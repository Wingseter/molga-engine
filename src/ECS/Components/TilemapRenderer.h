#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Rendering/SpriteSheet.h"
#include <string>
#include <vector>
#include <memory>

class Renderer;

class TilemapRenderer : public Component {
public:
    COMPONENT_TYPE(TilemapRenderer)

    TilemapRenderer();
    virtual ~TilemapRenderer() = default;

    // Tile editing
    void SetTile(int x, int y, int tileId);
    int GetTile(int x, int y) const;

    // Collision settings
    void SetSolid(int tileId, bool solid);
    bool IsSolid(int tileId) const;
    bool IsSolid(int x, int y) const; // Helper for coordinate-based query

    // Sorting order
    int GetSortingOrder() const { return sortingOrder; }
    void SetSortingOrder(int order) { sortingOrder = order; }

    // Collision queries
    std::vector<AABB> GetCollidingTiles(const AABB& worldBox) const;

    // Rendering
    void RenderSprite(Renderer* renderer) override;
    void CollectRender(molga::RenderQueue& queue) override;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void ResolveAssets() override;

    // Editor GUI
    void OnInspectorGUI() override;

    // Public fields
    std::string spriteSheetPath;
    int width = 10;
    int height = 10;
    int tileSize = 32;
    std::vector<int> tiles;
    std::vector<bool> solidTiles; // size 256 for collision flags
    int sortingOrder = 0;

private:
    std::unique_ptr<SpriteSheet> spriteSheet;
};
