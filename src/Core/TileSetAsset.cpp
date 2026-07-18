#include "Core/TileSetAsset.h"

#include "Core/Guid.h"
#include "Core/PersistentStorage.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <unordered_set>

namespace molga {

const TileDefinition* TileSetAsset::FindTile(int id) const {
    const auto found = std::find_if(tiles.begin(), tiles.end(),
        [&](const TileDefinition& tile) { return tile.id == id; });
    return found == tiles.end() ? nullptr : &*found;
}

int TileSetAsset::ResolveTerrain(int terrainId, int mask, int fallback) const {
    mask &= 0xF;
    const auto found = std::find_if(terrainRules.begin(), terrainRules.end(),
        [&](const TerrainTileRule& rule) {
            return rule.terrainId == terrainId && rule.mask == mask;
        });
    return found == terrainRules.end() ? fallback : found->tileId;
}

bool TileSetAsset::Validate(std::string* errorOut) const {
    const auto fail = [&](const std::string& message) {
        if (errorOut) *errorOut = message;
        return false;
    };
    if (schemaVersion != 1) return fail("unsupported TileSet schemaVersion");
    if (cellWidth <= 0 || cellHeight <= 0 || cellWidth > 8192 || cellHeight > 8192) {
        return fail("invalid cellSize");
    }

    std::unordered_set<int> ids;
    for (const auto& tile : tiles) {
        if (tile.id < 0 || tile.terrainId < -1 ||
            !Guid::IsValid(tile.sprite.textureGuid) ||
            (!tile.sprite.sliceId.empty() && !Guid::IsValid(tile.sprite.sliceId)) ||
            !ids.insert(tile.id).second) {
            return fail("tile IDs must be unique and sprites must be valid");
        }
    }
    std::set<std::pair<int, int>> rules;
    for (const auto& rule : terrainRules) {
        if (rule.terrainId < 0 || rule.mask < 0 || rule.mask > 15 ||
            !ids.count(rule.tileId) ||
            !rules.insert({rule.terrainId, rule.mask}).second) {
            return fail("invalid or duplicate terrain rule");
        }
    }
    if (errorOut) errorOut->clear();
    return true;
}

nlohmann::json TileSetAsset::Serialize() const {
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["cellSize"] = {cellWidth, cellHeight};
    json["tiles"] = nlohmann::json::array();
    for (const auto& tile : tiles) {
        json["tiles"].push_back({
            {"id", tile.id}, {"name", tile.name},
            {"sprite", SerializeSpriteRef(tile.sprite)},
            {"solid", tile.solid}, {"terrainId", tile.terrainId}
        });
    }
    json["terrainRules"] = nlohmann::json::array();
    for (const auto& rule : terrainRules) {
        json["terrainRules"].push_back({
            {"terrainId", rule.terrainId}, {"mask", rule.mask & 0xF},
            {"tileId", rule.tileId}
        });
    }
    return json;
}

bool TileSetAsset::Deserialize(const nlohmann::json& json, std::string* errorOut) {
    try {
        if (!json.is_object() || json.value("schemaVersion", 0) != 1) {
            throw std::runtime_error("unsupported TileSet schemaVersion");
        }
        int newWidth = 32;
        int newHeight = 32;
        if (json.contains("cellSize") && json["cellSize"].is_array() &&
            json["cellSize"].size() >= 2) {
            newWidth = json["cellSize"][0].get<int>();
            newHeight = json["cellSize"][1].get<int>();
        }
        if (newWidth <= 0 || newHeight <= 0 || newWidth > 8192 || newHeight > 8192) {
            throw std::runtime_error("invalid cellSize");
        }

        std::vector<TileDefinition> newTiles;
        std::unordered_set<int> ids;
        if (!json.contains("tiles") || !json["tiles"].is_array()) {
            throw std::runtime_error("tiles array is required");
        }
        for (const auto& value : json["tiles"]) {
            TileDefinition tile;
            tile.id = value.value("id", -1);
            tile.name = value.value("name", std::string{});
            tile.sprite = DeserializeSpriteRef(value.value("sprite", nlohmann::json::object()));
            tile.solid = value.value("solid", false);
            tile.terrainId = value.value("terrainId", -1);
            if (tile.id < 0 || tile.terrainId < -1 ||
                !Guid::IsValid(tile.sprite.textureGuid) ||
                (!tile.sprite.sliceId.empty() && !Guid::IsValid(tile.sprite.sliceId)) ||
                !ids.insert(tile.id).second) {
                throw std::runtime_error("tile IDs must be unique and sprites must be valid");
            }
            newTiles.push_back(std::move(tile));
        }

        std::vector<TerrainTileRule> newRules;
        std::set<std::pair<int, int>> rules;
        if (json.contains("terrainRules")) {
            if (!json["terrainRules"].is_array()) {
                throw std::runtime_error("terrainRules must be an array");
            }
            for (const auto& value : json["terrainRules"]) {
                TerrainTileRule rule;
                rule.terrainId = value.value("terrainId", -1);
                rule.mask = value.value("mask", -1);
                rule.tileId = value.value("tileId", -1);
                if (rule.terrainId < 0 || rule.mask < 0 || rule.mask > 15 ||
                    !ids.count(rule.tileId) ||
                    !rules.insert({rule.terrainId, rule.mask}).second) {
                    throw std::runtime_error("invalid or duplicate terrain rule");
                }
                newRules.push_back(rule);
            }
        }

        schemaVersion = 1;
        cellWidth = newWidth;
        cellHeight = newHeight;
        tiles = std::move(newTiles);
        terrainRules = std::move(newRules);
        if (errorOut) errorOut->clear();
        return true;
    } catch (const std::exception& error) {
        if (errorOut) *errorOut = error.what();
        return false;
    }
}

bool TileSetAsset::LoadFromFile(const std::filesystem::path& path,
                                std::string* errorOut) {
    try {
        std::ifstream file(path);
        if (!file) throw std::runtime_error("could not open TileSet: " + path.string());
        nlohmann::json json;
        file >> json;
        return Deserialize(json, errorOut);
    } catch (const std::exception& error) {
        if (errorOut) *errorOut = error.what();
        return false;
    }
}

bool TileSetAsset::SaveToFile(const std::filesystem::path& path,
                              std::string* errorOut) const {
    if (!Validate(errorOut)) return false;
    return PersistentStorage::AtomicWriteText(path, Serialize().dump(2), errorOut);
}

} // namespace molga
