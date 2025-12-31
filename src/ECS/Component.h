#ifndef MOLGA_COMPONENT_H
#define MOLGA_COMPONENT_H

#include <string>
#include <typeinfo>

class GameObject;

// Base class for all components
class Component {
public:
    virtual ~Component() = default;

    // Called when component is added to a GameObject
    virtual void OnAttach() {}

    // Called when component is removed from a GameObject
    virtual void OnDetach() {}

    // Called every frame
    virtual void Update(float dt) {}

    // Called for rendering (optional)
    virtual void Render() {}

    // Get the component type name
    virtual std::string GetTypeName() const = 0;

    // Serialization (for scene saving/loading)
    // Override in derived classes to implement serialization

    // Get/Set owner GameObject
    GameObject* GetGameObject() const { return gameObject; }
    void SetGameObject(GameObject* go) { gameObject = go; }

    // Enable/Disable component
    bool IsEnabled() const { return enabled; }
    void SetEnabled(bool value) { enabled = value; }

protected:
    GameObject* gameObject = nullptr;
    bool enabled = true;
};

// Macro to help define component type name
#define COMPONENT_TYPE(TypeName) \
    std::string GetTypeName() const override { return #TypeName; } \
    static std::string StaticTypeName() { return #TypeName; }

#endif // MOLGA_COMPONENT_H
