#pragma once

#include "Editor/EditorPreferences.h"
#include "Editor/GameViewLayout.h"
#include "Editor/Windows/EditorWindow.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/GameOutputRenderer.h"
#include "Rendering/PixelSize.h"
#include "Platform/Window.h"

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

class GameObject;
class Renderer;
class Shader;

class GameViewWindow : public EditorWindow {
public:
    GameViewWindow();
    ~GameViewWindow() override;

    void OnGUI() override;
    void SetSceneResources(
        Renderer* renderer,
        Shader* spriteShader,
        std::vector<std::shared_ptr<GameObject>>* objects);
    void SetGameObjects(std::vector<std::shared_ptr<GameObject>>* objects) {
        gameObjects_ = objects;
    }

    // Called before gameplay scripts. Input is sampled from this panel's
    // native platform window and mapped to exact game-output pixels.
    void ProcessPlayInput();
    void ResetPlayInput();

private:
    molga::PixelSize RequestedOutputSize() const;
    molga::PixelSize RequestedLogicalSize() const;
    molga::GameOutputScaleMode RequestedScaleMode() const;
    void DrawToolbar();
    void SavePreferences();
    void LoseInputFocus();

    Renderer* renderer_ = nullptr;
    Shader* spriteShader_ = nullptr;
    std::vector<std::shared_ptr<GameObject>>* gameObjects_ = nullptr;
    Framebuffer fbo_;
    molga::GameOutputRenderer outputRenderer_;

    molga::EditorPreferences preferences_;
    std::filesystem::path preferencePath_;
    molga::PixelSize activeOutput_{};
    molga::PixelSize lastAttemptedOutput_{};
    molga::PixelSize logicalOutput_{};
    molga::OutputPresentationLayout presentation_{};
    molga::GameViewLayout layout_{};
    std::string outputError_;
    bool hasOutputCamera_ = false;

    ImVec2 imageScreenOrigin_{0.0f, 0.0f};
    ImGuiID platformViewportId_ = 0;
    bool imageValid_ = false;
    bool inputFocused_ = false;
    bool rawMouseWasDown_ = false;
    molga::WindowId inputSourceWindow_ = 0;
};
