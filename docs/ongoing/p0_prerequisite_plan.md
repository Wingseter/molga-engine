# P0: 선행 정비 작업 상세 계획서

> 작성일: 2026-03-23 · 개정: 2026-03-23 (리뷰 반영)
> 기반: MASTER_PLAN.md Section 3 "선행 정비 작업"
> 목적: Phase 6 시작 전 코드베이스 기초 정비 (3개 작업, 2.5-3일)
> 브랜치: `p0/prerequisites`

---

## 변경 이력

- **v1** (2026-03-23): 초안 작성 (4개 작업)
- **v2** (2026-03-23): 외부 리뷰 반영
  - P0-1: AddComponent 중복 타입 방어, 순회 안정성(스냅샷), OnDisable 계약 명시
  - P0-2: GetWorldBounds 계약 정의 (회전 미지원/음수 스케일 정규화)
  - P0-3: accumulator를 Time::Update()에서 자동 누적하지 않음, ResetFixedAccumulator() 추가, timeScale은 누적 속도에만 적용, FixedUpdateScripts()를 GameObject에 캡슐화
  - P0-4: Phase 9로 이관 (현재 소비 시스템 없음)

---

## 목차

1. [P0-1: 컴포넌트 라이프사이클 정비](#p0-1-컴포넌트-라이프사이클-정비)
2. [P0-2: Collider2D 추상 기반 클래스](#p0-2-collider2d-추상-기반-클래스)
3. [P0-3: FixedUpdate 고정 시간 루프](#p0-3-fixedupdate-고정-시간-루프)
4. [실행 순서 및 의존성](#실행-순서-및-의존성)
5. [검증 계획](#검증-계획)

> **P0-4 (Broad Phase 공간 분할)**: Phase 9로 이관.
> 현재 코드베이스에 O(n²) 충돌 루프 자체가 없고, Phase 9에서 Box2D가 자체 broad phase를 제공한다.
> 소비자 없이 인프라만 만들면 Phase 9에서 다시 뜯어고칠 가능성이 높다.

---

## 현재 상태 요약

```
src/ECS/Component.h          ← 라이프사이클: OnAttach, OnDetach, OnEnable, OnDisable (OnDestroy 없음)
src/ECS/GameObject.h/cpp     ← ~GameObject()에서 OnDetach() 호출 후 componentMap.clear()
                                AddComponent: 같은 타입 덮어쓰기 시 기존 컴포넌트 OnDetach 미호출 (버그)
src/ECS/Components/
  BoxCollider2D.h/cpp        ← Component 직접 상속 (Collider2D 중간 계층 없음)
                                GetWorldAABB: 회전 미반영, 음수 스케일 미정의
  Transform.h/cpp
  SpriteRenderer.h/cpp
src/Physics/Collision.h/cpp  ← 정적 유틸 (CheckAABB, CheckCircle 등). 중앙 충돌 시스템 없음
src/Core/MolgaTime.h/cpp     ← deltaTime만 제공, fixedDeltaTime 없음
src/main.cpp                 ← while 루프에서 Time::Update() → dt → Update(dt) 순서
                                Play 모드에서만 게임 업데이트하지만, Time::Update()는 항상 호출
src/runtime_main.cpp         ← 동일 구조
src/Scripting/Script.h       ← FixedUpdate/LateUpdate/Start 시그니처 존재하나 호출되지 않음
```

---

## P0-1: 컴포넌트 라이프사이클 정비

### 예상 기간: 1일

### 필요 이유

1. **OnDestroy() 부재**: Box2D 바디, 오디오 핸들 등 외부 리소스 해제 콜백이 없다
2. **AddComponent 덮어쓰기 버그**: 같은 타입을 다시 추가하면 기존 컴포넌트가 `OnDetach()` 없이 소멸된다
3. **순회 중 수정 위험**: `NotifyDestroy()`나 `Update()` 순회 중 콜백 내부에서 컴포넌트 추가/제거 시 Iterator Invalidation 발생 가능

### 라이프사이클 계약 정의

```
컴포넌트 추가 (AddComponent<T>):
  - 같은 타입이 이미 존재하면 assert 실패 (교체 금지)
  - SetGameObject(this) → OnAttach()

컴포넌트 제거 (RemoveComponent<T>):
  - enabled인 경우: OnDisable() → OnDetach()
  - disabled인 경우: OnDetach()만

게임오브젝트 파괴 (Destroy/소멸자):
  - 각 컴포넌트에 대해:
    - enabled인 경우: OnDisable() → OnDestroy() → OnDetach()
    - disabled인 경우: OnDestroy() → OnDetach()
  - 순회는 스냅샷 기반 (콜백 내부의 수정이 순회를 깨뜨리지 않음)
```

### 변경 대상

#### 1. `src/ECS/Component.h` — OnDestroy 추가

```cpp
class Component {
public:
    // ... 기존 ...

    virtual void OnAttach() {}
    virtual void OnDetach() {}

    // Called when the owning GameObject is being destroyed.
    // Use for releasing external resources (physics bodies, GPU handles, etc.)
    // Called BEFORE OnDetach(). Guaranteed to be called exactly once.
    virtual void OnDestroy() {}

    // ... 나머지 ...
};
```

#### 2. `src/ECS/GameObject.h` — 중복 방어 + NotifyDestroy + 스크립트 훅

```cpp
class GameObject {
public:
    // ... 기존 ...

    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto typeId = ComponentTypeID::Get<T>();

        // 같은 타입 컴포넌트 중복 추가 금지
        assert(componentMap.find(typeId) == componentMap.end()
               && "Duplicate component type. Use RemoveComponent first.");

        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = component.get();
        ptr->SetGameObject(this);
        ptr->OnAttach();
        componentMap[typeId] = std::move(component);
        return ptr;
    }

    template<typename T>
    void RemoveComponent() {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto it = componentMap.find(ComponentTypeID::Get<T>());
        if (it != componentMap.end()) {
            auto& comp = it->second;
            if (comp->IsEnabled()) comp->OnDisable();  // ← 추가
            comp->OnDetach();
            componentMap.erase(it);
        }
    }

    // Notify all components that this GameObject is being destroyed.
    void NotifyDestroy();

    // Script lifecycle hooks (avoids duplicating dynamic_cast loops in main.cpp)
    void FixedUpdateScripts(float fixedDt);
    void LateUpdateScripts(float dt);

    // ... 나머지 ...

private:
    bool destroyed = false;
    // ... 기존 ...
};
```

#### 3. `src/ECS/GameObject.cpp` — 스냅샷 순회 + 스크립트 훅

```cpp
void GameObject::NotifyDestroy() {
    if (destroyed) return;
    destroyed = true;

    // 스냅샷을 뜬 뒤 순회 (콜백 내부에서 컴포넌트 추가/제거해도 안전)
    std::vector<Component*> snapshot;
    snapshot.reserve(componentMap.size());
    for (auto& [id, comp] : componentMap) {
        snapshot.push_back(comp.get());
    }

    for (auto* comp : snapshot) {
        if (comp->IsEnabled()) comp->OnDisable();
        comp->OnDestroy();
    }
}

GameObject::~GameObject() {
    NotifyDestroy();  // destroyed 플래그로 중복 방지
    for (auto& [id, comp] : componentMap) {
        comp->OnDetach();
    }
    componentMap.clear();
    if (parent) {
        parent->RemoveChild(this);
    }
    children.clear();
}

// AddComponentRaw도 중복 방어 추가
Component* GameObject::AddComponentRaw(Component* component) {
    if (!component) return nullptr;
    auto typeId = component->GetRuntimeTypeID();
    assert(componentMap.find(typeId) == componentMap.end()
           && "Duplicate component type via AddComponentRaw.");
    component->SetGameObject(this);
    component->OnAttach();
    componentMap[typeId] = std::unique_ptr<Component>(component);
    return component;
}

// Script lifecycle hooks
void GameObject::FixedUpdateScripts(float fixedDt) {
    if (!active) return;
    for (auto& [id, comp] : componentMap) {
        if (comp->IsEnabled()) {
            if (auto* script = dynamic_cast<Script*>(comp.get())) {
                script->FixedUpdate(fixedDt);
            }
        }
    }
}

void GameObject::LateUpdateScripts(float dt) {
    if (!active) return;
    for (auto& [id, comp] : componentMap) {
        if (comp->IsEnabled()) {
            if (auto* script = dynamic_cast<Script*>(comp.get())) {
                script->LateUpdate(dt);
            }
        }
    }
}
```

**참고**: `FixedUpdateScripts`/`LateUpdateScripts`에서 `dynamic_cast`를 사용한다. 성능이 문제되면 Phase 6 이후 Script 전용 리스트로 최적화할 수 있지만, 현재 규모에서는 충분하다.

### 영향 범위

- **하위 호환**: `OnDestroy()`는 기본 빈 구현. `AddComponent` 중복 추가 시 assert → 기존 코드에서 중복 추가하는 곳이 없으므로 안전 (검색 확인 완료)
- **수정 파일**: `Component.h` (1줄), `GameObject.h` (~20줄), `GameObject.cpp` (~40줄)
- **테스트 영향**: 기존 4개 테스트 통과 (중복 추가 없음)

### 테스트 계획

`tests/test_ecs.cpp`에 추가:

```cpp
// 1. OnDestroy가 파괴 시 호출되는지 검증
static void test_on_destroy_called() {
    struct Ctx { bool called = false; };
    class TestComp : public Component {
    public:
        COMPONENT_TYPE(TestComp)
        Ctx* ctx = nullptr;
        void OnDestroy() override { ctx->called = true; }
    };

    Ctx ctx;
    {
        auto obj = std::make_unique<GameObject>("test");
        auto* comp = obj->AddComponent<TestComp>();
        comp->ctx = &ctx;
    }
    assert(ctx.called);
}

// 2. NotifyDestroy 중복 호출 안전성 (멱등성)
static void test_notify_destroy_idempotent() {
    struct Ctx { int count = 0; };
    class CountComp : public Component {
    public:
        COMPONENT_TYPE(CountComp)
        Ctx* ctx = nullptr;
        void OnDestroy() override { ctx->count++; }
    };

    Ctx ctx;
    auto obj = std::make_unique<GameObject>("test");
    auto* comp = obj->AddComponent<CountComp>();
    comp->ctx = &ctx;
    obj->NotifyDestroy();
    obj->NotifyDestroy();
    obj.reset();
    assert(ctx.count == 1);
}

// 3. 같은 타입 중복 AddComponent 시 assert 발생 검증
// (assert 기반이므로 Debug 빌드에서 수동 확인)

// 4. disabled 컴포넌트 파괴 시 OnDisable 재호출되지 않는지 검증
static void test_destroy_disabled_no_double_disable() {
    struct Ctx { int disableCount = 0; };
    class TrackComp : public Component {
    public:
        COMPONENT_TYPE(TrackComp)
        Ctx* ctx = nullptr;
        void OnDisable() override { ctx->disableCount++; }
        void OnDestroy() override {}
    };

    Ctx ctx;
    auto obj = std::make_unique<GameObject>("test");
    auto* comp = obj->AddComponent<TrackComp>();
    comp->ctx = &ctx;
    comp->SetEnabled(false);  // OnDisable 1회 호출
    assert(ctx.disableCount == 1);
    obj.reset();  // 파괴 시 이미 disabled → OnDisable 재호출 안 함
    assert(ctx.disableCount == 1);
}
```

---

## P0-2: Collider2D 추상 기반 클래스

### 예상 기간: 1일

### 필요 이유

현재 `BoxCollider2D`가 `Component`를 직접 상속한다. Phase 9에서 `CircleCollider2D`, `PolygonCollider2D`를 추가하면, 물리 시스템이 이들을 다형적으로 처리할 수 있어야 한다.

```
현재:   Component ← BoxCollider2D
목표:   Component ← Collider2D ← BoxCollider2D
                               ← CircleCollider2D   (Phase 9)
                               ← PolygonCollider2D  (Phase 9)
```

### GetWorldBounds() 계약 정의

추상화보다 먼저, bounds 인터페이스의 정확도 계약을 명확히 정의한다.

```
GetWorldBounds() 계약:
  - 반환값: 이 콜라이더를 완전히 둘러싸는 월드 공간 축-정렬 바운딩 박스 (AABB)
  - 회전: 현재 미지원. 회전된 Box는 회전 후 4개 꼭지점의 min/max로
          확장된 AABB를 반환해야 하나, 현재는 회전 무시.
          → Phase 9 (Box2D 통합) 시 Box2D가 자체 shape 관리하므로,
            이 제한은 에디터 피킹/디버그 렌더링에만 영향.
  - 음수 스케일: 정규화. width/height는 항상 양수로 반환.
  - Broad Phase 용도: false positive 허용 (느슨한 bounds OK),
                      false negative 금지 (콜라이더가 bounds 밖으로 나가면 안 됨)
```

### 설계

#### 새 파일: `src/ECS/Components/Collider2D.h`

```cpp
#pragma once

#include "../Component.h"
#include "../../Common/Types.h"

class PhysicsMaterial2D;  // forward declaration (Phase 9)

// Abstract base class for all 2D colliders.
//
// GetWorldBounds() contract:
//   - Returns a world-space AABB that fully encloses the collider
//   - Rotation: NOT currently supported (ignored in AABB computation)
//   - Negative scale: normalized (width/height always positive)
//   - May be larger than the actual shape (false positives OK for broad phase)
class Collider2D : public Component {
public:
    virtual ~Collider2D() = default;

    // ── Collider shape type ──
    enum class ShapeType { Box, Circle, Polygon, Capsule };
    virtual ShapeType GetShapeType() const = 0;

    // ── Common properties ──

    void SetOffset(const Vector2& o) { offset = o; }
    void SetOffset(float x, float y) { offset = Vector2(x, y); }
    Vector2 GetOffset() const { return offset; }

    void SetTrigger(bool trigger) { isTrigger = trigger; }
    bool IsTrigger() const { return isTrigger; }

    // ── Bounds query ──

    // Returns world-space AABB enclosing this collider.
    // See contract above.
    virtual AABB GetWorldBounds() const = 0;

    // ── Common serialization helpers ──

    void SerializeBase(nlohmann::json& j) const;
    void DeserializeBase(const nlohmann::json& j);

protected:
    Vector2 offset = Vector2::Zero();
    bool isTrigger = false;

    // Helper: normalize an AABB so width/height are always positive
    static AABB NormalizeBounds(AABB aabb);
};
```

#### 새 파일: `src/ECS/Components/Collider2D.cpp`

```cpp
#include "Collider2D.h"
#include <nlohmann/json.hpp>
#include <cmath>

void Collider2D::SerializeBase(nlohmann::json& j) const {
    j["offset"] = { offset.x, offset.y };
    j["isTrigger"] = isTrigger;
}

void Collider2D::DeserializeBase(const nlohmann::json& j) {
    if (j.contains("offset") && j["offset"].is_array()) {
        offset = Vector2(j["offset"][0], j["offset"][1]);
    }
    if (j.contains("isTrigger")) {
        isTrigger = j["isTrigger"];
    }
}

AABB Collider2D::NormalizeBounds(AABB aabb) {
    // 음수 width/height 정규화
    if (aabb.width < 0.0f) {
        aabb.x += aabb.width;
        aabb.width = -aabb.width;
    }
    if (aabb.height < 0.0f) {
        aabb.y += aabb.height;
        aabb.height = -aabb.height;
    }
    return aabb;
}
```

#### 수정: `src/ECS/Components/BoxCollider2D.h`

```cpp
#pragma once

#include "Collider2D.h"
#include "Transform.h"

class BoxCollider2D : public Collider2D {  // ← Component → Collider2D
public:
    COMPONENT_TYPE(BoxCollider2D)

    BoxCollider2D() = default;
    BoxCollider2D(float width, float height) : size(width, height) {}

    // ── Collider2D interface ──
    ShapeType GetShapeType() const override { return ShapeType::Box; }
    AABB GetWorldBounds() const override;

    // Size
    void SetSize(float w, float h) { size.x = w; size.y = h; }
    void SetSize(const Vector2& s) { size = s; }
    Vector2 GetSize() const { return size; }

    // Legacy API (delegates to GetWorldBounds)
    AABB GetWorldAABB() const;

    // Collision checks
    bool CheckCollision(const BoxCollider2D* other) const;
    CollisionResult CheckCollisionWithResult(const BoxCollider2D* other) const;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    Vector2 size = Vector2(32.0f, 32.0f);
};
```

#### 수정: `src/ECS/Components/BoxCollider2D.cpp`

핵심 변경점만 표시:

```cpp
AABB BoxCollider2D::GetWorldBounds() const {
    AABB aabb;
    aabb.width = size.x;
    aabb.height = size.y;

    if (gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 worldPos = transform->GetWorldPosition();
            Vector2 worldScale = transform->GetWorldScale();

            aabb.x = worldPos.x + offset.x * worldScale.x;
            aabb.y = worldPos.y + offset.y * worldScale.y;
            aabb.width *= worldScale.x;
            aabb.height *= worldScale.y;
        }
    }

    return NormalizeBounds(aabb);  // ← 음수 스케일 정규화
}

AABB BoxCollider2D::GetWorldAABB() const {
    return GetWorldBounds();  // legacy API
}

void BoxCollider2D::Serialize(nlohmann::json& j) const {
    SerializeBase(j);              // ← Collider2D 공통
    j["size"] = { size.x, size.y };
}

void BoxCollider2D::Deserialize(const nlohmann::json& j) {
    DeserializeBase(j);            // ← Collider2D 공통
    if (j.contains("size") && j["size"].is_array()) {
        SetSize(j["size"][0], j["size"][1]);
    }
}
```

### CMakeLists.txt 수정

`ENGINE_SOURCES`에 추가:
```cmake
src/ECS/Components/Collider2D.cpp
```

### 영향 범위

- **하위 호환**: 완전 호환. public API 변경 없음
- **직렬화 호환**: 키 이름 동일. 기존 씬 파일 로드 가능
- **새 파일**: `Collider2D.h`, `Collider2D.cpp`
- **수정 파일**: `BoxCollider2D.h` (상속), `BoxCollider2D.cpp` (정규화 추가), `CMakeLists.txt`

### 테스트 계획

`tests/test_ecs.cpp`에 추가:

```cpp
// Collider2D 상속 검증
static void test_collider2d_inheritance() {
    auto obj = std::make_unique<GameObject>("test");
    auto* box = obj->AddComponent<BoxCollider2D>();

    Collider2D* collider = dynamic_cast<Collider2D*>(box);
    assert(collider != nullptr);
    assert(collider->GetShapeType() == Collider2D::ShapeType::Box);

    collider->SetOffset(5.0f, 10.0f);
    assert(collider->GetOffset().x == 5.0f);
    collider->SetTrigger(true);
    assert(collider->IsTrigger());
}

// GetWorldBounds와 GetWorldAABB 일치 검증
static void test_collider2d_world_bounds() {
    auto obj = std::make_unique<GameObject>("test");
    auto* transform = obj->AddComponent<Transform>();
    transform->SetPosition(100.0f, 200.0f);
    auto* box = obj->AddComponent<BoxCollider2D>(50.0f, 30.0f);

    AABB bounds = box->GetWorldBounds();
    AABB aabb = box->GetWorldAABB();
    assert(bounds.x == aabb.x && bounds.y == aabb.y);
    assert(bounds.width == aabb.width && bounds.height == aabb.height);
}

// 음수 스케일에서 bounds가 정규화되는지 검증
static void test_collider2d_negative_scale_normalized() {
    auto obj = std::make_unique<GameObject>("test");
    auto* transform = obj->AddComponent<Transform>();
    transform->SetPosition(0.0f, 0.0f);
    transform->SetScale(-2.0f, -1.0f);  // 음수 스케일
    auto* box = obj->AddComponent<BoxCollider2D>(10.0f, 10.0f);

    AABB bounds = box->GetWorldBounds();
    assert(bounds.width > 0.0f);   // 정규화됨
    assert(bounds.height > 0.0f);  // 정규화됨
}
```

---

## P0-3: FixedUpdate 고정 시간 루프

### 예상 기간: 1일

### 필요 이유

현재 메인 루프는 가변 deltaTime으로만 Update를 호출한다. 물리 시뮬레이션은 고정 시간 간격에서 실행해야 결정적 결과를 보장한다. `Script.h`에 `FixedUpdate(float)` 시그니처가 이미 존재하지만 호출되지 않고 있다.

### 핵심 설계 결정

#### 1. accumulator 관리 분리

`Time`은 raw time source로 유지하고, 시뮬레이션 시간 누적은 호출부에서 명시적으로 관리한다.

**이유**: `Time::Update()` 안에서 자동으로 `accumulator += deltaTime`하면, Edit 모드에서 대기 중에도 accumulator가 쌓여서 Edit→Play 전환 시 FixedUpdate가 대량 실행된다.

#### 2. timeScale은 누적 속도에만 적용

```
❌ 틀린 방식: fixedDt = GetFixedDeltaTime() * timeScale    (step 크기 변동 → 결정론 파괴)
✅ 올바른 방식: accumulatedDt = deltaTime * timeScale       (누적 속도 조절)
               fixedDt = GetFixedDeltaTime()               (step 크기 고정)
```

물리 step은 항상 고정 크기로 돌아야 Box2D의 안정성과 재현성이 보장된다. `timeScale < 1.0`이면 fixed step이 덜 자주 발생하고, `timeScale > 1.0`이면 더 자주 발생한다.

#### 3. 스크립트 훅은 GameObject에 캡슐화

`main.cpp`와 `runtime_main.cpp` 양쪽에 `dynamic_cast<Script*>` 루프를 중복 배치하지 않는다. `Start`, `LateUpdate`, 충돌 콜백 등이 추가될 때마다 같은 패턴이 반복되는 것을 방지한다.

### 변경 대상

#### 1. `src/Core/MolgaTime.h` — Fixed time 필드 추가

```cpp
class Time {
public:
    static void Init();
    static void Update();

    static float GetDeltaTime() { return deltaTime; }
    static float GetTime() { return currentTime; }
    static float GetFPS() { return fps; }
    static int GetFrameCount() { return frameCount; }

    // ── Fixed timestep ──
    static float GetFixedDeltaTime() { return fixedDeltaTime; }
    static void SetFixedDeltaTime(float dt) { fixedDeltaTime = dt; }

    // Accumulator management — caller-driven, not auto-accumulated.
    // Call AccumulateFixedTime() with the simulation dt (may differ from raw dt
    // due to timeScale or pause).
    static void AccumulateFixedTime(float simDt) { accumulator += simDt; }
    static bool HasPendingFixedStep() { return accumulator >= fixedDeltaTime; }
    static void ConsumeFixedStep() { accumulator -= fixedDeltaTime; }
    static void ResetFixedAccumulator() { accumulator = 0.0f; }

    // Interpolation alpha for rendering between physics steps
    static float GetFixedAlpha() {
        return fixedDeltaTime > 0.0f ? accumulator / fixedDeltaTime : 0.0f;
    }

private:
    static float deltaTime;
    static float lastTime;
    static float currentTime;
    static float fps;
    static int frameCount;
    static float fpsUpdateInterval;
    static float fpsAccumulator;
    static int fpsFrameCount;

    static float fixedDeltaTime;   // 기본 0.02초 (50Hz)
    static float accumulator;
};
```

#### 2. `src/Core/MolgaTime.cpp`

```cpp
float Time::fixedDeltaTime = 0.02f;
float Time::accumulator = 0.0f;

void Time::Init() {
    lastTime = static_cast<float>(glfwGetTime());
    currentTime = lastTime;
    deltaTime = 0.0f;
    fps = 0.0f;
    frameCount = 0;
    fpsAccumulator = 0.0f;
    fpsFrameCount = 0;
    accumulator = 0.0f;
}

void Time::Update() {
    currentTime = static_cast<float>(glfwGetTime());
    deltaTime = currentTime - lastTime;

    // Clamp to prevent spiral of death
    if (deltaTime > 0.25f) {
        deltaTime = 0.25f;
    }

    lastTime = currentTime;
    frameCount++;

    // FPS calculation (기존 코드 유지)
    fpsAccumulator += deltaTime;
    fpsFrameCount++;
    if (fpsAccumulator >= fpsUpdateInterval) {
        fps = static_cast<float>(fpsFrameCount) / fpsAccumulator;
        fpsAccumulator = 0.0f;
        fpsFrameCount = 0;
    }

    // NOTE: accumulator는 여기서 자동 누적하지 않음.
    // 호출부에서 AccumulateFixedTime(simDt)를 명시적으로 호출해야 함.
}
```

#### 3. `src/main.cpp` — 에디터 메인 루프

```cpp
// Play 모드 업데이트 섹션 변경
if (editorState.IsPlayMode()) {
    float timeScale = editorState.GetTimeScale();
    float scaledDt = dt * timeScale;

    // Fixed Update: timeScale은 누적 속도에만 적용
    Time::AccumulateFixedTime(scaledDt);

    while (Time::HasPendingFixedStep()) {
        float fixedDt = Time::GetFixedDeltaTime();  // 항상 고정 크기

        for (auto& obj : editorObjects) {
            if (obj && obj->IsActive()) {
                obj->FixedUpdateScripts(fixedDt);  // ← GameObject 메서드
            }
        }
        // TODO: Phase 9에서 PhysicsWorld::Step(fixedDt) 호출 위치
        Time::ConsumeFixedStep();
    }

    // Variable Update (기존 코드)
    SceneManager::Update(scaledDt);
    for (auto& obj : editorObjects) {
        if (obj && obj->IsActive()) {
            obj->Update(scaledDt);
        }
    }
}
```

**Edit → Play 전환 시 accumulator 리셋**: `EditorState`의 Play 진입 로직에서 `Time::ResetFixedAccumulator()` 호출.

```cpp
// EditorState.cpp의 Play 진입 지점에 추가
void EditorState::EnterPlayMode() {
    // ... 기존 코드 ...
    Time::ResetFixedAccumulator();  // ← 추가: backlog 방지
}
```

#### 4. `src/runtime_main.cpp` — 런타임 메인 루프

```cpp
while (!glfwWindowShouldClose(window)) {
    Time::Update();
    Input::Update();
    float dt = Time::GetDeltaTime();

    // Fixed Update
    Time::AccumulateFixedTime(dt);  // 런타임은 timeScale 없음 (또는 별도 관리)

    while (Time::HasPendingFixedStep()) {
        float fixedDt = Time::GetFixedDeltaTime();

        for (auto& obj : gameObjects) {
            if (obj && obj->IsActive()) {
                obj->FixedUpdateScripts(fixedDt);
            }
        }
        // TODO: Phase 9에서 PhysicsWorld::Step(fixedDt)
        Time::ConsumeFixedStep();
    }

    // Variable Update (기존 코드)
    for (auto& obj : gameObjects) {
        if (obj && obj->IsActive()) {
            obj->Update(dt);
        }
    }

    // Render (기존 코드 유지)
    // ...
}
```

### 영향 범위

- **하위 호환**: 완전 호환. 기존 `Update(dt)` 흐름 그대로
- **수정 파일**: `MolgaTime.h`, `MolgaTime.cpp`, `main.cpp`, `runtime_main.cpp`, `EditorState.cpp`, `GameObject.h`, `GameObject.cpp`
- **기존 동작 변경**: `deltaTime`에 0.25초 상한 추가
- **Script.h**: 변경 없음 (이미 시그니처 보유)

### 테스트 계획

accumulator 동작 검증 (유닛 테스트):

```cpp
static void test_fixed_accumulator_basic() {
    Time::Init();
    Time::SetFixedDeltaTime(0.02f);

    // 0.05초 누적 → 2회 step 가능
    Time::AccumulateFixedTime(0.05f);
    int steps = 0;
    while (Time::HasPendingFixedStep()) {
        steps++;
        Time::ConsumeFixedStep();
    }
    assert(steps == 2);
}

static void test_fixed_accumulator_reset() {
    Time::Init();
    Time::SetFixedDeltaTime(0.02f);
    Time::AccumulateFixedTime(1.0f);  // 대량 누적
    Time::ResetFixedAccumulator();
    assert(!Time::HasPendingFixedStep());  // 리셋 후 0
}
```

통합 테스트: 1초 실행 시 `FixedUpdate` ~50회 호출 확인.

---

## 실행 순서 및 의존성

```
P0-1 라이프사이클 정비  ─────┐
     (1일)                    │     P0-2가 P0-1에 의존 (OnDestroy 계약)
                              │
P0-2 Collider2D 기반     ────┤     P0-1 완료 후
     (1일)                    │
                              │
P0-3 FixedUpdate         ────┘     독립 (P0-1, P0-2와 병렬 가능)
     (0.5-1일)
```

### 권장 실행 순서

```
Day 1:    P0-1 라이프사이클 정비 + P0-3 FixedUpdate (병렬 가능)
Day 2:    P0-2 Collider2D 추출
Day 2-3:  전체 검증 + 테스트
```

**총 소요: 2.5-3일** (기존 4-5일에서 단축. P0-4 제거 + 병렬 실행)

---

## 검증 계획

### 완료 체크리스트

| # | 검증 항목 | 방법 | 통과 기준 |
|---|----------|------|----------|
| 1 | OnDestroy 호출 | 유닛 테스트 | 파괴 시 호출, 중복 호출 시 1회만 |
| 2 | AddComponent 중복 방어 | Debug 빌드 | assert 발생 (같은 타입 2회 추가 시) |
| 3 | disabled 컴포넌트 파괴 | 유닛 테스트 | OnDisable 재호출 없음 |
| 4 | Collider2D 상속 | 유닛 테스트 | dynamic_cast 성공, ShapeType::Box |
| 5 | 음수 스케일 정규화 | 유닛 테스트 | width/height > 0 |
| 6 | 기존 씬 로드 | 수동 테스트 | BoxCollider2D 직렬화/역직렬화 정상 |
| 7 | FixedUpdate 주기 | 유닛 테스트 | accumulator 기반 step 수 일치 |
| 8 | Edit→Play accumulator 리셋 | 수동 테스트 | Play 진입 시 FixedUpdate 폭주 없음 |
| 9 | timeScale 적용 | 수동 테스트 | timeScale 0.5에서 FixedUpdate 절반 빈도 |
| 10 | 빌드 성공 | CI | macOS 빌드 + CTest 전체 통과 |
| 11 | 에디터 정상 | 수동 테스트 | Play/Edit 모드 전환 정상 |
| 12 | 성능 회귀 없음 | 수동 테스트 | FPS 동일 또는 향상 |

### 커밋 구조

```
p0/prerequisites 브랜치:
  commit 1: "fix: guard AddComponent against duplicate types"
  commit 2: "feat: add OnDestroy lifecycle with snapshot-safe iteration"
  commit 3: "refactor: extract Collider2D abstract base with bounds contract"
  commit 4: "feat: add FixedUpdate loop with caller-driven accumulator"
  commit 5: "test: add P0 verification tests"
```

---

## 수정 파일 종합

| 파일 | P0-1 | P0-2 | P0-3 | 변경 유형 |
|------|:----:|:----:|:----:|----------|
| `src/ECS/Component.h` | ✅ | | | 1줄 추가 |
| `src/ECS/GameObject.h` | ✅ | | ✅ | 중복 방어 + NotifyDestroy + 스크립트 훅 |
| `src/ECS/GameObject.cpp` | ✅ | | ✅ | 스냅샷 순회 + 스크립트 훅 구현 |
| `src/ECS/Components/Collider2D.h` | | ✅ | | 새 파일 |
| `src/ECS/Components/Collider2D.cpp` | | ✅ | | 새 파일 |
| `src/ECS/Components/BoxCollider2D.h` | | ✅ | | 상속 변경 |
| `src/ECS/Components/BoxCollider2D.cpp` | | ✅ | | 정규화 + 직렬화 위임 |
| `src/Core/MolgaTime.h` | | | ✅ | fixed timestep API |
| `src/Core/MolgaTime.cpp` | | | ✅ | 초기화 + clamp |
| `src/main.cpp` | | | ✅ | FixedUpdate 루프 |
| `src/runtime_main.cpp` | | | ✅ | FixedUpdate 루프 |
| `src/Editor/EditorState.cpp` | | | ✅ | ResetFixedAccumulator |
| `CMakeLists.txt` | | ✅ | | 1줄 추가 |
| `tests/test_ecs.cpp` | ✅ | ✅ | | 테스트 추가 |
| `tests/CMakeLists.txt` | | | | 테스트 등록 확인 |

**총 변경**: 기존 파일 10개 수정, 새 파일 2개 생성
