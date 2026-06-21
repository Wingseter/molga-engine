#pragma once

#include "../ECS/Component.h"
#include "Common/Types.h"
#include "ScriptField.h"
#include "Core/World.h"          // FindObjectOfType<T>, Find 등 World API
#include "../Physics/Physics2D.h" // RaycastHit2D, Physics2D 질의
#include <vector>
#include <functional>

class Scheduler;

class GameObject;
class Transform;
class Input;

// Base class for all user scripts
// Users inherit from this class to create custom behaviors
class Script : public Component {
public:
    // Override to return script name instead of "Script"
    std::string GetTypeName() const override { return GetScriptName(); }
    static std::string StaticTypeName() { return "Script"; }

    virtual ~Script() = default;

    // Called once for self-initialization, before any Start (do not reference other objects).
    void Awake() override {}

    // Called once before the first Update, after all Awake calls.
    void Start() override {}

    // Called every frame
    virtual void Update(float deltaTime) override {}

    // Called at fixed intervals (for physics)
    virtual void FixedUpdate(float fixedDeltaTime) {}

    // Called after all Update calls
    virtual void LateUpdate(float deltaTime) {}

    // Called when the script is enabled
    void OnEnable() override {}

    // Called when the script is disabled
    void OnDisable() override {}

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

    // Runtime lifecycle API helpers
    GameObject* Instantiate(const GameObject* original);
    GameObject* Instantiate(const GameObject* original, const Vector2& position);
    GameObject* Instantiate(const GameObject* original, GameObject* parent);
    GameObject* InstantiatePrefab(const std::string& guid);
    void Destroy(GameObject* obj, float delay = 0.0f);
    void Destroy(float delay = 0.0f);

    // 참조 필드 헬퍼
    // ObjectRef를 살아있는 GameObject로 해석 (없으면 nullptr).
    GameObject* Resolve(const ObjectRef& ref) const;
    // PrefabRef가 가리키는 프리팹을 인스턴스화 (없으면 nullptr).
    GameObject* Instantiate(const PrefabRef& ref);

    // ── 씬 검색 헬퍼 (소유 World 기준) ──
    GameObject* Find(const std::string& name) const;
    GameObject* FindWithTag(const std::string& tag) const;
    std::vector<GameObject*> FindAllWithTag(const std::string& tag) const;

    template<typename T>
    T* FindObjectOfType() const {
        World* w = GetWorld();
        return w ? w->FindObjectOfType<T>() : nullptr;
    }
    template<typename T>
    std::vector<T*> FindObjectsOfType() const {
        World* w = GetWorld();
        return w ? w->FindObjectsOfType<T>() : std::vector<T*>{};
    }

    // ── 2D 물리 질의 (소유 World 기준) ──
    RaycastHit2D Raycast(const Vector2& origin, const Vector2& direction,
                         float maxDistance = Physics2D::kInfinity,
                         int layerMask = Physics2D::kAllLayers) const;
    std::vector<GameObject*> OverlapCircleAll(const Vector2& center, float radius,
                                              int layerMask = Physics2D::kAllLayers) const;
    std::vector<GameObject*> OverlapBoxAll(const Vector2& center, const Vector2& halfExtents,
                                           int layerMask = Physics2D::kAllLayers) const;
    GameObject* OverlapPoint(const Vector2& point, int layerMask = Physics2D::kAllLayers) const;

    // 소유 GameObject의 World (없으면 nullptr).
    World* GetWorld() const;

    // ── 타이머 / 코루틴 (Unity 스타일) ──
    // delay초 뒤 fn을 1회 호출.
    void Invoke(std::function<void()> fn, float delay);
    // delay초 뒤 처음, 이후 interval초마다 fn 반복 호출.
    void InvokeRepeating(std::function<void()> fn, float delay, float interval);
    // 이 스크립트가 예약한 모든 Invoke 취소.
    void CancelInvoke();
    // 대기 중인 Invoke가 있는지.
    bool IsInvoking() const;
    // 매 프레임 step(dt)를 호출(false 반환 시 종료). 간이 코루틴.
    void StartCoroutine(std::function<bool(float)> step);
    // 이 스크립트의 모든 코루틴 종료.
    void StopAllCoroutines();

    // Script name for identification
    virtual const char* GetScriptName() const { return "Script"; }

    // 노출/직렬화할 필드를 등록한다. 파생 스크립트에서 오버라이드:
    //   void RegisterFields(ScriptFieldRegistry& r) override {
    //       r.Float("speed", &speed).Bool("active", &active);
    //   }
    // 등록된 필드는 자동으로 씬에 저장/로드되고 인스펙터에 표시된다.
    virtual void RegisterFields(ScriptFieldRegistry& /*r*/) {}

    // 지연 빌드 + 캐시된 필드 레지스트리 (인스턴스당 1회 구성).
    const ScriptFieldRegistry& Fields();

    // 등록된 필드를 자동 직렬화한다 (RegisterFields 기반).
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // 복제 시 ObjectRef 필드의 id를 새 id로 리매핑한다.
    void RemapReferences(const std::unordered_map<unsigned int, unsigned int>& idRemap) override;

    // Inspector GUI - 등록된 필드를 자동 렌더링. 파생 스크립트에서
    // 커스텀 UI가 필요하면 그대로 오버라이드할 수 있다(그 경우 이 구현 대신 사용).
    void OnInspectorGUI() override;

    // Runtime type ID (default for Script base; overridden by SCRIPT_CLASS)
    size_t GetRuntimeTypeID() const override { return ComponentTypeID::Get<Script>(); }

private:
    ScriptFieldRegistry fieldRegistry_;
    bool fieldsBuilt_ = false;
};

// Macro for easy script definition
#define SCRIPT_CLASS(ClassName) \
    const char* GetScriptName() const override { return #ClassName; } \
    static const char* StaticScriptName() { return #ClassName; } \
    size_t GetRuntimeTypeID() const override { return ComponentTypeID::Get<ClassName>(); } \
    static size_t StaticRuntimeTypeID() { return ComponentTypeID::Get<ClassName>(); }
