#pragma once

#include "Script.h"
#include "../Systems/Input.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/GameObject.h"
#include "../Core/World.h"
#include <GLFW/glfw3.h>

#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

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

    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Float("Move Speed", &moveSpeed, 1.0f, 0.0f, 1000.0f);
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

    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Float("Rotation Speed", &rotationSpeed, 1.0f, -360.0f, 360.0f);
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

    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Float("Amplitude", &amplitude, 1.0f, 0.0f, 500.0f)
         .Float("Frequency", &frequency, 0.1f, 0.0f, 10.0f)
         .Bool("Horizontal", &horizontal);
    }

private:
    Vector2 startPosition;
    float time = 0.0f;
};

// Example: Spawner Script (clones a referenced object periodically)
class Spawner : public Script {
public:
    SCRIPT_CLASS(Spawner)

    float spawnInterval = 2.0f;
    ObjectRef target;   // 복제할 대상. 인스펙터에서 씬 오브젝트를 지정한다.

    void Start() override {
        // 수동 타이머 대신 InvokeRepeating으로 주기적 스폰을 예약한다.
        InvokeRepeating([this]() {
            if (GameObject* src = Resolve(target)) {
                Vector2 myPos = Vector2::Zero();
                if (auto* transform = GetTransform()) {
                    myPos = transform->GetPosition();
                }
                Instantiate(src, myPos);
            }
        }, spawnInterval, spawnInterval);
    }

    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Float("Spawn Interval", &spawnInterval, 0.1f, 0.1f, 10.0f)
         .Object("Target", &target);
    }
};

// Example: SelfDestruct Script (destroys itself after a delay)
class SelfDestruct : public Script {
public:
    SCRIPT_CLASS(SelfDestruct)

    float lifetime = 3.0f;

    void Start() override {
        Destroy(lifetime);
    }

    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Float("Lifetime", &lifetime, 0.1f, 0.0f, 60.0f);
    }
};

// Register all builtin scripts
void RegisterBuiltinScripts();
