#pragma once

#include "Rendering/SpriteRef.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace molga {

struct TileDefinition {
    int id = -1;
    std::string name;
    SpriteRef sprite;
    bool solid = false;
    int terrainId = -1;
};

struct TerrainTileRule {
    int terrainId = -1;
    int mask = 0; // NESW bits: N=1, E=2, S=4, W=8.
    int tileId = -1;
};

class TileSetAsset {
public:
    int schemaVersion = 1;
    int cellWidth = 32;
    int cellHeight = 32;
    std::vector<TileDefinition> tiles;
    std::vector<TerrainTileRule> terrainRules;

    const TileDefinition* FindTile(int id) const;
    int ResolveTerrain(int terrainId, int mask, int fallback = -1) const;
    bool Validate(std::string* errorOut = nullptr) const;

    nlohmann::json Serialize() const;
    bool Deserialize(const nlohmann::json& json, std::string* errorOut = nullptr);
    bool LoadFromFile(const std::filesystem::path& path,
                      std::string* errorOut = nullptr);
    bool SaveToFile(const std::filesystem::path& path,
                    std::string* errorOut = nullptr) const;
};

} // namespace molga
