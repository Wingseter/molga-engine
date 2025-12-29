#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>

#include "Shader.h"
#include "Texture.h"
#include "Sprite.h"
#include "Renderer.h"
#include "Time.h"
#include "Input.h"
#include "Camera2D.h"
#include "Collision.h"

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

    // Player sprite (controllable)
    Sprite player;
    player.SetPosition(400.0f, 300.0f);
    player.SetSize(50.0f, 50.0f);
    player.SetColor(0.2f, 0.8f, 0.3f, 1.0f);  // Green

    // Obstacles
    const int NUM_OBSTACLES = 5;
    Sprite obstacles[NUM_OBSTACLES];
    obstacles[0].SetPosition(200.0f, 200.0f);
    obstacles[0].SetSize(80.0f, 80.0f);
    obstacles[0].SetColor(1.0f, 0.5f, 0.2f, 1.0f);

    obstacles[1].SetPosition(500.0f, 150.0f);
    obstacles[1].SetSize(100.0f, 60.0f);
    obstacles[1].SetColor(0.3f, 0.5f, 0.9f, 1.0f);

    obstacles[2].SetPosition(350.0f, 400.0f);
    obstacles[2].SetSize(70.0f, 70.0f);
    obstacles[2].SetColor(0.9f, 0.2f, 0.5f, 1.0f);

    obstacles[3].SetPosition(100.0f, 350.0f);
    obstacles[3].SetSize(90.0f, 50.0f);
    obstacles[3].SetColor(0.5f, 0.9f, 0.2f, 1.0f);

    obstacles[4].SetPosition(600.0f, 400.0f);
    obstacles[4].SetSize(60.0f, 100.0f);
    obstacles[4].SetColor(0.9f, 0.9f, 0.2f, 1.0f);

    const float moveSpeed = 200.0f;
    int collisionCount = 0;

    // render loop
    while (!glfwWindowShouldClose(window)) {
        // Update systems
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();

        // Input handling
        if (Input::GetKey(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, true);
        }

        // Store old position for collision resolution
        float oldX = player.x;
        float oldY = player.y;

        // Player movement with WASD
        float dx = 0.0f, dy = 0.0f;
        if (Input::GetKey(GLFW_KEY_W) || Input::GetKey(GLFW_KEY_UP))    dy -= 1.0f;
        if (Input::GetKey(GLFW_KEY_S) || Input::GetKey(GLFW_KEY_DOWN))  dy += 1.0f;
        if (Input::GetKey(GLFW_KEY_A) || Input::GetKey(GLFW_KEY_LEFT))  dx -= 1.0f;
        if (Input::GetKey(GLFW_KEY_D) || Input::GetKey(GLFW_KEY_RIGHT)) dx += 1.0f;

        player.x += dx * moveSpeed * dt;
        player.y += dy * moveSpeed * dt;

        // Collision detection and resolution
        collisionCount = 0;
        for (int i = 0; i < NUM_OBSTACLES; i++) {
            CollisionResult result = Collision::CheckAABBWithResult(player.GetAABB(), obstacles[i].GetAABB());
            if (result.collided) {
                collisionCount++;
                // Push player out of obstacle
                player.x -= result.overlapX;
                player.y -= result.overlapY;
                // Flash player red on collision
                player.SetColor(1.0f, 0.3f, 0.3f, 1.0f);
            }
        }

        // Reset color if no collision
        if (collisionCount == 0) {
            player.SetColor(0.2f, 0.8f, 0.3f, 1.0f);
        }

        // Update window title
        std::ostringstream title;
        title << "Molga Engine - FPS: " << static_cast<int>(Time::GetFPS())
              << " | Collisions: " << collisionCount;
        glfwSetWindowTitle(window, title.str().c_str());

        // Camera follows player
        float targetCamX = player.x - SCR_WIDTH / 2.0f + player.width / 2.0f;
        float targetCamY = player.y - SCR_HEIGHT / 2.0f + player.height / 2.0f;
        camera.SetPosition(targetCamX, targetCamY);

        // Camera zoom with Q/E
        if (Input::GetKey(GLFW_KEY_Q)) camera.Zoom(1.0f - dt);
        if (Input::GetKey(GLFW_KEY_E)) camera.Zoom(1.0f + dt);

        // Camera zoom with scroll
        float scroll = Input::GetScrollY();
        if (scroll != 0.0f) camera.Zoom(1.0f + scroll * 0.1f);

        // Reset camera with R
        if (Input::GetKeyDown(GLFW_KEY_R)) {
            camera.SetZoom(1.0f);
            camera.SetRotation(0.0f);
        }

        // Render
        renderer.Clear(0.2f, 0.3f, 0.3f, 1.0f);

        renderer.Begin(&shader, &camera);
        for (int i = 0; i < NUM_OBSTACLES; i++) {
            renderer.DrawSprite(&obstacles[i]);
        }
        renderer.DrawSprite(&player);
        renderer.End();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}
