#pragma once

#include "EditorWindow.h"
#include <cstddef>

class ProfilerWindow : public EditorWindow {
public:
    ProfilerWindow();
    void OnGUI() override;

private:
    void DrawTimeline();
    void DrawSelectedFrame();
    long long selectedFrameIndex_ = -1;  // -1 = 최신 프레임 자동 추적
};
