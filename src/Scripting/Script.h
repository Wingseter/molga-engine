#ifndef MOLGA_SCRIPT_H
#define MOLGA_SCRIPT_H

#include "../ECS/Component.h"

class GameObject;
class Transform;
class Input;

// Base class for all user scripts
// Users inherit from this class to create custom behaviors
class Script : public Component {
public:
    COMPONENT_TYPE(Script)

    virtual ~Script() = default;

    // Called once when the script is first enabled
    virtual void Start() {}

    // Called every frame
    virtual void Update(float deltaTime) override {}

    // Called at fixed intervals (for physics)
    virtual void FixedUpdate(float fixedDeltaTime) {}

    // Called after all Update calls
    virtual void LateUpdate(float deltaTime) {}

    // Called when the script is enabled
    virtual void OnEnable() {}

    // Called when the script is disabled
    virtual void OnDisable() {}

    // Called when this object collides with another
    virtual void OnCollisionEnter(GameObject* other) {}
    virtual void OnCollisionStay(GameObject* other) {}
    virtual void OnCollisionExit(GameObject* other) {}

    // Called when this trigger overlaps with another
    virtual void OnTriggerEnter(GameObject* other) {}
    virtual void OnTriggerStay(GameObject* other) {}
    virtual void OnTriggerExit(GameObject* other) {}

    // Helper to get Transform component
    Transform* GetTransform();

    // Script name for identification
    virtual const char* GetScriptName() const { return "Script"; }

    // Has Start been called?
    bool HasStarted() const { return started; }
    void MarkStarted() { started = true; }

protected:
    bool started = false;
};

// Macro for easy script definition
#define SCRIPT_CLASS(ClassName) \
    const char* GetScriptName() const override { return #ClassName; } \
    static const char* StaticScriptName() { return #ClassName; }

#endif // MOLGA_SCRIPT_H
