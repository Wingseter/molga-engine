#include "Core/World.h"
#include "Core/AssetDatabase.h"
#include "Core/Guid.h"
#include "Core/TileSetAsset.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/TilemapRenderer.h"
#include "ECS/Components/Rigidbody2D.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/CircleCollider2D.h"
#include "Physics/Collision.h"
#include "Physics/Physics2D.h"
#include "Physics/PhysicsWorld.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/SpriteBatcher.h"
#include "doctest.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
#include <iostream>
#include <stdexcept>

namespace {

constexpr const char* TestTextureGuid = "11111111111111111111111111111111";
constexpr int TestTerrainId = 7;

molga::TileSetAsset MakeTerrainTileSet(bool solid = true) {
    molga::TileSetAsset tileSet;
    tileSet.cellWidth = 32;
    tileSet.cellHeight = 32;
    for (int mask = 0; mask < 16; ++mask) {
        molga::TileDefinition tile;
        tile.id = 100 + mask;
        tile.name = "terrain_" + std::to_string(mask);
        tile.sprite.textureGuid = TestTextureGuid;
        tile.solid = solid;
        tile.terrainId = TestTerrainId;
        tileSet.tiles.push_back(tile);
        tileSet.terrainRules.push_back({TestTerrainId, mask, tile.id});
    }
    return tileSet;
}

class ScopedTileSetFixture {
public:
    explicit ScopedTileSetFixture(const molga::TileSetAsset& tileSet) {
        root = std::filesystem::temp_directory_path() /
               ("molga-tileset-" + molga::Guid::Generate());
        const std::filesystem::path assets = root / "Assets";
        std::filesystem::create_directories(assets);
        std::string error;
        if (!tileSet.SaveToFile(assets / "terrain.tileset", &error)) {
            throw std::runtime_error(error);
        }
        molga::AssetDatabase::Get().Clear();
        molga::AssetDatabase::Get().ScanProject(assets);
        guid = molga::AssetDatabase::Get().GuidForSource("terrain.tileset");
        if (!molga::Guid::IsValid(guid)) {
            throw std::runtime_error("TileSet fixture did not receive an asset GUID");
        }
    }

