#ifndef MOLGA_EDITOR_STATE_H
#define MOLGA_EDITOR_STATE_H

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

    // Time scale for play mode
    float GetTimeScale() const { return timeScale; }
    void SetTimeScale(float scale) { timeScale = scale; }

private:
    EditorState() = default;
    EditorState(const EditorState&) = delete;
    EditorState& operator=(const EditorState&) = delete;

    EditorMode currentMode = EditorMode::Edit;
    float timeScale = 1.0f;
};

#endif // MOLGA_EDITOR_STATE_H
