#include "TilemapRenderer.h"

#include "Transform.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "../../Common/Log.h"
#include "../../Core/AssetDatabase.h"
#include "../../Core/Guid.h"
#include "../../Core/PathService.h"
#include "../../Core/SpriteResolver.h"
#include "../../Core/TextureManager.h"
#include "../../Physics/Collision.h"
#include "../../Rendering/RenderQueue.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Sprite.h"
#include "../../Rendering/Texture.h"
#include "../../Rendering/WorldRenderTraversal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>

REGISTER_COMPONENT(TilemapRenderer)

namespace {

bool Near(float a, float b, float epsilon = 0.0001f) {
    return std::abs(a - b) <= epsilon;
}

void AppendQuad(std::vector<molga::Vertex2D>& vertices,
                float left, float top, float right, float bottom,
                const Frame& uv, float alpha) {
    vertices.push_back({left,  top,    uv.u0, uv.v1, 1.0f, 1.0f, 1.0f, alpha});
    vertices.push_back({right, top,    uv.u1, uv.v1, 1.0f, 1.0f, 1.0f, alpha});
    vertices.push_back({right, bottom, uv.u1, uv.v0, 1.0f, 1.0f, 1.0f, alpha});
    vertices.push_back({left,  bottom, uv.u0, uv.v0, 1.0f, 1.0f, 1.0f, alpha});
}

} // namespace

TilemapRenderer::TilemapRenderer() {
    tiles.assign(static_cast<std::size_t>(width * height), -1);
    solidTiles.assign(256, false);
}

bool TilemapRenderer::InBounds(int x, int y) const {
    return x >= 0 && x < width && y >= 0 && y < height;
}

void TilemapRenderer::SetTile(int x, int y, int tileId) {
    if (!InBounds(x, y)) return;
    if (layered_) {
        if (activeLayerId_.empty() && !layers_.empty()) activeLayerId_ = layers_.front().id;
        SetCell(activeLayerId_, x, y, {tileId, -1});
        return;
    }
    const int index = CellIndex(x, y);
    if (index < 0 || static_cast<std::size_t>(index) >= tiles.size() || tiles[index] == tileId) {
        return;
    }
    tiles[index] = tileId;
    ++legacyRevision_;
    MarkAllChunksDirty();
}

int TilemapRenderer::GetTile(int x, int y) const {
    if (!InBounds(x, y)) return -1;
    if (layered_) {
        const TilemapCell cell = GetCell(activeLayerId_, x, y);
        return cell.tileId;
    }
    const int index = CellIndex(x, y);
    return index >= 0 && static_cast<std::size_t>(index) < tiles.size()
        ? tiles[index] : -1;
}

void TilemapRenderer::SetSolid(int tileId, bool solid) {
    if (tileId < 0 || tileId > 1'048'575) return;
    if (static_cast<std::size_t>(tileId) >= solidTiles.size()) {
        solidTiles.resize(static_cast<std::size_t>(tileId + 1), false);
    }
    if (solidTiles[static_cast<std::size_t>(tileId)] == solid) return;
    solidTiles[static_cast<std::size_t>(tileId)] = solid;
    ++legacyRevision_;
}

bool TilemapRenderer::IsSolid(int tileId) const {
    if (layered_) {
        const molga::TileDefinition* tile = tileSet_ ? tileSet_->FindTile(tileId) : nullptr;
        return tile && tile->solid;
    }
    return tileId >= 0 && static_cast<std::size_t>(tileId) < solidTiles.size() &&
           solidTiles[static_cast<std::size_t>(tileId)];
}

bool TilemapRenderer::IsSolid(int x, int y) const {
    if (!InBounds(x, y)) return false;
    if (!layered_) return IsSolid(GetTile(x, y));
    for (const auto& layer : layers_) {
        if (CellSolid(layer, x, y)) return true;
    }
    return false;
}

bool TilemapRenderer::ConvertToLayered(const std::string& tileSetGuid) {
    const std::uint64_t cellCount = static_cast<std::uint64_t>(width) *
                                    static_cast<std::uint64_t>(height);
    if (layered_ || !molga::Guid::IsValid(tileSetGuid) || width <= 0 || height <= 0 ||
        cellCount > 16'777'216ULL) {
        return false;
    }
    TilemapLayer layer;
    layer.id = molga::Guid::Generate();
    layer.name = "Layer 1";
    layer.visible = true;
    layer.collisionEnabled = std::any_of(solidTiles.begin(), solidTiles.end(),
                                         [](bool value) { return value; });
    layer.cells.resize(static_cast<std::size_t>(cellCount));
    for (std::size_t index = 0; index < layer.cells.size() && index < tiles.size(); ++index) {
        layer.cells[index].tileId = tiles[index];
    }
    layered_ = true;
    tileSetGuid_ = tileSetGuid;
    activeLayerId_ = layer.id;
    layers_.push_back(std::move(layer));
    layerCaches_.clear();
    collisionRuns_.clear();
    collisionLayerRuns_.clear();
    collisionRevisions_.clear();
    collisionLayerEnabled_.clear();
    collisionLayerIndices_.clear();
    MarkAllChunksDirty();
    return true;
}

void TilemapRenderer::SetTileSetGuid(const std::string& guid) {
    if (tileSetGuid_ == guid) return;
    tileSetGuid_ = guid;
    tileSet_.reset();
    resolvedTiles_.clear();
    collisionRevisions_.clear();
    collisionLayerEnabled_.clear();
    collisionLayerIndices_.clear();
    MarkAllChunksDirty();
}