    ~ScopedTileSetFixture() {
        molga::AssetDatabase::Get().Clear();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    std::filesystem::path root;
    std::string guid;
};

} // namespace

TEST_CASE("Tilemap: Properties and Tile Editing") {
    auto go = std::make_shared<GameObject>("TilemapObject");
    auto* tm = go->AddComponent<TilemapRenderer>();

    CHECK(tm->width == 10);
    CHECK(tm->height == 10);
    CHECK(tm->tileSize == 32);
    CHECK(tm->GetSortingOrder() == 0);

    // Set sorting order
    tm->SetSortingOrder(5);
    CHECK(tm->GetSortingOrder() == 5);

    // Edit tiles in bounds
    tm->SetTile(0, 0, 1);
    tm->SetTile(5, 5, 2);
    CHECK(tm->GetTile(0, 0) == 1);
    CHECK(tm->GetTile(5, 5) == 2);
    CHECK(tm->GetTile(3, 3) == -1); // Unset tiles defaults to -1

    // Edit tiles out of bounds
    tm->SetTile(-1, 0, 5);
    tm->SetTile(10, 10, 5);
    CHECK(tm->GetTile(-1, 0) == -1);
    CHECK(tm->GetTile(10, 10) == -1);

    // Solid flags
    tm->SetSolid(1, true);
    CHECK(tm->IsSolid(1) == true);
    CHECK(tm->IsSolid(2) == false);

    // Coordinates solid check
    CHECK(tm->IsSolid(0, 0) == true);
    CHECK(tm->IsSolid(5, 5) == false);
}

TEST_CASE("Tilemap: Local-to-World bounds conversions") {
    auto go = std::make_shared<GameObject>("TilemapObject");
    auto* trans = go->AddComponent<Transform>();
    trans->SetPosition(100.0f, 200.0f); // Non-zero position

    auto* tm = go->AddComponent<TilemapRenderer>();
    tm->tileSize = 32;
    tm->SetTile(1, 1, 10);
    tm->SetSolid(10, true); // Tile at local (32, 32) -> world (132, 232) is solid

    // World AABB that overlaps the solid tile
    // World space solid tile bounds: x = 132, y = 232, size = 32x32
    AABB worldQuery(140.0f, 240.0f, 10.0f, 10.0f);

    std::vector<AABB> colliding = tm->GetCollidingTiles(worldQuery);
    REQUIRE(colliding.size() == 1);
    CHECK(colliding[0].x == 132.0f);
    CHECK(colliding[0].y == 232.0f);
    CHECK(colliding[0].width == 32.0f);
    CHECK(colliding[0].height == 32.0f);

    // World AABB that does NOT overlap
    AABB nonOverlappingQuery(0.0f, 0.0f, 10.0f, 10.0f);
    colliding = tm->GetCollidingTiles(nonOverlappingQuery);
    CHECK(colliding.empty());
}

TEST_CASE("Tilemap: Serialization and Deserialization") {
    auto go = std::make_shared<GameObject>("TilemapObject");
    auto* tm = go->AddComponent<TilemapRenderer>();
    tm->width = 3;
    tm->height = 3;
    tm->tileSize = 16;
    tm->spriteSheetPath = "assets/textures/tiles.png";
    tm->SetSortingOrder(10);
    tm->SetTile(1, 1, 42);
    tm->SetSolid(42, true);

    nlohmann::json j;
    tm->Serialize(j);

    // Check JSON contents
    CHECK(j["width"] == 3);
    CHECK(j["height"] == 3);
    CHECK(j["tileSize"] == 16);
    CHECK(j["spriteSheetPath"] == "assets/textures/tiles.png");
    CHECK(j["sortingOrder"] == 10);
    CHECK(j["tiles"][4] == 42); // (1, 1) in 3x3 grid is index 4
    CHECK(j["solidTiles"][42] == true);

    // Deserialize into another component
    auto go2 = std::make_shared<GameObject>("TilemapObject2");
    auto* tm2 = go2->AddComponent<TilemapRenderer>();
    tm2->Deserialize(j);

    CHECK(tm2->width == 3);
    CHECK(tm2->height == 3);
    CHECK(tm2->tileSize == 16);
    CHECK(tm2->spriteSheetPath == "assets/textures/tiles.png");
    CHECK(tm2->GetSortingOrder() == 10);
    CHECK(tm2->GetTile(1, 1) == 42);
    CHECK(tm2->IsSolid(42) == true);
    CHECK(tm2->IsSolid(0) == false);
}

TEST_CASE("Tilemap: Physics collision resolution with dynamic colliders") {
    World world;

    // 1. Static Tilemap at (0.0f, 100.0f)
    auto goTilemap = std::make_shared<GameObject>("TilemapPlatform");
    auto* transTilemap = goTilemap->AddComponent<Transform>();
    transTilemap->SetPosition(0.0f, 100.0f);

    auto* tm = goTilemap->AddComponent<TilemapRenderer>();
    tm->width = 5;
    tm->height = 5;
    tm->tileSize = 32;
    tm->SetTile(0, 0, 1);
    tm->SetSolid(1, true); // Solid tile at local (0, 0) -> world (0.0f, 100.0f) to (32.0f, 132.0f)
    world.Add(goTilemap);

    // 2. Falling Box Collider 2D
    auto goBox = std::make_shared<GameObject>("FallingBox");
    auto* transBox = goBox->AddComponent<Transform>();
    // Starts centered over the tile, y coordinate makes it overlap slightly in the next step
    transBox->SetPosition(16.0f, 80.0f); // 80 + 20 (size) = 100.0f world bottom, exactly touching the platform
    
    auto* colBox = goBox->AddComponent<BoxCollider2D>();
    colBox->SetSize(20.0f, 20.0f);
    colBox->SetOffset(0.0f, 0.0f);

    auto* rbBox = goBox->AddComponent<Rigidbody2D>();
    rbBox->SetBodyType(Rigidbody2D::BodyType::Dynamic);
    rbBox->SetMass(1.0f);
    rbBox->SetGravityScale(1.0f); // Fall down
    world.Add(goBox);

    // Run physics simulation steps
    // Gravity should push it into the platform, and resolution should push it back up to remain above y = 100 - size = 80
    // Wait, let's step it
    for (int i = 0; i < 10; ++i) {
        world.FixedStep(0.016f);
    }

    // After simulation, Box position Y should be <= 80.0f (recovering/staying above the solid tile)
    // and definitely not falling below 80.0f (since gravity would pull it down past 100.0f without collision)
    // Let's verify this!
    CHECK(transBox->GetPosition().y <= 80.0f + 1.0f);
    CHECK(transBox->GetPosition().y >= 79.0f);
}

TEST_CASE("Tilemap: Box2D merges horizontal solid runs and updates them incrementally") {
    World world;
    auto object = std::make_shared<GameObject>("MergedTilemap");
    object->AddComponent<Transform>()->SetPosition(100.0f, 200.0f);
    auto* tilemap = object->AddComponent<TilemapRenderer>();
    tilemap->width = 5;
    tilemap->height = 2;
    tilemap->tileSize = 16;
    tilemap->tiles.assign(10, -1);
    tilemap->SetSolid(1, true);
    tilemap->SetTile(0, 0, 1);
    tilemap->SetTile(1, 0, 1);
    tilemap->SetTile(2, 0, 1); // first run
    tilemap->SetTile(4, 0, 1); // second run
    tilemap->SetTile(0, 1, 1);
    tilemap->SetTile(1, 1, 1); // third run
    world.Add(object);

    world.FixedStep(0.0f);
    REQUIRE(world.GetPhysicsWorld() != nullptr);
    CHECK(world.GetPhysicsWorld()->BodyCount() == 1);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 3);
    CHECK(Physics2D::OverlapPoint(world, {108.0f, 208.0f}) == object.get());
    CHECK(Physics2D::OverlapPoint(world, {156.0f, 208.0f}) == nullptr);
    CHECK(Physics2D::OverlapPoint(world, {172.0f, 208.0f}) == object.get());

