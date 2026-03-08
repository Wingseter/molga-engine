#ifndef MOLGA_BOOTSTRAP_H
#define MOLGA_BOOTSTRAP_H

#include <string>

struct GLFWwindow;

struct WindowConfig {
    std::string title = "Molga Engine";
    int width = 800;
    int height = 600;
    bool fullscreen = false;
};

// Initialize GLFW, create window, load GLAD, set up OpenGL blending,
// and init Time, Input, Audio subsystems.
// Returns window pointer on success, nullptr on failure.
GLFWwindow* EngineInit(const WindowConfig& config);

// Shutdown Audio and terminate GLFW.
// Caller must clean up all other resources BEFORE calling this.
void EngineShutdown();

#endif
