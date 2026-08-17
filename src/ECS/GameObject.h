#pragma once

#include <cstddef>
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
    ~GameObject() noexcept;

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
        auto existing = componentMap.find(id);
        if (existing != componentMap.end()) {
            // 중복 추가는 계약 위반. release에서 map을 덮어쓰면 componentOrder_에
            // 죽은 포인터가 남으므로, 덮어쓰지 않고 기존 컴포넌트를 반환한다.
            assert(false && "Duplicate component type. Use RemoveComponent first.");
            return static_cast<T*>(existing->second.get());
        }
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = component.get();
        ptr->SetGameObject(this);
        ptr->OnAttach();
        componentMap[id] = std::move(component);
        componentOrder_.push_back(ptr);  // 결정적 실행 순서(삽입 순) 유지
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

    // 자신부터 자손까지 DFS로 타입 T 컴포넌트를 찾는다 (Unity GetComponentInChildren).
    template<typename T>
    T* GetComponentInChildren() {
        if (T* c = GetComponent<T>()) return c;
        for (auto* child : children) {
            if (child) {
                if (T* c = child->GetComponentInChildren<T>()) return c;
            }
        }
        return nullptr;
    }

    // 자신부터 조상까지 거슬러 올라가며 타입 T 컴포넌트를 찾는다 (Unity GetComponentInParent).
    template<typename T>
    T* GetComponentInParent() {
        for (GameObject* node = this; node; node = node->parent) {
            if (T* c = node->GetComponent<T>()) return c;
        }
        return nullptr;
    }

    template<typename T>
    void RemoveComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        RemoveComponentById(ComponentTypeID::Get<T>());
    }

    // Add component from raw pointer (takes ownership, uses runtime type ID)
    Component* AddComponentRaw(Component* component);
    void RemoveComponentById(size_t typeId);

    // Get all components (returns vector of raw pointers for iteration)
    std::vector<Component*> GetComponents() const;

    // Hierarchy
    GameObject* GetParent() const { return parent; }
    const std::vector<GameObject*>& GetChildren() const { return children; }
    std::size_t GetSiblingIndex() const;

    // 성공 시 true. self/cycle을 만들면 거부하고 false를 반환한다.
    bool SetParent(GameObject* newParent);
    // Keeps the current parent and moves this object to an exact sibling slot.
    // Root ordering is owned by the World's flat object vector instead.
    bool SetSiblingIndex(std::size_t index);
    void AddChild(GameObject* child);
    void RemoveChild(GameObject* child);

    // node가 이 오브젝트의 자손이면 true (cycle 방지에 사용).
    bool IsAncestorOf(const GameObject* node) const;
    // 이 오브젝트와 모든 자손을 DFS 순서(부모 먼저)로 out에 추가.
    void CollectSubtree(std::vector<GameObject*>& out);

    // Active state
    bool IsActive() const { return active; }
    // 활성/비활성 전환 시(월드가 Play 중이면) 컴포넌트 라이프사이클을 전파한다:
    //   활성화 → (필요 시)Awake → OnEnable → (필요 시)Start
    //   비활성화 → OnDisable
    void SetActive(bool value);

    // Update all components
    void Update(float dt);

    // Render all components
    void Render();

    // Notify all components that this GameObject is being destroyed.
    // Safe to call multiple times (idempotent via destroyed flag).
    void NotifyDestroy() noexcept;

    // Script lifecycle hooks (avoid duplicating dynamic_cast loops in entry points)
    void FixedUpdateScripts(float fixedDt);
    void LateUpdateScripts(float dt);
    // 아직 깨우지 않은 컴포넌트의 Awake()를 1회 호출
    void AwakeScripts();
    // 활성 컴포넌트의 OnEnable()을 호출 (Awake 이후, Start 이전)
    void EnableScripts();
    // 활성 컴포넌트의 OnDisable()을 호출 (비활성화 시)
    void DisableScripts();
    // 아직 시작 안 한 컴포넌트의 Start()를 1회 호출
    void StartScripts();
    void ResolveAssets();

private:
    struct ComponentIdentity {
        size_t typeId = 0;
        std::uint64_t instanceId = 0;
    };

    std::vector<ComponentIdentity> SnapshotComponentIdentities() const;
    Component* ResolveComponent(const ComponentIdentity& identity);
    void InvokeOnDisable(Component* component);
    void InvokeOnDestroy(Component* component);

    static unsigned int nextID;

    unsigned int id;
    std::string name;
    std::string tag = "Untagged";
    int layer = 0;
    bool active = true;
    bool destroyed = false;

    std::unordered_map<size_t, std::unique_ptr<Component>> componentMap;
    // 삽입 순서를 보존하는 컴포넌트 목록(결정적 실행 순서). 소유권은 map이 가진다.
    std::vector<Component*> componentOrder_;
    // Guards OnDisable against reentrant component removal. An OnDisable
    // callback may remove itself; the removal path must not invoke it twice.
    std::vector<std::uint64_t> disableCallbacksInProgress_;
    std::vector<std::uint64_t> disableCallbacksCompletedDuringDestroy_;
    // OnDestroy remains exactly-once even if a destruction callback removes
    // its own component and re-enters the removal path.
    std::vector<std::uint64_t> destroyCallbacksInProgress_;
    std::vector<std::uint64_t> destroyCallbacksCompleted_;

    GameObject* parent = nullptr;
    std::vector<GameObject*> children;
    World* world = nullptr;
};
