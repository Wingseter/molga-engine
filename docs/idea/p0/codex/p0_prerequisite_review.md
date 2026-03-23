# P0 선행 정비 계획 리뷰 및 개선 제안 (Codex)

> 대상 문서: `docs/ongoing/p0_prerequisite_plan.md`
> 검토 기준: 2026-03-23 현재 `src/`, `tests/`, `docs/design/MASTER_PLAN.md` 대조

## 결론

P0-1, P0-2, P0-3의 방향 자체는 타당하다. 다만 현재 계획서는 "무엇을 추가할지"는 잘 적혀 있지만, 실제 코드베이스에서 이미 존재하는 라이프사이클 구멍과 호출 계약의 불일치를 충분히 반영하지 못했다.

가장 큰 보정 포인트는 4개다.

1. `OnDestroy()`를 넣기 전에 `AddComponent`의 교체 경로와 `RemoveComponent`의 계약을 먼저 정리해야 한다.
2. `FixedUpdate`는 에디터 모드 전환과 `timeScale`까지 고려하지 않으면 Play 진입 시 backlog 폭주가 생긴다.
3. `Collider2D` 추출은 가능하지만, 그 전에 `GetWorldAABB()`의 정확도 계약을 먼저 고정해야 한다.
4. `SpatialGrid`는 아직 소비하는 시스템이 없어서 지금 넣으면 미사용 인프라가 될 가능성이 높다.

---

## 1. 확인된 오류와 빠진 전제

### 1-1. `AddComponent` 교체 경로가 라이프사이클을 우회한다

현재 `GameObject`는 컴포넌트를 타입 ID 기반 `unordered_map`에 저장한다. 그런데 같은 타입을 다시 추가하면 기존 엔트리를 그냥 덮어쓴다.

- `src/ECS/GameObject.h:30-39`
- `src/ECS/GameObject.cpp:82-87`

이 경로에서는 기존 컴포넌트에 `OnDetach()`도, 새로 넣으려는 `OnDestroy()`도 호출되지 않는다. 즉 P0-1에서 파괴 라이프사이클을 도입해도, "교체" 케이스는 여전히 무음 파괴된다.

이건 계획서의 `RemoveComponent`/`Destroy` 정의만으로는 해결되지 않는다. 먼저 아래 계약을 정해야 한다.

- 같은 타입 컴포넌트를 다시 추가하는 것을 금지할지
- 허용한다면 기존 컴포넌트를 `OnDisable() -> OnDetach()`로 내릴지
- 교체를 "파괴"로 볼지, "분리"로 볼지

권장안은 단순하다.

- `AddComponent<T>`와 `AddComponentRaw`는 기존 타입이 있으면 실패하거나 assert
- 교체를 지원해야 하면 별도 `ReplaceComponent<T>` API를 만든다

P0-1을 지금 문서대로 진행하면 "오브젝트 파괴는 안전해졌는데 컴포넌트 교체는 여전히 새나감" 상태가 된다.

### 1-2. 계획서의 `OnDisable -> OnDestroy -> OnDetach` 순서는 현재 활성화 모델과 맞물려 있지 않다

현재 코드에서 컴포넌트 활성/비활성은 `Component::SetEnabled()`만이 콜백을 발생시킨다.

- `src/ECS/Component.h:55-66`

반면 `GameObject::SetActive()`는 단순히 `active` 플래그만 뒤집고 끝난다.

- `src/ECS/GameObject.h:91-93`

그리고 실제 제거는 아직 `OnDetach()`만 호출한다.

- `src/ECS/GameObject.h:67-75`
- `src/ECS/GameObject.cpp:11-16`

즉 계획서에 적힌

```text
RemoveComponent: OnDisable() -> OnDetach()
Destroy:         OnDisable() -> OnDestroy() -> OnDetach()
```

는 현재 엔진의 활성화 모델 위에 바로 올라갈 수 있는 정의가 아니다. 적어도 아래 둘 중 하나는 먼저 결정해야 한다.

- `GameObject` 파괴 시 "enabled=true 인 컴포넌트만" `OnDisable()`을 보낼지
- 이미 비활성인 컴포넌트는 `OnDisable()`을 재호출하지 않을지

추가로, `SetActive(false)`가 컴포넌트 `OnDisable()`을 유발하지 않는 한, "비활성화"라는 용어가 GameObject 레벨과 Component 레벨에서 서로 다른 의미로 계속 남는다.

P0-1 문서에는 이 계약 충돌이 빠져 있다.

### 1-3. `OnDestroy()`/`FixedUpdate()` 순회 중 컨테이너 수정 리스크가 실제로 존재한다

현재도 `GameObject`는 `unordered_map`을 직접 순회한다.