bool TilemapRenderer::Resize(int newWidth, int newHeight) {
    if (newWidth <= 0 || newHeight <= 0 || newWidth > 16384 || newHeight > 16384 ||
        static_cast<std::uint64_t>(newWidth) * static_cast<std::uint64_t>(newHeight) >
            16'777'216ULL ||
        (layered_ && static_cast<std::uint64_t>(newWidth) *
             static_cast<std::uint64_t>(newHeight) *
             static_cast<std::uint64_t>(std::max<std::size_t>(layers_.size(), 1)) >
             16'777'216ULL)) {
        return false;
    }
    if (newWidth == width && newHeight == height) return true;
    if (!layered_) {
        std::vector<int> resized(static_cast<std::size_t>(newWidth * newHeight), -1);
        for (int y = 0; y < std::min(height, newHeight); ++y) {
            for (int x = 0; x < std::min(width, newWidth); ++x) {
                const std::size_t oldIndex = static_cast<std::size_t>(y * width + x);
                if (oldIndex < tiles.size()) resized[static_cast<std::size_t>(y * newWidth + x)] = tiles[oldIndex];
            }
        }
        tiles = std::move(resized);
        ++legacyRevision_;
    } else {
        for (auto& layer : layers_) {
            std::vector<TilemapCell> resized(static_cast<std::size_t>(newWidth * newHeight));
            for (int y = 0; y < std::min(height, newHeight); ++y) {
                for (int x = 0; x < std::min(width, newWidth); ++x) {
                    const std::size_t oldIndex = static_cast<std::size_t>(y * width + x);
                    if (oldIndex < layer.cells.size()) {
                        resized[static_cast<std::size_t>(y * newWidth + x)] = layer.cells[oldIndex];
                    }
                }
            }
            layer.cells = std::move(resized);
            ++layer.revision;
        }
    }
    width = newWidth;
    height = newHeight;
    layerCaches_.clear();
    collisionRevisions_.clear();
    collisionLayerEnabled_.clear();
    collisionLayerIndices_.clear();
    MarkAllChunksDirty();
    return true;
}

std::string TilemapRenderer::AddLayer(const std::string& name) {
    constexpr std::uint64_t MaxStoredCells = 16'777'216ULL;
    if (!layered_ || layers_.size() >= 256 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) *
            static_cast<std::uint64_t>(layers_.size() + 1) > MaxStoredCells) {
        return {};
    }
    TilemapLayer layer;
    layer.id = molga::Guid::Generate();
    layer.name = name.empty() ? "Layer " + std::to_string(layers_.size() + 1) : name;
    layer.cells.resize(static_cast<std::size_t>(width * height));
    const std::string id = layer.id;
    layers_.push_back(std::move(layer));
    if (activeLayerId_.empty()) activeLayerId_ = id;
    MarkLayerChunksDirty(id);
    return id;
}

bool TilemapRenderer::RemoveLayer(const std::string& layerId) {
    if (!layered_ || layers_.size() <= 1) return false;
    const auto found = std::find_if(layers_.begin(), layers_.end(),
        [&](const TilemapLayer& layer) { return layer.id == layerId; });
    if (found == layers_.end()) return false;
    layers_.erase(found);
    layerCaches_.erase(layerId);
    collisionLayerRuns_.erase(layerId);
    collisionRevisions_.clear();
    collisionLayerEnabled_.clear();
    collisionLayerIndices_.clear();
    if (activeLayerId_ == layerId) activeLayerId_ = layers_.front().id;
    return true;
}

bool TilemapRenderer::MoveLayer(const std::string& layerId, int newIndex) {
    if (!layered_ || newIndex < 0 || newIndex >= static_cast<int>(layers_.size())) return false;
    const auto found = std::find_if(layers_.begin(), layers_.end(),
        [&](const TilemapLayer& layer) { return layer.id == layerId; });
    if (found == layers_.end()) return false;
    const int oldIndex = static_cast<int>(std::distance(layers_.begin(), found));
    if (oldIndex == newIndex) return true;
    TilemapLayer moved = std::move(*found);
    layers_.erase(found);
    layers_.insert(layers_.begin() + newIndex, std::move(moved));
    // Physics shape keys encode the current layer index. Force every layer's
    // horizontal runs to be republished after an ordering change.
    collisionRevisions_.clear();
    collisionLayerIndices_.clear();
    return true;
}

TilemapLayer* TilemapRenderer::GetLayer(const std::string& layerId) {
    const auto found = std::find_if(layers_.begin(), layers_.end(),
        [&](const TilemapLayer& layer) { return layer.id == layerId; });
    return found == layers_.end() ? nullptr : &*found;
}

const TilemapLayer* TilemapRenderer::GetLayer(const std::string& layerId) const {
    const auto found = std::find_if(layers_.begin(), layers_.end(),
        [&](const TilemapLayer& layer) { return layer.id == layerId; });
    return found == layers_.end() ? nullptr : &*found;
}

bool TilemapRenderer::SetActiveLayer(const std::string& layerId) {
    if (!GetLayer(layerId)) return false;
    activeLayerId_ = layerId;
    return true;
}

bool TilemapRenderer::SetLayerName(const std::string& layerId, const std::string& name) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!layer || name.empty() || layer->name == name) return false;
    layer->name = name;
    return true;
}

bool TilemapRenderer::SetLayerVisible(const std::string& layerId, bool visible) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!layer || layer->visible == visible) return false;
    layer->visible = visible;
    return true;
}

bool TilemapRenderer::SetLayerLocked(const std::string& layerId, bool locked) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!layer || layer->locked == locked) return false;
    layer->locked = locked;
    return true;
}

