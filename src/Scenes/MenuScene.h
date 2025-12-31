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

    std::vector<std::shared_ptr<GameObject>> gameObjects;

    // Screen dimensions (will be passed or obtained from Application)
    float screenWidth = 800.0f;
    float screenHeight = 600.0f;
};

#endif // MOLGA_MENU_SCENE_H
