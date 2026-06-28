#pragma once

#include "EditorWindow.h"

#include <nlohmann/json.hpp>
#include <string>

class GameObject;
class Component;

class InspectorWindow : public EditorWindow {
public:
    InspectorWindow();

    void OnGUI() override;

    // Object to inspect
    void SetTarget(GameObject* obj) { target = obj; }
    GameObject* GetTarget() const { return target; }

private:
    void DrawComponent(Component* component);

    GameObject* target = nullptr;

    Component* activeEditComponent_ = nullptr;
    nlohmann::json beforeEditSnap_;
    unsigned int activeEditTargetId_ = 0;
};
