#include "ImGuiLayer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

bool ImGuiLayer::initialized = false;

void ImGuiLayer::Init(GLFWwindow *window) {
  if (initialized)
    return;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  SetDarkTheme();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  initialized = true;
}

void ImGuiLayer::Shutdown() {
  if (!initialized)
    return;

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  initialized = false;
}

void ImGuiLayer::BeginFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiLayer::EndFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiLayer::SetDarkTheme() {
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *colors = style.Colors;

  // Dark theme inspired by Visual Studio
  colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.13f, 1.0f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_Border] = ImVec4(0.44f, 0.37f, 0.61f, 0.29f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.24f);
  colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
  colors[ImGuiCol_Header] = ImVec4(0.13f, 0.13f, 0.17f, 1.0f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.19f, 0.19f, 0.25f, 1.0f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_Button] = ImVec4(0.13f, 0.13f, 0.17f, 1.0f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.25f, 1.0f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.25f, 1.0f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.24f, 0.24f, 0.32f, 1.0f);
  colors[ImGuiCol_TabActive] = ImVec4(0.2f, 0.2f, 0.28f, 1.0f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_TitleBg] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1f, 0.1f, 0.13f, 1.0f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.16f, 0.16f, 0.21f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.19f, 0.19f, 0.25f, 1.0f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.24f, 0.24f, 0.32f, 1.0f);
  colors[ImGuiCol_CheckMark] = ImVec4(0.74f, 0.58f, 0.98f, 1.0f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.44f, 0.37f, 0.61f, 0.54f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.74f, 0.58f, 0.98f, 0.54f);
  colors[ImGuiCol_Separator] = ImVec4(0.44f, 0.37f, 0.61f, 0.29f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.74f, 0.58f, 0.98f, 0.29f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.74f, 0.58f, 0.98f, 0.29f);
  colors[ImGuiCol_ResizeGrip] = ImVec4(0.44f, 0.37f, 0.61f, 0.29f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.74f, 0.58f, 0.98f, 0.29f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(0.74f, 0.58f, 0.98f, 0.29f);

  style.TabRounding = 4;
  style.ScrollbarRounding = 9;
  style.WindowRounding = 7;
  style.GrabRounding = 3;
  style.FrameRounding = 3;
  style.PopupRounding = 4;
  style.ChildRounding = 4;
}

void ImGuiLayer::EnableDocking() {
  // Docking requires ImGui docking branch
  // For now, this is a no-op
}

bool ImGuiLayer::WantCaptureMouse() { return ImGui::GetIO().WantCaptureMouse; }

bool ImGuiLayer::WantCaptureKeyboard() {
  return ImGui::GetIO().WantCaptureKeyboard;
}