bool TilemapRenderer::SetLayerCollisionEnabled(const std::string& layerId, bool enabled) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!layer || layer->collisionEnabled == enabled) return false;
    layer->collisionEnabled = enabled;
    ++layer->revision;
    return true;
}

bool TilemapRenderer::SetLayerOpacity(const std::string& layerId, float opacity) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!std::isfinite(opacity)) opacity = 1.0f;
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (!layer || Near(layer->opacity, opacity)) return false;
    layer->opacity = opacity;
    MarkLayerChunksDirty(layerId);
    return true;
}

bool TilemapRenderer::SetLayerSortingOffset(const std::string& layerId, int offset) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!layer || layer->sortingOffset == offset) return false;
    layer->sortingOffset = offset;
    return true;
}

TilemapCell TilemapRenderer::GetCell(const std::string& layerId, int x, int y) const {
    const TilemapLayer* layer = GetLayer(layerId);
    if (!layer || !InBounds(x, y)) return {};
    const std::size_t index = static_cast<std::size_t>(CellIndex(x, y));
    return index < layer->cells.size() ? layer->cells[index] : TilemapCell{};
}

void TilemapRenderer::MarkCellDirty(TilemapLayer& layer, int x, int y) {
    ++layer.revision;
    layerCaches_[layer.id].dirty.insert(ChunkKey(x / ChunkSize, y / ChunkSize));
}

void TilemapRenderer::MarkLayerChunksDirty(const std::string& layerId) {
    if (layerId.empty()) return;
    const int chunksX = (width + ChunkSize - 1) / ChunkSize;
    const int chunksY = (height + ChunkSize - 1) / ChunkSize;
    auto& dirty = layerCaches_[layerId].dirty;
    for (int y = 0; y < chunksY; ++y) {
        for (int x = 0; x < chunksX; ++x) dirty.insert(ChunkKey(x, y));
    }
}

bool TilemapRenderer::SetCell(const std::string& layerId, int x, int y,
                              TilemapCell value, bool allowLocked) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!layer || !InBounds(x, y) || (layer->locked && !allowLocked)) return false;
    if (value.tileId < -1) value.tileId = -1;
    if (value.terrainId < -1) value.terrainId = -1;
    const std::size_t index = static_cast<std::size_t>(CellIndex(x, y));
    if (index >= layer->cells.size() || layer->cells[index] == value) return false;
    layer->cells[index] = value;
    MarkCellDirty(*layer, x, y);
    return true;
}

std::size_t TilemapRenderer::ApplyCellEdits(const std::string& layerId,
                                             const std::vector<TilemapCellEdit>& edits,
                                             bool allowLocked) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!layer || (layer->locked && !allowLocked)) return 0;
    std::size_t changed = 0;
    for (const auto& edit : edits) {
        if (!InBounds(edit.x, edit.y)) continue;
        TilemapCell value = edit.value;
        if (value.tileId < -1) value.tileId = -1;
        if (value.terrainId < -1) value.terrainId = -1;
        const std::size_t index = static_cast<std::size_t>(CellIndex(edit.x, edit.y));
        if (index >= layer->cells.size() || layer->cells[index] == value) continue;
        layer->cells[index] = value;
        layerCaches_[layer->id].dirty.insert(ChunkKey(edit.x / ChunkSize, edit.y / ChunkSize));
        ++changed;
    }
    if (changed != 0) ++layer->revision;
    return changed;
}

int TilemapRenderer::TerrainMask(const std::string& layerId, int x, int y) const {
    const TilemapLayer* layer = GetLayer(layerId);
    if (!layer || !InBounds(x, y)) return 0;
    const int terrain = GetCell(layerId, x, y).terrainId;
    if (terrain < 0) return 0;
    int mask = 0;
    if (y > 0 && GetCell(layerId, x, y - 1).terrainId == terrain) mask |= 1;
    if (x + 1 < width && GetCell(layerId, x + 1, y).terrainId == terrain) mask |= 2;
    if (y + 1 < height && GetCell(layerId, x, y + 1).terrainId == terrain) mask |= 4;
    if (x > 0 && GetCell(layerId, x - 1, y).terrainId == terrain) mask |= 8;
    return mask;
}

void TilemapRenderer::ReevaluateTerrain(TilemapLayer& layer, int x, int y) {
    if (!tileSet_ || !InBounds(x, y)) return;
    const std::size_t index = static_cast<std::size_t>(CellIndex(x, y));
    TilemapCell& cell = layer.cells[index];
    if (cell.terrainId < 0) return;
    const int resolved = tileSet_->ResolveTerrain(cell.terrainId,
                                                  TerrainMask(layer.id, x, y),
                                                  cell.tileId);
    if (resolved != cell.tileId) {
        cell.tileId = resolved;
        MarkCellDirty(layer, x, y);
    }
}

