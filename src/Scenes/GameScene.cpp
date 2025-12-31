#include "GameScene.h"
#include "../Renderer.h"
#include "../Shader.h"
#include "../Camera2D.h"
#include "../Input.h"
#include "../Collision.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/SpriteRenderer.h"
#include "../ECS/Components/BoxCollider2D.h"
#include <GLFW/glfw3.h>
#include <algorithm>

GameScene::GameScene() : Scene("Game") {
}

void GameScene::OnEnter() {
    CreateTilemap();
    CreatePlayer();
    CreateUI();
    CreateParticles();
}

void GameScene::OnExit() {
    gameObjects.clear();
    wallSprites.clear();
    floorSprites.clear();
    uiManager.Clear();
    tilemap.reset();
}

void GameScene::CreateTilemap() {
    const int TILE_SIZE = 32;
    const int MAP_WIDTH = 25;
    const int MAP_HEIGHT = 19;

    int mapData[19][25] = {
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,1},
        {1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1},
        {1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,1,1,1,0,1,1,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1},
        {1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
    };

    tilemap = std::make_unique<Tilemap>(MAP_WIDTH, MAP_HEIGHT, TILE_SIZE);
    tilemap->SetCollisionTile(1, true);

    wallSprites.clear();
    floorSprites.clear();

    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            tilemap->SetTile(x, y, mapData[y][x]);
            Sprite tile;
            tile.SetPosition(static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE));
            tile.SetSize(static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE));
            if (mapData[y][x] == 1) {
                tile.SetColor(0.4f, 0.4f, 0.5f, 1.0f);
                wallSprites.push_back(tile);
            } else {
                tile.SetColor(0.25f, 0.25f, 0.3f, 1.0f);
                floorSprites.push_back(tile);
            }
        }
    }
}

void GameScene::CreatePlayer() {
    playerSprite.SetPosition(100.0f, 100.0f);
    playerSprite.SetSize(24.0f, 24.0f);
    playerSprite.SetColor(0.2f, 0.8f, 0.3f, 1.0f);

    // Create player as GameObject for ECS
    playerObject = std::make_shared<GameObject>("Player");
    auto transform = playerObject->AddComponent<Transform>();
    transform->SetPosition(100.0f, 100.0f);
    auto spriteRenderer = playerObject->AddComponent<SpriteRenderer>();
    spriteRenderer->SetColor(0.2f, 0.8f, 0.3f, 1.0f);
    spriteRenderer->SetSize(24.0f, 24.0f);
    auto collider = playerObject->AddComponent<BoxCollider2D>();
    collider->SetSize(24.0f, 24.0f);
    gameObjects.push_back(playerObject);
}

void GameScene::CreateUI() {
    healthBar.SetPosition(10.0f, 10.0f);
    healthBar.SetSize(200.0f, 20.0f);
    healthBar.SetFillColor(0.8f, 0.2f, 0.2f, 1.0f);
    healthBar.SetBackgroundColor(0.3f, 0.1f, 0.1f, 1.0f);
    uiManager.AddElement(&healthBar);

    staminaBar.SetPosition(10.0f, 35.0f);
    staminaBar.SetSize(150.0f, 15.0f);
    staminaBar.SetFillColor(0.2f, 0.6f, 0.8f, 1.0f);
    staminaBar.SetBackgroundColor(0.1f, 0.2f, 0.3f, 1.0f);
    uiManager.AddElement(&staminaBar);

    playerStamina = 1.0f;
}

void GameScene::CreateParticles() {
    fireEmitter.SetConfig(ParticlePresets::Fire());
    fireEmitter.SetPosition(400.0f, 300.0f);
    fireEmitter.Start();

    sparkEmitter.SetConfig(ParticlePresets::Spark());
}

void GameScene::Update(float dt) {
    // ESC to return to menu
    if (Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
        SceneManager::ChangeScene("Menu");
        return;
    }

    UpdatePlayer(dt);

    // Update game objects
    for (auto& obj : gameObjects) {
        if (obj && obj->IsActive()) {
            obj->Update(dt);
        }
    }

    // Particle effects: F for spark burst at player position
    if (Input::GetKeyDown(GLFW_KEY_F)) {
        sparkEmitter.SetPosition(playerSprite.x + playerSprite.width / 2,
                                  playerSprite.y + playerSprite.height / 2);
        sparkEmitter.Burst(30);
    }

    // Update particles
    fireEmitter.Update(dt);
    sparkEmitter.Update(dt);

    uiManager.Update(dt);
}

