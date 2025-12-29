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

    // Initialize renderer
    Renderer renderer;
    renderer.Init();
    renderer.SetProjection(0.0f, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT), 0.0f);

    // Load shader
    Shader shader("src/Shaders/default.vert", "src/Shaders/default.frag");

    // Player sprite (controllable)
    Sprite player;
    player.SetPosition(400.0f, 300.0f);
    player.SetSize(50.0f, 50.0f);
    player.SetColor(0.2f, 0.8f, 0.3f, 1.0f);  // Green

    // Static sprites
    Sprite obstacle1;
    obstacle1.SetPosition(100.0f, 100.0f);
    obstacle1.SetSize(80.0f, 80.0f);
    obstacle1.SetColor(1.0f, 0.5f, 0.2f, 1.0f);  // Orange

    Sprite obstacle2;
    obstacle2.SetPosition(600.0f, 400.0f);
    obstacle2.SetSize(80.0f, 80.0f);
    obstacle2.SetColor(0.3f, 0.5f, 0.9f, 1.0f);  // Blue

    const float moveSpeed = 200.0f;  // pixels per second

    // render loop
    while (!glfwWindowShouldClose(window)) {
        // Update systems
        Time::Update();
        Input::Update();
        float dt = Time::GetDeltaTime();

        // Update window title with FPS
        std::ostringstream title;
        title << "Molga Engine - FPS: " << static_cast<int>(Time::GetFPS())
              << " | Mouse: (" << static_cast<int>(Input::GetMouseX())
              << ", " << static_cast<int>(Input::GetMouseY()) << ")";
        glfwSetWindowTitle(window, title.str().c_str());

        // Input handling
        if (Input::GetKey(GLFW_KEY_ESCAPE)) {
            glfwSetWindowShouldClose(window, true);
        }

        // Player movement with WASD
        float dx = 0.0f, dy = 0.0f;
        if (Input::GetKey(GLFW_KEY_W) || Input::GetKey(GLFW_KEY_UP))    dy -= 1.0f;
        if (Input::GetKey(GLFW_KEY_S) || Input::GetKey(GLFW_KEY_DOWN))  dy += 1.0f;
        if (Input::GetKey(GLFW_KEY_A) || Input::GetKey(GLFW_KEY_LEFT))  dx -= 1.0f;
        if (Input::GetKey(GLFW_KEY_D) || Input::GetKey(GLFW_KEY_RIGHT)) dx += 1.0f;

        player.x += dx * moveSpeed * dt;
        player.y += dy * moveSpeed * dt;

        // Change color on mouse click
        if (Input::GetMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) {
            player.SetColor(1.0f, 0.0f, 0.0f, 1.0f);  // Red
        }
        if (Input::GetMouseButtonUp(GLFW_MOUSE_BUTTON_LEFT)) {
            player.SetColor(0.2f, 0.8f, 0.3f, 1.0f);  // Green
        }

        // Scale with scroll
        float scroll = Input::GetScrollY();
        if (scroll != 0.0f) {
            float scale = 1.0f + scroll * 0.1f;
            player.width *= scale;
            player.height *= scale;
        }

        // Render
        renderer.Clear(0.2f, 0.3f, 0.3f, 1.0f);

        renderer.Begin(&shader);
        renderer.DrawSprite(&obstacle1);
        renderer.DrawSprite(&obstacle2);
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