bool TilemapRenderer::SetTerrain(const std::string& layerId, int x, int y, int terrainId) {
    TilemapLayer* layer = GetLayer(layerId);
    if (!layer || layer->locked || !InBounds(x, y)) return false;
    if (terrainId < -1) terrainId = -1;
    TilemapCell cell = GetCell(layerId, x, y);
    if (cell.terrainId == terrainId) return false;
    cell.terrainId = terrainId;
    if (terrainId < 0) cell.tileId = -1;
    SetCell(layerId, x, y, cell);
    static const int offsets[5][2] = {{0, 0}, {0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    for (const auto& offset : offsets) ReevaluateTerrain(*layer, x + offset[0], y + offset[1]);
    return true;
}

Vector2 TilemapRenderer::CellToWorld(int x, int y) const {
    Vector2 position = Vector2::Zero();
    Vector2 scale = Vector2::One();
    if (gameObject) {
        if (const Transform* transform = gameObject->GetComponent<Transform>()) {
            position = transform->GetWorldPosition();
            scale = transform->GetWorldScale();
        }
    }
    return {position.x + static_cast<float>(x * tileSize) * scale.x,
            position.y + static_cast<float>(y * tileSize) * scale.y};
}

bool TilemapRenderer::WorldToCell(const Vector2& world, int& x, int& y) const {
    Vector2 position = Vector2::Zero();
    Vector2 scale = Vector2::One();
    if (gameObject) {
        if (const Transform* transform = gameObject->GetComponent<Transform>()) {
            position = transform->GetWorldPosition();
            scale = transform->GetWorldScale();
        }
    }
    if (scale.x <= 0.0f || scale.y <= 0.0f || tileSize <= 0) return false;
    x = static_cast<int>(std::floor((world.x - position.x) / (tileSize * scale.x)));
    y = static_cast<int>(std::floor((world.y - position.y) / (tileSize * scale.y)));
    return InBounds(x, y);
}

bool TilemapRenderer::CanAuthor(std::string* warningOut) const {
    if (!gameObject) return true;
    const Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return true;
    const Vector2 scale = transform->GetWorldScale();
    if (!Near(transform->GetWorldRotation(), 0.0f) || scale.x <= 0.0f || scale.y <= 0.0f ||
        !Near(scale.x, scale.y)) {
        if (warningOut) {
            *warningOut = "Tile painting requires rotation 0 and a positive uniform scale.";
        }
        return false;
    }
    if (warningOut) warningOut->clear();
    return true;
}

bool TilemapRenderer::CellSolid(const TilemapLayer& layer, int x, int y) const {
    if (!layer.collisionEnabled || !InBounds(x, y)) return false;
    const std::size_t index = static_cast<std::size_t>(CellIndex(x, y));
    if (index >= layer.cells.size()) return false;
    const molga::TileDefinition* tile = tileSet_ ? tileSet_->FindTile(layer.cells[index].tileId) : nullptr;
    return tile && tile->solid;
}

const std::vector<TilemapCollisionRun>& TilemapRenderer::GetCollisionRuns() const {
    if (!layered_) {
        const std::string key = "__legacy";
        if (collisionRevisions_[key] != legacyRevision_) {
            std::vector<TilemapCollisionRun> runs;
            for (int y = 0; y < height; ++y) {
                int x = 0;
                while (x < width) {
                    while (x < width && !IsSolid(GetTile(x, y))) ++x;
                    if (x >= width) break;
                    const int start = x;
                    while (x < width && IsSolid(GetTile(x, y))) ++x;
                    runs.push_back({0, y, start, x});
                }
            }
            collisionLayerRuns_[key] = std::move(runs);
            collisionRevisions_[key] = legacyRevision_;
            ++collisionRebuildCount_;
        }
    } else {
        for (int layerIndex = 0; layerIndex < static_cast<int>(layers_.size()); ++layerIndex) {
            const TilemapLayer& layer = layers_[static_cast<std::size_t>(layerIndex)];
            const auto revision = collisionRevisions_.find(layer.id);
            const auto enabled = collisionLayerEnabled_.find(layer.id);
            const auto index = collisionLayerIndices_.find(layer.id);
            if (revision != collisionRevisions_.end() &&
                revision->second == layer.revision &&
                enabled != collisionLayerEnabled_.end() &&
                enabled->second == layer.collisionEnabled &&
                index != collisionLayerIndices_.end() &&
                index->second == layerIndex) {
                continue;
            }
            std::vector<TilemapCollisionRun> runs;
            if (layer.collisionEnabled) {
                for (int y = 0; y < height; ++y) {
                    int x = 0;
                    while (x < width) {
                        while (x < width && !CellSolid(layer, x, y)) ++x;
                        if (x >= width) break;
                        const int start = x;
                        while (x < width && CellSolid(layer, x, y)) ++x;
                        runs.push_back({layerIndex, y, start, x});
                    }
                }
            }
            collisionLayerRuns_[layer.id] = std::move(runs);
            collisionRevisions_[layer.id] = layer.revision;
            collisionLayerEnabled_[layer.id] = layer.collisionEnabled;
            collisionLayerIndices_[layer.id] = layerIndex;
            ++collisionRebuildCount_;
        }
    }

    collisionRuns_.clear();
    if (!layered_) {
        collisionRuns_ = collisionLayerRuns_["__legacy"];
    } else {
        for (const auto& layer : layers_) {
            const auto found = collisionLayerRuns_.find(layer.id);
            if (found != collisionLayerRuns_.end()) {
                collisionRuns_.insert(collisionRuns_.end(), found->second.begin(), found->second.end());
            }
        }
    }
    return collisionRuns_;
}

std::vector<AABB> TilemapRenderer::GetCollidingTiles(const AABB& worldBox) const {
    std::vector<AABB> result;
    if (!gameObject) return result;
    Vector2 scale = Vector2::One();
    Vector2 position = Vector2::Zero();
    if (gameObject) {
        if (const Transform* transform = gameObject->GetComponent<Transform>()) {
            scale = transform->GetWorldScale();
            position = transform->GetWorldPosition();
        }
    }
    for (const auto& run : GetCollisionRuns()) {
        // Preserve the legacy query contract (one AABB per solid cell). The
        // physics backend consumes GetCollisionRuns() directly and still gets
        // the horizontally merged representation.
        for (int x = run.start; x < run.end; ++x) {
            AABB bounds{
                position.x + static_cast<float>(x * tileSize) * scale.x,
                position.y + static_cast<float>(run.row * tileSize) * scale.y,
                static_cast<float>(tileSize) * scale.x,
                static_cast<float>(tileSize) * scale.y
            };
            if (bounds.Intersects(worldBox)) result.push_back(bounds);
        }
    }
    return result;
}

std::int64_t TilemapRenderer::ChunkKey(int x, int y) {
    return (static_cast<std::int64_t>(y) << 32) |
           static_cast<std::uint32_t>(x);
}
int TilemapRenderer::ChunkX(std::int64_t key) { return static_cast<int>(key & 0xffffffff); }
int TilemapRenderer::ChunkY(std::int64_t key) { return static_cast<int>(key >> 32); }

void TilemapRenderer::MarkAllChunksDirty() {
    const int chunksX = (width + ChunkSize - 1) / ChunkSize;
    const int chunksY = (height + ChunkSize - 1) / ChunkSize;
    if (layered_) {
        for (const auto& layer : layers_) {
            MarkLayerChunksDirty(layer.id);
        }
    } else {
        auto& dirty = layerCaches_["__legacy"].dirty;
        for (int y = 0; y < chunksY; ++y)
            for (int x = 0; x < chunksX; ++x) dirty.insert(ChunkKey(x, y));
    }
}

AABB TilemapRenderer::CalculateChunkBounds(int chunkX, int chunkY) const {
    const Transform* transform = gameObject ? gameObject->GetComponent<Transform>() : nullptr;
    const Vector2 position = transform ? transform->GetWorldPosition() : Vector2::Zero();
    const Vector2 scale = transform ? transform->GetWorldScale() : Vector2::One();
    const int startX = chunkX * ChunkSize;
    const int startY = chunkY * ChunkSize;
    const int endX = std::min(width, startX + ChunkSize);
    const int endY = std::min(height, startY + ChunkSize);
    return {
        position.x + static_cast<float>(startX * tileSize) * scale.x,
        position.y + static_cast<float>(startY * tileSize) * scale.y,
        static_cast<float>((endX - startX) * tileSize) * scale.x,
        static_cast<float>((endY - startY) * tileSize) * scale.y
    };
}

const TilemapRenderer::ChunkCache& TilemapRenderer::BuildChunk(
    const TilemapLayer& layer, int chunkX, int chunkY) const {
    const std::string cacheId = layered_ ? layer.id : "__legacy";
    LayerCache& layerCache = layerCaches_[cacheId];
    const std::int64_t key = ChunkKey(chunkX, chunkY);
    auto existing = layerCache.chunks.find(key);
    if (existing != layerCache.chunks.end() && layerCache.dirty.count(key) == 0) {
        return existing->second;
    }

    ChunkCache rebuilt;
    std::map<Texture*, std::vector<molga::Vertex2D>> groups;
    const Transform* transform = gameObject ? gameObject->GetComponent<Transform>() : nullptr;
    const Vector2 position = transform ? transform->GetWorldPosition() : Vector2::Zero();
    const Vector2 scale = transform ? transform->GetWorldScale() : Vector2::One();
    const int startX = chunkX * ChunkSize;
    const int startY = chunkY * ChunkSize;
    const int endX = std::min(width, startX + ChunkSize);
    const int endY = std::min(height, startY + ChunkSize);
    rebuilt.bounds = CalculateChunkBounds(chunkX, chunkY);

    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            Texture* texture = nullptr;
            Frame uv;
            int tileId = -1;
            float alpha = layered_ ? std::clamp(layer.opacity, 0.0f, 1.0f) : 1.0f;
            if (layered_) {
                const std::size_t index = static_cast<std::size_t>(CellIndex(x, y));
                if (index >= layer.cells.size()) continue;
                tileId = layer.cells[index].tileId;
                const auto resolved = resolvedTiles_.find(tileId);
                if (resolved == resolvedTiles_.end() || !resolved->second.valid) continue;
                texture = resolved->second.texture;
                uv = resolved->second.uv;
            } else {
                tileId = GetTile(x, y);
                if (tileId < 0 || !spriteSheet || tileId >= spriteSheet->GetFrameCount()) continue;
                texture = legacyTexture_;
                uv = spriteSheet->GetFrame(tileId);
            }
            if (!texture || tileId < 0) continue;
            const float left = position.x + static_cast<float>(x * tileSize) * scale.x;
            const float top = position.y + static_cast<float>(y * tileSize) * scale.y;
            const float right = left + static_cast<float>(tileSize) * scale.x;
            const float bottom = top + static_cast<float>(tileSize) * scale.y;
            AppendQuad(groups[texture], left, top, right, bottom, uv, alpha);
            ++rebuilt.quadCount;
        }
    }
    for (auto& [texture, vertices] : groups) {
        rebuilt.groups.push_back({texture,
            std::make_shared<const std::vector<molga::Vertex2D>>(std::move(vertices))});
    }
    layerCache.dirty.erase(key);
    ++chunkRebuildCount_;
    auto inserted = layerCache.chunks.insert_or_assign(key, std::move(rebuilt));
    return inserted.first->second;
}

