# 01. 물리 (Physics) 구현 계획

> 작성일: 2026-06-14
> 범위: 자동 충돌 처리 루프, 충돌/트리거 이벤트 발화, `Rigidbody2D`, `CircleCollider2D`
> 관련 문서: [`00_overview.md`](00_overview.md) · [`09_tags_layers.md`](09_tags_layers.md)(충돌 레이어)

---

## 1. 현재 상태 (코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| 충돌 수학 함수 | `src/Physics/Collision.h:5-22` (AABB/Circle/AABB-Circle, 점 테스트) | ✅ 동작 |
| 충돌 데이터 | `src/Common/Types.h:93-176` (`AABB`, `Circle`, `CollisionResult`) | ✅ 동작 |
| 콜라이더 베이스 | `src/ECS/Components/Collider2D.h` (추상, `ShapeType{Box,Circle,Polygon,Capsule}`, offset, isTrigger, `GetWorldBounds()`) | ✅ 잘 설계됨 |
| Box 콜라이더 | `src/ECS/Components/BoxCollider2D.{h,cpp}` (직렬화·Inspector 있음) | ✅ 데이터로 존재 |
| 충돌 이벤트 타입 | `src/Core/Events/PhysicsEvents.h` (`CollisionEvent`, `TriggerEvent`) | 🟡 **정의만, 발화 안 됨** |
| 타일맵 충돌 | `src/Rendering/Tilemap.h:25-32` (`CheckCollision`, `GetCollidingTiles`) | 🟡 독립 클래스 |

**핵심 격차:**
- 엔진 루프에 **물리 스텝이 없다**. `World::FixedStep`은 스크립트 `FixedUpdate`만 돈다(`src/Core/World.h:26`).
  콜라이더를 붙여도 **아무 일도 자동으로 일어나지 않는다.**
- `CollisionEvent`/`TriggerEvent`가 **어디서도 `Publish`/`QueueEvent`되지 않는다.**
- **`Rigidbody2D` 없음** (속도/중력/힘/적분 없음).
- `ShapeType::Circle`은 enum에 있으나 **`CircleCollider2D` 클래스 없음.**
- `Collider2D.h:6`에 `PhysicsMaterial2D` 전방 선언만 있고 구현 없음.
- 선행 결함: `EventBus::ProcessQueue()`가 루프에서 호출되지 않음([`00_overview.md`](00_overview.md) §5).

---

## 2. 목표 (완료의 정의)

게임 제작자가 에디터에서:
- 오브젝트에 `Rigidbody2D` + `Collider2D`를 붙이면 중력/속도로 자동 이동한다.
- 콜라이더끼리 겹치면 **스크립트가 `OnCollisionEnter/Stay/Exit`, `OnTriggerEnter/Exit`** 콜백을 받는다.
- Trigger 콜라이더는 통과시키되 이벤트만 발생시킨다.
- 충돌 응답으로 캐릭터가 벽/바닥을 통과하지 않는다(기본 AABB 분리).
- 이 동작이 **Play 모드와 빌드된 런타임에서 동일하게** 재현된다.

---

## 3. 설계

### 3.1 구현 방향 결정: 자체 구현 vs Box2D

| 방향 | 장점 | 단점 |
|---|---|---|
| **A. 자체 경량 물리 (권장 1차)** | 의존성 0, 기존 `Collision`/`Collider2D` 재사용, 작은 통합 표면 | 관절/연속충돌(CCD)/안정적 스택 한계 |
| **B. Box2D 3.x 통합** | 검증된 강체·관절·CCD | 큰 통합 작업, 빌드/직렬화 매핑, 좌표계 변환(미터↔픽셀) |

> **권장:** 먼저 **A(자체 경량)**로 `Rigidbody2D` + AABB/Circle 충돌 감지·이벤트·기본 분리까지
> 완성해 "게임이 도는" 상태를 만든다. 관절·복잡한 물리가 필요한 시점에 **B(Box2D)**를
> `PhysicsWorld` 인터페이스 뒤에서 교체한다. 따라서 §3.2의 `PhysicsWorld`를 **추상 경계**로 둔다.

### 3.2 신규 타입

```
PhysicsWorld (src/Physics/PhysicsWorld.{h,cpp})   // 물리 스텝 오케스트레이터
  - Step(World& world, float fixedDt)
  - Broad phase: 콜라이더 AABB 수집 → 후보 페어 (1차: O(n²), 이후 Uniform Grid)
  - Narrow phase: Collision::Check*WithResult
  - 레이어 마스크 필터(09 문서) 적용
  - Rigidbody 적분(중력·속도·위치) + 충돌 분리(non-trigger)
  - 이전 프레임 접촉 집합과 비교해 Enter/Stay/Exit 판정 → 이벤트 발화

Rigidbody2D : Component (src/ECS/Components/Rigidbody2D.{h,cpp})
  - BodyType { Static, Kinematic, Dynamic }
  - velocity(Vector2), gravityScale(float), mass, linearDamping, freezeRotation
  - AddForce / AddImpulse / SetVelocity
  - COMPONENT_TYPE + REGISTER_COMPONENT + Serialize/Deserialize/OnInspectorGUI

CircleCollider2D : Collider2D (src/ECS/Components/CircleCollider2D.{h,cpp})
  - radius, GetShapeType()==Circle, GetWorldBounds() (AABB로 감쌈)
  - BoxCollider2D와 동일한 직렬화/Inspector 패턴
```

