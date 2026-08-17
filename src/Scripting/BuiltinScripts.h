#pragma once

#include "Script.h"
#include "../Systems/Input.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Components/BoxCollider2D.h"
#include "../ECS/Components/Rigidbody2D.h"
#include "../ECS/Components/UIButton.h"
#include "../ECS/GameObject.h"
#include "../Core/World.h"

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

        if (Input::GetKey(Input::KeyCode::W) || Input::GetKey(Input::KeyCode::Up))    dy -= 1.0f;
        if (Input::GetKey(Input::KeyCode::S) || Input::GetKey(Input::KeyCode::Down))  dy += 1.0f;
        if (Input::GetKey(Input::KeyCode::A) || Input::GetKey(Input::KeyCode::Left))  dx -= 1.0f;
        if (Input::GetKey(Input::KeyCode::D) || Input::GetKey(Input::KeyCode::Right)) dx += 1.0f;

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

// Minimal serializable 2D platformer controller used by authored stages.
// World units are pixels and +Y points down, matching the engine's default
// gravity. Grounding uses the public Physics2D raycast contract.
class PlatformerController : public Script {
public:
    SCRIPT_CLASS(PlatformerController)

    float moveSpeed = 220.0f;
    float jumpSpeed = 420.0f;
    float groundCheckDistance = 8.0f;
    int groundLayerMask = Physics2D::kAllLayers;

    void FixedUpdate(float fixedDeltaTime) override;
    void RegisterFields(ScriptFieldRegistry& r) override;

    bool IsGrounded() const { return grounded_; }

private:
    bool grounded_ = false;
};

// Serializable adapter that lets an authored overlay UIButton request a
// registered scene without hard-coding file-system paths in UI components.
class SceneLoadButton : public Script {
public:
    SCRIPT_CLASS(SceneLoadButton)

    std::string scenePath;

    void OnEnable() override {
        if (!gameObject) return;
        if (auto* button = gameObject->GetComponent<UIButton>()) {
            button->SetOnClick([this]() {
                if (!scenePath.empty()) LoadScene(scenePath);
            });
        }
    }

    void OnDisable() override {
        if (!gameObject) return;
        if (auto* button = gameObject->GetComponent<UIButton>()) {
            button->SetOnClick({});
        }
    }

    void RegisterFields(ScriptFieldRegistry& r) override {
        r.String("Scene Path", &scenePath);
    }
};

// Serializable UI adapter for a boolean option. The authored key/value are
// persisted only when the owning UIButton is actually clicked.
class PlayerPrefsButton : public Script {
public:
    SCRIPT_CLASS(PlayerPrefsButton)

    std::string key;
    bool value = false;
    bool saveImmediately = true;

    void OnEnable() override;
    void OnDisable() override;
    void RegisterFields(ScriptFieldRegistry& r) override;

    bool ApplyPreference() const;
};

// Serializable vertical-slice completion action. Keeping the small JSON
// contract in a built-in script means the same authored scene works in editor
// Play mode and in a packaged runtime without a hand-written fixture hook.
class SaveSlotButton : public Script {
public:
    SCRIPT_CLASS(SaveSlotButton)

    std::string slotName = "slot-01";
    int completedStage = 1;
    bool completed = true;
    std::string title;

    void OnEnable() override;
    void OnDisable() override;
    void RegisterFields(ScriptFieldRegistry& r) override;

    nlohmann::json BuildPayload() const;
    bool SaveCompletion() const;
};

// Register all builtin scripts
void RegisterBuiltinScripts();