void GameScene::UpdatePlayer(float dt) {
    // Player movement
    float dx = 0.0f, dy = 0.0f;
    if (Input::GetKey(GLFW_KEY_W) || Input::GetKey(GLFW_KEY_UP))    dy -= 1.0f;
    if (Input::GetKey(GLFW_KEY_S) || Input::GetKey(GLFW_KEY_DOWN))  dy += 1.0f;
    if (Input::GetKey(GLFW_KEY_A) || Input::GetKey(GLFW_KEY_LEFT))  dx -= 1.0f;
    if (Input::GetKey(GLFW_KEY_D) || Input::GetKey(GLFW_KEY_RIGHT)) dx += 1.0f;

    // Move and resolve collision
    playerSprite.x += dx * moveSpeed * dt;
    std::vector<AABB> collidingTiles = tilemap->GetCollidingTiles(playerSprite.GetAABB());
    for (const AABB& tile : collidingTiles) {
        CollisionResult result = Collision::CheckAABBWithResult(playerSprite.GetAABB(), tile);
        if (result.collided) {
            playerSprite.x -= result.overlapX;
        }
    }

    playerSprite.y += dy * moveSpeed * dt;
    collidingTiles = tilemap->GetCollidingTiles(playerSprite.GetAABB());
    for (const AABB& tile : collidingTiles) {
        CollisionResult result = Collision::CheckAABBWithResult(playerSprite.GetAABB(), tile);
        if (result.collided) {
            playerSprite.y -= result.overlapY;
        }
    }

    // Sync GameObject position with sprite
    if (playerObject) {
        auto transform = playerObject->GetComponent<Transform>();
        if (transform) {
            transform->SetPosition(playerSprite.x, playerSprite.y);
        }
    }

    // Update stamina
    bool isMoving = (dx != 0.0f || dy != 0.0f);
    if (isMoving) {
        playerStamina -= 0.2f * dt;
    } else {
        playerStamina += 0.3f * dt;
    }
    playerStamina = std::max(0.0f, std::min(1.0f, playerStamina));
    staminaBar.SetValue(playerStamina);
}

void GameScene::UpdateCamera(float dt, Camera2D* camera) {
    float targetCamX = playerSprite.x - screenWidth / 2.0f + playerSprite.width / 2.0f;
    float targetCamY = playerSprite.y - screenHeight / 2.0f + playerSprite.height / 2.0f;
    camera->SetPosition(targetCamX, targetCamY);

    // Camera zoom
    if (Input::GetKey(GLFW_KEY_Q)) camera->Zoom(1.0f - dt);
    if (Input::GetKey(GLFW_KEY_E)) camera->Zoom(1.0f + dt);
    float scroll = Input::GetScrollY();
    if (scroll != 0.0f) camera->Zoom(1.0f + scroll * 0.1f);
    if (Input::GetKeyDown(GLFW_KEY_R)) {
        camera->SetZoom(1.0f);
        camera->SetRotation(0.0f);
    }
}

void GameScene::Render(Renderer* renderer, Shader* shader, Camera2D* camera) {
    renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);

    // Update camera position
    UpdateCamera(0.0f, camera);

    renderer->Begin(shader, camera);

    // Draw floor
    for (Sprite& floor : floorSprites) {
        renderer->DrawSprite(&floor);
    }

    // Draw walls
    for (Sprite& wall : wallSprites) {
        renderer->DrawSprite(&wall);
    }

    // Draw player sprite
    renderer->DrawSprite(&playerSprite);

    renderer->End();

    // Render particles
    fireEmitter.Render(renderer, shader, camera);
    sparkEmitter.Render(renderer, shader, camera);

    // Render UI
    uiManager.Render(renderer, shader, screenWidth, screenHeight);
}