    tilemap->SetTile(3, 0, 1); // joins both row-zero runs
    world.FixedStep(0.0f);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 2);
    CHECK(Physics2D::OverlapPoint(world, {156.0f, 208.0f}) == object.get());
}

TEST_CASE("TileSet: all NESW terrain masks validate and round-trip transactionally") {
    molga::TileSetAsset tileSet = MakeTerrainTileSet();
    std::string error;
    REQUIRE(tileSet.Validate(&error));
    CHECK(error.empty());
    for (int mask = 0; mask < 16; ++mask) {
        CHECK(tileSet.ResolveTerrain(TestTerrainId, mask, -1) == 100 + mask);
        // Inputs outside four bits are intentionally masked by the runtime.
        CHECK(tileSet.ResolveTerrain(TestTerrainId, mask | 0x30, -1) == 100 + mask);
    }
    CHECK(tileSet.ResolveTerrain(999, 0, 42) == 42);

    const nlohmann::json document = tileSet.Serialize();
    CHECK(document["schemaVersion"] == 1);
    CHECK(document["terrainRules"].size() == 16);
    molga::TileSetAsset restored;
    REQUIRE(restored.Deserialize(document, &error));
    REQUIRE(restored.Validate(&error));
    CHECK(restored.cellWidth == 32);
    CHECK(restored.tiles.size() == 16);

    nlohmann::json duplicate = document;
    duplicate["terrainRules"].push_back(duplicate["terrainRules"][0]);
    CHECK_FALSE(restored.Deserialize(duplicate, &error));
    CHECK_FALSE(error.empty());
    CHECK(restored.terrainRules.size() == 16); // failed load is transactional

    molga::TileSetAsset invalid = tileSet;
    invalid.terrainRules[0].mask = 16;
    CHECK_FALSE(invalid.Validate(&error));
    const auto destination = std::filesystem::temp_directory_path() /
                             ("invalid-" + molga::Guid::Generate() + ".tileset");
    CHECK_FALSE(invalid.SaveToFile(destination, &error));
    CHECK_FALSE(std::filesystem::exists(destination));
}

