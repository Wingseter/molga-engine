#include "EditorState.h"
#include "../Core/MolgaTime.h"
#include <iostream>
#include "Editor.h"
#include "EditorConstants.h"
#include "Windows/ConsoleWindow.h"

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
        if (onEnterPlay_ && !onEnterPlay_()) {
            return;
        }

        Time::ResetFixedAccumulator();

        if (auto* console = Editor::Get().GetWindowManager().GetAs<ConsoleWindow>(EditorConstants::WIN_CONSOLE)) {
            if (console->IsClearOnPlay()) {
                console->RequestClear();
            }
        }

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
        if (onExitPlay_) onExitPlay_();
        SetMode(EditorMode::Edit);
    }
}
