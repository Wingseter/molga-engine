#include "Application.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>

#include "../Editor/ImGuiLayer.h"
#include "../Input.h"
#include "../Time.h"

Application &Application::Get() {
  static Application instance;
  return instance;
}

bool Application::Init(const Config &config) {
  if (initialized) {
    std::cerr << "Application already initialized!" << std::endl;
    return false;
  }

  // Initialize GLFW
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return false;
  }

  // Configure OpenGL
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
  glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GL_TRUE : GL_FALSE);

  // Create window
  windowWidth = config.width;
  windowHeight = config.height;
  window = glfwCreateWindow(windowWidth, windowHeight, config.title.c_str(),
                            nullptr, nullptr);
  if (!window) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window);
  glfwSetWindowUserPointer(window, this);

  // Setup callbacks
  glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
  glfwSetKeyCallback(window, KeyCallback);
  glfwSetMouseButtonCallback(window, MouseButtonCallback);
  glfwSetCursorPosCallback(window, CursorPosCallback);

  // VSync
  glfwSwapInterval(config.vsync ? 1 : 0);

  // Initialize GLAD
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    glfwDestroyWindow(window);
    glfwTerminate();
    return false;
  }

  // Set viewport
  glViewport(0, 0, windowWidth, windowHeight);

  // Enable blending for transparency
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Initialize Input system
  Input::Init(window);

  // Initialize ImGui
  ImGuiLayer::Init(window);

  initialized = true;
  running = true;

  std::cout << "Molga Engine initialized successfully" << std::endl;
  std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;

  return true;
}

void Application::Run() {
  if (!initialized) {
    std::cerr << "Application not initialized!" << std::endl;
    return;
  }

  while (running && !glfwWindowShouldClose(window)) {
    // Calculate delta time
    float currentTime = static_cast<float>(glfwGetTime());
    deltaTime = currentTime - lastFrameTime;
    lastFrameTime = currentTime;

    // Update Time system
    Time::Update();

    // Process input
    ProcessInput();

    // Update
    Update(deltaTime);

    // Render
    Render();

    // Swap buffers and poll events
    glfwSwapBuffers(window);

    // Update input state
    Input::Update();

    glfwPollEvents();
  }
}

void Application::Shutdown() {
  if (!initialized)
    return;

  ImGuiLayer::Shutdown();

  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }

  glfwTerminate();
  initialized = false;
  running = false;

  std::cout << "Molga Engine shutdown complete" << std::endl;
}

void Application::RequestQuit() { running = false; }

float Application::GetTime() const { return static_cast<float>(glfwGetTime()); }

void Application::ProcessInput() {
  // ESC key to quit (can be disabled when ImGui captures input)
  if (!ImGuiLayer::WantCaptureKeyboard()) {
    if (Input::GetKeyDown(GLFW_KEY_ESCAPE)) {
      RequestQuit();
    }
  }
}

void Application::Update(float dt) {
  // Game logic update will be handled by Scene/Editor
}

void Application::Render() {
  glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  // Game rendering will be handled by Scene/Editor

  // ImGui rendering
  ImGuiLayer::BeginFrame();

  // Editor UI will be rendered here by Editor class
  // For now, show a simple demo
  {
    ImGui::BeginMainMenuBar();
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
      }
      if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {
      }
      if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit", "Alt+F4")) {
        RequestQuit();
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
      ImGui::MenuItem("Hierarchy");
      ImGui::MenuItem("Inspector");
      ImGui::MenuItem("Scene View");
      ImGui::MenuItem("Game View");
      ImGui::MenuItem("Console");
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  // Stats window
  {
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("Stats");
    ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
    ImGui::Text("Frame Time: %.3f ms", deltaTime * 1000.0f);
    ImGui::Text("Time: %.2f s", GetTime());
    ImGui::End();
  }

  ImGuiLayer::EndFrame();
}

// Callbacks
void Application::FramebufferSizeCallback(GLFWwindow *window, int width,
                                          int height) {
  Application *app =
      static_cast<Application *>(glfwGetWindowUserPointer(window));
  if (app) {
    app->windowWidth = width;
    app->windowHeight = height;
    glViewport(0, 0, width, height);
  }
}

void Application::KeyCallback(GLFWwindow *window, int key, int scancode,
                              int action, int mods) {
  // Input system handles this through polling
}

void Application::MouseButtonCallback(GLFWwindow *window, int button,
                                      int action, int mods) {
  // Input system handles this through polling
}

void Application::CursorPosCallback(GLFWwindow *window, double xpos,
                                    double ypos) {
  // Input system handles this through polling
}
