#pragma once

#include <map>
#include <memory>
#include <string>
#include "Windows/EditorWindow.h"

class WindowManager {
public:
    void Register(const std::string& name, std::unique_ptr<EditorWindow> window);
    EditorWindow* Get(const std::string& name) const;

    template<typename T>
    T* GetAs(const std::string& name) const {
        return dynamic_cast<T*>(Get(name));
    }

    void SetVisible(const std::string& name, bool visible);
    bool IsVisible(const std::string& name) const;
    void Toggle(const std::string& name);

    void RenderAll();
    void RenderWindowMenu();
    void ShutdownAll();

private:
    // Ordered map for consistent menu rendering order
    std::map<std::string, std::unique_ptr<EditorWindow>> windows;
};
