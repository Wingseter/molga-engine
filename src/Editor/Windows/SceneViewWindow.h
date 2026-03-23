#pragma once

#include "EditorWindow.h"

class SceneViewWindow : public EditorWindow {
public:
    SceneViewWindow();
    void OnGUI() override;
};