- `src/ECS/GameObject.cpp:11-16`
- `src/ECS/GameObject.cpp:62-79`

계획서의 `NotifyDestroy()`와 메인 루프의 `for (auto* comp : obj->GetComponents())`도 동일한 위험을 갖는다. 콜백 내부에서 `RemoveComponent`, `AddComponent`, 부모-자식 변경, 오브젝트 삭제 요청이 들어오면 순회 안정성이 깨진다.

권장안:

- 파괴/업데이트용 스냅샷 벡터를 먼저 뜬 뒤 순회
- 혹은 프레임 말미 지연 처리 큐를 도입

P0-1과 P0-3은 기능 추가 전에 "순회 중 수정 금지" 원칙을 먼저 문서화하는 편이 맞다.

### 1-4. `FixedUpdate`는 에디터 모드 전환을 고려하지 않으면 Play 진입 시 누적 시간이 폭발한다

현재 에디터 메인 루프는 항상 `Time::Update()`를 호출하지만, 게임 업데이트는 Play 모드에서만 돈다.

- `src/main.cpp:143-180`
- `src/Editor/EditorState.h:16-26`
- `src/Editor/EditorState.cpp:27-46`

계획서대로 `Time::Update()` 내부에서 `accumulator += deltaTime`를 수행하면, Edit 모드에 머무는 동안에도 fixed accumulator가 계속 증가한다. 그 상태에서 Play로 전환하면 첫 프레임에 `while (HasPendingFixedStep())`가 대량 실행될 수 있다.

이 문제는 런타임에는 없지만 에디터에는 명백히 있다.

권장안:

- `Time::Update()`는 raw `deltaTime`만 계산
- 시뮬레이션 시간이 필요한 쪽에서만 `AccumulateSimulationTime(simDt)` 호출
- Edit -> Play 진입 시 `ResetFixedAccumulator()` 호출
- Pause -> Play 복귀 정책도 명시

계획서의 현재 구조는 "시간 계산"과 "시뮬레이션 시간 누적"의 책임을 한 곳에 섞어 놓았다.

### 1-5. `timeScale`을 `fixedDt`에 곱하는 설계는 고정 timestep의 목적과 충돌한다

계획서의 에디터 루프 제안은 아래 형태다.

```cpp
while (Time::HasPendingFixedStep()) {
    float fixedDt = Time::GetFixedDeltaTime() * editorState.GetTimeScale();
    script->FixedUpdate(fixedDt);
    Time::ConsumeFixedStep();
}
```

하지만 문서 앞부분은 고정 timestep의 이유를 "결정적 결과"라고 설명한다. 그러면 step 크기는 고정이어야 한다.

`timeScale`을 step 크기에 직접 곱하면:

- `timeScale != 1.0`에서 물리 step 크기가 변한다
- 나중에 Box2D `PhysicsWorld::Step(fixedDt)`를 붙이면 안정성과 재현성이 흔들린다

권장안:

- 물리/고정 시뮬레이션은 항상 고정 `fixedDeltaTime`으로 돈다
- `timeScale`은 누적할 시뮬레이션 시간 쪽에 반영하거나, 스크립트용 scaled time과 physics용 unscaled fixed time을 분리한다

즉 P0-3은 지금 단계에서라도 `FixedUpdate` 계약을 두 개로 나누는 편이 낫다.

- `GetFixedDeltaTime()` 또는 `GetFixedUnscaledDeltaTime()`
- `GetFixedScaledDeltaTime()`이 필요하면 별도 제공

### 1-6. `Script` 라이프사이클은 이미 반쯤 존재하는데, 계획서는 `FixedUpdate`만 메인 루프에 박아 넣는다

`Script`에는 이미 `Start()`, `FixedUpdate()`, `LateUpdate()`, `HasStarted()`가 있다.

- `src/Scripting/Script.h:19-29`
- `src/Scripting/Script.h:59-64`

그런데 현재 메인 루프는 `GameObject::Update()`만 호출하고 있고, `Start()`와 `LateUpdate()`는 실제 어디에서도 쓰이지 않는다.

- `src/main.cpp:169-179`
- `src/runtime_main.cpp:101-112`

이 상태에서 `main.cpp`와 `runtime_main.cpp` 안에 `dynamic_cast<Script*>` 루프를 하나 더 넣으면, 스크립트 생명주기가 엔트리포인트 파일 두 곳에 분산된다. 이후 `Start`, `LateUpdate`, 충돌 콜백까지 늘어나면 같은 패턴이 반복된다.

권장안:

- `GameObject::FixedUpdateScripts(float)`
- `GameObject::LateUpdateScripts(float)`
- 가능하면 `GameObject::Update(float)` 내부에서 Script 전용 훅을 통합

