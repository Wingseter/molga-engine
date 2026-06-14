#pragma once

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>

#include "Component.h"

class World;

class GameObject {
public:
    explicit GameObject(const std::string& name = "GameObject");
    ~GameObject();

    // World association
    World* GetWorld() const { return world; }
    void SetWorld(World* w) { world = w; }

    // Name
    const std::string& GetName() const { return name; }
    void SetName(const std::string& newName) { name = newName; }

    // Tag
    const std::string& GetTag() const { return tag; }
    void SetTag(const std::string& newTag) { tag = newTag; }
    bool CompareTag(const std::string& otherTag) const { return tag == otherTag; }

    // Layer
    int GetLayer() const { return layer; }
    void SetLayer(int newLayer) { layer = newLayer; }

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
        assert(componentMap.find(id) == componentMap.end()
               && "Duplicate component type. Use RemoveComponent first.");
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
            if (it->second->IsEnabled()) it->second->OnDisable();
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

    // 성공 시 true. self/cycle을 만들면 거부하고 false를 반환한다.
    bool SetParent(GameObject* newParent);
    void AddChild(GameObject* child);
    void RemoveChild(GameObject* child);

    // node가 이 오브젝트의 자손이면 true (cycle 방지에 사용).
    bool IsAncestorOf(const GameObject* node) const;
    // 이 오브젝트와 모든 자손을 DFS 순서(부모 먼저)로 out에 추가.
    void CollectSubtree(std::vector<GameObject*>& out);

    // Active state
    bool IsActive() const { return active; }
    void SetActive(bool value) { active = value; }

    // Update all components
    void Update(float dt);

    // Render all components
    void Render();

    // Notify all components that this GameObject is being destroyed.
    // Safe to call multiple times (idempotent via destroyed flag).
    void NotifyDestroy();

    // Script lifecycle hooks (avoid duplicating dynamic_cast loops in entry points)
    void FixedUpdateScripts(float fixedDt);
    void LateUpdateScripts(float dt);
    // 아직 시작 안 한 스크립트의 Start()를 1회 호출
    void StartScripts();
    void ResolveAssets();

private:
    static unsigned int nextID;

    unsigned int id;
    std::string name;
    std::string tag = "Untagged";
    int layer = 0;
    bool active = true;
    bool destroyed = false;

    std::unordered_map<size_t, std::unique_ptr<Component>> componentMap;

    GameObject* parent = nullptr;
    std::vector<GameObject*> children;
    World* world = nullptr;
};
