#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <sstream>

#include "Shader.h"
#include "Texture.h"
#include "Sprite.h"
#include "Renderer.h"
#include "Time.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow* window);

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

    // Initialize time system
    Time::Init();

    // Initialize renderer
    Renderer renderer;
    renderer.Init();
    renderer.SetProjection(0.0f, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT), 0.0f);

    // Load shader
    Shader shader("src/Shaders/default.vert", "src/Shaders/default.frag");

    // Create sprites without texture (color only)
    Sprite sprite1;
    sprite1.SetPosition(100.0f, 100.0f);
    sprite1.SetSize(100.0f, 100.0f);
    sprite1.SetColor(1.0f, 0.5f, 0.2f, 1.0f);  // Orange

    Sprite sprite2;
    sprite2.SetPosition(250.0f, 150.0f);
    sprite2.SetSize(80.0f, 80.0f);
    sprite2.SetColor(0.2f, 0.8f, 0.3f, 1.0f);  // Green

    Sprite sprite3;
    sprite3.SetPosition(400.0f, 200.0f);
    sprite3.SetSize(120.0f, 60.0f);
    sprite3.SetColor(0.3f, 0.5f, 0.9f, 1.0f);  // Blue

    float rotation = 0.0f;
    const float rotationSpeed = 90.0f;  // degrees per second

    // render loop
    while (!glfwWindowShouldClose(window)) {
        // Update time
        Time::Update();
        float dt = Time::GetDeltaTime();

        // Update window title with FPS
        std::ostringstream title;
        title << "Molga Engine - FPS: " << static_cast<int>(Time::GetFPS());
        glfwSetWindowTitle(window, title.str().c_str());

        processInput(window);

        // Update rotation using deltaTime (frame-independent)
        rotation += rotationSpeed * dt;
        sprite2.SetRotation(rotation);

        // Render
        renderer.Clear(0.2f, 0.3f, 0.3f, 1.0f);

        renderer.Begin(&shader);
        renderer.DrawSprite(&sprite1);
        renderer.DrawSprite(&sprite2);
        renderer.DrawSprite(&sprite3);
        renderer.End();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}
