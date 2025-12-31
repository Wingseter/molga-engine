#include "MenuScene.h"
#include "../Renderer.h"
#include "../Shader.h"
#include "../Camera2D.h"
#include "../Input.h"
#include "../Sprite.h"
#include "../TextRenderer.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/SpriteRenderer.h"
#include <GLFW/glfw3.h>

MenuScene::MenuScene() : Scene("Menu") {
}

void MenuScene::OnEnter() {
    CreateUI();
}

void MenuScene::OnExit() {
    gameObjects.clear();
}

void MenuScene::CreateUI() {
    // Title background
    auto titleBg = std::make_shared<GameObject>("TitleBackground");
    auto transform = titleBg->AddComponent<Transform>();
    transform->SetPosition(screenWidth / 2.0f - 200.0f, 150.0f);
    auto sprite = titleBg->AddComponent<SpriteRenderer>();
    sprite->SetSize(400.0f, 80.0f);
    sprite->SetColor(0.2f, 0.3f, 0.5f, 1.0f);
    gameObjects.push_back(titleBg);

    // Start button
    auto startBtn = std::make_shared<GameObject>("StartButton");
    transform = startBtn->AddComponent<Transform>();
    transform->SetPosition(screenWidth / 2.0f - 100.0f, 300.0f);
    sprite = startBtn->AddComponent<SpriteRenderer>();
    sprite->SetSize(200.0f, 50.0f);
    sprite->SetColor(0.3f, 0.6f, 0.3f, 1.0f);
    gameObjects.push_back(startBtn);

    // Quit button
    auto quitBtn = std::make_shared<GameObject>("QuitButton");
    transform = quitBtn->AddComponent<Transform>();
    transform->SetPosition(screenWidth / 2.0f - 100.0f, 370.0f);
    sprite = quitBtn->AddComponent<SpriteRenderer>();
    sprite->SetSize(200.0f, 50.0f);
    sprite->SetColor(0.6f, 0.3f, 0.3f, 1.0f);
    gameObjects.push_back(quitBtn);
}

void MenuScene::Update(float dt) {
    // Press ENTER or SPACE to start game
    if (Input::GetKeyDown(GLFW_KEY_ENTER) || Input::GetKeyDown(GLFW_KEY_SPACE)) {
        SceneManager::ChangeScene("Game");
    }

    // Update all game objects
    for (auto& obj : gameObjects) {
        if (obj && obj->IsActive()) {
            obj->Update(dt);
        }
    }
}

void MenuScene::Render(Renderer* renderer, Shader* shader, Camera2D* camera) {
    renderer->Clear(0.1f, 0.1f, 0.15f, 1.0f);

    // Render without camera for UI
    renderer->Begin(shader, nullptr);

    for (auto& obj : gameObjects) {
        if (obj && obj->IsActive()) {
            auto sr = obj->GetComponent<SpriteRenderer>();
            if (sr) {
                auto transform = obj->GetComponent<Transform>();
                if (transform) {
                    Sprite sprite;
                    sprite.SetPosition(transform->GetX(), transform->GetY());
                    sprite.SetSize(sr->GetWidth(), sr->GetHeight());
                    const Color& c = sr->GetColor();
                    sprite.SetColor(c.r, c.g, c.b, c.a);
                    renderer->DrawSprite(&sprite);
                }
            }
        }
    }

    renderer->End();

    // Render text labels
    TextRenderer& textRenderer = TextRenderer::Get();

    // Title text - centered on title background
    float titleScale = 4.0f;
    std::string titleText = "MOLGA ENGINE";
    float titleWidth = textRenderer.GetTextWidth(titleText, titleScale);
    float titleX = screenWidth / 2.0f - titleWidth / 2.0f;
    textRenderer.RenderText(renderer, shader, titleText, titleX, 165.0f, titleScale, Color::White());

    // Start button text - centered on button
    float buttonScale = 3.0f;
    std::string startText = "Start";
    float startWidth = textRenderer.GetTextWidth(startText, buttonScale);
    float startX = screenWidth / 2.0f - startWidth / 2.0f;
    textRenderer.RenderText(renderer, shader, startText, startX, 312.0f, buttonScale, Color::White());

    // Quit button text - centered on button
    std::string quitText = "Quit";
    float quitWidth = textRenderer.GetTextWidth(quitText, buttonScale);
    float quitX = screenWidth / 2.0f - quitWidth / 2.0f;
    textRenderer.RenderText(renderer, shader, quitText, quitX, 382.0f, buttonScale, Color::White());

    // Instructions text
    float instrScale = 2.0f;
    std::string instrText = "Press ENTER or SPACE to start";
    float instrWidth = textRenderer.GetTextWidth(instrText, instrScale);
    float instrX = screenWidth / 2.0f - instrWidth / 2.0f;
    textRenderer.RenderText(renderer, shader, instrText, instrX, 500.0f, instrScale, Color(0.7f, 0.7f, 0.7f, 1.0f));
}
