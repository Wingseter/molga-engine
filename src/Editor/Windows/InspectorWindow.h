#pragma once

#include "EditorWindow.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

class GameObject;
class Component;

class InspectorWindow : public EditorWindow {
public:
    InspectorWindow();

    void OnGUI() override;

    // Object to inspect
    void SetTarget(GameObject* obj);
    GameObject* GetTarget() const { return target; }

private:
    void DrawComponent(Component* component);
    void ClearActiveEdit();
    bool IsActiveEdit(unsigned int targetId, const Component* component) const;

    GameObject* target = nullptr;
    unsigned int targetId_ = 0;

    // Keep only value identities across frames. Components may be removed or
    // replaced synchronously by undo/redo, script reload, or inspector code.
    // A raw pointer here would become dangling before the edit is committed.
    std::string activeEditComponentType_;
    std::uint64_t activeEditComponentInstanceId_ = 0;
    nlohmann::json beforeEditSnap_;
    unsigned int activeEditTargetId_ = 0;
};