void TilemapRenderer::CollectRender(molga::RenderQueue& queue) {
    if (!gameObject || !enabled || width <= 0 || height <= 0 || tileSize <= 0) return;
    Transform* transform = gameObject->GetComponent<Transform>();
    const Vector2 position = transform ? transform->GetWorldPosition() : Vector2::Zero();
    const Vector2 scale = transform ? transform->GetWorldScale() : Vector2::One();
    if (position != cachedWorldPosition_ || scale != cachedWorldScale_ ||
        tileSize != cachedTileSize_) {
        cachedWorldPosition_ = position;
        cachedWorldScale_ = scale;
        cachedTileSize_ = tileSize;
        MarkAllChunksDirty();
    }

    lastSubmittedChunkCount_ = 0;
    const int chunksX = (width + ChunkSize - 1) / ChunkSize;
    const int chunksY = (height + ChunkSize - 1) / ChunkSize;
    const auto submitLayer = [&](const TilemapLayer& layer, int layerOrder) {
        if (layered_ && !layer.visible) return;
        if (layered_) {
            LayerCache& cache = layerCaches_[layer.id];
            const float opacity = std::clamp(layer.opacity, 0.0f, 1.0f);
            if (!Near(cache.cachedOpacity, opacity)) {
                MarkLayerChunksDirty(layer.id);
                cache.cachedOpacity = opacity;
            }
        }
        for (int chunkY = 0; chunkY < chunksY; ++chunkY) {
            for (int chunkX = 0; chunkX < chunksX; ++chunkX) {
                const AABB bounds = CalculateChunkBounds(chunkX, chunkY);
                if (queue.GetViewBounds() &&
                    !queue.GetViewBounds()->Intersects(bounds)) continue;
                const ChunkCache& chunk = BuildChunk(layer, chunkX, chunkY);
                if (chunk.quadCount == 0) continue;
                ++lastSubmittedChunkCount_;
                for (const auto& group : chunk.groups) {
                    molga::RenderCommand command;
                    molga::WorldSortSettings2D sortSettings;
                    sortSettings.sortingLayer = sortingLayer_;
                    sortSettings.sortingOrder = molga::ComposeWorldSortingOrder(
                        sortingOrder, layerOrder, layer.sortingOffset);
                    command.sortKey = molga::MakeWorldSortKey(sortSettings);
                    command.batchKey.texture = group.texture;
                    command.batchKey.blendMode = BlendMode::Alpha;
                    command.batchKey.isBatchable = true;
                    if (lightingMode_ == SpriteLightingMode2D::Lit) {
                        command.batchKey.lit = true;
                        command.batchKey.normalTexture = nullptr;
                        command.batchKey.normalStrength = 1.0f;
                        command.batchKey.receiverLayer =
                            static_cast<std::uint32_t>(
                                molga::NormalizeWorldRenderLayer(
                                    gameObject->GetLayer()));
                    }
                    command.geometry = group.vertices;
                    command.worldBounds = chunk.bounds;
                    queue.Submit(command);
                }
            }
        }
    };

    if (layered_) {
        for (int index = 0; index < static_cast<int>(layers_.size()); ++index) {
            submitLayer(layers_[static_cast<std::size_t>(index)], index);
        }
    } else {
        static const TilemapLayer legacyLayer{};
        submitLayer(legacyLayer, 0);
    }
}

