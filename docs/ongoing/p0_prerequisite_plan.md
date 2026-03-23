# P0: 선행 정비 작업 상세 계획서

> 작성일: 2026-03-23
> 기반: MASTER_PLAN.md Section 3 "선행 정비 작업"
> 목적: Phase 6 시작 전 코드베이스 기초 정비 (4개 작업, 4-5일)
> 브랜치: `p0/prerequisites`

---

## 목차

1. [P0-1: Component에 OnDestroy() 추가](#p0-1-component에-ondestroy-추가)
2. [P0-2: Collider2D 추상 기반 클래스](#p0-2-collider2d-추상-기반-클래스)
3. [P0-3: FixedUpdate 고정 시간 루프](#p0-3-fixedupdate-고정-시간-루프)
4. [P0-4: Broad Phase 공간 분할](#p0-4-broad-phase-공간-분할)
5. [실행 순서 및 의존성](#실행-순서-및-의존성)
6. [검증 계획](#검증-계획)

---

## 현재 상태 요약

분석 대상 파일과 현재 구조:

```
src/ECS/Component.h          ← 라이프사이클: OnAttach, OnDetach, OnEnable, OnDisable (OnDestroy 없음)
src/ECS/GameObject.h/cpp     ← ~GameObject()에서 OnDetach() 호출 후 componentMap.clear()
src/ECS/Components/
  BoxCollider2D.h/cpp        ← Component 직접 상속 (Collider2D 중간 계층 없음)
  Transform.h/cpp
  SpriteRenderer.h/cpp
src/Physics/Collision.h/cpp  ← 정적 유틸 (CheckAABB, CheckCircle 등). 공간 분할 없음
src/Core/MolgaTime.h/cpp     ← deltaTime만 제공, fixedDeltaTime 없음
src/Core/Bootstrap.cpp       ← 메인 루프에 FixedUpdate 호출 없음
src/main.cpp                 ← while 루프에서 Time::Update() → dt → Update(dt) 순서
src/runtime_main.cpp         ← 동일 구조
src/Scripting/Script.h       ← FixedUpdate(float) 시그니처 존재하나, 호출되지 않음
src/Common/Types.h           ← AABB, Circle, CollisionResult 정의
```

---

## P0-1: Component에 OnDestroy() 추가

### 예상 기간: 0.5일

### 필요 이유

현재 `Component`에는 `OnAttach()`/`OnDetach()`만 있다. `OnDetach()`는 컴포넌트가 맵에서 제거될 때 호출되지만, `GameObject` 자체가 파괴될 때의 리소스 해제 콜백이 명시적으로 없다.

Phase 9에서 Box2D 통합 시, `Rigidbody2D`는 `OnDestroy()`에서 `b2DestroyBody(bodyId)`를 호출해야 한다. 오디오 소스, 파티클 시스템 등도 파괴 시 리소스 해제가 필요하다.

현재 `~GameObject()`가 `OnDetach()`를 이미 호출하므로, `OnDestroy()`의 호출 시점과 의미를 명확히 구분해야 한다.

### 설계

**호출 순서 정의**:
```
컴포넌트 제거 (RemoveComponent):  OnDisable() → OnDetach()
게임오브젝트 파괴 (Destroy):      OnDisable() → OnDestroy() → OnDetach()
```

- `OnDetach()`: 컴포넌트가 맵에서 분리될 때 (개별 제거 또는 파괴 시)
- `OnDestroy()`: 게임오브젝트가 파괴되어 컴포넌트도 함께 사라질 때. 외부 리소스 해제 담당

### 변경 대상

#### 1. `src/ECS/Component.h`

```cpp
// Component 클래스에 추가할 가상 메서드
class Component {
public:
    // ... 기존 메서드들 ...

    // Called when the owning GameObject is being destroyed.
    // Use for releasing external resources (physics bodies, GPU handles, etc.)
    // Called BEFORE OnDetach().
    virtual void OnDestroy() {}

    // ... 나머지 ...
};
```

**변경 위치**: `OnDetach()` 선언 아래 (`Component.h:32` 부근)
**추가할 코드**: `virtual void OnDestroy() {}` 한 줄

#### 2. `src/ECS/GameObject.h`

```cpp
// 새로 추가할 public 메서드
class GameObject {
public:
    // ... 기존 메서드들 ...

    // Notify all components that this GameObject is being destroyed.
    // Called by the destruction system before the destructor.
    void NotifyDestroy();

    // ... 나머지 ...
};
```

#### 3. `src/ECS/GameObject.cpp`

**`NotifyDestroy()` 구현 추가**:
```cpp
void GameObject::NotifyDestroy() {
    for (auto& [id, comp] : componentMap) {
        comp->OnDestroy();
    }
}
```

**`~GameObject()` 수정** — 소멸자에서도 `OnDestroy()` 호출 추가 (안전망):
```cpp
GameObject::~GameObject() {
    // Destroy all components (release external resources)
    for (auto& [id, comp] : componentMap) {
        comp->OnDestroy();  // ← 추가
    }
    // Detach all components
    for (auto& [id, comp] : componentMap) {
        comp->OnDetach();
    }
    componentMap.clear();

    if (parent) {
        parent->RemoveChild(this);
    }
    children.clear();
}
```

**참고**: `NotifyDestroy()`는 미래의 D2(오브젝트 라이프사이클)에서 `Destroy()` 함수가 지연 파괴 전에 호출할 진입점이다. 소멸자에서의 호출은 `NotifyDestroy()`가 호출되지 않은 경우의 안전망이다. 중복 호출 방지를 위해 `destroyed` 플래그를 추가한다.

#### 4. `src/ECS/GameObject.h` — destroyed 플래그

```cpp
class GameObject {
    // ... private 섹션에 추가 ...
    bool destroyed = false;
};
```

#### 5. `src/ECS/GameObject.cpp` — 중복 호출 방지

```cpp
void GameObject::NotifyDestroy() {
    if (destroyed) return;
    destroyed = true;
    for (auto& [id, comp] : componentMap) {
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
```

### 영향 범위

- **하위 호환**: 완전 호환. `OnDestroy()`는 기본 빈 구현이므로 기존 컴포넌트에 영향 없음
- **수정 파일**: `Component.h` (1줄), `GameObject.h` (2줄), `GameObject.cpp` (~15줄)
- **테스트 영향**: 기존 4개 테스트에 영향 없음

### 테스트 계획

`tests/test_ecs.cpp`에 추가:

```cpp
// OnDestroy가 GameObject 파괴 시 호출되는지 검증
static void test_on_destroy_called() {
    static bool destroyCalled = false;

    class TestComponent : public Component {
    public:
        COMPONENT_TYPE(TestComponent)
        void OnDestroy() override { destroyCalled = true; }
    };

    {
        auto obj = std::make_unique<GameObject>("test");
        obj->AddComponent<TestComponent>();
        // obj goes out of scope → ~GameObject → OnDestroy
    }
    assert(destroyCalled);
}

// NotifyDestroy 중복 호출 안전성 검증
static void test_notify_destroy_idempotent() {
    static int destroyCount = 0;

    class CountComponent : public Component {
    public:
        COMPONENT_TYPE(CountComponent)
        void OnDestroy() override { destroyCount++; }
    };

    auto obj = std::make_unique<GameObject>("test");
    obj->AddComponent<CountComponent>();
    obj->NotifyDestroy();
    obj->NotifyDestroy();  // 두 번째 호출은 무시
    obj.reset();           // 소멸자에서도 무시
    assert(destroyCount == 1);
}
```

---

## P0-2: Collider2D 추상 기반 클래스

### 예상 기간: 1일

### 필요 이유

현재 `BoxCollider2D`가 `Component`를 직접 상속한다. Phase 9에서 `CircleCollider2D`, `PolygonCollider2D`, `CapsuleCollider2D`를 추가하면, 물리 시스템이 이들을 다형적으로 처리할 수 있어야 한다.

```
현재:   Component ← BoxCollider2D
목표:   Component ← Collider2D ← BoxCollider2D
                               ← CircleCollider2D   (Phase 9)
                               ← PolygonCollider2D  (Phase 9)
```

`Collider2D` 기반 클래스가 있으면:
- `PhysicsWorld`가 `Collider2D*`로 모든 콜라이더를 순회 가능
- 공통 속성(offset, isTrigger, physicsMaterial)을 한 곳에서 관리
- `GetWorldBounds()` 같은 공통 인터페이스로 broad phase 통합

### 설계

#### 새 파일: `src/ECS/Components/Collider2D.h`

```cpp
#pragma once

#include "../Component.h"
#include "../../Common/Types.h"

class PhysicsMaterial2D;  // forward declaration (Phase 9)

// Abstract base class for all 2D colliders
class Collider2D : public Component {
public:
    virtual ~Collider2D() = default;

    // ── Collider shape type ──
    enum class ShapeType { Box, Circle, Polygon, Capsule };
    virtual ShapeType GetShapeType() const = 0;

    // ── Common properties ──

    // Offset from Transform position
    void SetOffset(const Vector2& o) { offset = o; }
    void SetOffset(float x, float y) { offset = Vector2(x, y); }
    Vector2 GetOffset() const { return offset; }

    // Trigger mode (no physical collision response, detection only)
    void SetTrigger(bool trigger) { isTrigger = trigger; }
    bool IsTrigger() const { return isTrigger; }

    // ── Bounds query (for broad phase) ──

    // Returns the world-space AABB that fully encloses this collider.
    // Every collider subclass must implement this for broad phase compatibility.
    virtual AABB GetWorldBounds() const = 0;

    // ── Common serialization helpers ──

    void SerializeBase(nlohmann::json& j) const;
    void DeserializeBase(const nlohmann::json& j);

protected:
    Vector2 offset = Vector2::Zero();
    bool isTrigger = false;
    // PhysicsMaterial2D* material = nullptr;  // Phase 9에서 활성화
};
```

#### 새 파일: `src/ECS/Components/Collider2D.cpp`

```cpp
#include "Collider2D.h"
#include <nlohmann/json.hpp>

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
```

#### 수정: `src/ECS/Components/BoxCollider2D.h`

```cpp
#pragma once

#include "Collider2D.h"      // ← 변경: Component.h → Collider2D.h
#include "Transform.h"
// #include "../../Physics/Collision.h"  ← 제거 (Collider2D.h에서 Types.h 포함)

class BoxCollider2D : public Collider2D {  // ← 변경: Component → Collider2D
public:
    COMPONENT_TYPE(BoxCollider2D)

    BoxCollider2D() = default;
    BoxCollider2D(float width, float height) : size(width, height) {}

    // ── Collider2D interface ──
    ShapeType GetShapeType() const override { return ShapeType::Box; }
    AABB GetWorldBounds() const override;  // ← 새 메서드 (기존 GetWorldAABB 리다이렉트)

    // Size
    void SetSize(float w, float h) { size.x = w; size.y = h; }
    void SetSize(const Vector2& s) { size = s; }
    Vector2 GetSize() const { return size; }

    // ── 기존 API 유지 (하위 호환) ──

    // Offset은 Collider2D에서 상속 (SetOffset/GetOffset)
    // IsTrigger는 Collider2D에서 상속 (SetTrigger/IsTrigger)

    // Get world AABB (legacy API, delegates to GetWorldBounds)
    AABB GetWorldAABB() const;

    // Check collision with another BoxCollider2D
    bool CheckCollision(const BoxCollider2D* other) const;
    CollisionResult CheckCollisionWithResult(const BoxCollider2D* other) const;

    // Serialization
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

    // Editor GUI
    void OnInspectorGUI() override;

private:
    Vector2 size = Vector2(32.0f, 32.0f);
    // offset, isTrigger는 Collider2D에서 상속
};
```

#### 수정: `src/ECS/Components/BoxCollider2D.cpp`

```cpp
#include "BoxCollider2D.h"
#include "../GameObject.h"
#include "../ComponentFactory.h"
#include "../../Physics/Collision.h"  // CheckAABB 등
#include <nlohmann/json.hpp>

REGISTER_COMPONENT(BoxCollider2D)
#ifdef MOLGA_EDITOR
#include <imgui.h>
#endif

using json = nlohmann::json;

AABB BoxCollider2D::GetWorldBounds() const {
    return GetWorldAABB();  // 기존 구현 재사용
}

AABB BoxCollider2D::GetWorldAABB() const {
    AABB aabb;
    aabb.width = size.x;
    aabb.height = size.y;

    if (gameObject) {
        Transform* transform = gameObject->GetComponent<Transform>();
        if (transform) {
            Vector2 worldPos = transform->GetWorldPosition();
            Vector2 worldScale = transform->GetWorldScale();

            aabb.x = worldPos.x + offset.x * worldScale.x;  // Collider2D::offset 사용
            aabb.y = worldPos.y + offset.y * worldScale.y;
            aabb.width *= worldScale.x;
            aabb.height *= worldScale.y;
        }
    }

    return aabb;
}

// CheckCollision, CheckCollisionWithResult는 변경 없음

void BoxCollider2D::Serialize(nlohmann::json& j) const {
    SerializeBase(j);                      // ← Collider2D 공통 직렬화
    j["size"] = { size.x, size.y };
}

void BoxCollider2D::Deserialize(const nlohmann::json& j) {
    DeserializeBase(j);                    // ← Collider2D 공통 역직렬화
    if (j.contains("size") && j["size"].is_array()) {
        SetSize(j["size"][0], j["size"][1]);
    }
}

void BoxCollider2D::OnInspectorGUI() {
#ifdef MOLGA_EDITOR
    float sizeArr[2] = { size.x, size.y };
    if (ImGui::DragFloat2("Size", sizeArr, 0.5f)) {
        SetSize(sizeArr[0], sizeArr[1]);
    }

    float offsetArr[2] = { offset.x, offset.y };  // Collider2D::offset
    if (ImGui::DragFloat2("Offset", offsetArr, 0.5f)) {
        SetOffset(offsetArr[0], offsetArr[1]);
    }

    bool trigger = isTrigger;                       // Collider2D::isTrigger
    if (ImGui::Checkbox("Is Trigger", &trigger)) {
        SetTrigger(trigger);
    }
#endif
}
```

### CMakeLists.txt 수정

`ENGINE_SOURCES`에 추가:
```cmake
src/ECS/Components/Collider2D.cpp
```

### 영향 범위

- **하위 호환**: 완전 호환. `BoxCollider2D`의 public API 변경 없음
  - `SetOffset()`, `GetOffset()`, `SetTrigger()`, `IsTrigger()`는 `Collider2D`에서 상속되므로 동일 시그니처
  - `GetWorldAABB()`는 기존대로 유지
- **직렬화 호환**: `offset`, `isTrigger` 키 이름 동일. 기존 씬 파일 로드 가능
- **수정 파일**: `BoxCollider2D.h` (상속 변경), `BoxCollider2D.cpp` (직렬화 위임)
- **새 파일**: `Collider2D.h`, `Collider2D.cpp`
- **테스트 영향**: `test_collision.cpp`는 `Collision` 유틸만 테스트하므로 영향 없음

### 테스트 계획

`tests/test_ecs.cpp`에 추가:

```cpp
// BoxCollider2D가 Collider2D를 상속하는지 검증
static void test_collider2d_inheritance() {
    auto obj = std::make_unique<GameObject>("test");
    auto box = obj->AddComponent<BoxCollider2D>();

    // BoxCollider2D는 Collider2D의 인터페이스를 제공해야 함
    Collider2D* collider = dynamic_cast<Collider2D*>(box);
    assert(collider != nullptr);
    assert(collider->GetShapeType() == Collider2D::ShapeType::Box);

    // 공통 프로퍼티가 동작해야 함
    collider->SetOffset(5.0f, 10.0f);
    assert(collider->GetOffset().x == 5.0f);
    collider->SetTrigger(true);
    assert(collider->IsTrigger());
}

// GetWorldBounds와 GetWorldAABB가 동일한 결과를 반환하는지 검증
static void test_collider2d_world_bounds() {
    auto obj = std::make_unique<GameObject>("test");
    auto transform = obj->AddComponent<Transform>();
    transform->SetPosition(100.0f, 200.0f);
    auto box = obj->AddComponent<BoxCollider2D>(50.0f, 30.0f);

    AABB bounds = box->GetWorldBounds();
    AABB aabb = box->GetWorldAABB();

    assert(bounds.x == aabb.x);
    assert(bounds.y == aabb.y);
    assert(bounds.width == aabb.width);
    assert(bounds.height == aabb.height);
}
```

---

## P0-3: FixedUpdate 고정 시간 루프

### 예상 기간: 1일

### 필요 이유

현재 메인 루프는 가변 deltaTime으로만 Update를 호출한다. 물리 시뮬레이션은 **고정 시간 간격(fixed timestep)**에서 실행해야 결정적(deterministic) 결과를 보장한다.

```
현재:   while(...) { dt = Time::GetDeltaTime(); Update(dt); Render(); }
목표:   while(...) { dt = Time::GetDeltaTime();
                     accumulator += dt;
                     while(accumulator >= fixedDT) { FixedUpdate(fixedDT); accumulator -= fixedDT; }
                     Update(dt);
                     Render(); }
```

Script.h에 `FixedUpdate(float)` 시그니처가 이미 존재하지만, 어디에서도 호출하지 않고 있다. 이를 실제로 구동시킨다.

### 설계

#### 1. `src/Core/MolgaTime.h` — Fixed time 필드 추가

```cpp
class Time {
public:
    // ... 기존 ...
    static void Init();
    static void Update();

    static float GetDeltaTime() { return deltaTime; }
    static float GetTime() { return currentTime; }
    static float GetFPS() { return fps; }
    static int GetFrameCount() { return frameCount; }

    // ── Fixed timestep (추가) ──
    static float GetFixedDeltaTime() { return fixedDeltaTime; }
    static void SetFixedDeltaTime(float dt) { fixedDeltaTime = dt; }

    // Accumulator for fixed timestep loop.
    // Returns true while there is a pending fixed step.
    // Caller should call ConsumeFixedStep() after each FixedUpdate.
    static bool HasPendingFixedStep() { return accumulator >= fixedDeltaTime; }
    static void ConsumeFixedStep() { accumulator -= fixedDeltaTime; }
    static void AccumulateTime() { accumulator += deltaTime; }

    // Interpolation alpha for rendering between physics steps
    // alpha = accumulator / fixedDeltaTime  (0.0 ~ 1.0)
    static float GetFixedAlpha() {
        return fixedDeltaTime > 0.0f ? accumulator / fixedDeltaTime : 0.0f;
    }

private:
    // ... 기존 ...
    static float fixedDeltaTime;   // 기본 0.02초 (50Hz)
    static float accumulator;       // 미소비된 시간 누적
};
```

#### 2. `src/Core/MolgaTime.cpp` — 정적 변수 초기화 및 Update 수정

```cpp
// 새 정적 변수 추가
float Time::fixedDeltaTime = 0.02f;  // 50Hz (Unity 기본값)
float Time::accumulator = 0.0f;

void Time::Init() {
    lastTime = static_cast<float>(glfwGetTime());
    currentTime = lastTime;
    deltaTime = 0.0f;
    fps = 0.0f;
    frameCount = 0;
    fpsAccumulator = 0.0f;
    fpsFrameCount = 0;
    accumulator = 0.0f;     // ← 추가
}

void Time::Update() {
    currentTime = static_cast<float>(glfwGetTime());
    deltaTime = currentTime - lastTime;

    // Clamp deltaTime to prevent spiral of death
    // (e.g., after breakpoint, window drag, etc.)
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

    // Fixed timestep accumulation
    accumulator += deltaTime;
}
```

**중요 변경**: `deltaTime` 상한(0.25초) 추가. 브레이크포인트나 윈도우 드래그 후 대량 시간이 축적되어 물리 루프가 폭주하는 "spiral of death"를 방지한다.

#### 3. `src/main.cpp` — 에디터 메인 루프 수정

```cpp
// Play 모드 업데이트 섹션 변경
if (editorState.IsPlayMode()) {
    float scaledDt = dt * editorState.GetTimeScale();

    // ── Fixed Update Loop (물리/스크립트 FixedUpdate) ──
    // AccumulateTime은 Time::Update()에서 이미 수행됨
    while (Time::HasPendingFixedStep()) {
        float fixedDt = Time::GetFixedDeltaTime() * editorState.GetTimeScale();

        // FixedUpdate for all game objects (Script::FixedUpdate)
        for (auto& obj : editorObjects) {
            if (obj && obj->IsActive()) {
                for (auto* comp : obj->GetComponents()) {
                    if (comp->IsEnabled()) {
                        // Script 클래스만 FixedUpdate를 가짐
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->FixedUpdate(fixedDt);
                        }
                    }
                }
            }
        }

        // TODO: Phase 9에서 PhysicsWorld::Step(fixedDt) 호출 위치
        Time::ConsumeFixedStep();
    }

    // ── Variable Update (기존 코드) ──
    SceneManager::Update(scaledDt);
    for (auto& obj : editorObjects) {
        if (obj && obj->IsActive()) {
            obj->Update(scaledDt);
        }
    }
}
```

#### 4. `src/runtime_main.cpp` — 런타임 메인 루프 수정

```cpp
// Main game loop
while (!glfwWindowShouldClose(window)) {
    Time::Update();
    Input::Update();
    float dt = Time::GetDeltaTime();

    // ── Fixed Update Loop ──
    while (Time::HasPendingFixedStep()) {
        float fixedDt = Time::GetFixedDeltaTime();

        for (auto& obj : gameObjects) {
            if (obj && obj->IsActive()) {
                for (auto* comp : obj->GetComponents()) {
                    if (comp->IsEnabled()) {
                        if (auto* script = dynamic_cast<Script*>(comp)) {
                            script->FixedUpdate(fixedDt);
                        }
                    }
                }
            }
        }

        // TODO: Phase 9에서 PhysicsWorld::Step(fixedDt) 호출 위치
        Time::ConsumeFixedStep();
    }

    // ── Variable Update (기존 코드) ──
    for (auto& obj : gameObjects) {
        if (obj && obj->IsActive()) {
            obj->Update(dt);
        }
    }

    // Render (기존 코드 유지)
    // ...
}
```

### Time::Update()에서 AccumulateTime 제거

`Time::Update()` 내에서 `accumulator += deltaTime`을 수행하므로 별도 `AccumulateTime()` 호출은 불필요하다. API는 유지하되 메인 루프에서는 사용하지 않는다.

### 영향 범위

- **하위 호환**: 완전 호환. 기존 `Update(dt)` 흐름 변경 없음
- **수정 파일**: `MolgaTime.h` (~15줄), `MolgaTime.cpp` (~10줄), `main.cpp` (~20줄), `runtime_main.cpp` (~15줄)
- **기존 동작 변경**: `deltaTime`에 0.25초 상한 추가 (안전성 향상)
- **Script.h**: 변경 없음 (이미 `FixedUpdate` 시그니처 보유)

### 테스트 계획

```cpp
// Fixed timestep accumulator 동작 검증
static void test_fixed_timestep_basic() {
    Time::Init();
    Time::SetFixedDeltaTime(0.02f);  // 50Hz

    // 시뮬레이션: 0.05초가 경과 (2.5 fixed step)
    // Time::Update()를 직접 호출하기 어려우므로 accumulator를 수동 테스트
    // → 통합 테스트에서 검증 (빌드 후 FixedUpdate 호출 횟수 카운트)
}
```

실질적으로는 **통합 테스트**로 검증:
1. FixedUpdate에서 카운터를 증가시키는 테스트 스크립트 작성
2. 1초 실행 시 카운터가 ~50 (±1)인지 확인

---

## P0-4: Broad Phase 공간 분할

### 예상 기간: 2일

### 필요 이유

현재 충돌 감지는 모든 콜라이더 쌍을 순회하는 O(n²) 방식이다. 오브젝트 수가 100개만 넘어도 5,000번 비교가 필요하다.

Broad Phase는 AABB 기반으로 "충돌 가능성이 있는 쌍"만 빠르게 추출한다. 이후 Narrow Phase (현재 `Collision` 클래스)가 정밀 검사를 수행한다.

```
현재:   모든 쌍 O(n²) → Narrow Phase
목표:   Broad Phase O(n log n) → 후보 쌍 → Narrow Phase
```

### 알고리즘 선택: Uniform Grid

| 옵션 | 장점 | 단점 |
|------|------|------|
| **Uniform Grid** | 구현 단순, 삽입/쿼리 O(1), 캐시 친화적 | 오브젝트 크기 차이가 크면 비효율 |
| Quadtree | 동적 분할, 크기 차이 처리 가능 | 구현 복잡, 캐시 비친화적 |
| Sort & Sweep | 거의 정렬된 상태에서 효율적 | 구현 복잡, 축 하나만 사용 |

**Uniform Grid 선택 이유**: 2D 게임에서 대부분의 콜라이더는 비슷한 크기이며, 구현 난이도가 가장 낮다. Phase 9에서 Box2D가 자체 broad phase를 제공하므로, 여기서는 기존 `Collision` 시스템의 성능 개선에 집중한다.

### 설계

#### 새 파일: `src/Physics/SpatialGrid.h`

```cpp
#pragma once

#include "../Common/Types.h"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <functional>

// Forward declaration
class Collider2D;

// A collision pair (unordered)
struct CollisionPair {
    Collider2D* a;
    Collider2D* b;
};

// Uniform grid spatial partitioning for broad phase collision detection.
// Cell size should roughly match the average collider size.
class SpatialGrid {
public:
    explicit SpatialGrid(float cellSize = 64.0f);

    // Set the cell size (call before inserting objects)
    void SetCellSize(float size);
    float GetCellSize() const { return cellSize; }

    // Clear all cells for a new frame
    void Clear();

    // Insert a collider into the grid based on its world bounds
    void Insert(Collider2D* collider);

    // Retrieve all potential collision pairs (broad phase)
    // Returns unique pairs — each (A,B) appears only once.
    std::vector<CollisionPair> GetPotentialPairs() const;

    // Query: find all colliders whose cells overlap with the given AABB
    std::vector<Collider2D*> Query(const AABB& bounds) const;

    // Stats
    int GetColliderCount() const { return colliderCount; }
    int GetCellCount() const { return static_cast<int>(cells.size()); }

private:
    float cellSize;
    int colliderCount = 0;

    // Cell key from grid coordinates
    struct CellKey {
        int x, y;
        bool operator==(const CellKey& other) const {
            return x == other.x && y == other.y;
        }
    };

    struct CellKeyHash {
        size_t operator()(const CellKey& k) const {
            // Cantor pairing function
            size_t h1 = std::hash<int>{}(k.x);
            size_t h2 = std::hash<int>{}(k.y);
            return h1 ^ (h2 << 16) ^ (h2 >> 16);
        }
    };

    // Each cell stores a list of colliders
    std::unordered_map<CellKey, std::vector<Collider2D*>, CellKeyHash> cells;

    // Convert world position to cell coordinate
    CellKey WorldToCell(float wx, float wy) const;

    // Get all cells that an AABB overlaps
    void GetOverlappingCells(const AABB& bounds,
                             std::vector<CellKey>& outCells) const;
};
```

#### 새 파일: `src/Physics/SpatialGrid.cpp`

```cpp
#include "SpatialGrid.h"
#include "../ECS/Components/Collider2D.h"
#include <unordered_set>
#include <algorithm>

SpatialGrid::SpatialGrid(float cellSize)
    : cellSize(cellSize) {}

void SpatialGrid::SetCellSize(float size) {
    cellSize = size > 0.0f ? size : 64.0f;
}

void SpatialGrid::Clear() {
    cells.clear();
    colliderCount = 0;
}

SpatialGrid::CellKey SpatialGrid::WorldToCell(float wx, float wy) const {
    return {
        static_cast<int>(std::floor(wx / cellSize)),
        static_cast<int>(std::floor(wy / cellSize))
    };
}

void SpatialGrid::GetOverlappingCells(const AABB& bounds,
                                       std::vector<CellKey>& outCells) const {
    int minX = static_cast<int>(std::floor(bounds.Left() / cellSize));
    int maxX = static_cast<int>(std::floor(bounds.Right() / cellSize));
    int minY = static_cast<int>(std::floor(bounds.Top() / cellSize));
    int maxY = static_cast<int>(std::floor(bounds.Bottom() / cellSize));

    outCells.clear();
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            outCells.push_back({x, y});
        }
    }
}

void SpatialGrid::Insert(Collider2D* collider) {
    if (!collider) return;

    AABB bounds = collider->GetWorldBounds();
    std::vector<CellKey> overlapping;
    GetOverlappingCells(bounds, overlapping);

    for (const auto& key : overlapping) {
        cells[key].push_back(collider);
    }
    colliderCount++;
}

std::vector<CollisionPair> SpatialGrid::GetPotentialPairs() const {
    // Use a set to deduplicate pairs
    // Key: min(ptrA, ptrB), max(ptrA, ptrB)
    struct PairKey {
        Collider2D* a;
        Collider2D* b;
        bool operator==(const PairKey& other) const {
            return a == other.a && b == other.b;
        }
    };
    struct PairHash {
        size_t operator()(const PairKey& p) const {
            auto h1 = std::hash<void*>{}(p.a);
            auto h2 = std::hash<void*>{}(p.b);
            return h1 ^ (h2 * 2654435761u);
        }
    };

    std::unordered_set<PairKey, PairHash> seen;
    std::vector<CollisionPair> pairs;

    for (const auto& [key, colliders] : cells) {
        for (size_t i = 0; i < colliders.size(); ++i) {
            for (size_t j = i + 1; j < colliders.size(); ++j) {
                Collider2D* a = colliders[i];
                Collider2D* b = colliders[j];

                // Normalize order for deduplication
                if (a > b) std::swap(a, b);

                PairKey pk{a, b};
                if (seen.insert(pk).second) {
                    pairs.push_back({a, b});
                }
            }
        }
    }

    return pairs;
}

std::vector<Collider2D*> SpatialGrid::Query(const AABB& bounds) const {
    std::vector<CellKey> overlapping;
    GetOverlappingCells(bounds, overlapping);

    // Deduplicate (a collider may be in multiple cells)
    std::unordered_set<Collider2D*> found;
    for (const auto& key : overlapping) {
        auto it = cells.find(key);
        if (it != cells.end()) {
            for (auto* c : it->second) {
                found.insert(c);
            }
        }
    }

    return std::vector<Collider2D*>(found.begin(), found.end());
}
```

### CMakeLists.txt 수정

`ENGINE_SOURCES`에 추가:
```cmake
src/Physics/SpatialGrid.cpp
```

### 사용 예시 (Phase 9 미리보기)

```cpp
// 매 FixedUpdate마다:
SpatialGrid broadPhase(64.0f);
broadPhase.Clear();

// 모든 콜라이더 삽입
for (auto& obj : gameObjects) {
    if (auto* collider = obj->GetComponent<BoxCollider2D>()) {
        broadPhase.Insert(collider);
    }
}

// 후보 쌍 추출 → Narrow Phase
auto pairs = broadPhase.GetPotentialPairs();
for (const auto& pair : pairs) {
    // Narrow phase: 정밀 충돌 검사
    AABB a = pair.a->GetWorldBounds();
    AABB b = pair.b->GetWorldBounds();
    if (Collision::CheckAABB(a, b)) {
        // 충돌 이벤트 발행
    }
}
```

### 영향 범위

- **하위 호환**: 완전 호환. 새 파일 추가만으로, 기존 `Collision` 클래스는 변경하지 않음
- **새 파일**: `SpatialGrid.h`, `SpatialGrid.cpp`
- **의존성**: P0-2 `Collider2D` 기반 클래스에 의존 (`GetWorldBounds()` 사용)

### 테스트 계획

`tests/test_spatial_grid.cpp` (새 파일):

```cpp
// 1. 비어있는 그리드에서 GetPotentialPairs → 빈 벡터
static void test_empty_grid() {
    SpatialGrid grid(64.0f);
    auto pairs = grid.GetPotentialPairs();
    assert(pairs.empty());
}

// 2. 같은 셀의 두 콜라이더 → 1쌍 반환
static void test_same_cell_pair() {
    // BoxCollider2D 2개를 가까이 배치
    // Insert → GetPotentialPairs → 1쌍
}

// 3. 멀리 떨어진 콜라이더 → 0쌍 반환
static void test_far_apart_no_pair() {
    // BoxCollider2D 2개를 여러 셀 떨어뜨려 배치
    // Insert → GetPotentialPairs → 0쌍
}

// 4. Query(AABB) → 해당 영역의 콜라이더만 반환
static void test_query_region() {
    // 3개 콜라이더 배치, 1개만 쿼리 영역에 포함
    // Query → 1개 반환
}

// 5. 중복 제거: 여러 셀에 걸친 콜라이더가 중복되지 않음
static void test_deduplication() {
    // 큰 콜라이더(여러 셀에 걸침) + 작은 콜라이더
    // GetPotentialPairs → 1쌍 (중복 없음)
}
```

---

## 실행 순서 및 의존성

```
P0-1 OnDestroy()     ─────┐
     (0.5일)               │     독립
                           │
P0-2 Collider2D 기반  ────┤     P0-4가 P0-2에 의존
     (1일)                 │
                           │
P0-3 FixedUpdate      ────┤     독립 (P0-1, P0-2와 무관)
     (1일)                 │
                           │
P0-4 Broad Phase      ────┘     P0-2 완료 후
     (2일)
```

### 권장 실행 순서

```
Day 1 (AM): P0-1 OnDestroy (0.5일) ──→ Day 1 (PM): P0-3 FixedUpdate 시작
Day 2:      P0-3 FixedUpdate 완료 ──→ P0-2 Collider2D 시작
Day 3:      P0-2 Collider2D 완료 ──→ P0-4 Broad Phase 시작
Day 4-5:    P0-4 Broad Phase 완료 ──→ 전체 검증
```

**병렬 가능 조합** (2인 이상 시):
- 트랙 A: P0-1 → P0-2 → P0-4
- 트랙 B: P0-3 (독립)

---

## 검증 계획

### Phase 완료 체크리스트

| # | 검증 항목 | 방법 | 통과 기준 |
|---|----------|------|----------|
| 1 | OnDestroy 호출 | 유닛 테스트 | destroyCalled == true, 중복 호출 시 count == 1 |
| 2 | Collider2D 상속 | 유닛 테스트 | dynamic_cast 성공, ShapeType::Box |
| 3 | 기존 씬 로드 | 수동 테스트 | BoxCollider2D 직렬화/역직렬화 정상 |
| 4 | FixedUpdate 주기 | 통합 테스트 | 1초간 ~50회 호출 (±2) |
| 5 | deltaTime 상한 | 수동 테스트 | 브레이크포인트 후 복귀 시 폭주 없음 |
| 6 | SpatialGrid 쌍 추출 | 유닛 테스트 | 가까운 쌍만 반환, 먼 쌍 미반환 |
| 7 | SpatialGrid 중복 제거 | 유닛 테스트 | 다중 셀 콜라이더 → 1쌍 |
| 8 | 빌드 성공 | CI | macOS 빌드 + CTest 전체 통과 |
| 9 | 에디터 정상 | 수동 테스트 | 에디터 실행, Play/Edit 모드 전환 정상 |
| 10 | 성능 회귀 없음 | 수동 테스트 | FPS가 P0 이전 대비 동일 또는 향상 |

### 최종 커밋 구조

```
p0/prerequisites 브랜치:
  commit 1: "feat: add OnDestroy() lifecycle to Component"
  commit 2: "refactor: extract Collider2D abstract base class"
  commit 3: "feat: add FixedUpdate loop with fixed timestep"
  commit 4: "feat: add SpatialGrid broad phase collision detection"
  commit 5: "test: add P0 verification tests"
```

---

## 수정 파일 종합

| 파일 | P0-1 | P0-2 | P0-3 | P0-4 | 변경 유형 |
|------|:----:|:----:|:----:|:----:|----------|
| `src/ECS/Component.h` | ✅ | | | | 1줄 추가 |
| `src/ECS/GameObject.h` | ✅ | | | | 3줄 추가 |
| `src/ECS/GameObject.cpp` | ✅ | | | | ~15줄 수정 |
| `src/ECS/Components/Collider2D.h` | | ✅ | | | 새 파일 |
| `src/ECS/Components/Collider2D.cpp` | | ✅ | | | 새 파일 |
| `src/ECS/Components/BoxCollider2D.h` | | ✅ | | | 상속 변경 |
| `src/ECS/Components/BoxCollider2D.cpp` | | ✅ | | | 직렬화 위임 |
| `src/Core/MolgaTime.h` | | | ✅ | | ~15줄 추가 |
| `src/Core/MolgaTime.cpp` | | | ✅ | | ~10줄 수정 |
| `src/main.cpp` | | | ✅ | | ~20줄 추가 |
| `src/runtime_main.cpp` | | | ✅ | | ~15줄 추가 |
| `src/Physics/SpatialGrid.h` | | | | ✅ | 새 파일 |
| `src/Physics/SpatialGrid.cpp` | | | | ✅ | 새 파일 |
| `CMakeLists.txt` | | ✅ | | ✅ | 2줄 추가 |
| `tests/test_ecs.cpp` | ✅ | ✅ | | | 테스트 추가 |
| `tests/test_spatial_grid.cpp` | | | | ✅ | 새 파일 |

**총 변경**: 기존 파일 9개 수정, 새 파일 4개 생성