TEST_CASE("Tilemap v2: layer properties, locked edits, RLE, move, resize, and removal round-trip") {
    auto object = std::make_shared<GameObject>("LayeredTilemap");
    auto* transform = object->AddComponent<Transform>();
    transform->SetPosition(10.0f, 20.0f);
    transform->SetScale(2.0f);
    auto* tilemap = object->AddComponent<TilemapRenderer>();
    REQUIRE(tilemap->Resize(4, 3));
    tilemap->SetTile(1, 1, 9);
    CHECK_FALSE(tilemap->IsLayered());

    const std::string tileSetGuid = "22222222222222222222222222222222";
    REQUIRE(tilemap->ConvertToLayered(tileSetGuid));
    CHECK_FALSE(tilemap->ConvertToLayered(tileSetGuid));
    const std::string baseId = tilemap->GetActiveLayerId();
    REQUIRE(molga::Guid::IsValid(baseId));
    CHECK(tilemap->GetCell(baseId, 1, 1).tileId == 9);

    CHECK(tilemap->SetLayerName(baseId, "Background"));
    CHECK(tilemap->SetLayerOpacity(baseId, 0.25f));
    CHECK(tilemap->SetLayerSortingOffset(baseId, -2));
    CHECK(tilemap->SetLayerCollisionEnabled(baseId, true));
    CHECK(tilemap->SetCell(baseId, 2, 1, {12, -1}));
    const std::uint64_t beforeSparseEdit = tilemap->GetLayer(baseId)->revision;
    CHECK(tilemap->ApplyCellEdits(baseId, {
        {0, 0, {7, -1}}, {3, 2, {8, -1}}, {99, 99, {9, -1}}
    }) == 2);
    CHECK(tilemap->GetLayer(baseId)->revision == beforeSparseEdit + 1);
    CHECK(tilemap->ApplyCellEdits(baseId, {
        {0, 0, {-1, -1}}, {3, 2, {-1, -1}}
    }) == 2);

    const std::string foregroundId = tilemap->AddLayer("Foreground");
    REQUIRE(molga::Guid::IsValid(foregroundId));
    REQUIRE(tilemap->SetActiveLayer(foregroundId));
    CHECK(tilemap->SetLayerVisible(foregroundId, false));
    CHECK(tilemap->SetLayerLocked(foregroundId, true));
    CHECK_FALSE(tilemap->SetCell(foregroundId, 0, 0, {4, -1}));
    CHECK(tilemap->ApplyCellEdits(foregroundId, {{0, 0, {4, -1}}}, true) == 1);
    REQUIRE(tilemap->MoveLayer(foregroundId, 0));
    CHECK(tilemap->GetLayers()[0].id == foregroundId);

    int cellX = -1;
    int cellY = -1;
    const Vector2 world = tilemap->CellToWorld(1, 2);
    CHECK(world.x == doctest::Approx(74.0f));
    CHECK(world.y == doctest::Approx(148.0f));
    REQUIRE(tilemap->WorldToCell({world.x + 1.0f, world.y + 1.0f}, cellX, cellY));
    CHECK(cellX == 1);
    CHECK(cellY == 2);
    std::string warning;
    CHECK(tilemap->CanAuthor(&warning));
    transform->SetScale(2.0f, 1.0f);
    CHECK_FALSE(tilemap->CanAuthor(&warning));
    CHECK_FALSE(warning.empty());
    transform->SetScale(2.0f);
    transform->SetRotation(5.0f);
    CHECK_FALSE(tilemap->CanAuthor(&warning));
    transform->SetRotation(0.0f);

    nlohmann::json document;
    tilemap->Serialize(document);
    CHECK(document["schemaVersion"] == 2);
    CHECK(document["tileSetGuid"] == tileSetGuid);
    REQUIRE(document["layers"].size() == 2);
    for (const auto& layer : document["layers"]) {
        std::size_t decodedCount = 0;
        for (const auto& run : layer["rle"]) decodedCount += run[0].get<std::size_t>();
        CHECK(decodedCount == 12);
    }

    auto restoredObject = std::make_shared<GameObject>("RestoredLayeredTilemap");
    auto* restored = restoredObject->AddComponent<TilemapRenderer>();
    restored->Deserialize(document);
    CHECK(restored->IsLayered());
    CHECK(restored->GetLayers().size() == 2);
    CHECK(restored->GetLayers()[0].id == foregroundId);
    CHECK(restored->GetActiveLayerId() == foregroundId);
    REQUIRE(restored->GetLayer(baseId) != nullptr);
    CHECK(restored->GetLayer(baseId)->name == "Background");
    CHECK(restored->GetLayer(baseId)->opacity == doctest::Approx(0.25f));
    CHECK(restored->GetLayer(baseId)->sortingOffset == -2);
    CHECK(restored->GetCell(baseId, 1, 1).tileId == 9);
    CHECK(restored->GetCell(foregroundId, 0, 0).tileId == 4);

    nlohmann::json malformedRle = document;
    malformedRle["layers"][0]["rle"] = {{13, 4, -1}};
    restored->Deserialize(malformedRle);
    CHECK(restored->GetCell(foregroundId, 0, 0).tileId == -1);
    restored->Deserialize(document);

    REQUIRE(restored->Resize(6, 4));
    CHECK(restored->GetCell(baseId, 1, 1).tileId == 9);
    CHECK(restored->GetCell(baseId, 5, 3).tileId == -1);
    REQUIRE(restored->Resize(2, 2));
    CHECK(restored->GetCell(baseId, 1, 1).tileId == 9);
    CHECK(restored->RemoveLayer(foregroundId));
    CHECK(restored->GetLayers().size() == 1);
    CHECK(restored->GetActiveLayerId() == baseId);
    CHECK_FALSE(restored->RemoveLayer(baseId));

    // Duplicate serialized stable IDs are repaired rather than aliasing two layers.
    document["layers"][1]["id"] = document["layers"][0]["id"];
    restored->Deserialize(document);
    REQUIRE(restored->GetLayers().size() == 2);
    CHECK(restored->GetLayers()[0].id != restored->GetLayers()[1].id);

    // Unsupported/oversized documents do not partially replace live state.
    const int previousWidth = restored->width;
    nlohmann::json invalid = document;
    invalid["width"] = 16384;
    invalid["height"] = 16384;
    restored->Deserialize(invalid);
    CHECK(restored->width == previousWidth);
}