void TilemapRenderer::RenderSprite(Renderer* renderer) {
    if (!renderer || layered_ || !spriteSheet || !gameObject || !enabled) return;
    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;
    const Vector2 position = transform->GetWorldPosition();
    const Vector2 scale = transform->GetWorldScale();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int tileId = GetTile(x, y);
            if (tileId < 0 || tileId >= spriteSheet->GetFrameCount()) continue;
            Sprite sprite;
            sprite.SetPosition(position.x + x * tileSize * scale.x,
                               position.y + y * tileSize * scale.y);
            sprite.SetSize(tileSize * scale.x, tileSize * scale.y);
            sprite.SetTexture(legacyTexture_);
            sprite.SetFrame(spriteSheet->GetFrame(tileId));
            renderer->DrawSprite(&sprite);
        }
    }
}

nlohmann::json TilemapRenderer::EncodeLayerRle(const TilemapLayer& layer) const {
    nlohmann::json result = nlohmann::json::array();
    if (width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) >
            16'777'216ULL) {
        return result;
    }
    const std::size_t expected = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height);
    if (expected == 0) return result;
    const auto cellAt = [&](std::size_t index) {
        return index < layer.cells.size() ? layer.cells[index] : TilemapCell{};
    };
    TilemapCell current = cellAt(0);
    int count = 0;
    auto append = [&]() { result.push_back({count, current.tileId, current.terrainId}); };
    for (std::size_t index = 0; index < expected; ++index) {
        const TilemapCell cell = cellAt(index);
        if (cell == current && count < std::numeric_limits<int>::max()) {
            ++count;
        } else {
            append();
            current = cell;
            count = 1;
        }
    }
    append();
    return result;
}

bool TilemapRenderer::DecodeLayerRle(const nlohmann::json& json, TilemapLayer& layer,
                                     int decodeWidth, int decodeHeight) {
    if (!json.is_array()) return false;
    layer.cells.clear();
    const std::size_t expected = static_cast<std::size_t>(decodeWidth) *
                                 static_cast<std::size_t>(decodeHeight);
    layer.cells.reserve(expected);
    try {
        for (const auto& run : json) {
            if (!run.is_array() || run.size() != 3 || !run[0].is_number_integer() ||
                !run[1].is_number_integer() || !run[2].is_number_integer()) {
                return false;
            }
            const int count = run[0].get<int>();
            if (count <= 0 ||
                static_cast<std::size_t>(count) > expected - layer.cells.size()) {
                return false;
            }
            TilemapCell cell{run[1].get<int>(), run[2].get<int>()};
            if (cell.tileId < -1 || cell.terrainId < -1) return false;
            layer.cells.insert(layer.cells.end(), static_cast<std::size_t>(count), cell);
        }
    } catch (const std::exception&) {
        return false;
    }
    return layer.cells.size() == expected;
}

