#ifndef MOLGA_EDITOR_WINDOW_H
#define MOLGA_EDITOR_WINDOW_H

#include <string>

class EditorWindow {
public:
    EditorWindow(const std::string& title) : title(title) {}
    virtual ~EditorWindow() = default;

    // Called every frame to render the window
    virtual void OnGUI() = 0;

    // Window visibility
    bool IsOpen() const { return isOpen; }
    void SetOpen(bool open) { isOpen = open; }
    void Toggle() { isOpen = !isOpen; }

    // Window title
    const std::string& GetTitle() const { return title; }
    void SetTitle(const std::string& t) { title = t; }

protected:
    std::string title;
    bool isOpen = true;
};

#endif // MOLGA_EDITOR_WINDOW_H
