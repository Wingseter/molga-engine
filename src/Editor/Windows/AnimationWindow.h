#pragma once

#include "EditorWindow.h"
#include "Rendering/AnimationClip2D.h"
#include "Rendering/AnimatorController2D.h"

#include <filesystem>
#include <string>

// Asset-only animation authoring. Preview state is deliberately kept in the
// window instead of a scene component, so scrubbing never dirties the scene or
// enters its undo history.
class AnimationWindow final : public EditorWindow {
public:
    AnimationWindow();

    void SetAsset(const std::string& path);
    void OnGUI() override;

private:
    enum class AssetKind { None, Clip, Controller };

    void Reload();
    void Save();
    void DrawToolbar();
    void DrawClip();
    void DrawController();

    std::filesystem::path assetPath_;
    std::string assetGuid_;
    AssetKind kind_ = AssetKind::None;
    molga::AnimationClip2D clip_;
    molga::AnimatorController2D controller_;
    std::string error_;
    bool dirty_ = false;
    bool previewPlaying_ = false;
    float previewTime_ = 0.0f;
};
