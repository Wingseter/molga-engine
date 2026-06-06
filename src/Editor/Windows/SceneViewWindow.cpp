#include "SceneViewWindow.h"
#include "../EditorConstants.h"
#include <imgui.h>

SceneViewWindow::SceneViewWindow()
    : EditorWindow(EditorConstants::WIN_SCENE) {
}

void SceneViewWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::Begin(title.c_str(), &isOpen);

    // Get available size for the scene view
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    // Placeholder for scene rendering
    ImGui::Text("Scene View");
    ImGui::Text("Size: %.0f x %.0f", viewportSize.x, viewportSize.y);

    // Draw a placeholder rectangle
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(pos, ImVec2(pos.x + viewportSize.x, pos.y + viewportSize.y - 40),
                            IM_COL32(30, 30, 50, 255));
    drawList->AddRect(pos, ImVec2(pos.x + viewportSize.x, pos.y + viewportSize.y - 40),
                      IM_COL32(100, 100, 150, 255));

    ImGui::End();
}
