#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>
#include <algorithm>
#include <memory>

#include "Shader.h"
#include "Texture.h"
#include "Sprite.h"
#include "Renderer.h"
#include "Time.h"
#include "Input.h"
#include "Camera2D.h"
#include "Collision.h"
#include "Tilemap.h"
#include "UI.h"
#include "Audio.h"
#include "Scene.h"
#include "Particle.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// Global resources (shared between scenes)
Renderer* g_renderer = nullptr;
Shader* g_shader = nullptr;
Camera2D* g_camera = nullptr;

// ============ Menu Scene ============
class MenuScene : public Scene {
public:
    MenuScene() : Scene("Menu") {}

    void OnEnter() override {
        // Reset camera for menu
        g_camera->SetPosition(0, 0);
        g_camera->SetZoom(1.0f);
    }

    void Update(float dt) override {
        // Press ENTER or SPACE to start game
        if (Input::GetKeyDown(GLFW_KEY_ENTER) || Input::GetKeyDown(GLFW_KEY_SPACE)) {
            SceneManager::ChangeScene("Game");
        }
    }

    void Render(Renderer* renderer, Shader* shader, Camera2D* camera) override {
        renderer->Clear(0.1f, 0.1f, 0.15f, 1.0f);

        // Simple menu UI
        renderer->Begin(shader, nullptr);  // No camera for UI

        // Title background
        Sprite titleBg;
        titleBg.SetPosition(SCR_WIDTH / 2.0f - 200.0f, 150.0f);
        titleBg.SetSize(400.0f, 80.0f);
        titleBg.SetColor(0.2f, 0.3f, 0.5f, 1.0f);
        renderer->DrawSprite(&titleBg);

        // Start button
        Sprite startBtn;
        startBtn.SetPosition(SCR_WIDTH / 2.0f - 100.0f, 300.0f);
        startBtn.SetSize(200.0f, 50.0f);
        startBtn.SetColor(0.3f, 0.6f, 0.3f, 1.0f);
        renderer->DrawSprite(&startBtn);

        // Quit button
        Sprite quitBtn;
        quitBtn.SetPosition(SCR_WIDTH / 2.0f - 100.0f, 370.0f);
        quitBtn.SetSize(200.0f, 50.0f);
        quitBtn.SetColor(0.6f, 0.3f, 0.3f, 1.0f);
        renderer->DrawSprite(&quitBtn);

        renderer->End();
    }
};

// ============ Game Scene ============
class GameScene : public Scene {
public:
    GameScene() : Scene("Game"), moveSpeed(200.0f), playerStamina(1.0f) {}

    void OnEnter() override {
        // Initialize tilemap
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

        // Initialize player
        player.SetPosition(100.0f, 100.0f);
        player.SetSize(24.0f, 24.0f);
        player.SetColor(0.2f, 0.8f, 0.3f, 1.0f);

        // Setup UI
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

        // Setup particle emitters
        fireEmitter.SetConfig(ParticlePresets::Fire());
        fireEmitter.SetPosition(400.0f, 300.0f);
        fireEmitter.Start();

        sparkEmitter.SetConfig(ParticlePresets::Spark());
    }

    void OnExit() override {
        uiManager.Clear();
    }