TEST_CASE("Tilemap v2: terrain painting resolves all masks and only uses NESW neighbors") {
    ScopedTileSetFixture fixture(MakeTerrainTileSet());
    auto object = std::make_shared<GameObject>("TerrainTilemap");
    object->AddComponent<Transform>();
    auto* tilemap = object->AddComponent<TilemapRenderer>();
    REQUIRE(tilemap->ConvertToLayered(fixture.guid));
    REQUIRE(tilemap->Resize(3, 3));
    tilemap->ResolveAssets();
    REQUIRE(tilemap->GetTileSet() != nullptr);
    const std::string layerId = tilemap->GetActiveLayerId();

    std::vector<TilemapCellEdit> clear;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) clear.push_back({x, y, {-1, -1}});
    }
    for (int mask = 0; mask < 16; ++mask) {
        tilemap->ApplyCellEdits(layerId, clear, true);
        REQUIRE(tilemap->SetTerrain(layerId, 1, 1, TestTerrainId));
        if (mask & 1) REQUIRE(tilemap->SetTerrain(layerId, 1, 0, TestTerrainId));
        if (mask & 2) REQUIRE(tilemap->SetTerrain(layerId, 2, 1, TestTerrainId));
        if (mask & 4) REQUIRE(tilemap->SetTerrain(layerId, 1, 2, TestTerrainId));
        if (mask & 8) REQUIRE(tilemap->SetTerrain(layerId, 0, 1, TestTerrainId));
        CHECK(tilemap->TerrainMask(layerId, 1, 1) == mask);
        CHECK(tilemap->GetCell(layerId, 1, 1).tileId == 100 + mask);
    }

    tilemap->ApplyCellEdits(layerId, clear, true);
    REQUIRE(tilemap->SetTerrain(layerId, 1, 1, TestTerrainId));
    REQUIRE(tilemap->SetTerrain(layerId, 0, 0, TestTerrainId));
    CHECK(tilemap->TerrainMask(layerId, 1, 1) == 0); // diagonal is excluded
    CHECK(tilemap->GetCell(layerId, 1, 1).tileId == 100);

    molga::TileSetAsset reimported = MakeTerrainTileSet();
    reimported.terrainRules[0].tileId = 101;
    std::string error;
    REQUIRE(reimported.SaveToFile(fixture.root / "Assets" / "terrain.tileset", &error));
    REQUIRE(molga::AssetDatabase::Get().TryReimport(fixture.guid, &error));
    tilemap->ResolveAssets();
    CHECK(tilemap->GetCell(layerId, 1, 1).tileId == 101);

    REQUIRE(tilemap->SetLayerLocked(layerId, true));
    CHECK_FALSE(tilemap->SetTerrain(layerId, 1, 1, -1));
    CHECK_FALSE(tilemap->SetCell(layerId, 1, 1, {-1, -1}));
}

