#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>
#include <algorithm>

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

void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

int main() {
    // glfw: initialize and configure
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // glfw window creation
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Molga Engine", nullptr, nullptr);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Initialize systems
    Time::Init();
    Input::Init(window);

    // Initialize renderer and camera
    Renderer renderer;
    renderer.Init();

    Camera2D camera(static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));

    // Load shader
    Shader shader("src/Shaders/default.vert", "src/Shaders/default.frag");

    // Create tilemap (20x15 tiles, 32px each)
    const int TILE_SIZE = 32;
    const int MAP_WIDTH = 25;
    const int MAP_HEIGHT = 19;
    Tilemap tilemap(MAP_WIDTH, MAP_HEIGHT, TILE_SIZE);

    // Define map layout (0 = floor, 1 = wall)
    int mapData[MAP_HEIGHT][MAP_WIDTH] = {
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

    // Set tiles and collision
    tilemap.SetCollisionTile(1, true);  // Wall is solid
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            tilemap.SetTile(x, y, mapData[y][x]);
        }
    }

    // Wall sprites for rendering (since we don't have tileset texture)
    std::vector<Sprite> wallSprites;
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (mapData[y][x] == 1) {
                Sprite wall;
                wall.SetPosition(static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE));
                wall.SetSize(static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE));
                wall.SetColor(0.4f, 0.4f, 0.5f, 1.0f);  // Gray walls
                wallSprites.push_back(wall);
            }
        }
    }

    // Player sprite
    Sprite player;
    player.SetPosition(100.0f, 100.0f);
    player.SetSize(24.0f, 24.0f);
    player.SetColor(0.2f, 0.8f, 0.3f, 1.0f);

    const float moveSpeed = 200.0f;

    // UI Setup
    UIManager uiManager;

    // Health bar
    ProgressBar healthBar;
    healthBar.SetPosition(10.0f, 10.0f);
    healthBar.SetSize(200.0f, 20.0f);
    healthBar.SetFillColor(0.8f, 0.2f, 0.2f, 1.0f);  // Red
    healthBar.SetBackgroundColor(0.3f, 0.1f, 0.1f, 1.0f);
    uiManager.AddElement(&healthBar);

    // Stamina bar
    ProgressBar staminaBar;
    staminaBar.SetPosition(10.0f, 35.0f);
    staminaBar.SetSize(150.0f, 15.0f);
    staminaBar.SetFillColor(0.2f, 0.6f, 0.8f, 1.0f);  // Blue
    staminaBar.SetBackgroundColor(0.1f, 0.2f, 0.3f, 1.0f);
    uiManager.AddElement(&staminaBar);

    // Panel background for buttons
    Panel buttonPanel;
    buttonPanel.SetPosition(static_cast<float>(SCR_WIDTH) - 110.0f, 10.0f);
    buttonPanel.SetSize(100.0f, 70.0f);
    buttonPanel.SetColor(0.1f, 0.1f, 0.15f, 0.8f);
    buttonPanel.SetBorderColor(0.4f, 0.4f, 0.5f, 1.0f);
    buttonPanel.SetBorderWidth(2.0f);
    uiManager.AddElement(&buttonPanel);

    // Heal button
    Button healButton;
    healButton.SetPosition(static_cast<float>(SCR_WIDTH) - 100.0f, 20.0f);
    healButton.SetSize(80.0f, 25.0f);
    healButton.SetColor(0.2f, 0.5f, 0.2f, 1.0f);
    healButton.SetHoverColor(0.3f, 0.6f, 0.3f, 1.0f);
    healButton.SetPressedColor(0.1f, 0.4f, 0.1f, 1.0f);
    healButton.SetOnClick([&healthBar]() {
        healthBar.SetValue(1.0f);
    });
    uiManager.AddElement(&healButton);

    // Damage button
    Button damageButton;
    damageButton.SetPosition(static_cast<float>(SCR_WIDTH) - 100.0f, 50.0f);
    damageButton.SetSize(80.0f, 25.0f);
    damageButton.SetColor(0.5f, 0.2f, 0.2f, 1.0f);
    damageButton.SetHoverColor(0.6f, 0.3f, 0.3f, 1.0f);
    damageButton.SetPressedColor(0.4f, 0.1f, 0.1f, 1.0f);
    damageButton.SetOnClick([&healthBar]() {
        healthBar.SetValue(healthBar.value - 0.1f);
    });
    uiManager.AddElement(&damageButton);

    float playerHealth = 1.0f;
    float playerStamina = 1.0f;

    // render loop
    while (!glfwWindowShouldClose(window)) {
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();

        if (Input::GetKey(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, true);
        }

        // Player movement
        float dx = 0.0f, dy = 0.0f;
        if (Input::GetKey(GLFW_KEY_W) || Input::GetKey(GLFW_KEY_UP))    dy -= 1.0f;
        if (Input::GetKey(GLFW_KEY_S) || Input::GetKey(GLFW_KEY_DOWN))  dy += 1.0f;
        if (Input::GetKey(GLFW_KEY_A) || Input::GetKey(GLFW_KEY_LEFT))  dx -= 1.0f;
        if (Input::GetKey(GLFW_KEY_D) || Input::GetKey(GLFW_KEY_RIGHT)) dx += 1.0f;

        // Move X first, then resolve collision
        player.x += dx * moveSpeed * dt;
        std::vector<AABB> collidingTiles = tilemap.GetCollidingTiles(player.GetAABB());
        for (const AABB& tile : collidingTiles) {
            CollisionResult result = Collision::CheckAABBWithResult(player.GetAABB(), tile);
            if (result.collided) {
                player.x -= result.overlapX;
            }
        }

        // Move Y, then resolve collision
        player.y += dy * moveSpeed * dt;
        collidingTiles = tilemap.GetCollidingTiles(player.GetAABB());
        for (const AABB& tile : collidingTiles) {
            CollisionResult result = Collision::CheckAABBWithResult(player.GetAABB(), tile);
            if (result.collided) {
                player.y -= result.overlapY;
            }
        }

        // Camera follows player
        float targetCamX = player.x - SCR_WIDTH / 2.0f + player.width / 2.0f;
        float targetCamY = player.y - SCR_HEIGHT / 2.0f + player.height / 2.0f;
        camera.SetPosition(targetCamX, targetCamY);

        // Camera zoom
        if (Input::GetKey(GLFW_KEY_Q)) camera.Zoom(1.0f - dt);
        if (Input::GetKey(GLFW_KEY_E)) camera.Zoom(1.0f + dt);
        float scroll = Input::GetScrollY();
        if (scroll != 0.0f) camera.Zoom(1.0f + scroll * 0.1f);
        if (Input::GetKeyDown(GLFW_KEY_R)) {
            camera.SetZoom(1.0f);
            camera.SetRotation(0.0f);
        }

        // Update stamina (drains when moving, regenerates when still)
        bool isMoving = (dx != 0.0f || dy != 0.0f);
        if (isMoving) {
            playerStamina -= 0.2f * dt;
        } else {
            playerStamina += 0.3f * dt;
        }
        playerStamina = std::max(0.0f, std::min(1.0f, playerStamina));
        staminaBar.SetValue(playerStamina);

        // Update UI
        uiManager.Update(dt);

        // Update title
        std::ostringstream title;
        title << "Molga Engine - FPS: " << static_cast<int>(Time::GetFPS())
              << " | Pos: (" << static_cast<int>(player.x) << ", " << static_cast<int>(player.y) << ")";
        glfwSetWindowTitle(window, title.str().c_str());

        // Render
        renderer.Clear(0.15f, 0.15f, 0.2f, 1.0f);

        renderer.Begin(&shader, &camera);

        // Draw floor tiles
        for (int y = 0; y < MAP_HEIGHT; y++) {
            for (int x = 0; x < MAP_WIDTH; x++) {
                if (mapData[y][x] == 0) {
                    Sprite floor;
                    floor.SetPosition(static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE));
                    floor.SetSize(static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE));
                    floor.SetColor(0.25f, 0.25f, 0.3f, 1.0f);
                    renderer.DrawSprite(&floor);
                }
            }
        }

        // Draw walls
        for (Sprite& wall : wallSprites) {
            renderer.DrawSprite(&wall);
        }

        // Draw player
        renderer.DrawSprite(&player);

        renderer.End();

        // Render UI (after game world, before swap)
        uiManager.Render(&renderer, &shader, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT));

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}
