#include "ImGuiLayer.h"
#include "FontManager.h"
#include "Core/Bootstrap.h"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

bool ImGuiLayer::initialized = false;
EngineHost* ImGuiLayer::currentHost = nullptr;

void ImGuiLayer::Init(EngineHost& host) {
  if (initialized)
    return;

  currentHost = &host;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;    // Enable Multi-Viewport

  // When viewports are enabled, tweak WindowRounding/WindowBg for platform windows
  ImGuiStyle& style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  // Initialize custom fonts before backends
  FontManager::Get().Init();

  SetModernTheme();

  auto* window = static_cast<SDL_Window*>(host.NativeWindowHandle());
  ImGui_ImplSDL3_InitForOpenGL(window, host.NativeGLContextHandle());
  ImGui_ImplOpenGL3_Init("#version 330");
  host.SetNativeEventObserver([](const void* event) {
    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(event));
  });

  initialized = true;
}

void ImGuiLayer::Shutdown() {
  if (!initialized)
    return;

  ImGui_ImplOpenGL3_Shutdown();
  if (currentHost) currentHost->SetNativeEventObserver({});
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  initialized = false;
  currentHost = nullptr;
}

void ImGuiLayer::BeginFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void ImGuiLayer::EndFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  // Handle multi-viewport rendering
  ImGuiIO& io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
    SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    if (backupWindow && backupContext) {
      SDL_GL_MakeCurrent(backupWindow, backupContext);
    }
  }
}

