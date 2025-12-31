#ifndef MOLGA_APPLICATION_H
#define MOLGA_APPLICATION_H

#include <string>

struct GLFWwindow;

class Application {
public:
    struct Config {
        std::string title;
        int width;
        int height;
        bool vsync;
        bool resizable;

        Config() : title("Molga Engine"), width(1280), height(720), vsync(true), resizable(true) {}
    };

    static Application& Get();

    bool Init(const Config& config = Config{});
    void Run();
    void Shutdown();

    void RequestQuit();
    bool IsRunning() const { return running; }

    GLFWwindow* GetWindow() const { return window; }
    int GetWindowWidth() const { return windowWidth; }
    int GetWindowHeight() const { return windowHeight; }
    float GetDeltaTime() const { return deltaTime; }
    float GetTime() const;

private:
    Application() = default;
    ~Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void ProcessInput();
    void Update(float dt);
    void Render();

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);

private:
    GLFWwindow* window = nullptr;
    int windowWidth = 1280;
    int windowHeight = 720;

    bool running = false;
    bool initialized = false;

    float deltaTime = 0.0f;
    float lastFrameTime = 0.0f;
};

#endif // MOLGA_APPLICATION_H