최소한 엔트리포인트에 직접 `dynamic_cast<Script*>` 루프를 중복 배치하는 구조는 피하는 편이 낫다.

### 1-7. `Collider2D` 추출 자체보다 `GetWorldBounds()`의 정확도 계약이 먼저다

현재 `BoxCollider2D::GetWorldAABB()`는 위치, 오프셋, 스케일만 반영하고 회전은 반영하지 않는다.

- `src/ECS/Components/BoxCollider2D.cpp:13-31`

또한 `AABB`는 `Left/Right/Top/Bottom`이 `x + width`, `y + height`를 그대로 사용하므로, 음수 스케일이 들어오면 width/height가 음수가 되어 경계 계산 자체가 비정상일 수 있다.

- `src/Common/Types.h:103-123`

Broad phase는 `GetWorldBounds()`를 신뢰하고 동작하므로, 이 계약이 불완전하면 `SpatialGrid`도 같이 잘못된 후보 쌍을 낸다.

권장안:

- 최소한 P0 문서에 "현재 bounds는 회전 미지원 / 음수 스케일 미정의"를 명시
- 더 좋게는 `AABB Normalize()` 또는 `Collider2D::GetWorldBounds()`가 항상 canonical bounds를 반환하도록 계약화

즉 P0-2는 단순 추상화보다 "정확한 world bounds 인터페이스 정의"를 먼저 써야 한다.

### 1-8. `SpatialGrid`는 지금 코드베이스에서 아직 소비자가 없다

저장소 검색 기준으로 현재 `src/`에는 `PhysicsWorld`, `CollisionWorld`, `BroadPhase`, `SpatialGrid` 같은 중앙 충돌 시스템이 없다. 콜라이더는 에디터, 직렬화, 개별 `BoxCollider2D::CheckCollision()` 수준에서만 존재한다.

- `src/ECS/Components/BoxCollider2D.cpp:34-50`
- `src/Physics/Collision.h`

즉 계획서의

```text
현재: 모든 쌍 O(n²) -> Narrow Phase
```

는 "앞으로 만들 충돌 시스템의 예상 구조"에는 맞지만, 현재 코드베이스의 실제 병목을 가리키는 설명은 아니다.

그래서 P0-4를 지금 바로 넣으면 다음 둘 중 하나가 되기 쉽다.

- 테스트만 있고 실제 사용처가 없는 유틸
- Phase 9에서 다시 뜯어고칠 임시 broad phase

권장안:

- P0-4를 독립 작업으로 두지 말고
- `PhysicsWorld` 또는 최소 `CollisionWorld`가 생기는 시점과 같이 묶는다

정말 P0에서 미리 만들고 싶다면 "미사용 인프라를 허용한다"는 점을 문서에 명확히 적는 편이 낫다.

### 1-9. `SpatialGrid` 설계 초안에는 구현상 경계 조건 오류가 있다

계획서의 `GetOverlappingCells()`는 아래 식을 사용한다.

```cpp
int minX = floor(bounds.Left() / cellSize);
int maxX = floor(bounds.Right() / cellSize);
```

이 방식은 경계에 정확히 걸친 AABB를 한 칸 더 큰 셀에 넣는다. 예를 들어 `x=0, width=64, cellSize=64`면 실제로는 셀 0 하나만 덮어도 되는데 `Right()==64`라서 셀 1까지 포함된다.

이건 정확도 문제라기보다는 false positive 증가 문제지만, broad phase에서는 꽤 민감하다.

권장안:

- `maxX = floor((Right() - epsilon) / cellSize)` 형태로 보정
- 혹은 half-open interval `[left, right)` 규약을 문서에 명시

추가로 계획서의 pair dedup 코드에서 `if (a > b) std::swap(a, b);`로 포인터 순서를 비교하는 부분도 피하는 편이 낫다. 관련 없는 객체 포인터를 relational compare에 기대기보다 `std::less<Collider2D*>` 기반 정규화가 더 안전하다.

### 1-10. 테스트/빌드 반영 목록이 하나 빠져 있다

계획서 마지막 표에는 `tests/test_spatial_grid.cpp`가 새 파일로 들어가 있지만, 실제 테스트 빌드는 `tests/CMakeLists.txt`에서 명시적으로 등록해야 한다.

- `tests/CMakeLists.txt:1-10`

즉 P0-4를 문서대로 구현하면 루트 `CMakeLists.txt` 수정만으로는 테스트가 돌지 않는다. `tests/CMakeLists.txt`도 같이 수정 대상에 넣어야 한다.

---

## 2. 더 나은 실행 방식

### 권장 재정렬

현재 문서:

