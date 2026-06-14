#pragma once
#include <functional>

enum class EditorMode {
    Edit,   // Scene editing mode
    Play,   // Game preview mode
    Pause   // Game paused
};

class EditorState {
public:
    static EditorState& Get();

    void SetMode(EditorMode mode);
    EditorMode GetMode() const { return currentMode; }

    bool IsEditMode() const { return currentMode == EditorMode::Edit; }
    bool IsPlayMode() const { return currentMode == EditorMode::Play; }
    bool IsPaused() const { return currentMode == EditorMode::Pause; }

    void Play();
    void Pause();
    void Stop();

    using PlayTransition = std::function<void()>;
    void SetPlayCallbacks(PlayTransition onEnterPlay, PlayTransition onExitPlay) {
        onEnterPlay_ = std::move(onEnterPlay);
        onExitPlay_ = std::move(onExitPlay);
    }

    // Time scale for play mode
    float GetTimeScale() const { return timeScale; }
    void SetTimeScale(float scale) { timeScale = scale; }

private:
    EditorState() = default;
    EditorState(const EditorState&) = delete;
    EditorState& operator=(const EditorState&) = delete;

    EditorMode currentMode = EditorMode::Edit;
    float timeScale = 1.0f;
    PlayTransition onEnterPlay_;
    PlayTransition onExitPlay_;
};
