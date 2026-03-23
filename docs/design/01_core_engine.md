# Molga Engine - 핵심 시스템 조사 보고서

> 조사 일자: 2026-03-22
> 대상: Unity 수준의 2D 게임 엔진에 필요한 7개 핵심 시스템
> 엔진 사양: C++17, OpenGL 3.3, ECS 아키텍처

---

## 목차

1. [2D 물리 엔진](#1-2d-물리-엔진)
2. [이벤트/메시징 시스템](#2-이벤트메시징-시스템)
3. [코루틴/태스크 시스템](#3-코루틴태스크-시스템)
4. [오브젝트 풀링](#4-오브젝트-풀링)
5. [리소스/에셋 관리 시스템](#5-리소스에셋-관리-시스템)
6. [프리팹 시스템](#6-프리팹-시스템)
7. [고급 입력 시스템](#7-고급-입력-시스템)
8. [시스템 간 의존성 맵](#8-시스템-간-의존성-맵)
9. [권장 구현 순서](#9-권장-구현-순서)

---

## 1. 2D 물리 엔진

### 1.1 정의 및 필요성

2D 물리 엔진은 게임 월드 내 물체의 운동, 충돌 반응, 제약 조건을 시뮬레이션하는 시스템이다. 현재 Molga Engine의 `Collision` 클래스는 충돌 **감지(detection)**만 수행하고, 충돌 **반응(response)**과 물리 **시뮬레이션(simulation)**이 없다.

물리 엔진이 없으면 다음이 불가능하다:
- 중력에 의한 자연스러운 낙하
- 물체 간 충돌 후 튕김/밀림
- 관절로 연결된 물체 (체인, 래그돌, 흔들리는 플랫폼)
- 레이캐스트 기반 시야/라인 검사
- 물리 기반 퍼즐, 플랫포머 점프 메커닉

### 1.2 Unity의 구현 방식

Unity는 내부적으로 **Box2D**를 래핑하여 2D 물리를 제공한다 (Unity 6부터는 자체 구현으로 전환 추세).

**핵심 API:**

| 컴포넌트/클래스 | 역할 |
|---|---|
| `Rigidbody2D` | 물리 바디. BodyType (Dynamic/Kinematic/Static), mass, drag, gravityScale |
| `Collider2D` | 충돌 형상. BoxCollider2D, CircleCollider2D, PolygonCollider2D, EdgeCollider2D, CapsuleCollider2D |
| `PhysicsMaterial2D` | 마찰(friction), 반발(bounciness) 계수 |
| `Joint2D` | 제약 조건. DistanceJoint2D, HingeJoint2D, SpringJoint2D, SliderJoint2D, FixedJoint2D, WheelJoint2D |
| `Physics2D` (static) | Raycast, OverlapCircle, OverlapBox, BoxCast, CircleCast 등 쿼리 |
| `ContactFilter2D` | 레이어 마스크, 각도 필터로 물리 쿼리 필터링 |
| `CompositeCollider2D` | 여러 콜라이더를 하나로 병합 (타일맵 최적화) |

**Unity 물리 루프 구조:**
```
FixedUpdate (고정 시간 간격, 기본 0.02초)
  -> Physics2D.Simulate(deltaTime)
    -> Broadphase (AABB 트리로 후보 쌍 추출)
    -> Narrowphase (정밀 충돌 검사, 접촉점/법선 계산)
    -> Solver (제약 조건 풀기: 관통 해소, 속도 보정, 관절 유지)
    -> 콜백 발생: OnCollisionEnter2D / Stay2D / Exit2D
                   OnTriggerEnter2D / Stay2D / Exit2D
```

### 1.3 추천 C++ 라이브러리

| 라이브러리 | 장점 | 단점 | 라이선스 |
|---|---|---|---|
| **Box2D 3.x** (Erin Catto) | 업계 표준, 방대한 레퍼런스, SIMD 최적화(v3), 멀티스레드 지원(v3), C API로 바인딩 용이 | v3는 API가 v2와 완전히 다름, C 스타일 API | MIT |
| **Chipmunk2D** (Scott Lembcke) | 순수 C, 가볍고 임베딩 쉬움, Persistent Contact 지원, 공간 해싱 | Box2D보다 커뮤니티 작음, 업데이트 빈도 낮음 | MIT |
| **직접 구현** | 완전한 제어권, 학습 가치 | 개발 기간 수 개월, 안정성 검증 어려움 | - |

**추천: Box2D 3.x** -- Unity/Godot/Cocos2d 등 주요 엔진의 레퍼런스 물리 라이브러리이며, v3는 C API 기반으로 C++ 래퍼를 만들기에 적합하다.

### 1.4 Molga Engine 통합 설계

#### 아키텍처 구조

```
src/Physics/
├── PhysicsWorld.h/cpp          // Box2D b2WorldId 래핑, Simulate 루프
├── Rigidbody2D.h/cpp           // Component: Box2D 바디 래핑
├── PhysicsMaterial2D.h/cpp     // friction, restitution 데이터
├── PhysicsQuery.h/cpp          // Raycast, OverlapCircle 등 정적 쿼리
├── Collision.h/cpp             // 기존 유지 (간단한 쿼리용)
└── Joints/
    ├── Joint2D.h               // 조인트 기본 클래스
    ├── DistanceJoint2D.h/cpp
    ├── HingeJoint2D.h/cpp
    └── SpringJoint2D.h/cpp
```

#### Rigidbody2D 컴포넌트 설계

```cpp
// src/ECS/Components/Rigidbody2D.h
#pragma once
#include "ECS/Component.h"
#include "Common/Types.h"

enum class BodyType { Static, Kinematic, Dynamic };

class Rigidbody2D : public Component {
    COMPONENT_TYPE(Rigidbody2D)
public:
    // --- 프로퍼티 ---
    BodyType bodyType = BodyType::Dynamic;
    float mass = 1.0f;
    float linearDamping = 0.0f;       // 선형 감쇠 (공기 저항)
    float angularDamping = 0.05f;     // 각 감쇠
    float gravityScale = 1.0f;        // 중력 배율
    bool fixedRotation = false;       // 회전 잠금
    bool isBullet = false;            // CCD(연속 충돌 감지) 활성화

    // --- 힘/속도 API ---
    void AddForce(Vector2 force);                    // 힘 적용 (뉴턴)
    void AddForceAtPoint(Vector2 force, Vector2 point);
    void AddTorque(float torque);                    // 토크 적용
    void SetLinearVelocity(Vector2 velocity);
    Vector2 GetLinearVelocity() const;
    void SetAngularVelocity(float omega);
    float GetAngularVelocity() const;

    // --- 생명주기 ---
    void OnAttach() override;   // Box2D 바디 생성
    void OnDetach() override;   // Box2D 바디 파괴
    void SyncFromPhysics();     // 물리 -> Transform 동기화
    void SyncToPhysics();       // Transform -> 물리 동기화

    // --- 직렬화 ---
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void OnInspectorGUI() override;

private:
    b2BodyId bodyId = b2_nullBodyId;  // Box2D 3.x 핸들
};
```

#### PhysicsWorld 설계

```cpp
// src/Physics/PhysicsWorld.h
#pragma once
#include "Common/Types.h"
#include <box2d/box2d.h>
#include <vector>
#include <functional>

struct RaycastHit2D {
    Vector2 point;
    Vector2 normal;
    float distance;
    GameObject* gameObject;
};

class PhysicsWorld {
public:
    static void Init(Vector2 gravity = {0.0f, 9.81f});
    static void Shutdown();

    // 고정 시간 간격으로 호출 (FixedUpdate에서)
    static void Step(float fixedDeltaTime);

    // 물리 쿼리
    static bool Raycast(Vector2 origin, Vector2 direction, float distance,
                        RaycastHit2D& hit, uint16_t layerMask = 0xFFFF);
    static std::vector<RaycastHit2D> RaycastAll(Vector2 origin, Vector2 direction,
                                                 float distance, uint16_t layerMask = 0xFFFF);
    static bool OverlapCircle(Vector2 center, float radius, uint16_t layerMask = 0xFFFF);
    static bool OverlapBox(Vector2 center, Vector2 size, float angle = 0.0f,
                           uint16_t layerMask = 0xFFFF);

    // 설정
    static void SetGravity(Vector2 gravity);
    static Vector2 GetGravity();
    static void SetVelocityIterations(int iterations);   // 기본 8
    static void SetPositionIterations(int iterations);    // 기본 3

    static b2WorldId GetWorldId();

private:
    static b2WorldId worldId;
    static Vector2 gravity;
    static int velocityIterations;
    static int positionIterations;
};
```

#### 통합 시 주의사항

1. **좌표계 변환**: Box2D는 미터 단위, 게임은 픽셀 단위. PTM(Pixel-To-Meter) 비율 상수 필요 (보통 32~100 px/m).
2. **고정 타임스텝**: 물리는 `FixedUpdate`에서 고정 간격(0.02초)으로, 렌더링은 가변 간격으로. 보간(interpolation) 필요.
3. **Transform 동기화**: `Step()` 후 모든 Rigidbody2D의 위치/회전을 Transform에 반영.
4. **기존 Collision 공존**: 간단한 트리거 검사는 기존 `Collision` 클래스 유지, 물리 반응이 필요한 경우 Box2D 사용.
5. **콜라이더 확장**: 기존 `BoxCollider2D`에 Box2D shape 연동 추가, `CircleCollider2D`, `PolygonCollider2D` 신규 생성.

#### CMake 통합

```cmake
# Box2D 3.x 추가
add_subdirectory(external/box2d)
target_link_libraries(molga_core PUBLIC box2d)
```

### 1.5 복잡도 및 의존성

| 항목 | 값 |
|---|---|
| **예상 복잡도** | **Very Large** (4-6주) |
| **신규 파일** | 8-12개 |
| **수정 파일** | BoxCollider2D, Transform, Scene, Bootstrap, CMakeLists |
| **의존성** | Transform, BoxCollider2D, 이벤트 시스템 (충돌 콜백), Time (FixedUpdate) |
| **외부 의존성** | Box2D 3.x (MIT, CMake 서브모듈) |

---

## 2. 이벤트/메시징 시스템

### 2.1 정의 및 필요성

이벤트 시스템은 게임 오브젝트와 시스템 간의 **느슨한 결합(loose coupling)**을 실현하는 통신 메커니즘이다. 발행자(publisher)와 구독자(subscriber)가 서로를 직접 참조하지 않고 메시지를 교환한다.

이벤트 시스템이 없으면:
- 물리 충돌 발생 시 스크립트에 알릴 방법이 없음 (현재 Script의 `OnCollisionEnter`가 호출되지 않음)
- 씬 전환, 게임 상태 변경을 각 시스템에 직접 통보해야 함 (강결합)
- UI 버튼 클릭 이벤트를 게임 로직에 전달할 수 없음
- 오디오가 게임 이벤트에 반응할 수 없음 (적 사망 -> 효과음 재생)

### 2.2 Unity의 구현 방식

Unity는 여러 계층의 이벤트 메커니즘을 제공한다:

| 메커니즘 | 용도 | 특징 |
|---|---|---|
| `C# event/delegate` | 프로그래머 레벨 타입 안전 이벤트 | 컴파일 타임 타입 체크, GC 영향 |
| `UnityEvent<T>` | 에디터 직렬화 가능 이벤트 | Inspector에서 드래그앤드롭으로 연결 |
| `SendMessage("method")` | 문자열 기반 메서드 호출 | 느림, 타입 안전하지 않음, 리플렉션 |
| `UnityAction<T>` | 경량 콜백 | UnityEvent 내부 사용 |
| `EventSystem` (UI) | UI 입력 이벤트 라우팅 | Raycaster + EventData |
| 물리 콜백 | OnCollisionEnter, OnTriggerEnter | 엔진 내부에서 자동 호출 |

### 2.3 C++17 이벤트 시스템 설계 패턴 비교

#### 패턴 A: Observer 패턴 (전통적)

```cpp
class IObserver {
public:
    virtual ~IObserver() = default;
    virtual void OnNotify(const Event& event) = 0;
};
class Subject {
    std::vector<IObserver*> observers;
public:
    void AddObserver(IObserver* obs);
    void Notify(const Event& event);
};
```
- 장점: 단순, 이해하기 쉬움
- 단점: 가상 함수 오버헤드, 다운캐스팅 필요, 수명 관리 어려움

#### 패턴 B: Signal/Slot (Qt 스타일)

```cpp
Signal<int, float> onDamage;
onDamage.Connect([](int dmg, float knockback) { ... });
onDamage.Emit(10, 1.5f);
```
- 장점: 타입 안전, 유연한 시그니처
- 단점: 구현 복잡, 연결/해제 관리 필요

#### 패턴 C: 타입 기반 이벤트 버스 (추천)

```cpp
EventBus::Subscribe<CollisionEvent>([](const CollisionEvent& e) { ... });
EventBus::Publish(CollisionEvent{obj1, obj2, contactPoint});
```
- 장점: 완전한 타입 안전, 전역 디커플링, 이벤트 구조체로 데이터 전달
- 단점: 전역 상태, 이벤트 순서 보장 필요

### 2.4 Molga Engine 구현 설계 (패턴 C 채택)

#### 아키텍처 구조

```
src/Core/
├── Event.h                  // EventBase, 이벤트 타입 ID
├── EventBus.h/cpp           // 중앙 이벤트 디스패처
└── Events/
    ├── PhysicsEvents.h      // CollisionEvent, TriggerEvent
    ├── SceneEvents.h        // SceneLoadEvent, SceneUnloadEvent
    ├── InputEvents.h        // (고급 입력 시스템과 연동)
    └── GameEvents.h         // 사용자 정의 게임 이벤트
```

#### 핵심 구현

```cpp
// src/Core/Event.h
#pragma once
#include <cstddef>
#include <functional>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <memory>

// 이벤트 타입별 고유 ID 생성 (ComponentTypeID와 동일한 패턴)
class EventTypeID {
    static inline size_t nextID = 0;
public:
    template<typename T>
    static size_t Get() {
        static size_t id = nextID++;
        return id;
    }
};

// 구독 핸들 (구독 해제에 사용)
using SubscriptionID = uint64_t;

// 핸들러 래퍼 (타입 소거)
class HandlerBase {
public:
    virtual ~HandlerBase() = default;
    SubscriptionID id;
};

template<typename EventT>
class Handler : public HandlerBase {
public:
    using Callback = std::function<void(const EventT&)>;
    Callback callback;

    explicit Handler(Callback cb, SubscriptionID subId)
        : callback(std::move(cb)) { id = subId; }
};

// src/Core/EventBus.h
#pragma once
#include "Event.h"

class EventBus {
public:
    // 이벤트 구독 -- 핸들 반환 (해제용)
    template<typename EventT>
    static SubscriptionID Subscribe(std::function<void(const EventT&)> callback) {
        auto subId = nextSubscriptionID++;
        auto handler = std::make_unique<Handler<EventT>>(std::move(callback), subId);
        auto typeId = EventTypeID::Get<EventT>();
        handlers[typeId].push_back(std::move(handler));
        return subId;
    }

    // 구독 해제
    template<typename EventT>
    static void Unsubscribe(SubscriptionID subId) {
        auto typeId = EventTypeID::Get<EventT>();
        auto it = handlers.find(typeId);
        if (it != handlers.end()) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [subId](const auto& h) { return h->id == subId; }), vec.end());
        }
    }

    // 이벤트 발행 (즉시 디스패치)
    template<typename EventT>
    static void Publish(const EventT& event) {
        auto typeId = EventTypeID::Get<EventT>();
        auto it = handlers.find(typeId);
        if (it != handlers.end()) {
            for (auto& handlerBase : it->second) {
                auto* handler = static_cast<Handler<EventT>*>(handlerBase.get());
                handler->callback(event);
            }
        }
    }

    // 지연 이벤트 (큐에 넣고 ProcessQueue에서 일괄 처리)
    template<typename EventT>
    static void PublishDeferred(EventT event) {
        deferredQueue.push_back([event = std::move(event)]() {
            Publish(event);
        });
    }

    // 큐에 쌓인 지연 이벤트 처리 (프레임 시작/끝에 호출)
    static void ProcessQueue() {
        auto queue = std::move(deferredQueue);
        deferredQueue.clear();
        for (auto& fn : queue) {
            fn();
        }
    }

    // 전체 초기화
    static void Clear() {
        handlers.clear();
        deferredQueue.clear();
    }

private:
    static inline std::unordered_map<size_t,
        std::vector<std::unique_ptr<HandlerBase>>> handlers;
    static inline std::vector<std::function<void()>> deferredQueue;
    static inline SubscriptionID nextSubscriptionID = 1;
};
```

#### 물리 이벤트 예시

```cpp
// src/Core/Events/PhysicsEvents.h
#pragma once
#include "Common/Types.h"

class GameObject;

struct CollisionEvent {
    GameObject* objectA = nullptr;
    GameObject* objectB = nullptr;
    Vector2 contactPoint;
    Vector2 normal;
    float penetration = 0.0f;
};

struct TriggerEvent {
    GameObject* trigger = nullptr;
    GameObject* other = nullptr;
};

// 사용 예:
// 물리 시스템 내부:
//   EventBus::Publish(CollisionEvent{objA, objB, contact, normal, depth});
//
// 스크립트에서:
//   EventBus::Subscribe<CollisionEvent>([this](const CollisionEvent& e) {
//       if (e.objectA == gameObject || e.objectB == gameObject)
//           OnCollisionEnter(e);
//   });
```

### 2.5 추천 오픈소스 라이브러리

| 라이브러리 | 설명 | 장단점 |
|---|---|---|
| **eventpp** (wqking) | C++11 이벤트 라이브러리, EventDispatcher/EventQueue/CallbackList | 성숙, 헤더 온리, 스레드 안전 옵션. 다소 과한 기능. |
| **entt::dispatcher** (skypjack) | EnTT ECS의 이벤트 디스패처 | 경량, ECS와 궁합 좋음. 단 EnTT 전체 도입 부담. |
| **직접 구현 (추천)** | 위 설계 기반 | 200줄 미만, 엔진 ECS 패턴과 일관성 유지, 외부 의존성 없음 |

**추천: 직접 구현** -- 이미 `ComponentTypeID` 패턴이 있으므로 동일한 패턴으로 `EventTypeID`를 만들면 된다. 코드량이 적고 엔진 전체에 걸쳐 가장 근본적인 인프라이므로 외부 의존성 없이 유지하는 것이 좋다.

### 2.6 복잡도 및 의존성

| 항목 | 값 |
|---|---|
| **예상 복잡도** | **Small** (2-3일) |
| **신규 파일** | 3-5개 |
| **수정 파일** | Script (콜백 연동), Bootstrap (초기화/정리) |
| **의존성** | 없음 (가장 기초적인 인프라) |
| **외부 의존성** | 없음 |

---

## 3. 코루틴/태스크 시스템

### 3.1 정의 및 필요성

코루틴은 실행을 **일시 중단(yield)** 했다가 다음 프레임 또는 특정 조건 충족 시 **재개(resume)**할 수 있는 함수이다. 게임 프로그래밍에서 다음과 같은 시간 기반 로직에 필수적이다:

- **지연 실행**: 3초 후 폭발 이펙트 재생
- **시퀀스 연출**: 카메라 흔들림 -> 대기 -> 페이드 아웃 -> 씬 전환
- **스폰 패턴**: 2초마다 적 생성, 웨이브 간 5초 대기
- **애니메이션 시퀀스**: 문 열림 -> 대기 -> 캐릭터 진입 -> 대화
- **비동기 리소스 로딩**: 에셋 로딩 중 로딩 화면 표시

코루틴 없이는 이러한 로직을 상태 머신이나 타이머 플래그로 구현해야 하며, 코드가 급격히 복잡해진다.

### 3.2 Unity의 구현 방식

Unity는 C#의 `IEnumerator`를 활용한 코루틴 시스템을 제공한다:

```csharp
// Unity 코루틴 예시
IEnumerator SpawnWave() {
    for (int i = 0; i < 5; i++) {
        Instantiate(enemyPrefab, spawnPoint, Quaternion.identity);
        yield return new WaitForSeconds(2.0f);  // 2초 대기
    }
    yield return new WaitForSeconds(5.0f);       // 웨이브 간 5초
    StartCoroutine(SpawnWave());                  // 다음 웨이브
}

// 시작/중지
Coroutine handle = StartCoroutine(SpawnWave());
StopCoroutine(handle);
```

**Unity의 YieldInstruction 종류:**

| 클래스 | 동작 |
|---|---|
| `yield return null` | 다음 프레임까지 대기 |
| `WaitForSeconds(t)` | t초 대기 (Time.timeScale 영향) |
| `WaitForSecondsRealtime(t)` | t초 대기 (timeScale 무시) |
| `WaitForEndOfFrame` | 프레임 렌더링 완료 후 재개 |
| `WaitForFixedUpdate` | 다음 FixedUpdate에서 재개 |
| `WaitUntil(predicate)` | 조건이 true일 때 재개 |
| `WaitWhile(predicate)` | 조건이 false일 때 재개 |

### 3.3 C++17 구현 접근법

C++20의 `co_await`/`co_yield`가 이상적이지만, Molga Engine은 C++17 타겟이므로 **이터레이터 기반 커스텀 코루틴**을 구현한다.

#### 핵심 아이디어

- 코루틴 함수는 `Coroutine` 객체를 반환
- `Coroutine`은 내부적으로 `YieldInstruction`의 시퀀스를 보유
- 매 프레임 `CoroutineManager`가 활성 코루틴들의 현재 yield를 평가
- yield 조건 충족 시 다음 단계로 진행

#### 아키텍처 구조

```
src/Core/
├── Coroutine.h/cpp           // Coroutine 클래스, YieldInstruction
└── CoroutineManager.h/cpp    // 전역 코루틴 스케줄러
```

#### 구현 설계

```cpp
// src/Core/Coroutine.h
#pragma once
#include <functional>
#include <memory>
#include <vector>

// ============================================================================
// YieldInstruction - 코루틴 대기 조건 기본 클래스
// ============================================================================
class YieldInstruction {
public:
    virtual ~YieldInstruction() = default;
    // true를 반환하면 대기 완료, 코루틴 재개
    virtual bool IsCompleted(float deltaTime) = 0;
};

// 다음 프레임까지 대기
class WaitForNextFrame : public YieldInstruction {
public:
    bool IsCompleted(float deltaTime) override { return true; }
};

// N초 대기
class WaitForSeconds : public YieldInstruction {
    float remaining;
public:
    explicit WaitForSeconds(float seconds) : remaining(seconds) {}
    bool IsCompleted(float deltaTime) override {
        remaining -= deltaTime;
        return remaining <= 0.0f;
    }
};

// 실시간 N초 대기 (timeScale 무시)
class WaitForSecondsRealtime : public YieldInstruction {
    float remaining;
public:
    explicit WaitForSecondsRealtime(float seconds) : remaining(seconds) {}
    bool IsCompleted(float deltaTime) override {
        // 실제 deltaTime 사용 (timeScale 미적용)
        remaining -= deltaTime; // CoroutineManager에서 unscaled dt 전달
        return remaining <= 0.0f;
    }
};

// 조건이 true가 될 때까지 대기
class WaitUntil : public YieldInstruction {
    std::function<bool()> predicate;
public:
    explicit WaitUntil(std::function<bool()> pred) : predicate(std::move(pred)) {}
    bool IsCompleted(float deltaTime) override { return predicate(); }
};

// 조건이 false가 될 때까지 대기
class WaitWhile : public YieldInstruction {
    std::function<bool()> predicate;
public:
    explicit WaitWhile(std::function<bool()> pred) : predicate(std::move(pred)) {}
    bool IsCompleted(float deltaTime) override { return !predicate(); }
};

// ============================================================================
// Coroutine - 단계별 실행 함수
// ============================================================================
using CoroutineID = uint32_t;

// 코루틴 빌더 -- 람다 체이닝으로 단계 정의
class CoroutineBuilder {
public:
    // 액션 실행 후 다음 프레임 대기
    CoroutineBuilder& Then(std::function<void()> action) {
        steps.push_back({std::move(action), std::make_unique<WaitForNextFrame>()});
        return *this;
    }

    // 액션 실행 후 N초 대기
    CoroutineBuilder& ThenWait(float seconds, std::function<void()> action = nullptr) {
        steps.push_back({std::move(action), std::make_unique<WaitForSeconds>(seconds)});
        return *this;
    }

    // 조건 충족 때까지 대기
    CoroutineBuilder& ThenWaitUntil(std::function<bool()> predicate,
                                     std::function<void()> action = nullptr) {
        steps.push_back({std::move(action), std::make_unique<WaitUntil>(std::move(predicate))});
        return *this;
    }

    // 커스텀 YieldInstruction
    CoroutineBuilder& ThenYield(std::unique_ptr<YieldInstruction> yield,
                                 std::function<void()> action = nullptr) {
        steps.push_back({std::move(action), std::move(yield)});
        return *this;
    }

    struct Step {
        std::function<void()> action;                // 이 단계에서 실행할 코드 (nullable)
        std::unique_ptr<YieldInstruction> yield;      // 다음 단계로 넘어가기 위한 조건
    };
    std::vector<Step> steps;
};

// ============================================================================
// 사용 예시
// ============================================================================
// auto routine = CoroutineBuilder()
//     .Then([this]() { SpawnEnemy(); })
//     .ThenWait(2.0f)
//     .Then([this]() { SpawnEnemy(); })
//     .ThenWait(2.0f)
//     .Then([this]() { SpawnEnemy(); })
//     .ThenWait(5.0f, [this]() { ShowWaveComplete(); });
//
// CoroutineID id = CoroutineManager::Start(std::move(routine));
// CoroutineManager::Stop(id);
```

```cpp
// src/Core/CoroutineManager.h
#pragma once
#include "Coroutine.h"
#include <unordered_map>

class CoroutineManager {
public:
    // 코루틴 시작, 핸들 반환
    static CoroutineID Start(CoroutineBuilder builder);

    // 코루틴 중지
    static void Stop(CoroutineID id);

    // 모든 코루틴 중지
    static void StopAll();

    // 매 프레임 호출 -- 활성 코루틴 진행
    static void Update(float deltaTime);

    // 활성 코루틴 수
    static size_t ActiveCount();

private:
    struct RunningCoroutine {
        CoroutineBuilder builder;
        size_t currentStep = 0;
        bool actionExecuted = false;  // 현재 단계의 액션 실행 여부
    };

    static inline std::unordered_map<CoroutineID, RunningCoroutine> coroutines;
    static inline CoroutineID nextID = 1;
    static inline std::vector<CoroutineID> toRemove;  // 완료된 코루틴
};
```

### 3.4 대안: C++20 코루틴 (미래 마이그레이션용 참고)

C++20으로 전환 시 `co_yield` 기반 구현이 가능하다:

```cpp
// C++20 참고용 (현재 사용 불가)
Task SpawnWave() {
    for (int i = 0; i < 5; i++) {
        SpawnEnemy();
        co_yield WaitForSeconds(2.0f);
    }
    co_yield WaitForSeconds(5.0f);
}
```

C++17 빌더 패턴은 C++20 마이그레이션 시에도 `YieldInstruction` 계층을 그대로 재사용할 수 있으므로 투자 가치가 있다.

### 3.5 복잡도 및 의존성

| 항목 | 값 |
|---|---|
| **예상 복잡도** | **Medium** (3-5일) |
| **신규 파일** | 2-4개 |
| **수정 파일** | Bootstrap (초기화), Scene/GameLoop (Update 루프 통합) |
| **의존성** | Time (deltaTime) |
| **외부 의존성** | 없음 |

---

## 4. 오브젝트 풀링

### 4.1 정의 및 필요성

오브젝트 풀링은 자주 생성/파괴되는 객체를 미리 할당해두고 **재활용**하는 메모리 관리 패턴이다. `new`/`delete`(또는 `std::make_unique`) 호출을 최소화하여 다음을 방지한다:

- **힙 할당 지연**: 매 프레임 수백 개의 총알/파티클 생성 시 할당 비용
- **메모리 단편화**: 반복적 할당/해제로 인한 메모리 조각화
- **캐시 미스**: 산발적 메모리 배치로 인한 CPU 캐시 효율 저하
- **GC 스파이크**: (C++에는 GC가 없지만) 소멸자 체인 호출 비용

**풀링이 필요한 대표적 객체:**
- 총알/발사체 (초당 수십~수백 개)
- 파티클 (현재 `Particle` 시스템에서 사용 가능)
- 적 유닛 (웨이브 스폰)
- 이펙트 (폭발, 히트 이펙트)
- UI 요소 (인벤토리 슬롯, 채팅 메시지)

### 4.2 Unity의 구현 방식

Unity 2021+에서 `UnityEngine.Pool` 네임스페이스를 도입했다:

```csharp
// Unity ObjectPool 사용 예시
private ObjectPool<Bullet> bulletPool;

void Start() {
    bulletPool = new ObjectPool<Bullet>(
        createFunc:      () => Instantiate(bulletPrefab),     // 생성
        actionOnGet:     (b) => b.gameObject.SetActive(true), // 풀에서 꺼낼 때
        actionOnRelease: (b) => b.gameObject.SetActive(false),// 풀에 반환할 때
        actionOnDestroy: (b) => Destroy(b.gameObject),        // 풀 정리 시
        collectionCheck: true,                                 // 중복 반환 방지
        defaultCapacity: 20,                                   // 초기 용량
        maxSize:         100                                   // 최대 크기
    );
}

void Fire() {
    Bullet bullet = bulletPool.Get();
    bullet.transform.position = firePoint.position;
}

void OnBulletHit(Bullet bullet) {
    bulletPool.Release(bullet);
}
```

**Unity Pool API:**

| 클래스 | 용도 |
|---|---|
| `ObjectPool<T>` | 범용 오브젝트 풀 (리스트 기반) |
| `LinkedPool<T>` | 링크드 리스트 기반 풀 (할당 0) |
| `ListPool<T>`, `DictionaryPool<T>` | 컬렉션 풀링 |
| `HashSetPool<T>` | HashSet 풀링 |

### 4.3 Molga Engine 구현 설계

#### 템플릿 기반 범용 풀

```cpp
// src/Core/ObjectPool.h
#pragma once
#include <vector>
#include <functional>
#include <cassert>
#include <memory>
#include <stack>

template<typename T>
class ObjectPool {
public:
    using CreateFunc  = std::function<T*()>;
    using OnGetFunc   = std::function<void(T*)>;
    using OnReturnFunc = std::function<void(T*)>;
    using OnDestroyFunc = std::function<void(T*)>;

    ObjectPool(CreateFunc create,
               OnGetFunc onGet = nullptr,
               OnReturnFunc onReturn = nullptr,
               OnDestroyFunc onDestroy = nullptr,
               size_t initialCapacity = 16,
               size_t maxSize = 256)
        : createFunc(std::move(create))
        , onGetFunc(std::move(onGet))
        , onReturnFunc(std::move(onReturn))
        , onDestroyFunc(std::move(onDestroy))
        , maxSize(maxSize)
    {
        // 초기 할당
        for (size_t i = 0; i < initialCapacity; ++i) {
            T* obj = createFunc();
            available.push(obj);
            allObjects.push_back(std::unique_ptr<T>(obj));
        }
    }

    ~ObjectPool() {
        // allObjects의 unique_ptr이 자동 정리
        // onDestroy 콜백은 명시적 Clear에서만 호출
    }

    // 풀에서 객체 획득
    T* Get() {
        T* obj = nullptr;
        if (!available.empty()) {
            obj = available.top();
            available.pop();
        } else {
            // 풀 소진 시 새로 생성 (maxSize 이내)
            if (allObjects.size() < maxSize) {
                obj = createFunc();
                allObjects.push_back(std::unique_ptr<T>(obj));
            } else {
                return nullptr;  // 풀 가득 참
            }
        }
        if (onGetFunc) onGetFunc(obj);
        ++activeCount;
        return obj;
    }

    // 풀에 객체 반환
    void Return(T* obj) {
        assert(obj != nullptr);
        if (onReturnFunc) onReturnFunc(obj);
        available.push(obj);
        --activeCount;
    }

    // 통계
    size_t GetActiveCount() const { return activeCount; }
    size_t GetAvailableCount() const { return available.size(); }
    size_t GetTotalCount() const { return allObjects.size(); }

    // 전체 정리
    void Clear() {
        if (onDestroyFunc) {
            for (auto& obj : allObjects) {
                onDestroyFunc(obj.get());
            }
        }
        while (!available.empty()) available.pop();
        allObjects.clear();
        activeCount = 0;
    }

private:
    CreateFunc createFunc;
    OnGetFunc onGetFunc;
    OnReturnFunc onReturnFunc;
    OnDestroyFunc onDestroyFunc;

    std::stack<T*> available;                       // 사용 가능한 객체
    std::vector<std::unique_ptr<T>> allObjects;     // 전체 객체 (소유권)
    size_t maxSize;
    size_t activeCount = 0;
};
```

#### GameObject 풀 특화 (프리팹 시스템과 연동)

```cpp
// src/Core/GameObjectPool.h
#pragma once
#include "ObjectPool.h"
#include "ECS/GameObject.h"

class GameObjectPool {
public:
    // 프리팹 이름(또는 ID)별 풀 관리
    static void CreatePool(const std::string& prefabName, size_t initialSize, size_t maxSize);
    static GameObject* Spawn(const std::string& prefabName, Vector2 position);
    static void Despawn(const std::string& prefabName, GameObject* obj);
    static void ClearPool(const std::string& prefabName);
    static void ClearAll();

private:
    static inline std::unordered_map<std::string,
        std::unique_ptr<ObjectPool<GameObject>>> pools;
};
```

#### 파티클 시스템 통합 예시

현재 `Particle` 시스템에 풀링을 적용하면:

```cpp
// 기존: 매 프레임 new/delete
particles.push_back(Particle{...});  // 할당
particles.erase(it);                  // 해제

// 개선: 풀 기반
Particle* p = particlePool.Get();
p->Reset(config);                     // 재활용
// ...수명 만료 시
particlePool.Return(p);               // 반환
```

### 4.4 추천 오픈소스 라이브러리

| 라이브러리 | 설명 |
|---|---|
| **boost::pool** | 범용 메모리 풀, 오브젝트 풀 아님 |
| **직접 구현 (추천)** | 위 설계 기반, 엔진 특화 최적화 가능 |

**추천: 직접 구현** -- 오브젝트 풀은 100줄 미만의 코드로 구현 가능하며, 엔진의 GameObject/Component 수명 관리와 밀접하게 연동되어야 하므로 직접 구현이 효율적이다.

### 4.5 복잡도 및 의존성

| 항목 | 값 |
|---|---|
| **예상 복잡도** | **Small** (1-2일) |
| **신규 파일** | 2-3개 |
| **수정 파일** | Particle (기존 파티클 시스템 풀 적용 가능) |
| **의존성** | 없음 (독립적). 프리팹 시스템과 함께 사용 시 시너지 |
| **외부 의존성** | 없음 |

---

## 5. 리소스/에셋 관리 시스템

### 5.1 정의 및 필요성

에셋 관리 시스템은 게임에서 사용하는 모든 리소스(텍스처, 오디오, 씬, 셰이더, 폰트, 프리팹 등)의 **로딩, 캐싱, 언로딩, 참조 추적**을 통합 관리하는 시스템이다.

현재 Molga Engine의 문제점:
- `TextureManager`는 텍스처만 캐싱, 다른 에셋 타입은 관리 안 됨
- `Audio`는 자체적으로 사운드를 관리하지만 통합 시스템이 아님
- 에셋 참조가 **파일 경로 문자열** 기반 → 파일 이동/이름 변경 시 모든 참조 깨짐
- 비동기 로딩 없음 → 대용량 에셋 로딩 시 프레임 끊김
- 에셋 수명 관리 없음 → 씬 전환 시 메모리 누수 가능

### 5.2 Unity의 구현 방식

Unity는 여러 세대의 에셋 시스템을 거쳤다:

| 시스템 | 세대 | 특징 |
|---|---|---|
| `Resources.Load()` | 레거시 | Resources 폴더에서 경로 기반 동기 로딩. 빌드 크기 증가 |
| `AssetDatabase` | 에디터 전용 | GUID 기반 에셋 추적, 메타 파일, Import 설정 |
| `AssetBundle` | 2세대 | 에셋 묶음, 다운로드/캐싱, 의존성 관리 |
| `Addressables` | 3세대 (현재 표준) | GUID+주소 기반, 비동기 로딩, 참조 카운팅, 원격/로컬 통합 |

**Unity의 GUID 시스템:**
- 모든 에셋에 `.meta` 파일 자동 생성
- `.meta` 파일에 128비트 GUID 포함
- 씬/프리팹은 파일 경로가 아닌 GUID로 에셋 참조
- 파일 이동/이름 변경 시에도 참조 유지

**핵심 API:**
```csharp
// 동기 로딩
Texture2D tex = Resources.Load<Texture2D>("Sprites/Player");

// 비동기 로딩 (Addressables)
var handle = Addressables.LoadAssetAsync<Texture2D>("player_sprite");
handle.Completed += (op) => { mySprite.texture = op.Result; };

// 참조 카운팅 해제
Addressables.Release(handle);

// 에디터 전용
string guid = AssetDatabase.AssetPathToGUID("Assets/Sprites/Player.png");
string path = AssetDatabase.GUIDToAssetPath(guid);
```

### 5.3 Molga Engine 구현 설계

#### 아키텍처 구조

```
src/Core/
├── Asset.h                    // AssetHandle, AssetType enum, AssetMetadata
├── AssetManager.h/cpp         // 중앙 에셋 레지스트리 (GUID->Asset 매핑)
├── AssetImporter.h/cpp        // .meta 파일 생성/관리, GUID 부여
├── AssetLoader.h/cpp          // 타입별 로더 (텍스처, 오디오, 씬 등)
└── AssetRef.h                 // GUID 기반 에셋 참조 (직렬화 가능)
```

#### GUID 생성 및 메타 파일

```cpp
// src/Core/Asset.h
#pragma once
#include <string>
#include <cstdint>
#include <random>
#include <sstream>
#include <iomanip>

// 128비트 GUID를 문자열로 표현 (32자 hex)
// 예: "a1b2c3d4e5f678901234567890abcdef"
using AssetGUID = std::string;

enum class AssetType {
    Unknown,
    Texture,
    Audio,
    Scene,
    Prefab,
    Shader,
    Font,
    Script,
    SpriteSheet,
    Animation,
    TileMap
};

struct AssetMetadata {
    AssetGUID guid;
    AssetType type = AssetType::Unknown;
    std::string path;          // 프로젝트 상대 경로
    uint64_t lastModified = 0; // 마지막 수정 시각
    // 타입별 임포트 설정 (향후 확장)
    // nlohmann::json importSettings;
};

// GUID 생성기
class GUIDGenerator {
public:
    static AssetGUID Generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dist;

        uint64_t high = dist(gen);
        uint64_t low = dist(gen);

        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(16) << high
           << std::setw(16) << low;
        return ss.str();
    }
};
```

#### AssetManager 설계

```cpp
// src/Core/AssetManager.h
#pragma once
#include "Asset.h"
#include <unordered_map>
#include <memory>
#include <functional>
#include <any>

class AssetManager {
public:
    static void Init(const std::string& projectRoot);
    static void Shutdown();

    // --- 에셋 등록/탐색 ---
    // 프로젝트 폴더 스캔, .meta 파일 생성/로딩
    static void ScanAssets();

    // GUID로 메타데이터 조회
    static const AssetMetadata* GetMetadata(const AssetGUID& guid);

    // 경로로 GUID 조회
    static AssetGUID PathToGUID(const std::string& relativePath);

    // GUID로 경로 조회
    static std::string GUIDToPath(const AssetGUID& guid);

    // --- 에셋 로딩 ---
    // 동기 로딩 (타입별 캐싱)
    template<typename T>
    static T* Load(const AssetGUID& guid);

    // 비동기 로딩 (콜백 기반)
    template<typename T>
    static void LoadAsync(const AssetGUID& guid,
                          std::function<void(T*)> onComplete);

    // --- 에셋 언로딩 ---
    // 참조 카운트 감소, 0이면 실제 언로딩
    static void Release(const AssetGUID& guid);

    // 미사용 에셋 일괄 언로딩
    static void UnloadUnused();

    // 씬 전환 시 현재 씬 에셋 전부 해제
    static void UnloadScene(const std::string& sceneName);

    // --- 파일 변경 감지 ---
    // 파일 이동/이름 변경 시 GUID 유지
    static void OnFileRenamed(const std::string& oldPath, const std::string& newPath);
    static void OnFileDeleted(const std::string& path);
    static void OnFileCreated(const std::string& path);

private:
    struct LoadedAsset {
        std::any data;           // 실제 에셋 데이터 (Texture*, AudioClip* 등)
        int refCount = 0;
        AssetType type;
    };

    static inline std::unordered_map<AssetGUID, AssetMetadata> registry;
    static inline std::unordered_map<AssetGUID, LoadedAsset> loadedAssets;
    static inline std::unordered_map<std::string, AssetGUID> pathToGuid;
    static inline std::string projectRoot;
};
```

#### AssetRef -- 직렬화 가능한 에셋 참조

```cpp
// src/Core/AssetRef.h
#pragma once
#include "Asset.h"
#include "AssetManager.h"

// 직렬화 시 GUID만 저장, 런타임에 실제 에셋 포인터로 해석
template<typename T>
class AssetRef {
public:
    AssetRef() = default;
    explicit AssetRef(const AssetGUID& guid) : guid(guid) {}

    // 에셋 획득 (캐시된 포인터 반환, 없으면 로딩)
    T* Get() {
        if (!cached) {
            cached = AssetManager::Load<T>(guid);
        }
        return cached;
    }

    const AssetGUID& GetGUID() const { return guid; }
    void SetGUID(const AssetGUID& newGuid) {
        guid = newGuid;
        cached = nullptr;  // 캐시 무효화
    }

    bool IsValid() const { return !guid.empty(); }
    explicit operator bool() const { return IsValid(); }

    // JSON 직렬화
    void Serialize(nlohmann::json& j) const {
        j["guid"] = guid;
    }
    void Deserialize(const nlohmann::json& j) {
        guid = j.value("guid", "");
        cached = nullptr;
    }

private:
    AssetGUID guid;
    T* cached = nullptr;  // 런타임 캐시 (직렬화 안 됨)
};
```

#### 기존 시스템 마이그레이션 계획

```
현재 (경로 기반)                      목표 (GUID 기반)
─────────────────                    ──────────────────
SpriteRenderer::texturePath          SpriteRenderer::textureRef (AssetRef<Texture>)
Audio::LoadSound(name, filepath)     Audio::LoadSound(name, AssetGUID)
TextureManager::Load(filepath)       AssetManager::Load<Texture>(guid)
```

기존 `TextureManager`는 `AssetManager` 내부의 텍스처 로더로 흡수된다.

### 5.4 추천 오픈소스 라이브러리

| 라이브러리 | 용도 |
|---|---|
| **crossguid** | 크로스플랫폼 GUID 생성 (OS 네이티브 UUID 활용) |
| **efsw** (James Wynn) | 크로스플랫폼 파일 감시 (파일 변경 감지용) |
| **직접 구현 (추천)** | GUID 생성은 간단, 파일 감시는 에디터 전용 |

### 5.5 복잡도 및 의존성

| 항목 | 값 |
|---|---|
| **예상 복잡도** | **Large** (2-3주) |
| **신규 파일** | 5-8개 |
| **수정 파일** | SpriteRenderer, Audio, TextureManager(흡수), SceneSerializer, InspectorWindow |
| **의존성** | 없음 (기초 인프라이지만 마이그레이션 범위가 넓음) |
| **외부 의존성** | 없음 (선택: crossguid, efsw) |

---

## 6. 프리팹 시스템

### 6.1 정의 및 필요성

프리팹(Prefab)은 **GameObject의 재사용 가능한 청사진(template)**이다. 한 번 정의한 오브젝트 구성(컴포넌트, 자식 구조, 프로퍼티)을 여러 곳에서 인스턴스화하고, 원본 수정 시 모든 인스턴스에 반영할 수 있다.

프리팹 없이는:
- 같은 구성의 적을 10종류 만들려면 매번 수동 설정
- 적의 체력을 변경하려면 모든 씬에서 하나씩 수정
- 런타임 스폰이 코드에서 모든 컴포넌트를 수동 추가해야 함
- 에셋으로서의 재사용이 불가능

### 6.2 Unity의 구현 방식

**Unity 프리팹 워크플로우:**

1. **생성**: Hierarchy의 GameObject를 Project 폴더로 드래그 → `.prefab` 파일 생성
2. **인스턴스화**: `Instantiate(prefab)` 또는 에디터에서 드래그
3. **프리팹 인스턴스**: 씬 내의 프리팹 복사본, 원본과 링크 유지
4. **오버라이드**: 인스턴스에서 프로퍼티 변경 시 오버라이드로 기록 (굵은 글씨 표시)
5. **적용**: 인스턴스의 변경을 원본에 적용 → 모든 인스턴스에 전파
6. **중첩 프리팹**: 프리팹 안에 다른 프리팹을 포함
7. **프리팹 배리언트**: 프리팹을 상속하여 변형 생성

**핵심 API:**
```csharp
// 인스턴스 생성
GameObject instance = Instantiate(prefab, position, rotation);

// 프리팹 연결 확인
bool isPrefab = PrefabUtility.IsPartOfPrefabInstance(obj);
GameObject source = PrefabUtility.GetCorrespondingObjectFromSource(obj);

// 오버라이드 관리
var overrides = PrefabUtility.GetObjectOverrides(instance);
PrefabUtility.ApplyPrefabInstance(instance, InteractionMode.UserAction);
PrefabUtility.RevertPrefabInstance(instance, InteractionMode.UserAction);
```

### 6.3 Molga Engine 구현 설계

#### 단계별 접근 (MVP -> 고급)

**단계 1 (MVP)**: 직렬화/역직렬화 기반 단순 프리팹
- 프리팹 = GameObject를 JSON으로 직렬화한 `.prefab` 파일
- `Instantiate()` = JSON 역직렬화로 새 GameObject 생성
- 프리팹 원본 링크 없음

**단계 2**: 원본 링크 + 오버라이드
- 프리팹 인스턴스가 원본 GUID를 기억
- 프로퍼티별 오버라이드 추적
- Apply/Revert 기능

**단계 3**: 중첩 프리팹
- 프리팹 내에서 다른 프리팹 참조
- 재귀적 인스턴스화

#### 아키텍처 구조

```
src/Core/
├── Prefab.h/cpp              // Prefab 에셋 클래스
├── PrefabInstance.h/cpp      // 프리팹 인스턴스 컴포넌트 (원본 링크, 오버라이드)
└── PrefabManager.h/cpp       // 프리팹 로딩, 인스턴스화
```

#### 핵심 구현

```cpp
// src/Core/Prefab.h
#pragma once
#include "Asset.h"
#include <nlohmann/json.hpp>
#include <string>

class GameObject;

// 프리팹 = 직렬화된 GameObject 트리
class Prefab {
public:
    // .prefab 파일에서 로딩
    bool Load(const std::string& filepath);

    // GameObject를 프리팹으로 저장
    bool Save(const std::string& filepath, const GameObject* source);

    // 프리팹에서 새 GameObject 인스턴스 생성
    std::shared_ptr<GameObject> Instantiate(Vector2 position = {0, 0});

    const AssetGUID& GetGUID() const { return guid; }
    const std::string& GetName() const { return name; }

private:
    AssetGUID guid;
    std::string name;
    nlohmann::json serializedData;  // GameObject 트리의 JSON 표현
};
```

```cpp
// src/Core/PrefabInstance.h
#pragma once
#include "ECS/Component.h"
#include "Asset.h"
#include <nlohmann/json.hpp>
#include <unordered_map>

// 프리팹 인스턴스를 나타내는 컴포넌트
// 이 컴포넌트가 있으면 해당 GameObject는 프리팹의 인스턴스
class PrefabInstance : public Component {
    COMPONENT_TYPE(PrefabInstance)
public:
    // 원본 프리팹 GUID
    AssetGUID prefabGUID;

    // 오버라이드된 프로퍼티 (component_type.property -> 값)
    // 예: "Transform.position.x" -> 150.0f
    nlohmann::json overrides;

    // 오버라이드 추적
    void RecordOverride(const std::string& propertyPath, const nlohmann::json& value);
    bool HasOverride(const std::string& propertyPath) const;
    void RemoveOverride(const std::string& propertyPath);

    // 원본 프리팹에 변경 적용
    void ApplyToSource();

    // 원본 프리팹으로 복원
    void RevertToSource();

    // 직렬화
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;
    void OnInspectorGUI() override;
};
```

```cpp
// src/Core/PrefabManager.h
#pragma once
#include "Prefab.h"
#include "ECS/GameObject.h"
#include <unordered_map>
#include <memory>

class PrefabManager {
public:
    // 프리팹 생성 (GameObject -> .prefab 파일)
    static AssetGUID CreatePrefab(const GameObject* source,
                                   const std::string& savePath);

    // 프리팹 인스턴스화
    static std::shared_ptr<GameObject> Instantiate(const AssetGUID& prefabGUID,
                                                    Vector2 position = {0, 0});

    // 프리팹 로딩 (캐싱)
    static Prefab* GetPrefab(const AssetGUID& guid);

    // 프리팹 변경 시 모든 인스턴스 업데이트
    static void UpdateAllInstances(const AssetGUID& prefabGUID);

    // 캐시 정리
    static void Clear();

private:
    static inline std::unordered_map<AssetGUID, std::unique_ptr<Prefab>> cache;
};
```

#### SceneSerializer 확장

현재 `SceneSerializer`는 이미 JSON 기반 직렬화를 지원하므로, 프리팹은 동일한 JSON 포맷의 확장이다:

```json
// .prefab 파일 예시
{
    "prefab": {
        "guid": "a1b2c3d4e5f678901234567890abcdef",
        "name": "Enemy_Skeleton",
        "root": {
            "name": "Skeleton",
            "components": [
                {
                    "type": "Transform",
                    "position": {"x": 0, "y": 0},
                    "rotation": 0,
                    "scale": {"x": 1, "y": 1}
                },
                {
                    "type": "SpriteRenderer",
                    "textureGUID": "ff00ee11...",
                    "color": {"r": 1, "g": 1, "b": 1, "a": 1}
                },
                {
                    "type": "Rigidbody2D",
                    "bodyType": "Dynamic",
                    "mass": 1.5
                },
                {
                    "type": "BoxCollider2D",
                    "size": {"x": 32, "y": 48}
                }
            ],
            "children": []
        }
    }
}
```

#### 에디터 통합

- **HierarchyWindow**: 프리팹 인스턴스는 파란색으로 표시, 우클릭 메뉴에 "Unpack Prefab", "Select Prefab Asset" 추가
- **InspectorWindow**: 오버라이드된 프로퍼티는 굵은 글씨, "Apply All" / "Revert All" 버튼
- **ProjectBrowserWindow**: `.prefab` 파일 아이콘, 더블클릭으로 프리팹 편집 모드

### 6.4 복잡도 및 의존성

| 항목 | 값 |
|---|---|
| **예상 복잡도** | **Large** (2-3주) |
| **신규 파일** | 4-6개 |
| **수정 파일** | SceneSerializer, HierarchyWindow, InspectorWindow, ProjectBrowserWindow |
| **의존성** | **에셋 관리 시스템** (GUID 참조), SceneSerializer (직렬화 재사용), ComponentFactory |
| **외부 의존성** | 없음 (nlohmann/json 기존 사용) |

---

## 7. 고급 입력 시스템

### 7.1 정의 및 필요성

고급 입력 시스템은 물리적 입력 장치(키보드, 마우스, 게임패드)와 게임 로직 사이에 **추상화 계층(abstraction layer)**을 제공한다.

현재 Molga Engine의 `Input` 클래스의 한계:
- **하드코딩**: `Input::GetKey(GLFW_KEY_W)`로 직접 키 코드 참조 → 키 리바인딩 불가
- **게임패드 미지원**: GLFW에 조이스틱 API가 있지만 래핑되지 않음
- **복합 입력 불가**: WASD를 하나의 2D 벡터 입력으로 결합 불가
- **멀티 플레이어 불가**: 입력 장치를 플레이어에 매핑할 수 없음

### 7.2 Unity의 구현 방식

**Unity New Input System (패키지):**

```
InputAction(논리적 액션)
  ├── Binding(물리적 바인딩)
  │   ├── Keyboard/W
  │   ├── Gamepad/LeftStick/Up
  │   └── Composite (WASD → Vector2)
  └── ActionType
      ├── Button (눌림/떼임)
      ├── Value (연속 값)
      └── PassThrough (모든 변경)
```

**핵심 API:**
```csharp
// InputAction 정의 (코드 또는 에디터)
var moveAction = new InputAction("Move", InputActionType.Value);
moveAction.AddCompositeBinding("2DVector")
    .With("Up", "<Keyboard>/w")
    .With("Down", "<Keyboard>/s")
    .With("Left", "<Keyboard>/a")
    .With("Right", "<Keyboard>/d");

moveAction.AddBinding("<Gamepad>/leftStick");

moveAction.Enable();

// 값 읽기
Vector2 move = moveAction.ReadValue<Vector2>();

// 콜백
moveAction.performed += ctx => OnMove(ctx.ReadValue<Vector2>());
moveAction.canceled += ctx => OnMoveStop();

// InputActionMap -- 컨텍스트별 액션 그룹
var gameplay = new InputActionMap("Gameplay");
var menu = new InputActionMap("Menu");
gameplay.Enable();   // gameplay 입력만 활성
menu.Disable();
```

**Unity 입력 시스템의 주요 개념:**

| 개념 | 설명 |
|---|---|
| **InputAction** | "Jump", "Move", "Fire" 같은 논리적 액션 |
| **Binding** | 액션과 물리 입력의 연결 |
| **Composite Binding** | 여러 입력을 하나로 결합 (WASD -> Vector2) |
| **InputActionMap** | 액션들의 그룹 (Gameplay, UI, Vehicle) |
| **InputActionAsset** | 여러 맵을 포함하는 에셋 (직렬화) |
| **Control Scheme** | 입력 장치 조합 (Keyboard+Mouse, Gamepad) |
| **Interaction** | 눌림/떼임/유지/멀티탭 등의 입력 패턴 |
| **Processor** | 데드존, 정규화, 반전 등 값 가공 |

### 7.3 Molga Engine 구현 설계

#### 아키텍처 구조

```
src/Systems/
├── Input.h/cpp               // 기존 유지 (저수준 폴링)
├── InputAction.h/cpp          // 논리적 액션 정의
├── InputBinding.h/cpp         // 물리 입력 -> 액션 매핑
├── InputActionMap.h/cpp       // 액션 그룹 (컨텍스트별)
└── InputManager.h/cpp         // 중앙 입력 관리자
```

#### InputAction 설계

```cpp
// src/Systems/InputAction.h
#pragma once
#include "Common/Types.h"
#include <string>
#include <vector>
#include <functional>
#include <variant>

// 액션 값 타입
enum class ActionType {
    Button,     // bool (눌림/떼임)
    Axis1D,     // float (-1 ~ 1)
    Axis2D      // Vector2 (방향 입력)
};

// 입력 소스 식별
enum class InputDevice {
    Keyboard,
    Mouse,
    Gamepad
};

// 물리 입력 식별자
struct InputSource {
    InputDevice device;
    int code;           // GLFW 키 코드 또는 게임패드 버튼/축 인덱스
    float scale = 1.0f; // 축 스케일 (반전용: -1.0f)
};

// 복합 바인딩 (WASD -> Vector2)
struct CompositeBinding {
    InputSource up;
    InputSource down;
    InputSource left;
    InputSource right;
};

// 바인딩 = 단일 입력 또는 복합 입력
using Binding = std::variant<InputSource, CompositeBinding>;

// 액션 상태
enum class ActionPhase {
    Waiting,     // 입력 없음
    Started,     // 이번 프레임에 시작
    Performed,   // 지속 중 또는 이번 프레임에 발생
    Canceled     // 이번 프레임에 해제
};

// 액션 컨텍스트 (콜백에 전달)
struct InputActionContext {
    ActionPhase phase;
    float value1D = 0.0f;
    Vector2 value2D = {0, 0};
    bool boolValue = false;
};

class InputAction {
public:
    explicit InputAction(const std::string& name, ActionType type = ActionType::Button);

    // 바인딩 추가
    void AddBinding(InputSource source);
    void AddCompositeBinding(CompositeBinding composite);

    // 콜백 등록
    void OnStarted(std::function<void(const InputActionContext&)> callback);
    void OnPerformed(std::function<void(const InputActionContext&)> callback);
    void OnCanceled(std::function<void(const InputActionContext&)> callback);

    // 현재 값 읽기
    bool ReadBool() const;
    float ReadFloat() const;
    Vector2 ReadVector2() const;

    // 활성/비활성
    void Enable();
    void Disable();
    bool IsEnabled() const { return enabled; }

    // 내부: 매 프레임 업데이트 (InputManager에서 호출)
    void Update();

    const std::string& GetName() const { return name; }
    ActionType GetType() const { return type; }

private:
    std::string name;
    ActionType type;
    bool enabled = false;
    ActionPhase currentPhase = ActionPhase::Waiting;

    std::vector<Binding> bindings;

    // 콜백
    std::vector<std::function<void(const InputActionContext&)>> startedCallbacks;
    std::vector<std::function<void(const InputActionContext&)>> performedCallbacks;
    std::vector<std::function<void(const InputActionContext&)>> canceledCallbacks;

    // 이전/현재 값
    float previousValue = 0.0f;
    float currentValue = 0.0f;
    Vector2 previousValue2D = {0, 0};
    Vector2 currentValue2D = {0, 0};

    // 바인딩에서 현재 값 계산
    float EvaluateBindings() const;
    Vector2 EvaluateBindings2D() const;
};
```

#### InputActionMap 설계

```cpp
// src/Systems/InputActionMap.h
#pragma once
#include "InputAction.h"
#include <string>
#include <unordered_map>
#include <memory>

class InputActionMap {
public:
    explicit InputActionMap(const std::string& name);

    // 액션 추가/조회
    InputAction* AddAction(const std::string& name, ActionType type);
    InputAction* GetAction(const std::string& name);

    // 전체 활성/비활성 (컨텍스트 전환용)
    void Enable();
    void Disable();

    // 매 프레임 업데이트
    void Update();

    // 직렬화 (에디터 설정 저장)
    void Serialize(nlohmann::json& j) const;
    void Deserialize(const nlohmann::json& j);

    const std::string& GetName() const { return name; }

private:
    std::string name;
    bool enabled = false;
    std::unordered_map<std::string, std::unique_ptr<InputAction>> actions;
};
```

#### InputManager 설계

```cpp
// src/Systems/InputManager.h
#pragma once
#include "InputActionMap.h"
#include <string>
#include <unordered_map>
#include <memory>

class InputManager {
public:
    static void Init();
    static void Shutdown();

    // 매 프레임 호출 (기존 Input::Update() 후)
    static void Update();

    // 액션 맵 관리
    static InputActionMap* CreateActionMap(const std::string& name);
    static InputActionMap* GetActionMap(const std::string& name);
    static void RemoveActionMap(const std::string& name);

    // 컨텍스트 전환 (하나만 활성)
    static void SwitchActionMap(const std::string& name);

    // 글로벌 액션 (항상 활성, 예: 일시정지)
    static InputAction* CreateGlobalAction(const std::string& name, ActionType type);

    // 키 리바인딩
    // 다음에 입력되는 키를 해당 액션에 바인딩
    static void StartRebinding(InputAction* action, int bindingIndex);
    static bool IsRebinding();
    static void CancelRebinding();

    // 설정 저장/로드
    static void SaveBindings(const std::string& filepath);
    static void LoadBindings(const std::string& filepath);

    // 게임패드 지원
    static bool IsGamepadConnected(int index = 0);
    static int GetConnectedGamepadCount();

private:
    static inline std::unordered_map<std::string,
        std::unique_ptr<InputActionMap>> actionMaps;
    static inline std::vector<std::unique_ptr<InputAction>> globalActions;
    static inline std::string activeMapName;
    static inline bool rebinding = false;
    static inline InputAction* rebindTarget = nullptr;
    static inline int rebindIndex = -1;
};
```

#### 사용 예시

```cpp
// 게임 초기화 시
void GameScene::OnEnter() {
    auto* gameplay = InputManager::CreateActionMap("Gameplay");

    // 이동 액션 (WASD + 게임패드 스틱)
    auto* move = gameplay->AddAction("Move", ActionType::Axis2D);
    move->AddCompositeBinding({
        {InputDevice::Keyboard, GLFW_KEY_W},      // Up
        {InputDevice::Keyboard, GLFW_KEY_S},      // Down
        {InputDevice::Keyboard, GLFW_KEY_A},      // Left
        {InputDevice::Keyboard, GLFW_KEY_D}       // Right
    });

    // 점프 액션
    auto* jump = gameplay->AddAction("Jump", ActionType::Button);
    jump->AddBinding({InputDevice::Keyboard, GLFW_KEY_SPACE});
    jump->OnStarted([this](const InputActionContext& ctx) {
        player->Jump();
    });

    // 공격 액션
    auto* attack = gameplay->AddAction("Attack", ActionType::Button);
    attack->AddBinding({InputDevice::Mouse, GLFW_MOUSE_BUTTON_LEFT});

    gameplay->Enable();
}

// 스크립트에서
void PlayerController::Update(float dt) {
    auto* move = InputManager::GetActionMap("Gameplay")->GetAction("Move");
    Vector2 dir = move->ReadVector2();
    transform->Translate(dir * speed * dt);
}
```

#### GLFW 게임패드 통합

GLFW는 이미 조이스틱/게임패드 API를 제공한다:

```cpp
// 기존 Input 클래스에 게임패드 폴링 추가
if (glfwJoystickPresent(GLFW_JOYSTICK_1)) {
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1)) {
        GLFWgamepadstate state;
        glfwGetGamepadState(GLFW_JOYSTICK_1, &state);
        // state.buttons[GLFW_GAMEPAD_BUTTON_A]
        // state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]
    }
}
```

### 7.4 추천 오픈소스 라이브러리

| 라이브러리 | 설명 |
|---|---|
| **gainput** (jkuhlmann) | 크로스플랫폼 입력 라이브러리, 디바이스 추상화, 매핑 | MIT |
| **SDL2 Input** | SDL의 입력 하위 시스템 (GLFW 대체 시) |
| **직접 구현 (추천)** | GLFW 위에 액션 매핑 레이어만 추가 |

**추천: 직접 구현** -- GLFW가 이미 키보드/마우스/게임패드를 모두 지원하므로, 그 위에 액션 매핑 레이어를 구축하는 것이 가장 효율적이다. gainput은 좋은 라이브러리이지만 GLFW와 중복이 많다.

### 7.5 복잡도 및 의존성

| 항목 | 값 |
|---|---|
| **예상 복잡도** | **Medium** (1-2주) |
| **신규 파일** | 4-5개 |
| **수정 파일** | Input (게임패드 폴링 추가), Bootstrap (초기화) |
| **의존성** | 기존 Input (저수준 폴링 재사용), 이벤트 시스템 (콜백 발행) |
| **외부 의존성** | 없음 (GLFW 기존 사용) |

---

## 8. 시스템 간 의존성 맵

```
                    ┌─────────────────────┐
                    │   이벤트 시스템 (2)   │ ◄── 가장 먼저 구현
                    │   (다른 모든 시스템의  │
                    │    통신 인프라)       │
                    └──────────┬──────────┘
                               │
           ┌───────────────────┼───────────────────┐
           │                   │                   │
           ▼                   ▼                   ▼
   ┌───────────────┐  ┌───────────────┐  ┌───────────────┐
   │ 코루틴 (3)     │  │오브젝트 풀 (4) │  │ 입력 시스템 (7)│
   │               │  │               │  │               │
   └───────┬───────┘  └───────┬───────┘  └───────────────┘
           │                   │
           │                   │
           ▼                   ▼
   ┌───────────────────────────────────┐
   │      에셋 관리 시스템 (5)          │ ◄── 리소스 인프라
   │  (GUID, 캐싱, 참조 카운팅)         │
   └──────────────────┬────────────────┘
                      │
           ┌──────────┼──────────┐
           ▼                     ▼
   ┌───────────────┐    ┌───────────────┐
   │ 프리팹 (6)     │    │ 물리 엔진 (1)  │
   │ (에셋 GUID 참조│    │ (Box2D 통합,   │
   │  직렬화 확장)  │    │  이벤트 콜백)  │
   └───────────────┘    └───────────────┘
```

**의존 관계 요약:**

| 시스템 | 필수 선행 | 선택 선행 |
|---|---|---|
| 이벤트 시스템 | 없음 | - |
| 코루틴 시스템 | 없음 (Time만) | 이벤트 (완료 이벤트) |
| 오브젝트 풀링 | 없음 | 프리팹 (풀 기반 인스턴스화) |
| 에셋 관리 | 없음 | 이벤트 (로딩 완료 이벤트) |
| 프리팹 시스템 | 에셋 관리 (GUID) | 오브젝트 풀 (풀 기반 스폰) |
| 물리 엔진 | 이벤트 (충돌 콜백) | 에셋 관리 (PhysicsMaterial), 코루틴 (물리 시퀀스) |
| 입력 시스템 | 기존 Input | 이벤트 (입력 이벤트 발행), 에셋 관리 (바인딩 설정 저장) |

---

## 9. 권장 구현 순서

### Phase A: 기초 인프라 (1-2주)

| 순서 | 시스템 | 복잡도 | 근거 |
|---|---|---|---|
| A-1 | **이벤트 시스템** | Small (2-3일) | 다른 모든 시스템의 통신 기반. 200줄 미만. |
| A-2 | **오브젝트 풀링** | Small (1-2일) | 독립적, 즉시 파티클 시스템에 적용 가능 |
| A-3 | **코루틴 시스템** | Medium (3-5일) | 물리/입력과 독립적, 게임플레이 로직 즉시 활용 |

### Phase B: 핵심 인프라 (3-5주)

| 순서 | 시스템 | 복잡도 | 근거 |
|---|---|---|---|
| B-1 | **고급 입력 시스템** | Medium (1-2주) | 게임플레이에 즉시 체감, GLFW 위에 구축 |
| B-2 | **에셋 관리 시스템** | Large (2-3주) | 프리팹과 물리의 전제 조건, 기존 코드 마이그레이션 포함 |

### Phase C: 고급 시스템 (5-9주)

| 순서 | 시스템 | 복잡도 | 근거 |
|---|---|---|---|
| C-1 | **프리팹 시스템** | Large (2-3주) | 에셋 관리 완료 후 구현, 에디터 워크플로우 혁신 |
| C-2 | **2D 물리 엔진** | Very Large (4-6주) | 가장 큰 작업, Box2D 통합 + 기존 ECS 연동 |

### 전체 타임라인 요약

```
주차  1    2    3    4    5    6    7    8    9   10   11   12
     ├────────┤
     Phase A: 이벤트 + 풀 + 코루틴
              ├──────────────────┤
              Phase B: 입력 + 에셋 관리
                                 ├────────────────────────────┤
                                 Phase C: 프리팹 + 물리 엔진

총 예상 기간: 10-16주 (한 명 기준, 풀타임)
```

### 각 시스템 구현 후 검증 기준

| 시스템 | 최소 검증 기준 |
|---|---|
| 이벤트 | 발행/구독/해제 단위 테스트, 지연 이벤트 큐 테스트 |
| 오브젝트 풀 | Get/Return 사이클 테스트, 풀 소진 시 동작, 메모리 누수 검사 |
| 코루틴 | WaitForSeconds 정확도, 중첩 코루틴, 중간 중지 |
| 입력 | 액션 매핑 키 변경 후 동작, 복합 입력 Vector2 정규화, 컨텍스트 전환 |
| 에셋 관리 | GUID 유지(파일 이동 후), 참조 카운팅, 미사용 언로딩 |
| 프리팹 | 인스턴스화 왕복(저장-로드), 오버라이드 기록/복원, 원본 변경 전파 |
| 물리 | 중력 낙하, AABB 충돌 반응, 레이캐스트, FixedUpdate 안정성 |

---

## 부록: 복잡도 요약 비교

| 시스템 | 복잡도 | 예상 기간 | 신규 파일 | 외부 의존성 |
|---|---|---|---|---|
| 이벤트 시스템 | **Small** | 2-3일 | 3-5 | 없음 |
| 오브젝트 풀링 | **Small** | 1-2일 | 2-3 | 없음 |
| 코루틴 시스템 | **Medium** | 3-5일 | 2-4 | 없음 |
| 고급 입력 시스템 | **Medium** | 1-2주 | 4-5 | 없음 |
| 에셋 관리 시스템 | **Large** | 2-3주 | 5-8 | 선택 (crossguid, efsw) |
| 프리팹 시스템 | **Large** | 2-3주 | 4-6 | 없음 |
| 2D 물리 엔진 | **Very Large** | 4-6주 | 8-12 | Box2D 3.x (MIT) |

**총합**: 신규 파일 28-43개, 총 기간 10-16주
