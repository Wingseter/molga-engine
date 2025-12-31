#include "EditorState.h"
#include <iostream>

EditorState& EditorState::Get() {
    static EditorState instance;
    return instance;
}

void EditorState::SetMode(EditorMode mode) {
    if (currentMode != mode) {
        currentMode = mode;

        switch (mode) {
            case EditorMode::Edit:
                std::cout << "[Editor] Switched to Edit Mode" << std::endl;
                break;
            case EditorMode::Play:
                std::cout << "[Editor] Switched to Play Mode" << std::endl;
                break;
            case EditorMode::Pause:
                std::cout << "[Editor] Game Paused" << std::endl;
                break;
        }
    }
}

void EditorState::Play() {
    if (currentMode == EditorMode::Edit) {
        // TODO: Save scene state before playing
        SetMode(EditorMode::Play);
    } else if (currentMode == EditorMode::Pause) {
        SetMode(EditorMode::Play);
    }
}

void EditorState::Pause() {
    if (currentMode == EditorMode::Play) {
        SetMode(EditorMode::Pause);
    }
}

void EditorState::Stop() {
    if (currentMode == EditorMode::Play || currentMode == EditorMode::Pause) {
        // TODO: Restore scene state after stopping
        SetMode(EditorMode::Edit);
    }
}
