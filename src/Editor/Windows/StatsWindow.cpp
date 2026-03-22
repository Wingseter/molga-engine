#include "StatsWindow.h"
#include "../EditorConstants.h"
#include "../../MolgaTime.h"
#include <imgui.h>

StatsWindow::StatsWindow()
    : EditorWindow(EditorConstants::WIN_STATS) {
}

void StatsWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::Begin(title.c_str(), &isOpen);
    ImGui::Text("FPS: %.1f", Time::GetFPS());
    ImGui::Text("Delta Time: %.3f ms", Time::GetDeltaTime() * 1000.0f);
    ImGui::Text("Frame: %d", Time::GetFrameCount());
    ImGui::Separator();
    ImGui::Text("Docking: Enabled");
    ImGui::Text("Viewports: Enabled");
    ImGui::End();
}
