#include "Core/World.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/TilemapRenderer.h"
#include "ECS/Components/Rigidbody2D.h"
#include "ECS/Components/BoxCollider2D.h"
#include "ECS/Components/CircleCollider2D.h"
#include "Physics/Collision.h"
#include "doctest.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <iostream>

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
