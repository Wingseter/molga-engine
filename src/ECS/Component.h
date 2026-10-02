#pragma once

#include <string>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <nlohmann/json.hpp>

class GameObject;
class Renderer;

namespace molga { class RenderQueue; }
// Task 8.2 Step 7c: 월드 순회가 텍스트 컴포넌트에 건네는 renderer/서비스/sink
// 권한. 전역 이름인 것은 TextRenderer와 같은 이유다 — 이 헤더는 그것을 값으로
// 담지 않으므로 선언만 있으면 된다.
struct WorldRenderCollectionContext;

// Task 9.1: 인스턴스 id 할당기 테스트 훅. 소진 경로는 프로세스 전역 원자값을
// 직접 세워 놓고 관찰해야만 재현되는데, 그 원자값은 Component.cpp의 익명
// 이름공간에 산다. 테스트 전용이며 스코프를 벗어나면 직전 값으로 되돌린다.
std::uint64_t ComponentInstanceIdForTesting();

class ScopedComponentInstanceIdForTesting {
public:
    explicit ScopedComponentInstanceIdForTesting(std::uint64_t next);
    ~ScopedComponentInstanceIdForTesting();

    ScopedComponentInstanceIdForTesting(
        const ScopedComponentInstanceIdForTesting&) = delete;
    ScopedComponentInstanceIdForTesting& operator=(
        const ScopedComponentInstanceIdForTesting&) = delete;

private:
    std::uint64_t previous_;
    std::uint64_t seeded_;
};

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
    Component();
    Component(const Component& other);
    // Task 9.1: 이동 생성은 새 인스턴스 id를 할당하고 할당은 소진 시 던진다.
    // noexcept로 두면 소진이 예외가 아니라 std::terminate가 된다. 대입은
    // 목적지의 id를 그대로 두므로 할당하지 않고 noexcept로 남는다.
    Component(Component&& other);
    Component& operator=(const Component& other);
    Component& operator=(Component&& other) noexcept;
    virtual ~Component() = default;

    // Runtime-only identity. Unlike an address, this cannot be reused when a
    // callback removes a component and immediately adds another of the same
    // type. Systems that dispatch user callbacks can snapshot this value and
    // re-resolve the component before every invocation.
    std::uint64_t GetInstanceID() const { return instanceId_; }

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

    // Task 8.2 Step 7c: 텍스트를 그릴 수 있는 컴포넌트의 유일한 수집 진입점.
    //
    // 기본 구현은 텍스트가 아닌 컴포넌트를 위해 위의 한 인자짜리 가상 함수로
    // 넘긴다. 그래서 스프라이트/타일맵/파티클은 이 태스크에서 한 줄도 바뀌지
    // 않고, 텍스트만 문맥을 요구한다. TextRenderer2D는 이쪽만 재정의하므로
    // 문맥 없는 호출로는 텍스트가 큐에 들어갈 수 없다 — 그것이 "renderer를
    // 스스로 찾지 않는다"를 컴파일러의 것으로 만드는 방법이다.
    virtual void CollectRender(molga::RenderQueue& queue,
                               const WorldRenderCollectionContext& /*context*/) {
        CollectRender(queue);
    }

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
    virtual void SetEnabled(bool value) {
        if (enabled == value) return;
        enabled = value;
        if (enabled) OnEnable();
        else OnDisable();
    }

    // Scene/prefab deserialization restores persisted state while objects are
    // still being assembled. It must not enter user lifecycle code: the World
    // owns the later Awake/OnEnable/Start transition once loading is complete.
    // Runtime and editor interactions must continue to use SetEnabled().
    void SetEnabledFromSerializedState(bool value) noexcept { enabled = value; }

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

private:
    std::uint64_t instanceId_ = 0;
};

// Macro to help define component type name and runtime type ID
#define COMPONENT_TYPE(TypeName) \
    std::string GetTypeName() const override { return #TypeName; } \
    static std::string StaticTypeName() { return #TypeName; } \
    size_t GetRuntimeTypeID() const override { return ComponentTypeID::Get<TypeName>(); } \
    static size_t StaticRuntimeTypeID() { return ComponentTypeID::Get<TypeName>(); }