### 3.3 통합 지점

- **`World::FixedStep`** (`src/Core/World.h:26`)에서 스크립트 `FixedUpdate` **전/후**에
  `PhysicsWorld::Step(*this, fixedDt)` 호출. (현재 main 루프는 이미
  `while HasPendingFixedStep → ActiveWorld().FixedStep` 구조: `src/main.cpp:247-249`)
- **충돌 응답 순서(권장):** ① 힘/중력→속도 적분 ② 위치 적분 ③ 충돌 감지 ④ non-trigger 분리·속도 보정 ⑤ 접촉 비교 후 이벤트 발화.
- **이벤트 발화:** `CollisionEvent`/`TriggerEvent`를 `EventBus::QueueEvent`로 큐잉하고,
  FixedStep 직후 또는 프레임 끝에 **`EventBus::ProcessQueue()` 호출을 main 루프에 추가**(선행 결함).
- **스크립트 콜백 브리지:** 이벤트를 받아 해당 `GameObject`의 `Script` 컴포넌트에
  `OnCollisionEnter(other)` 등을 디스패치하는 얇은 어댑터(`Script` 인터페이스에 가상 훅 추가).

### 3.4 직렬화

- `Rigidbody2D`: bodyType, gravityScale, mass, linearDamping, freezeRotation, velocity(초기값)
- `CircleCollider2D`: radius + `Collider2D::SerializeBase`(offset, isTrigger) — 기존 `BoxCollider2D` 패턴 그대로
- 신규 컴포넌트 **`REGISTER_COMPONENT` 필수** (누락 시 로드 무음 실패)

### 3.5 에디터

- `Rigidbody2D` Inspector: bodyType 콤보, gravityScale/mass 슬라이더, freezeRotation 체크
- `CircleCollider2D` Inspector: radius, offset, isTrigger
- (이후) Scene View **콜라이더 디버그 오버레이** — FBO Scene View([`../2026-06-14_scene-view-and-creation-workflow-plan.md`])에 의존

---

## 4. 작업 체크리스트

**선행**
- [ ] main 루프에 `EventBus::ProcessQueue()` 호출 추가 + 호출 시점 단위 테스트
- [ ] 충돌 레이어가 필요하면 [`09_tags_layers.md`](09_tags_layers.md) 먼저

**1차: 감지 + 이벤트 (응답 없음)**
- [ ] `PhysicsWorld` 골격: 콜라이더 수집 → 페어 → narrow phase
- [ ] 접촉 집합 Enter/Stay/Exit 판정
- [ ] `CollisionEvent`/`TriggerEvent` 발화 + `Script` 콜백 브리지
- [ ] `CircleCollider2D` 구현 + 등록 + 직렬화 + Inspector
- [ ] 단위 테스트: 두 박스 진입/유지/이탈 이벤트 시퀀스, 박스-원, 트리거

**2차: Rigidbody + 응답**
- [ ] `Rigidbody2D` 컴포넌트 + 등록 + 직렬화 + Inspector
- [ ] 중력·속도·위치 적분 (semi-implicit Euler)
- [ ] non-trigger AABB 분리(최소 침투축) + 속도 보정
- [ ] Static/Kinematic/Dynamic 조합 테스트(낙하→바닥 정지, 벽 통과 방지)
- [ ] Play→Stop 시 물리 상태가 편집값으로 복원되는지 확인(World Clone 경로)

**3차: 성능·확장(이후)**
- [ ] Broad phase Uniform Grid (O(n²) 제거)
- [ ] 타일맵 충돌을 `PhysicsWorld`에 통합([`07_tilemap.md`](07_tilemap.md))
- [ ] (필요 시) Box2D 백엔드로 `PhysicsWorld` 교체, `PhysicsMaterial2D`(반발/마찰)

---

## 5. 완료 기준

- [ ] Dynamic Rigidbody + BoxCollider가 중력으로 낙하해 Static 바닥에서 멈춘다.
- [ ] 두 콜라이더 진입/이탈 시 스크립트가 `OnCollisionEnter/Exit`를 정확히 1회씩 받는다.
- [ ] Trigger는 물체를 통과시키되 `OnTriggerEnter/Exit`를 발생시킨다.
- [ ] 위 동작이 빌드된 런타임에서 동일하게 재현된다.
- [ ] Debug/Release 양쪽에서 물리 단위 테스트 통과(assert 아닌 테스트 프레임워크).

---

## 6. 의존성·위험·결정 필요

- **결정:** 1차 자체 구현으로 시작하는 데 동의하는가, 아니면 처음부터 Box2D인가? (재작업량 차이 큼)
- **의존:** 충돌 필터링은 [`09_tags_layers.md`](09_tags_layers.md)의 layer/collision matrix에 의존.
- **위험:** 자체 충돌 분리는 스택/고속 물체에서 불안정 → 1차 범위를 "플랫포머 수준"으로 제한.
- **위험:** `World::Clone()`이 직렬화 기반(`src/Core/World.h:32`)이므로 Rigidbody **런타임 상태**(속도 등)는
  복제되지 않고 초기값으로 리셋됨 → Play/Stop 복원 의미에는 부합하나, 문서화 필요.
