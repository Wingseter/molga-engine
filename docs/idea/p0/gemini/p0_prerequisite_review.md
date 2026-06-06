# P0: 선행 정비 작업 상세 계획서 리뷰 및 개선 제안 (Gemini)

`docs/ongoing/p0_prerequisite_plan.md` 문서를 분석하여 발견한 잠재적 오류 및 더 나은 구조적/성능적 개선 아이디어를 정리했습니다.

---

## ⚠️ 1. 치명적 오류 및 안정성 이슈 (Critical Issues)

### 1-1. P0-2: `GetWorldAABB()`에서 Transform 회전(Rotation) 미고려 문제
* **상황**: `BoxCollider2D`의 `GetWorldAABB()` 함수 구현부에 Scale과 Offset만 적용되고 있으며, **Rotation** 처리가 완전히 누락되어 있습니다.
* **문제점**: 2D 트랜스폼에 회전이 적용된 경우 축-정렬 바운딩 박스(AABB)는 원래 박스의 각 4 꼭지점을 회전 변환한 후 최소/최대 $X,Y$ 값을 다시 구해서 만들어져야 합니다. 단순히 `width *= scale;` 로만 처리한다면 회전된 물체의 충돌 영역이 시각적 표시와 일치하지 않게 되어 심각한 물리 버그를 유발합니다.
* **해결 제안**:
  ```cpp
  // Box의 4개 코너를 Transform 행렬(혹은 Pos/Rot/Scale) 기반으로 월드 공간으로 변환 후 min, max를 구하는 로직 필요
  Vector2 corners[4] = { ... };
  float minX = INFTY, maxX = -INFTY, minY = INFTY, maxY = -INFTY;
  for(auto& v : corners) {
      // transform by world matrix
      // update min/max
  }
  aabb.x = minX; aabb.y = minY;
  aabb.width = maxX - minX; aabb.height = maxY - minY;
  ```

### 1-2. P0-1 & P0-3: 순회 중 컬렉션 수정에 의한 Iterator Invalidation
* **상황**: `GameObject::~GameObject()` 또는 `GameObject::NotifyDestroy()` 내에서 `componentMap`을 `for (auto& [id, comp] : componentMap)` 형태로 순회하며 `OnDestroy()`를 호출합니다. 또한, P0-3에서 `main.cpp` 등은 컴포넌트 목록을 순회하며 `FixedUpdate()`를 호출합니다.
* **문제점**: 만약 임의의 `Component::OnDestroy()` 또는 `Script::FixedUpdate()` 내부에서 새로운 오브젝트를 생성하거나, 기존 컴포넌트를 지우게 되면(`AddComponent` / `RemoveComponent`), 순회 중인 컨테이너가 변경되어 Iterator Invalidation이 발생하여 엔진이 크래시(Segfault) 날 위험이 매우 큽니다.
* **해결 제안**:
  * **P0-1**: 파괴 시 처리인 경우 순회 전 임시 벡터(복사본)로 컴포넌트 목록을 떠놓고 반복하거나, 추가/삭제 요청을 큐(Queue)에 넣었다가 프레임 끝에서 지연 처리(Deferred Deletion) 패턴 도입.
  * **P0-3**: 메인 루프에서 컴포넌트나 게임 오브젝트 리스트를 순회할 때, 역순 순회 또는 지연 생성/삭제 구조(Ex: Destroy 큐) 구축 여부를 함께 고려해야 합니다.

---

## 💡 2. 성능 최적화 개선점 (Performance Improvements)

### 2-1. P0-4: Broad Phase 페어 중복 제거(`unordered_set`) 오버헤드
* **상황**: 중복된 충돌 생성 방지를 위해 `GetPotentialPairs()`에서 매 프레임 `std::unordered_set<PairKey, PairHash>` 객체를 생성하여 키 삽입 및 검사를 수행하고 있습니다.
* **문제점**: 매 FixedUpdate(물리 업데이트)마다 다수의 객체를 다이나믹 메모리 할당을 요구하는 해시맵/셋 연산에 통과시키는 것은 가비지나 오버헤드가 크며 성능(특히 캐시 히트율)에 악영향을 미칩니다.
* **해결/개선 아이디어 (Half-edge Cell Check)**
  Set을 쓰는 대신, 공간 분할 순회 패턴을 변경하면 중복을 원천 차단할 수 있습니다.
  1. 현재 Cell 내의 오브젝트 쌍끼리 검사
  2. 현재 Cell의 오브젝트와 **우측, 하단, 우하단, 좌하단 (4방향) Cell**의 오브젝트 간의 충돌만 검사.
  이렇게 고정된 한 방향으로만 순회하면 Set 자료구조 없이도 `(A, B)` 쌍이 중복 탐색되는 일을 수학적으로 막을 수 있으며 성능이 최고 속도로 향상됩니다.

### 2-2. P0-4: 정적 물체(Static)와 동적 물체(Dynamic) 그리드 재삽입 비용
* **상황**: 매 프레임 `broadPhase.Clear();` 후 씬의 모든 물리 객체를 다시 삽입(Insert)하고 있습니다.
* **문제점**: 배경, 땅, 벽같이 움직이지 않는 오브젝트(Static Collider)들까지 매 프레임 지워지고 다시 해시 그리드에 삽입되는 비용을 유발합니다.
* **해결 제안**:
  `SpatialGrid`를 두 개(정적 객체용, 동적 객체용) 운용하거나 객체에 정적/동적 플래그를 두어:
  * Static 객체들은 한 번만 삽입해두고 갱신하지 않음.
  * Dynamic 객체들만 매 프레임 Clear / Insert 방식 사용.
  충돌 검사는 `Static vs Dynamic`, `Dynamic vs Dynamic` 두 쌍으로 나누어 평가하면 퍼포먼스가 혁신적으로 증가합니다.

---

## 🛠 3. 기타 구조적 논의 (Architectural Suggestions)

### 3-1. P0-3: FixedAlpha 에 따른 Transform 렌더링 보간 누락
* 문서 상 `Time::GetFixedAlpha()` 함수가 마련되어 있지만, 이를 `Transform` 등에서 받아 실제로 **가변 시간 렌더 루프**와 **고정 물리 루프**간의 시각적 엇갈림(Stuttering)을 해결해주는 보간 로직계획이 아직 구체화되지 않았습니다.
* 이는 당장 적용할 필요는 없지만, Box2D 연동 단계(Phase 9) 직전이나 직후에 `Transform::UpdatePreviousPosition()` 등과 맞물려 SpritePosition을 선형 보간해주도록 계획을 세워두는 것이 좋습니다.

### 3-2. P0-1: 테스트 코드의 불안전한 Global State
* `tests/test_ecs.cpp`의 샘플 테스트 코드에 `static bool destroyCalled`, `static int destroyCount` 등의 전역/정적 변수를 두는 방식이 문서에 적혀있습니다.
* 병렬 단위 테스트를 돌리거나 다중 `test_` 함수가 호출될 때 상태 의존성(State Pollution)에 의해 불안정한 에러를 초래할 수 있습니다. 캡처 가능한 람다, 혹은 `Context` 구조체를 테스트 인스턴스로 전달하는 방식이 이상적입니다.