    void Update(float dt) override {
        // ESC to return to menu
        if (Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
            SceneManager::ChangeScene("Menu");
            return;
        }

        // Player movement
        float dx = 0.0f, dy = 0.0f;
        if (Input::GetKey(GLFW_KEY_W) || Input::GetKey(GLFW_KEY_UP))    dy -= 1.0f;
        if (Input::GetKey(GLFW_KEY_S) || Input::GetKey(GLFW_KEY_DOWN))  dy += 1.0f;
        if (Input::GetKey(GLFW_KEY_A) || Input::GetKey(GLFW_KEY_LEFT))  dx -= 1.0f;
        if (Input::GetKey(GLFW_KEY_D) || Input::GetKey(GLFW_KEY_RIGHT)) dx += 1.0f;

        // Move and resolve collision
        player.x += dx * moveSpeed * dt;
        std::vector<AABB> collidingTiles = tilemap->GetCollidingTiles(player.GetAABB());
        for (const AABB& tile : collidingTiles) {
            CollisionResult result = Collision::CheckAABBWithResult(player.GetAABB(), tile);
            if (result.collided) {
                player.x -= result.overlapX;
            }
        }

        player.y += dy * moveSpeed * dt;
        collidingTiles = tilemap->GetCollidingTiles(player.GetAABB());
        for (const AABB& tile : collidingTiles) {
            CollisionResult result = Collision::CheckAABBWithResult(player.GetAABB(), tile);
            if (result.collided) {
                player.y -= result.overlapY;
            }
        }

        // Camera follows player
        float targetCamX = player.x - SCR_WIDTH / 2.0f + player.width / 2.0f;
        float targetCamY = player.y - SCR_HEIGHT / 2.0f + player.height / 2.0f;
        g_camera->SetPosition(targetCamX, targetCamY);

        // Camera zoom
        if (Input::GetKey(GLFW_KEY_Q)) g_camera->Zoom(1.0f - dt);
        if (Input::GetKey(GLFW_KEY_E)) g_camera->Zoom(1.0f + dt);
        float scroll = Input::GetScrollY();
        if (scroll != 0.0f) g_camera->Zoom(1.0f + scroll * 0.1f);
        if (Input::GetKeyDown(GLFW_KEY_R)) {
            g_camera->SetZoom(1.0f);
            g_camera->SetRotation(0.0f);
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

        // Particle effects: F for spark burst at player position
        if (Input::GetKeyDown(GLFW_KEY_F)) {
            sparkEmitter.SetPosition(player.x + player.width / 2, player.y + player.height / 2);
            sparkEmitter.Burst(30);
        }

        // Update particles
        fireEmitter.Update(dt);
        sparkEmitter.Update(dt);

        uiManager.Update(dt);
    }

    void Render(Renderer* renderer, Shader* shader, Camera2D* camera) override {
        renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);

        renderer->Begin(shader, camera);

        // Draw floor
        for (Sprite& floor : floorSprites) {
            renderer->DrawSprite(&floor);
        }

        // Draw walls
        for (Sprite& wall : wallSprites) {
            renderer->DrawSprite(&wall);
        }

        // Draw player
        renderer->DrawSprite(&player);

        renderer->End();

        // Render particles
        fireEmitter.Render(renderer, shader, camera);
        sparkEmitter.Render(renderer, shader, camera);

        // Render UI
        uiManager.Render(renderer, shader, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));
    }

private:
    std::unique_ptr<Tilemap> tilemap;
    std::vector<Sprite> wallSprites;
    std::vector<Sprite> floorSprites;
    Sprite player;
    float moveSpeed;
    float playerStamina;

    UIManager uiManager;
    ProgressBar healthBar;
    ProgressBar staminaBar;

    ParticleEmitter fireEmitter;
    ParticleEmitter sparkEmitter;
};

// ============ Main ============
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Molga Engine", nullptr, nullptr);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize systems
    Time::Init();
    Input::Init(window);
    Audio::Init();

    // Initialize global resources
    g_renderer = new Renderer();
    g_renderer->Init();
    g_shader = new Shader("src/Shaders/default.vert", "src/Shaders/default.frag");
    g_camera = new Camera2D(static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));

    // Create scenes
    SceneManager::AddScene("Menu", std::make_shared<MenuScene>());
    SceneManager::AddScene("Game", std::make_shared<GameScene>());
    SceneManager::ChangeScene("Menu");

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();

        // Update title
        std::ostringstream title;
        title << "Molga Engine - FPS: " << static_cast<int>(Time::GetFPS())
              << " | Scene: " << SceneManager::GetCurrentSceneName();
        glfwSetWindowTitle(window, title.str().c_str());

        // Update and render current scene
        SceneManager::Update(dt);
        SceneManager::Render(g_renderer, g_shader, g_camera);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    SceneManager::Clear();
    delete g_camera;
    delete g_shader;
    delete g_renderer;
    Audio::Shutdown();
    glfwTerminate();
    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}
