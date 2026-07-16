#include "BuiltinScripts.h"
#include "ScriptManager.h"
#include "Core/PlayerPrefs.h"
#include "Core/SaveSystem.h"

#include <algorithm>
#include <cmath>

void PlatformerController::FixedUpdate(float) {
    if (!gameObject) return;
    auto* body = gameObject->GetComponent<Rigidbody2D>();
    auto* transform = gameObject->GetComponent<Transform>();
    if (!body || !transform ||
        body->GetBodyType() != Rigidbody2D::BodyType::Dynamic) return;

    float horizontal = 0.0f;
    if (Input::GetKey(GLFW_KEY_A) || Input::GetKey(GLFW_KEY_LEFT)) horizontal -= 1.0f;
    if (Input::GetKey(GLFW_KEY_D) || Input::GetKey(GLFW_KEY_RIGHT)) horizontal += 1.0f;

    Vector2 velocity = body->GetVelocity();
    velocity.x = horizontal * moveSpeed;

    float halfHeight = 16.0f;
    if (auto* collider = gameObject->GetComponent<BoxCollider2D>()) {
        halfHeight = collider->GetSize().y *
                     std::abs(transform->GetWorldScale().y) * 0.5f;
    }
    const Vector2 origin = transform->GetWorldPosition() +
                           Vector2{0.0f, halfHeight + 0.5f};
    const RaycastHit2D ground = Raycast(
        origin, {0.0f, 1.0f}, std::max(groundCheckDistance, 0.0f),
        groundLayerMask);
    grounded_ = ground.hit && ground.collider != gameObject;

    const bool jumpPressed = Input::GetKeyDown(GLFW_KEY_SPACE) ||
                             Input::GetKeyDown(GLFW_KEY_W) ||
                             Input::GetKeyDown(GLFW_KEY_UP);
    if (grounded_ && jumpPressed) velocity.y = -std::abs(jumpSpeed);
    body->SetVelocity(velocity);
}

void PlatformerController::RegisterFields(ScriptFieldRegistry& r) {
    r.Float("Move Speed", &moveSpeed, 1.0f, 0.0f, 2000.0f)
     .Float("Jump Speed", &jumpSpeed, 1.0f, 0.0f, 2000.0f)
     .Float("Ground Check Distance", &groundCheckDistance, 0.25f, 0.0f, 100.0f)
     .Int("Ground Layer Mask", &groundLayerMask);
}

void PlayerPrefsButton::OnEnable() {
    if (!gameObject) return;
    if (auto* button = gameObject->GetComponent<UIButton>()) {
        button->SetOnClick([this]() { ApplyPreference(); });
    }
}

void PlayerPrefsButton::OnDisable() {
    if (!gameObject) return;
    if (auto* button = gameObject->GetComponent<UIButton>()) {
        button->SetOnClick({});
    }
}

void PlayerPrefsButton::RegisterFields(ScriptFieldRegistry& r) {
    r.String("Key", &key)
     .Bool("Value", &value)
     .Bool("Save Immediately", &saveImmediately);
}

bool PlayerPrefsButton::ApplyPreference() const {
    if (key.empty()) return false;
    PlayerPrefs::SetBool(key, value);
    return !saveImmediately || PlayerPrefs::Save();
}

void SaveSlotButton::OnEnable() {
    if (!gameObject) return;
    if (auto* button = gameObject->GetComponent<UIButton>()) {
        button->SetOnClick([this]() { SaveCompletion(); });
    }
}

void SaveSlotButton::OnDisable() {
    if (!gameObject) return;
    if (auto* button = gameObject->GetComponent<UIButton>()) {
        button->SetOnClick({});
    }
}

void SaveSlotButton::RegisterFields(ScriptFieldRegistry& r) {
    r.String("Slot Name", &slotName)
     .Int("Completed Stage", &completedStage)
     .Bool("Completed", &completed)
     .String("Title", &title);
}

nlohmann::json SaveSlotButton::BuildPayload() const {
    return {
        {"completed", completed},
        {"stage", completedStage},
        {"title", title}
    };
}

bool SaveSlotButton::SaveCompletion() const {
    return !slotName.empty() && SaveSystem::SaveSlot(slotName, BuildPayload());
}

void RegisterBuiltinScripts() {
    ScriptManager::Get().RegisterBuiltin("PlayerController", []() -> std::unique_ptr<Script> {
        return std::make_unique<PlayerController>();
    });

    ScriptManager::Get().RegisterBuiltin("Rotator", []() -> std::unique_ptr<Script> {
        return std::make_unique<Rotator>();
    });

    ScriptManager::Get().RegisterBuiltin("Oscillator", []() -> std::unique_ptr<Script> {
        return std::make_unique<Oscillator>();
    });

    ScriptManager::Get().RegisterBuiltin("Spawner", []() -> std::unique_ptr<Script> {
        return std::make_unique<Spawner>();
    });

    ScriptManager::Get().RegisterBuiltin("SelfDestruct", []() -> std::unique_ptr<Script> {
        return std::make_unique<SelfDestruct>();
    });

    ScriptManager::Get().RegisterBuiltin("PlatformerController", []() -> std::unique_ptr<Script> {
        return std::make_unique<PlatformerController>();
    });

    ScriptManager::Get().RegisterBuiltin("SceneLoadButton", []() -> std::unique_ptr<Script> {
        return std::make_unique<SceneLoadButton>();
    });

    ScriptManager::Get().RegisterBuiltin("PlayerPrefsButton", []() -> std::unique_ptr<Script> {
        return std::make_unique<PlayerPrefsButton>();
    });

    ScriptManager::Get().RegisterBuiltin("SaveSlotButton", []() -> std::unique_ptr<Script> {
        return std::make_unique<SaveSlotButton>();
    });
}
