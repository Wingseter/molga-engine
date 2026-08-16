#pragma once

#include "../Component.h"
#include "../../Common/Types.h"
#include "../../Core/TileSetAsset.h"
#include "../../Rendering/SpriteSheet.h"
#include "../../Rendering/RenderQueue.h"
#include "../../Rendering/LightingTypes2D.h"
#include "../../Rendering/WorldSort2D.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Renderer;
class Texture;

struct TilemapCell {
    int tileId = -1;
    int terrainId = -1;

    bool operator==(const TilemapCell& other) const {
        return tileId == other.tileId && terrainId == other.terrainId;
    }
    bool operator!=(const TilemapCell& other) const { return !(*this == other); }
};

struct TilemapLayer {
    std::string id;
    std::string name = "Layer";
    bool visible = true;
    bool locked = false;
    bool collisionEnabled = false;
    float opacity = 1.0f;
    int sortingOffset = 0;
    std::vector<TilemapCell> cells;
    std::uint64_t revision = 1;
};

struct TilemapCellEdit {
    int x = 0;
    int y = 0;
    TilemapCell value;
};

struct TilemapCollisionRun {
    int layerIndex = 0;
    int row = 0;
    int start = 0;
    int end = 0; // exclusive
};

class TilemapRenderer : public Component {
public:
    COMPONENT_TYPE(TilemapRenderer)
    static constexpr int ChunkSize = 32;

    TilemapRenderer();
    ~TilemapRenderer() override = default;

    // Legacy single-layer API remains source-compatible.
    void SetTile(int x, int y, int tileId);
    int GetTile(int x, int y) const;
    void SetSolid(int tileId, bool solid);
    bool IsSolid(int tileId) const;
    bool IsSolid(int x, int y) const;

    // Layered v2 authoring API.
    bool IsLayered() const { return layered_; }
    bool ConvertToLayered(const std::string& tileSetGuid);
    bool Resize(int newWidth, int newHeight);
    std::string AddLayer(const std::string& name);
    bool RemoveLayer(const std::string& layerId);
    bool MoveLayer(const std::string& layerId, int newIndex);
    TilemapLayer* GetLayer(const std::string& layerId);
    const TilemapLayer* GetLayer(const std::string& layerId) const;
    const std::vector<TilemapLayer>& GetLayers() const { return layers_; }
    const std::string& GetActiveLayerId() const { return activeLayerId_; }
    bool SetActiveLayer(const std::string& layerId);
    bool SetLayerName(const std::string& layerId, const std::string& name);
    bool SetLayerVisible(const std::string& layerId, bool visible);
    bool SetLayerLocked(const std::string& layerId, bool locked);
    bool SetLayerCollisionEnabled(const std::string& layerId, bool enabled);
    bool SetLayerOpacity(const std::string& layerId, float opacity);
    bool SetLayerSortingOffset(const std::string& layerId, int offset);

    TilemapCell GetCell(const std::string& layerId, int x, int y) const;
    bool SetCell(const std::string& layerId, int x, int y, TilemapCell value,
                 bool allowLocked = false);
    std::size_t ApplyCellEdits(const std::string& layerId,
                               const std::vector<TilemapCellEdit>& edits,
                               bool allowLocked = false);
    bool SetTerrain(const std::string& layerId, int x, int y, int terrainId);
    int TerrainMask(const std::string& layerId, int x, int y) const;

    Vector2 CellToWorld(int x, int y) const;
    bool WorldToCell(const Vector2& world, int& x, int& y) const;
    bool CanAuthor(std::string* warningOut = nullptr) const;

    void SetTileSetGuid(const std::string& guid);
    const std::string& GetTileSetGuid() const { return tileSetGuid_; }
    const molga::TileSetAsset* GetTileSet() const { return tileSet_.get(); }

    int GetSortingOrder() const { return sortingOrder; }
    void SetSortingOrder(int order) { sortingOrder = order; }
    void SetSortingLayer(const std::string& layer) { sortingLayer_ = layer; }
    const std::string& GetSortingLayer() const { return sortingLayer_; }

