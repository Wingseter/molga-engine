#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>

#include "Component.h"

class GameObject {
public:
    explicit GameObject(const std::string& name = "GameObject");
    ~GameObject();

    // Name
    const std::string& GetName() const { return name; }
    void SetName(const std::string& newName) { name = newName; }

    // ID (unique identifier)
    unsigned int GetID() const { return id; }
    void SetID(unsigned int newID) {
        id = newID;
        if (newID >= nextID) {
            nextID = newID + 1;
        }
    }

    // Component management
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto id = ComponentTypeID::Get<T>();
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = component.get();
        ptr->SetGameObject(this);
        ptr->OnAttach();
        componentMap[id] = std::move(component);
        return ptr;
    }

    template<typename T>
    T* GetComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto it = componentMap.find(ComponentTypeID::Get<T>());
        if (it != componentMap.end()) {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    template<typename T>
    const T* GetComponent() const {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto it = componentMap.find(ComponentTypeID::Get<T>());
        if (it != componentMap.end()) {
            return static_cast<const T*>(it->second.get());
        }
        return nullptr;
    }

    template<typename T>
    bool HasComponent() const {
        return componentMap.count(ComponentTypeID::Get<T>()) > 0;
    }

    template<typename T>
    void RemoveComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto it = componentMap.find(ComponentTypeID::Get<T>());
        if (it != componentMap.end()) {
            it->second->OnDetach();
            componentMap.erase(it);
        }
    }

    // Add component from raw pointer (takes ownership, uses runtime type ID)
    Component* AddComponentRaw(Component* component);

    // Get all components (returns vector of raw pointers for iteration)
    std::vector<Component*> GetComponents() const;

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

    std::unordered_map<size_t, std::unique_ptr<Component>> componentMap;

    GameObject* parent = nullptr;
    std::vector<GameObject*> children;
};
