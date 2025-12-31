#ifndef MOLGA_GAME_SCENE_H
#define MOLGA_GAME_SCENE_H

#include "../Scene.h"
#include "../ECS/GameObject.h"
#include "../Tilemap.h"
#include "../Sprite.h"
#include "../UI.h"
#include "../Particle.h"
#include <vector>
#include <memory>

class GameScene : public Scene {
public:
    GameScene();
    ~GameScene() override = default;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(Renderer* renderer, Shader* shader, Camera2D* camera) override;

    // Access to game objects for ECS integration
    std::vector<std::shared_ptr<GameObject>>& GetGameObjects() { return gameObjects; }

private:
    void CreateTilemap();
    void CreatePlayer();
    void CreateUI();
    void CreateParticles();

    void UpdatePlayer(float dt);
    void UpdateCamera(float dt, Camera2D* camera);

    std::vector<std::shared_ptr<GameObject>> gameObjects;

    // Tilemap
    std::unique_ptr<Tilemap> tilemap;
    std::vector<Sprite> wallSprites;
    std::vector<Sprite> floorSprites;

    // Player (legacy sprite for collision with tilemap)
    Sprite playerSprite;
    std::shared_ptr<GameObject> playerObject;
    float moveSpeed = 200.0f;
    float playerStamina = 1.0f;

    // UI
    UIManager uiManager;
    ProgressBar healthBar;
    ProgressBar staminaBar;

    // Particles
    ParticleEmitter fireEmitter;
    ParticleEmitter sparkEmitter;

    // Screen dimensions
    float screenWidth = 800.0f;
    float screenHeight = 600.0f;
};

#endif // MOLGA_GAME_SCENE_H
