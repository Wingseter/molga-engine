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
    // Calculate button positions
    startBtnX = screenWidth / 2.0f - 100.0f;
    startBtnY = 300.0f;
    startBtnW = 200.0f;
    startBtnH = 50.0f;

    quitBtnX = screenWidth / 2.0f - 100.0f;
    quitBtnY = 370.0f;
    quitBtnW = 200.0f;
    quitBtnH = 50.0f;

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
    transform->SetPosition(startBtnX, startBtnY);
    sprite = startBtn->AddComponent<SpriteRenderer>();
    sprite->SetSize(startBtnW, startBtnH);
    sprite->SetColor(0.3f, 0.6f, 0.3f, 1.0f);
    gameObjects.push_back(startBtn);

    // Quit button
    auto quitBtn = std::make_shared<GameObject>("QuitButton");
    transform = quitBtn->AddComponent<Transform>();
    transform->SetPosition(quitBtnX, quitBtnY);
    sprite = quitBtn->AddComponent<SpriteRenderer>();
    sprite->SetSize(quitBtnW, quitBtnH);
    sprite->SetColor(0.6f, 0.3f, 0.3f, 1.0f);
    gameObjects.push_back(quitBtn);
}

bool MenuScene::IsPointInButton(float px, float py, float bx, float by, float bw, float bh) {
    return px >= bx && px <= bx + bw && py >= by && py <= by + bh;
}

void MenuScene::Update(float dt) {
    float mouseX = Input::GetMouseX();
    float mouseY = Input::GetMouseY();

    // Check hover states
    startHovered = IsPointInButton(mouseX, mouseY, startBtnX, startBtnY, startBtnW, startBtnH);
    quitHovered = IsPointInButton(mouseX, mouseY, quitBtnX, quitBtnY, quitBtnW, quitBtnH);

    // Update button colors based on hover/selected state
    for (auto& obj : gameObjects) {
        if (obj->GetName() == "StartButton") {
            auto sr = obj->GetComponent<SpriteRenderer>();
            if (sr) {
                if (startHovered || selectedButton == 0) {
                    sr->SetColor(0.4f, 0.8f, 0.4f, 1.0f);  // Bright green when hovered/selected
                } else {
                    sr->SetColor(0.3f, 0.6f, 0.3f, 1.0f);  // Normal green
                }
            }
        } else if (obj->GetName() == "QuitButton") {
            auto sr = obj->GetComponent<SpriteRenderer>();
            if (sr) {
                if (quitHovered || selectedButton == 1) {
                    sr->SetColor(0.8f, 0.4f, 0.4f, 1.0f);  // Bright red when hovered/selected
                } else {
                    sr->SetColor(0.6f, 0.3f, 0.3f, 1.0f);  // Normal red
                }
            }
        }
    }

    // Keyboard navigation
    if (Input::GetKeyDown(GLFW_KEY_UP) || Input::GetKeyDown(GLFW_KEY_W)) {
        selectedButton = 0;
    }
    if (Input::GetKeyDown(GLFW_KEY_DOWN) || Input::GetKeyDown(GLFW_KEY_S)) {
        selectedButton = 1;
    }

    // Keyboard activation
    if (Input::GetKeyDown(GLFW_KEY_ENTER) || Input::GetKeyDown(GLFW_KEY_SPACE)) {
        if (selectedButton == 0) {
            SceneManager::ChangeScene("Game");
        } else if (selectedButton == 1) {
            // Request quit - will be handled by main loop
            extern GLFWwindow* g_window;
            if (g_window) {
                glfwSetWindowShouldClose(g_window, true);
            }
        }
    }

    // Mouse click
    if (Input::GetMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT)) {
        if (startHovered) {
            SceneManager::ChangeScene("Game");
        } else if (quitHovered) {
            extern GLFWwindow* g_window;
            if (g_window) {
                glfwSetWindowShouldClose(g_window, true);
            }
        }
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
