#ifndef MOLGA_GAME_OBJECT_H
#define MOLGA_GAME_OBJECT_H

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <typeinfo>

class Component;

class GameObject {
public:
    explicit GameObject(const std::string& name = "GameObject");
    ~GameObject();

    // Name
    const std::string& GetName() const { return name; }
    void SetName(const std::string& newName) { name = newName; }

    // ID (unique identifier)
    unsigned int GetID() const { return id; }

    // Component management
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = component.get();
        ptr->SetGameObject(this);
        ptr->OnAttach();
        components.push_back(std::move(component));
        return ptr;
    }

    template<typename T>
    T* GetComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        for (auto& comp : components) {
            T* result = dynamic_cast<T*>(comp.get());
            if (result) return result;
        }
        return nullptr;
    }

    template<typename T>
    const T* GetComponent() const {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        for (const auto& comp : components) {
            const T* result = dynamic_cast<const T*>(comp.get());
            if (result) return result;
        }
        return nullptr;
    }

    template<typename T>
    bool HasComponent() const {
        return GetComponent<T>() != nullptr;
    }

    template<typename T>
    void RemoveComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto it = std::remove_if(components.begin(), components.end(),
            [](const std::unique_ptr<Component>& comp) {
                T* result = dynamic_cast<T*>(comp.get());
                if (result) {
                    result->OnDetach();
                    return true;
                }
                return false;
            });
        components.erase(it, components.end());
    }

    // Add component from raw pointer (takes ownership)
    Component* AddComponentRaw(Component* component);

    // Get all components
    const std::vector<std::unique_ptr<Component>>& GetComponents() const { return components; }

    // Hierarchy
    GameObject* GetParent() const { return parent; }
    const std::vector<GameObject*>& GetChildren() const { return children; }

    void SetParent(GameObject* newParent);
    void AddChild(GameObject* child);
    void RemoveChild(GameObject* child);

    // Active state
    bool IsActive() const { return active; }
    void SetActive(bool value) { active = value; }

    // Update all components
    void Update(float dt);

    // Render all components
    void Render();

private:
    static unsigned int nextID;

    unsigned int id;
    std::string name;
    bool active = true;

    std::vector<std::unique_ptr<Component>> components;

    GameObject* parent = nullptr;
    std::vector<GameObject*> children;
};

#endif // MOLGA_GAME_OBJECT_H
