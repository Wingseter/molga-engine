#ifndef MOLGA_BUILTIN_SCRIPTS_H
#define MOLGA_BUILTIN_SCRIPTS_H

#include "Script.h"
#include "../Input.h"
#include "../ECS/Components/Transform.h"
#include <GLFW/glfw3.h>

// Example: Player Controller Script
class PlayerController : public Script {
public:
    SCRIPT_CLASS(PlayerController)

    float moveSpeed = 200.0f;

    void Start() override {
        // Called once when the game starts
    }

    void Update(float dt) override {
        Transform* transform = GetTransform();
        if (!transform) return;

        float dx = 0.0f, dy = 0.0f;

        if (Input::GetKey(GLFW_KEY_W) || Input::GetKey(GLFW_KEY_UP))    dy -= 1.0f;
        if (Input::GetKey(GLFW_KEY_S) || Input::GetKey(GLFW_KEY_DOWN))  dy += 1.0f;
        if (Input::GetKey(GLFW_KEY_A) || Input::GetKey(GLFW_KEY_LEFT))  dx -= 1.0f;
        if (Input::GetKey(GLFW_KEY_D) || Input::GetKey(GLFW_KEY_RIGHT)) dx += 1.0f;

        transform->Translate(dx * moveSpeed * dt, dy * moveSpeed * dt);
    }
};

// Example: Rotator Script
class Rotator : public Script {
public:
    SCRIPT_CLASS(Rotator)

    float rotationSpeed = 90.0f;  // degrees per second

    void Update(float dt) override {
        Transform* transform = GetTransform();
        if (!transform) return;

        float currentRot = transform->GetRotation();
        transform->SetRotation(currentRot + rotationSpeed * dt);
    }
};

// Example: Oscillator Script (moves back and forth)
class Oscillator : public Script {
public:
    SCRIPT_CLASS(Oscillator)

    float amplitude = 50.0f;
    float frequency = 2.0f;
    bool horizontal = true;

    void Start() override {
        Transform* transform = GetTransform();
        if (transform) {
            startPosition = transform->GetPosition();
        }
    }

    void Update(float dt) override {
        Transform* transform = GetTransform();
        if (!transform) return;

        time += dt;
        float offset = amplitude * std::sin(time * frequency * 3.14159f * 2.0f);

        if (horizontal) {
            transform->SetPosition(startPosition.x + offset, startPosition.y);
        } else {
            transform->SetPosition(startPosition.x, startPosition.y + offset);
        }
    }

private:
    Vector2 startPosition;
    float time = 0.0f;
};

// Register all builtin scripts
void RegisterBuiltinScripts();

#endif // MOLGA_BUILTIN_SCRIPTS_H