    SpriteLightingMode2D GetLightingMode() const { return lightingMode_; }
    void SetLightingMode(SpriteLightingMode2D mode) {
        lightingMode_ = mode == SpriteLightingMode2D::Lit
            ? SpriteLightingMode2D::Lit : SpriteLightingMode2D::Unlit;
    }

    std::vector<AABB> GetCollidingTiles(const AABB& worldBox) const;
    const std::vector<TilemapCollisionRun>& GetCollisionRuns() const;
    std::uint64_t GetCollisionRebuildCount() const { return collisionRebuildCount_; }

    void RenderSprite(Renderer* renderer) override;
    void CollectRender(molga::RenderQueue& queue) override;
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void ResolveAssets() override;
    void OnInspectorGUI() override;

    std::size_t GetLastSubmittedChunkCount() const { return lastSubmittedChunkCount_; }
    std::size_t GetChunkRebuildCount() const { return chunkRebuildCount_; }

    static nlohmann::json CanonicalizeSerializedData(
        const nlohmann::json& serialized);

    // Legacy serialized fields. They remain authoritative until explicit
    // ConvertToLayered() is invoked.
    std::string spriteSheetPath;
    int width = 10;
    int height = 10;
    int tileSize = 32;
    std::vector<int> tiles;
    std::vector<bool> solidTiles;
    int sortingOrder = 0;

private:
    std::string sortingLayer_ = "Default";
    SpriteLightingMode2D lightingMode_ = SpriteLightingMode2D::Unlit;

    struct GeometryGroup {
        Texture* texture = nullptr;
        std::shared_ptr<const std::vector<molga::Vertex2D>> vertices;
    };
    struct ChunkCache {
        AABB bounds;
        std::vector<GeometryGroup> groups;
        std::size_t quadCount = 0;
    };
    struct LayerCache {
        std::unordered_map<std::int64_t, ChunkCache> chunks;
        std::set<std::int64_t> dirty;
        float cachedOpacity = -1.0f;
    };

    bool InBounds(int x, int y) const;
    int CellIndex(int x, int y) const { return y * width + x; }
    void MarkCellDirty(TilemapLayer& layer, int x, int y);
    void MarkLayerChunksDirty(const std::string& layerId);
    void MarkAllChunksDirty();
    void ReevaluateTerrain(TilemapLayer& layer, int x, int y);
    bool CellSolid(const TilemapLayer& layer, int x, int y) const;
    static std::int64_t ChunkKey(int x, int y);
    static int ChunkX(std::int64_t key);
    static int ChunkY(std::int64_t key);
    AABB CalculateChunkBounds(int chunkX, int chunkY) const;
    const ChunkCache& BuildChunk(const TilemapLayer& layer, int chunkX, int chunkY) const;
    nlohmann::json EncodeLayerRle(const TilemapLayer& layer) const;
    static bool DecodeLayerRle(const nlohmann::json& json, TilemapLayer& layer,
                               int decodeWidth, int decodeHeight);

    bool layered_ = false;
    std::string tileSetGuid_;
    std::string activeLayerId_;
    std::vector<TilemapLayer> layers_;
    std::unique_ptr<molga::TileSetAsset> tileSet_;
    std::unordered_map<int, molga::ResolvedSprite> resolvedTiles_;

    std::unique_ptr<SpriteSheet> spriteSheet;
    Texture* legacyTexture_ = nullptr;

    mutable std::unordered_map<std::string, LayerCache> layerCaches_;
    mutable std::vector<TilemapCollisionRun> collisionRuns_;
    mutable std::unordered_map<std::string, std::vector<TilemapCollisionRun>> collisionLayerRuns_;
    mutable std::unordered_map<std::string, std::uint64_t> collisionRevisions_;
    mutable std::unordered_map<std::string, bool> collisionLayerEnabled_;
    mutable std::unordered_map<std::string, int> collisionLayerIndices_;
    std::uint64_t legacyRevision_ = 1;
    mutable std::uint64_t collisionRebuildCount_ = 0;
    mutable std::size_t chunkRebuildCount_ = 0;
    mutable std::size_t lastSubmittedChunkCount_ = 0;
    mutable Vector2 cachedWorldPosition_{9999999.0f, 9999999.0f};
    mutable Vector2 cachedWorldScale_{9999999.0f, 9999999.0f};
    mutable int cachedTileSize_ = -1;
};
