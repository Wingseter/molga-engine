#include "Script.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"
#include "Core/World.h"
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

Transform* Script::GetTransform() {
    if (gameObject) {
        return gameObject->GetComponent<Transform>();
    }
    return nullptr;
}

GameObject* Script::Instantiate(const GameObject* original) {
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->Instantiate(original);
    }
    return nullptr;
}

GameObject* Script::Instantiate(const GameObject* original, const Vector2& position) {
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->Instantiate(original, position);
    }
    return nullptr;
}

GameObject* Script::Instantiate(const GameObject* original, GameObject* parent) {
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->Instantiate(original, parent);
    }
    return nullptr;
}

GameObject* Script::InstantiatePrefab(const std::string& guid) {
    if (gameObject && gameObject->GetWorld()) {
        return gameObject->GetWorld()->InstantiatePrefab(guid);
    }
    return nullptr;
}

void Script::Destroy(GameObject* obj, float delay) {
    if (gameObject && gameObject->GetWorld()) {
        gameObject->GetWorld()->Destroy(obj, delay);
    }
}

void Script::Destroy(float delay) {
    if (gameObject && gameObject->GetWorld()) {
        gameObject->GetWorld()->Destroy(gameObject, delay);
    }
}

void Script::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    ImGui::Text("Script: %s", GetScriptName());
    ImGui::TextDisabled("Override OnInspectorGUI() for custom properties");
#endif
}