void TilemapRenderer::Serialize(nlohmann::json& json) const {
    if (!layered_) {
        std::vector<int> serializedTiles;
        if (width > 0 && height > 0 &&
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <=
                16'777'216ULL) {
            const std::size_t expected = static_cast<std::size_t>(width) *
                                         static_cast<std::size_t>(height);
            serializedTiles.assign(expected, -1);
            std::copy_n(tiles.begin(), std::min(tiles.size(), serializedTiles.size()),
                        serializedTiles.begin());
        } else {
            serializedTiles.assign(tiles.begin(),
                tiles.begin() + static_cast<std::ptrdiff_t>(
                    std::min<std::size_t>(tiles.size(), 16'777'216)));
        }
        json["spriteSheetPath"] = spriteSheetPath;
        json["width"] = width;
        json["height"] = height;
        json["tileSize"] = tileSize;
        json["tiles"] = std::move(serializedTiles);
        json["solidTiles"] = solidTiles;
        json["sortingLayer"] = sortingLayer_;
        json["sortingOrder"] = sortingOrder;
        json["lightingMode"] = SpriteLightingMode2DName(lightingMode_);
        return;
    }
    json["schemaVersion"] = 2;
    json["width"] = width;
    json["height"] = height;
    json["tileSize"] = tileSize;
    json["tileSetGuid"] = tileSetGuid_;
    json["activeLayerId"] = activeLayerId_;
    json["sortingLayer"] = sortingLayer_;
    json["sortingOrder"] = sortingOrder;
    json["lightingMode"] = SpriteLightingMode2DName(lightingMode_);
    json["layers"] = nlohmann::json::array();
    for (const auto& layer : layers_) {
        json["layers"].push_back({
            {"id", layer.id}, {"name", layer.name}, {"visible", layer.visible},
            {"locked", layer.locked}, {"collision", layer.collisionEnabled},
            {"opacity", layer.opacity}, {"sortingOffset", layer.sortingOffset},
            {"rle", EncodeLayerRle(layer)}
        });
    }
}

/* static */ nlohmann::json TilemapRenderer::CanonicalizeSerializedData(
    const nlohmann::json& serialized) {
    nlohmann::json canonical = serialized.is_object()
        ? serialized : nlohmann::json::object();
    const auto mode = canonical.find("lightingMode");
    canonical["lightingMode"] =
        mode != canonical.end() && mode->is_string() &&
                mode->get<std::string>() == "Lit"
            ? "Lit" : "Unlit";
    return canonical;
}

