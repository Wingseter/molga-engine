#include "Script.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"
#include <imgui.h>

Transform* Script::GetTransform() {
    if (gameObject) {
        return gameObject->GetComponent<Transform>();
    }
    return nullptr;
}

void Script::OnInspectorGUI() {
    ImGui::Text("Script: %s", GetScriptName());
    ImGui::TextDisabled("Override OnInspectorGUI() for custom properties");
}
