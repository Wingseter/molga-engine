#ifndef MOLGA_INSPECTOR_WINDOW_H
#define MOLGA_INSPECTOR_WINDOW_H

#include "EditorWindow.h"

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
    void DrawTransformComponent();
    void DrawSpriteRendererComponent();
    void DrawBoxCollider2DComponent();

    GameObject* target = nullptr;
};

#endif // MOLGA_INSPECTOR_WINDOW_H
