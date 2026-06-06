#pragma once

#include <imgui.h>

namespace EditorTheme {
    // Play controls
    inline const ImVec4 PLAY_BUTTON    = ImVec4(0.2f, 0.6f, 0.2f, 1.0f);
    inline const ImVec4 PAUSE_BUTTON   = ImVec4(0.8f, 0.6f, 0.2f, 1.0f);
    inline const ImVec4 STOP_BUTTON    = ImVec4(0.7f, 0.2f, 0.2f, 1.0f);

    // Play state indicators
    inline const ImVec4 STATE_EDIT     = ImVec4(0.5f, 0.8f, 0.5f, 1.0f);
    inline const ImVec4 STATE_PLAYING  = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
    inline const ImVec4 STATE_PAUSED   = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);

    // File type colors (Project Browser)
    namespace FileColor {
        inline const ImVec4 FOLDER   = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
        inline const ImVec4 IMAGE    = ImVec4(0.2f, 0.7f, 0.9f, 1.0f);
        inline const ImVec4 JSON     = ImVec4(0.2f, 0.9f, 0.4f, 1.0f);
        inline const ImVec4 AUDIO    = ImVec4(0.9f, 0.4f, 0.6f, 1.0f);
        inline const ImVec4 DEFAULT  = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    }

    // Warning/Error colors
    inline const ImVec4 WARNING_TEXT   = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    inline const ImVec4 ERROR_TEXT     = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    inline const ImVec4 DISABLED_TEXT  = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
}