TEST_CASE("Tilemap v2: culling defers invisible dirty chunks and batch limits are deterministic") {
    auto object = std::make_shared<GameObject>("ChunkedTilemap");
    object->AddComponent<Transform>();
    auto* tilemap = object->AddComponent<TilemapRenderer>();
    REQUIRE(tilemap->ConvertToLayered("33333333333333333333333333333333"));
    REQUIRE(tilemap->Resize(128, 64));
    const std::string layerId = tilemap->GetActiveLayerId();
    REQUIRE(tilemap->SetCell(layerId, 1, 1, {1, -1}));
    REQUIRE(tilemap->SetCell(layerId, 100, 1, {1, -1}));

    molga::RenderQueue queue;
    queue.SetViewBounds({1.0f, 1.0f, 10.0f, 10.0f});
    const std::size_t initial = tilemap->GetChunkRebuildCount();
    tilemap->CollectRender(queue);
    CHECK(tilemap->GetChunkRebuildCount() == initial + 1);
    CHECK(queue.GetCommands().empty()); // no resolved texture in this headless test

    tilemap->CollectRender(queue);
    CHECK(tilemap->GetChunkRebuildCount() == initial + 1);
    REQUIRE(tilemap->SetCell(layerId, 2, 2, {2, -1}));
    tilemap->CollectRender(queue);
    CHECK(tilemap->GetChunkRebuildCount() == initial + 2);

    REQUIRE(tilemap->SetCell(layerId, 101, 2, {2, -1}));
    tilemap->CollectRender(queue);
    CHECK(tilemap->GetChunkRebuildCount() == initial + 2); // offscreen dirty stays deferred
    queue.SetViewBounds({3201.0f, 1.0f, 10.0f, 10.0f});
    tilemap->CollectRender(queue);
    CHECK(tilemap->GetChunkRebuildCount() == initial + 3);

    CHECK(molga::SpriteBatcher::RequiredBatchCount(0) == 0);
    CHECK(molga::SpriteBatcher::RequiredBatchCount(2048) == 1);
    CHECK(molga::SpriteBatcher::RequiredBatchCount(2049) == 2);
    CHECK(molga::SpriteBatcher::RequiredBatchCount(4096) == 2);
}