void ImGuiLayer::SetModernTheme() {
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *colors = style.Colors;

  // === Modern Color Palette ===
  // Background colors
  const ImVec4 bgDark       = ImVec4(0.059f, 0.059f, 0.102f, 1.0f);  // #0F0F1A
  const ImVec4 bgPanel      = ImVec4(0.102f, 0.102f, 0.180f, 1.0f);  // #1A1A2E
  const ImVec4 bgElevated   = ImVec4(0.145f, 0.145f, 0.212f, 1.0f);  // #252536
  const ImVec4 bgHover      = ImVec4(0.180f, 0.180f, 0.250f, 1.0f);  // Slightly lighter

  // Accent colors
  const ImVec4 accentPurple = ImVec4(0.420f, 0.298f, 0.902f, 1.0f);  // #6B4CE6
  const ImVec4 accentCyan   = ImVec4(0.306f, 0.804f, 0.769f, 1.0f);  // #4ECDC4
  const ImVec4 accentBlue   = ImVec4(0.231f, 0.510f, 0.965f, 1.0f);  // #3B82F6

  // Text colors
  const ImVec4 textPrimary  = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  const ImVec4 textSecondary= ImVec4(0.627f, 0.627f, 0.690f, 1.0f);  // #A0A0B0
  const ImVec4 textDisabled = ImVec4(0.376f, 0.376f, 0.439f, 1.0f);  // #606070

  // Border
  const ImVec4 border       = ImVec4(0.239f, 0.239f, 0.361f, 0.5f);  // Subtle border

  // === Apply Colors ===
  // Window
  colors[ImGuiCol_WindowBg]           = bgPanel;
  colors[ImGuiCol_ChildBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_PopupBg]            = bgElevated;
  colors[ImGuiCol_Border]             = border;
  colors[ImGuiCol_BorderShadow]       = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

  // Frame
  colors[ImGuiCol_FrameBg]            = bgDark;
  colors[ImGuiCol_FrameBgHovered]     = bgElevated;
  colors[ImGuiCol_FrameBgActive]      = accentPurple;

  // Title
  colors[ImGuiCol_TitleBg]            = bgDark;
  colors[ImGuiCol_TitleBgActive]      = bgPanel;
  colors[ImGuiCol_TitleBgCollapsed]   = bgDark;

  // Menu
  colors[ImGuiCol_MenuBarBg]          = bgDark;

  // Scrollbar
  colors[ImGuiCol_ScrollbarBg]        = bgDark;
  colors[ImGuiCol_ScrollbarGrab]      = bgElevated;
  colors[ImGuiCol_ScrollbarGrabHovered] = accentPurple;
  colors[ImGuiCol_ScrollbarGrabActive]  = accentCyan;

  // Checkmark & Slider
  colors[ImGuiCol_CheckMark]          = accentCyan;
  colors[ImGuiCol_SliderGrab]         = accentPurple;
  colors[ImGuiCol_SliderGrabActive]   = accentCyan;

  // Button
  colors[ImGuiCol_Button]             = bgElevated;
  colors[ImGuiCol_ButtonHovered]      = accentPurple;
  colors[ImGuiCol_ButtonActive]       = accentCyan;

  // Header (collapsing headers, tree nodes, selectable)
  colors[ImGuiCol_Header]             = bgElevated;
  colors[ImGuiCol_HeaderHovered]      = accentPurple;
  colors[ImGuiCol_HeaderActive]       = accentCyan;

  // Separator
  colors[ImGuiCol_Separator]          = border;
  colors[ImGuiCol_SeparatorHovered]   = accentPurple;
  colors[ImGuiCol_SeparatorActive]    = accentCyan;

  // Resize grip
  colors[ImGuiCol_ResizeGrip]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_ResizeGripHovered]  = accentPurple;
  colors[ImGuiCol_ResizeGripActive]   = accentCyan;

  // Tabs
  colors[ImGuiCol_Tab]                = bgDark;
  colors[ImGuiCol_TabHovered]         = accentPurple;
  colors[ImGuiCol_TabActive]          = bgPanel;
  colors[ImGuiCol_TabUnfocused]       = bgDark;
  colors[ImGuiCol_TabUnfocusedActive] = bgElevated;

  // Docking
  colors[ImGuiCol_DockingPreview]     = ImVec4(accentPurple.x, accentPurple.y, accentPurple.z, 0.7f);
  colors[ImGuiCol_DockingEmptyBg]     = bgDark;

  // Text
  colors[ImGuiCol_Text]               = textPrimary;
  colors[ImGuiCol_TextDisabled]       = textDisabled;

  // Plot
  colors[ImGuiCol_PlotLines]          = accentCyan;
  colors[ImGuiCol_PlotLinesHovered]   = accentPurple;
  colors[ImGuiCol_PlotHistogram]      = accentCyan;
  colors[ImGuiCol_PlotHistogramHovered] = accentPurple;

  // Table
  colors[ImGuiCol_TableHeaderBg]      = bgElevated;
  colors[ImGuiCol_TableBorderStrong]  = border;
  colors[ImGuiCol_TableBorderLight]   = ImVec4(border.x, border.y, border.z, 0.3f);
  colors[ImGuiCol_TableRowBg]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
  colors[ImGuiCol_TableRowBgAlt]      = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

  // Navigation highlight
  colors[ImGuiCol_NavHighlight]       = accentCyan;
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
  colors[ImGuiCol_NavWindowingDimBg]  = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
  colors[ImGuiCol_ModalWindowDimBg]   = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);

  // === Style Variables ===
  style.WindowRounding    = 6.0f;
  style.ChildRounding     = 4.0f;
  style.FrameRounding     = 4.0f;
  style.PopupRounding     = 4.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding      = 4.0f;
  style.TabRounding       = 4.0f;

  style.WindowPadding     = ImVec2(10.0f, 10.0f);
  style.FramePadding      = ImVec2(8.0f, 4.0f);
  style.ItemSpacing       = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
  style.IndentSpacing     = 20.0f;

  style.WindowBorderSize  = 1.0f;
  style.FrameBorderSize   = 0.0f;
  style.PopupBorderSize   = 1.0f;
  style.TabBorderSize     = 0.0f;

  style.ScrollbarSize     = 12.0f;
  style.GrabMinSize       = 10.0f;

  style.WindowMenuButtonPosition = ImGuiDir_None;  // Hide window menu button for cleaner look
}

void ImGuiLayer::SetDarkTheme() {
  // Legacy function - now calls SetModernTheme
  SetModernTheme();
}

void ImGuiLayer::EnableDocking() {
  // Docking is now enabled by default in Init()
  // This function is kept for backward compatibility
}

bool ImGuiLayer::WantCaptureMouse() {
  return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::WantCaptureKeyboard() {
  return ImGui::GetIO().WantCaptureKeyboard;
}
