#pragma once

#include "EditorWindow.h"

class ProjectSettingsWindow : public EditorWindow {
public:
    ProjectSettingsWindow();
    void OnGUI() override;
};