1. P0-1 OnDestroy
2. P0-3 FixedUpdate
3. P0-2 Collider2D
4. P0-4 Broad Phase

권장 순서:

1. 라이프사이클 계약 정리
2. 스크립트/시간 루프 정리
3. Collider bounds 계약 정리 + Collider2D 추출
4. Broad phase는 소비 시스템과 함께 진행

### P0-1 개선안

단순히 `OnDestroy()` 한 줄 추가로 끝내지 말고 아래를 한 번에 묶는 편이 낫다.

- `AddComponent` 중복 타입 정책 결정
- `RemoveComponent`의 `OnDisable` 여부 결정
- `NotifyDestroy()` 순회 안정성 확보
- disabled 컴포넌트 파괴 시 콜백 순서 정의

작업명도 `OnDestroy 추가`보다 `Component lifecycle contract 정리`가 더 정확하다.

### P0-3 개선안

`Time`은 raw time source로 두고, 시뮬레이션 누적은 호출부에서 관리하는 편이 안전하다.

권장 API 예시:

```cpp
Time::Update();
Time::ResetFixedAccumulator();
Time::AccumulateSimulationTime(simulationDt);
Time::ConsumeFixedStep();
```

그리고 스크립트 훅은 엔트리포인트가 아니라 `GameObject` 쪽으로 끌어내리는 편이 낫다.

예시:

```cpp
obj->FixedUpdateScripts(fixedDt);
obj->Update(dt);
obj->LateUpdateScripts(dt);
```

이렇게 해야 `main.cpp`와 `runtime_main.cpp`가 나중에 또 같은 로직을 복붙하지 않는다.

### P0-2 개선안

`Collider2D`는 유지하되, 추상화보다 먼저 아래 계약을 문서화하는 편이 좋다.

- `GetWorldBounds()`는 회전을 반영한 월드 AABB인가
- 음수 스케일을 허용하는가
- broad phase용 bounds와 narrow phase용 shape 데이터를 분리할 것인가

지금 계획서는 "상속 구조" 설명은 충분하지만, 실제 충돌 정확도를 좌우하는 bounds 계약은 약하다.

### P0-4 개선안

Broad phase를 지금 꼭 넣는다면 최소 소비자도 같이 넣어야 한다.

- `CollisionWorld` 또는 `PhysicsWorld` 초석 추가
- `BuildPairs()` / `Step()` 같은 진입점 정의
- 최소 1개 실제 호출 경로 확보

그게 아니라면 P0-4는 Phase 9 직전으로 미루는 편이 더 경제적이다.

---

## 3. 추천 테스트 보강

현재 계획서 테스트는 방향은 맞지만 경계 케이스가 부족하다. 아래 항목은 꼭 추가하는 편이 좋다.

- 같은 타입 컴포넌트를 두 번 추가할 때 기존 인스턴스 처리 방식 검증
- disabled 컴포넌트에 대해 `NotifyDestroy()` 호출 시 `OnDisable` 중복 여부 검증
- `OnDestroy()` 내부에서 다른 컴포넌트를 제거해도 크래시 나지 않는지 검증
- Edit 모드에서 오래 대기 후 Play 전환 시 fixed step backlog가 쌓이지 않는지 검증
- `timeScale != 1.0`에서 fixed update 호출 횟수와 step 크기 계약 검증
- 회전된 `Transform` 또는 음수 scale에서 `GetWorldBounds()` 결과 검증
- `SpatialGrid` 셀 경계값 케이스 검증
- `tests/test_spatial_grid.cpp`가 실제 CTest에 등록되는지 검증

테스트는 `static bool` 전역 플래그보다 지역 카운터 객체를 컴포넌트 안에 두는 편이 더 안전하다.

---

## 4. 최종 제안

이 계획서는 그대로 구현해도 일부 기능은 들어가겠지만, "라이프사이클 계약", "에디터 모드 전환", "bounds 정확도", "실사용 없는 broad phase" 네 지점 때문에 다시 뜯을 가능성이 크다.

따라서 P0는 아래처럼 축소/정교화하는 편이 낫다.

1. `OnDestroy()` 단독 추가가 아니라 컴포넌트 lifecycle contract 정리
2. `FixedUpdate`는 에디터 상태 전환과 `timeScale`까지 포함해 설계
3. `Collider2D`는 bounds 계약을 먼저 정의한 뒤 추출
4. `SpatialGrid`는 `PhysicsWorld`가 생길 때 같이 붙이거나, 최소 소비자 시스템을 함께 만든다

이 네 가지를 반영하면 P0는 "Phase 9 전 임시 보수"가 아니라, 이후 물리/스크립트 확장의 실제 기반이 된다.