void TilemapRenderer::Deserialize(const nlohmann::json& json) {
    constexpr std::uint64_t MaxStoredCells = 16'777'216ULL;
    try {
        if (!json.is_object()) throw std::runtime_error("tilemap data must be an object");
        const nlohmann::json canonical = CanonicalizeSerializedData(json);
        const SpriteLightingMode2D newLightingMode =
            SpriteLightingMode2DFromString(
                canonical["lightingMode"].get<std::string>());
        const int schemaVersion = json.value("schemaVersion", 1);
        if (schemaVersion != 1 && schemaVersion != 2) {
            throw std::runtime_error("unsupported TilemapRenderer schemaVersion");
        }

        const int newWidth = json.value("width", 10);
        const int newHeight = json.value("height", 10);
        const int newTileSize = json.value("tileSize", 32);
        const std::string newSortingLayer =
            json.contains("sortingLayer") && json["sortingLayer"].is_string()
                ? json["sortingLayer"].get<std::string>()
                : std::string("Default");
        const int newSortingOrder = json.value("sortingOrder", 0);
        const std::uint64_t cellCount = static_cast<std::uint64_t>(newWidth) *
                                        static_cast<std::uint64_t>(newHeight);
        if (newWidth <= 0 || newHeight <= 0 || newWidth > 16384 ||
            newHeight > 16384 || newTileSize <= 0 || newTileSize > 8192 ||
            cellCount > MaxStoredCells) {
            throw std::runtime_error("invalid tilemap dimensions");
        }

        const bool newLayered = schemaVersion == 2;
        std::string newSpriteSheetPath;
        std::vector<int> newTiles;
        std::vector<bool> newSolidTiles;
        std::string newTileSetGuid;
        std::string newActiveLayerId;
        std::vector<TilemapLayer> newLayers;

        if (!newLayered) {
            newSpriteSheetPath = json.value("spriteSheetPath", std::string{});
            newTiles.assign(static_cast<std::size_t>(cellCount), -1);
            if (json.contains("tiles")) {
                if (!json["tiles"].is_array()) {
                    throw std::runtime_error("legacy tiles must be an array");
                }
                const auto values = json["tiles"].get<std::vector<int>>();
                std::copy_n(values.begin(), std::min(values.size(), newTiles.size()),
                            newTiles.begin());
            }
            newSolidTiles.assign(256, false);
            if (json.contains("solidTiles")) {
                if (!json["solidTiles"].is_array() ||
                    json["solidTiles"].size() > 1'048'576) {
                    throw std::runtime_error("invalid legacy solidTiles array");
                }
                newSolidTiles = json["solidTiles"].get<std::vector<bool>>();
                if (newSolidTiles.size() < 256) newSolidTiles.resize(256, false);
            }
        } else {
            if (!json.contains("layers") || !json["layers"].is_array() ||
                json["layers"].size() > 256 ||
                cellCount * std::max<std::size_t>(json["layers"].size(), 1) >
                    MaxStoredCells) {
                throw std::runtime_error("invalid tilemap layers array");
            }
            newTileSetGuid = json.value("tileSetGuid", std::string{});
            newActiveLayerId = json.value("activeLayerId", std::string{});
            std::unordered_set<std::string> layerIds;
            for (const auto& value : json["layers"]) {
                if (!value.is_object()) throw std::runtime_error("layer must be an object");
                TilemapLayer layer;
                layer.id = value.value("id", std::string{});
                if (!molga::Guid::IsValid(layer.id) || !layerIds.insert(layer.id).second) {
                    do {
                        layer.id = molga::Guid::Generate();
                    } while (!layerIds.insert(layer.id).second);
                }
                layer.name = value.value("name", std::string("Layer"));
                if (layer.name.empty()) layer.name = "Layer";
                layer.visible = value.value("visible", true);
                layer.locked = value.value("locked", false);
                layer.collisionEnabled = value.value("collision", false);
                layer.opacity = std::clamp(value.value("opacity", 1.0f), 0.0f, 1.0f);
                layer.sortingOffset = value.value("sortingOffset", 0);
                if (!DecodeLayerRle(value.value("rle", nlohmann::json::array()),
                                    layer, newWidth, newHeight)) {
                    layer.cells.assign(static_cast<std::size_t>(cellCount), {});
                }
                newLayers.push_back(std::move(layer));
            }
            if (newLayers.empty()) {
                TilemapLayer layer;
                layer.id = molga::Guid::Generate();
                layer.name = "Layer 1";
                layer.cells.assign(static_cast<std::size_t>(cellCount), {});
                newLayers.push_back(std::move(layer));
            }
            const auto active = std::find_if(newLayers.begin(), newLayers.end(),
                [&](const TilemapLayer& layer) { return layer.id == newActiveLayerId; });
            if (active == newLayers.end()) newActiveLayerId = newLayers.front().id;
        }

        width = newWidth;
        height = newHeight;
        tileSize = newTileSize;
        sortingLayer_ = newSortingLayer;
        sortingOrder = newSortingOrder;
        lightingMode_ = newLightingMode;
        layered_ = newLayered;
        spriteSheetPath = std::move(newSpriteSheetPath);
        tiles = std::move(newTiles);
        solidTiles = std::move(newSolidTiles);
        tileSetGuid_ = std::move(newTileSetGuid);
        activeLayerId_ = std::move(newActiveLayerId);
        layers_ = std::move(newLayers);
        ++legacyRevision_;

        tileSet_.reset();
        resolvedTiles_.clear();
        spriteSheet.reset();
        legacyTexture_ = nullptr;
        layerCaches_.clear();
        collisionRuns_.clear();
        collisionLayerRuns_.clear();
        collisionRevisions_.clear();
        collisionLayerEnabled_.clear();
        collisionLayerIndices_.clear();
        MarkAllChunksDirty();
    } catch (const std::exception& error) {
        Log::Warn("TilemapRenderer", std::string("Deserialize failed: ") + error.what());
    }
}

void TilemapRenderer::ResolveAssets() {
    tileSet_.reset();
    resolvedTiles_.clear();
    spriteSheet.reset();
    legacyTexture_ = nullptr;
    collisionRevisions_.clear();
    collisionLayerEnabled_.clear();
    collisionLayerIndices_.clear();
    if (layered_) {
        const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(tileSetGuid_);
        if (!record || record->importer != "TileSetImporter" || record->importFailed) {
            Log::Warn("TilemapRenderer", "Missing or invalid TileSet GUID: " + tileSetGuid_);
            return;
        }
        auto tileSet = std::make_unique<molga::TileSetAsset>();
        std::string error;
        if (!tileSet->LoadFromFile(molga::AssetDatabase::Get().AbsoluteSourcePath(tileSetGuid_), &error)) {
            Log::Warn("TilemapRenderer", error);
            return;
        }
        for (const auto& tile : tileSet->tiles) {
            molga::ResolvedSprite sprite = molga::SpriteResolver::Resolve(tile.sprite);
            if (sprite.valid) resolvedTiles_[tile.id] = sprite;
        }
        tileSet_ = std::move(tileSet);
        // Terrain cells persist their stable terrain IDs. Re-resolve their
        // concrete tile IDs so a TileSet rule edit/reimport is reflected
        // without repainting the map.
        for (auto& layer : layers_) {
            bool changed = false;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(CellIndex(x, y));
                    if (index >= layer.cells.size() || layer.cells[index].terrainId < 0) continue;
                    TilemapCell& cell = layer.cells[index];
                    const int resolved = tileSet_->ResolveTerrain(
                        cell.terrainId, TerrainMask(layer.id, x, y), cell.tileId);
                    if (resolved != cell.tileId) {
                        cell.tileId = resolved;
                        changed = true;
                    }
                }
            }
            if (changed) ++layer.revision;
        }
    } else if (!spriteSheetPath.empty()) {
        const std::string absolute = PathService::Get().ResolveAsset(spriteSheetPath);
        legacyTexture_ = TextureManager::Get().Load(absolute, "TilemapRenderer");
        if (legacyTexture_) spriteSheet = std::make_unique<SpriteSheet>(legacyTexture_, tileSize, tileSize);
    }
    MarkAllChunksDirty();
}

void TilemapRenderer::OnInspectorGUI() {
    // P1 authoring lives in editor-side inspectors/windows. molga_core is built
    // without MOLGA_EDITOR, so keeping ImGui out of this component also avoids
    // a build-mode-dependent runtime contract.
}
