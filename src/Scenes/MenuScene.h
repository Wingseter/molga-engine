#ifndef MOLGA_MENU_SCENE_H
#define MOLGA_MENU_SCENE_H

#include "../Scene.h"
#include "../ECS/GameObject.h"
#include <vector>
#include <memory>

class MenuScene : public Scene {
public:
    MenuScene();
    ~MenuScene() override = default;

    void OnEnter() override;
    void OnExit() override;
    void Update(float dt) override;
    void Render(Renderer* renderer, Shader* shader, Camera2D* camera) override;

    // Access to game objects for ECS integration
    std::vector<std::shared_ptr<GameObject>>& GetGameObjects() { return gameObjects; }

private:
    void CreateUI();
    bool IsPointInButton(float px, float py, float bx, float by, float bw, float bh);

    std::vector<std::shared_ptr<GameObject>> gameObjects;

    // Screen dimensions (will be passed or obtained from Application)
    float screenWidth = 800.0f;
    float screenHeight = 600.0f;

    // Button rectangles (x, y, width, height)
    float startBtnX = 0.0f, startBtnY = 0.0f, startBtnW = 200.0f, startBtnH = 50.0f;
    float quitBtnX = 0.0f, quitBtnY = 0.0f, quitBtnW = 200.0f, quitBtnH = 50.0f;

    // Button states
    bool startHovered = false;
    bool quitHovered = false;
    int selectedButton = 0;  // 0 = Start, 1 = Quit (for keyboard navigation)
};

#endif // MOLGA_MENU_SCENE_H
