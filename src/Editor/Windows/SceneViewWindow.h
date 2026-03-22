#ifndef MOLGA_SCENE_VIEW_WINDOW_H
#define MOLGA_SCENE_VIEW_WINDOW_H

#include "EditorWindow.h"

class SceneViewWindow : public EditorWindow {
public:
    SceneViewWindow();
    void OnGUI() override;
};

#endif // MOLGA_SCENE_VIEW_WINDOW_H
