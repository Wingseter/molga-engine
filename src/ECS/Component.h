#pragma once

#include <string>
#include <cstddef>
#include <unordered_map>
#include <nlohmann/json.hpp>

class GameObject;
class Renderer;
namespace molga { class RenderQueue; }

// Compile-time type ID for O(1) component lookup
class ComponentTypeID {
    static inline size_t nextID = 0;
public:
    template<typename T>
    static size_t Get() {
        static size_t id = nextID++;
        return id;
    }
};

// Base class for all components
class Component {
public:
    virtual ~Component() = default;

    // Runtime type ID for O(1) map-based lookup
    virtual size_t GetRuntimeTypeID() const = 0;

    // Called when component is added to a GameObject
    virtual void OnAttach() {}

    // Called when component is removed from a GameObject
    virtual void OnDetach() {}

    // Called every frame
    virtual void Update(float dt) {}

    // Called for rendering (optional)
    virtual void Render() {}
    virtual void RenderSprite(Renderer* renderer) {}
    virtual void CollectRender(molga::RenderQueue& queue) {}

    // Called when the owning GameObject is being destroyed.
    // Use for releasing external resources (physics bodies, GPU handles, etc.)
    // Called BEFORE OnDetach(). Guaranteed exactly once.
    virtual void OnDestroy() {}

    // Get the component type name
    virtual std::string GetTypeName() const = 0;

    // Serialization (for scene saving/loading)
    // Override in derived classes to implement serialization
    virtual void Serialize(nlohmann::json& j) const;
    virtual void Deserialize(const nlohmann::json& j);

    // Editor Inspector GUI (override in derived classes for custom UI)
    virtual void OnInspectorGUI() {}

    // 직렬화 이후, GL 컨텍스트가 있는 시점에 에셋(텍스처 등)을 지연 로드한다.
    virtual void ResolveAssets() {}

    // Instantiate/Prefab 복제 시 id가 재할당된 후 호출된다. 다른 오브젝트를
    // id로 참조하는 컴포넌트는 이 훅에서 idRemap(원본id -> 새id)으로 참조를
    // 갱신한다. 맵에 없는 id는 외부 참조이므로 그대로 둔다.
    virtual void RemapReferences(const std::unordered_map<unsigned int, unsigned int>& /*idRemap*/) {}

    // Get/Set owner GameObject
    GameObject* GetGameObject() const { return gameObject; }
    void SetGameObject(GameObject* go) { gameObject = go; }

    // Lifecycle callbacks (override in derived classes)
    // Awake: 자기 초기화(다른 오브젝트 참조 금지). 모든 Awake가 Start보다 먼저 실행된다.
    virtual void Awake() {}
    // Start: 첫 Update 직전, 모든 Awake 이후. 상호 참조 초기화에 적합.
    virtual void Start() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}

    // Enable/Disable component
    bool IsEnabled() const { return enabled; }
    void SetEnabled(bool value) {
        if (enabled == value) return;
        enabled = value;
        if (enabled) OnEnable();
        else OnDisable();
    }

    // Awake/Start state tracking (각각 1회만 실행 보장)
    bool HasAwoken() const { return awoken; }
    void MarkAwoken() { awoken = true; }
    bool HasStarted() const { return started; }
    void MarkStarted() { started = true; }

protected:
    GameObject* gameObject = nullptr;
    bool enabled = true;
    bool awoken = false;
    bool started = false;
};

// Macro to help define component type name and runtime type ID
#define COMPONENT_TYPE(TypeName) \
    std::string GetTypeName() const override { return #TypeName; } \
    static std::string StaticTypeName() { return #TypeName; } \
    size_t GetRuntimeTypeID() const override { return ComponentTypeID::Get<TypeName>(); } \
    static size_t StaticRuntimeTypeID() { return ComponentTypeID::Get<TypeName>(); }
