#include "WindowManager.h"
#include <imgui.h>

void WindowManager::Register(const std::string& name, std::unique_ptr<EditorWindow> window) {
    windows[name] = std::move(window);
}

EditorWindow* WindowManager::Get(const std::string& name) const {
    auto it = windows.find(name);
    if (it != windows.end()) {
        return it->second.get();
    }
    return nullptr;
}

void WindowManager::SetVisible(const std::string& name, bool visible) {
    auto* win = Get(name);
    if (win) {
        win->SetOpen(visible);
    }
}

bool WindowManager::IsVisible(const std::string& name) const {
    auto* win = Get(name);
    return win && win->IsOpen();
}

void WindowManager::Toggle(const std::string& name) {
    auto* win = Get(name);
    if (win) {
        win->Toggle();
    }
}

void WindowManager::RenderAll() {
    for (auto& [name, window] : windows) {
        if (window && window->IsOpen()) {
            window->OnGUI();
        }
    }
}

void WindowManager::RenderWindowMenu() {
    for (auto& [name, window] : windows) {
        if (window) {
            bool open = window->IsOpen();
            if (ImGui::MenuItem(name.c_str(), nullptr, &open)) {
                window->SetOpen(open);
            }
        }
    }
}

void WindowManager::ShutdownAll() {
    windows.clear();
}