TEST_CASE("Tilemap v2: collision runs rebuild only dirty layers and follow layer order") {
    ScopedTileSetFixture fixture(MakeTerrainTileSet());
    auto object = std::make_shared<GameObject>("CollisionLayers");
    object->AddComponent<Transform>();
    auto* tilemap = object->AddComponent<TilemapRenderer>();
    REQUIRE(tilemap->ConvertToLayered(fixture.guid));
    REQUIRE(tilemap->Resize(8, 2));
    tilemap->ResolveAssets();
    const std::string baseId = tilemap->GetActiveLayerId();
    const std::string upperId = tilemap->AddLayer("Upper");
    REQUIRE(tilemap->SetLayerCollisionEnabled(baseId, true));
    REQUIRE(tilemap->SetLayerCollisionEnabled(upperId, true));
    REQUIRE(tilemap->SetCell(baseId, 0, 0, {100, -1}));
    REQUIRE(tilemap->SetCell(baseId, 1, 0, {100, -1}));
    REQUIRE(tilemap->SetCell(baseId, 2, 0, {100, -1}));
    REQUIRE(tilemap->SetCell(upperId, 6, 1, {100, -1}));

    const std::uint64_t before = tilemap->GetCollisionRebuildCount();
    const auto& first = tilemap->GetCollisionRuns();
    REQUIRE(first.size() == 2);
    CHECK(first[0].start == 0);
    CHECK(first[0].end == 3);
    CHECK(tilemap->GetCollisionRebuildCount() == before + 2);
    tilemap->GetCollisionRuns();
    CHECK(tilemap->GetCollisionRebuildCount() == before + 2);

    REQUIRE(tilemap->SetCell(baseId, 3, 0, {100, -1}));
    const auto& expanded = tilemap->GetCollisionRuns();
    REQUIRE(expanded.size() == 2);
    CHECK(expanded[0].end == 4);
    CHECK(tilemap->GetCollisionRebuildCount() == before + 3);

    // Direct property edits are still detected for compatibility with older editor code.
    tilemap->GetLayer(upperId)->collisionEnabled = false;
    const auto& disabled = tilemap->GetCollisionRuns();
    REQUIRE(disabled.size() == 1);
    CHECK(tilemap->GetCollisionRebuildCount() == before + 4);

    REQUIRE(tilemap->MoveLayer(baseId, 1));
    const auto& moved = tilemap->GetCollisionRuns();
    REQUIRE(moved.size() == 1);
    CHECK(moved[0].layerIndex == 1);
    CHECK(tilemap->GetCollisionRebuildCount() == before + 6);

    REQUIRE(tilemap->RemoveLayer(upperId));
    const auto& afterRemove = tilemap->GetCollisionRuns();
    REQUIRE(afterRemove.size() == 1);
    CHECK(afterRemove[0].layerIndex == 0);
    CHECK(tilemap->GetCollisionRebuildCount() == before + 7);

    World world;
    world.Add(object);
    world.FixedStep(0.0f);
    REQUIRE(world.GetPhysicsWorld() != nullptr);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 1);
    REQUIRE(tilemap->SetCell(baseId, 1, 0, {-1, -1}));
    world.FixedStep(0.0f);
    CHECK(world.GetPhysicsWorld()->ShapeCount() == 2);
}

TEST_CASE("Tilemap legacy: serialization stays v1, queries stay per-cell, conversion is explicit") {
    auto object = std::make_shared<GameObject>("LegacyCompatibility");
    object->AddComponent<Transform>();
    auto* tilemap = object->AddComponent<TilemapRenderer>();
    REQUIRE(tilemap->Resize(3, 1));
    tilemap->SetSolid(5, true);
    tilemap->SetTile(0, 0, 5);
    tilemap->SetTile(1, 0, 5);

    nlohmann::json legacy;
    tilemap->Serialize(legacy);
    CHECK_FALSE(legacy.contains("schemaVersion"));
    CHECK(legacy["tiles"].size() == 3);
    CHECK_FALSE(tilemap->IsLayered());

    const auto cells = tilemap->GetCollidingTiles({0.0f, 0.0f, 64.0f, 32.0f});
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].width == doctest::Approx(32.0f));
    CHECK(cells[1].x == doctest::Approx(32.0f));
    const auto& runs = tilemap->GetCollisionRuns();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].start == 0);
    CHECK(runs[0].end == 2);

    auto restoredObject = std::make_shared<GameObject>("LegacyRestored");
    auto* restored = restoredObject->AddComponent<TilemapRenderer>();
    restored->Deserialize(legacy);
    CHECK_FALSE(restored->IsLayered());
    CHECK(restored->GetTile(1, 0) == 5);
    REQUIRE(restored->ConvertToLayered("44444444444444444444444444444444"));
    CHECK(restored->IsLayered());
    CHECK(restored->GetCell(restored->GetActiveLayerId(), 1, 0).tileId == 5);
}
