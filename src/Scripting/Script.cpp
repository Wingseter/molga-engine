#include "Script.h"
#include "../ECS/GameObject.h"
#include "../ECS/Components/Transform.h"
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

Transform* Script::GetTransform() {
    if (gameObject) {
        return gameObject->GetComponent<Transform>();
    }
    return nullptr;
}

void Script::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    ImGui::Text("Script: %s", GetScriptName());
    ImGui::TextDisabled("Override OnInspectorGUI() for custom properties");
#endif
}
