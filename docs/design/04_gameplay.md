# Phase 4: 게임플레이 시스템 리서치 보고서

> Molga Engine (C++17 Custom ECS) - Unity 비교 기반 누락 시스템 분석
> 작성일: 2026-03-22

---

## 목차

1. [애니메이션 상태 머신 (Animation State Machine)](#1-애니메이션-상태-머신)
2. [2D 내비게이션 / 길찾기 (Pathfinding)](#2-2d-내비게이션--길찾기)
3. [태그 & 레이어 시스템 (Tags & Layers)](#3-태그--레이어-시스템)
4. [추가 빌트인 컴포넌트](#4-추가-빌트인-컴포넌트)
5. [오브젝트 라이프사이클](#5-오브젝트-라이프사이클)
6. [씬 관리 시스템](#6-씬-관리-시스템)
7. [전역 게임 상태](#7-전역-게임-상태)
8. [트위닝 / 이징 시스템](#8-트위닝--이징-시스템)
9. [2D 특화 시스템](#9-2d-특화-시스템)
10. [구현 우선순위 로드맵](#10-구현-우선순위-로드맵)

---

## 현재 Molga Engine 아키텍처 요약

```
현재 구현 완료:
- GameObject + Component (unordered_map 기반 O(1) 룩업)
- ComponentTypeID (컴파일 타임 타입 ID, 정적 카운터)
- ComponentFactory (매크로 기반 자동 등록, 문자열 동적 생성)
- Transform (position, rotation, scale, parent-child 계층)
- SpriteRenderer (texture, color tint, flip, sorting order)
- BoxCollider2D (AABB, size/offset, trigger)
- Script (C++ hot-reload, Unity 스타일 라이프사이클)
- Scene/SceneManager (OnEnter/OnExit, ChangeScene)
- Animation (SpriteSheet 기반, 프레임 시퀀스, 루프)
- Collision (AABB, Circle, AABB-Circle, Point 검사)
- ParticleEmitter (구조체 기반, 프리셋)
- Camera2D (position, zoom, rotation, 행렬)
- Audio (miniaudio, SFX + Music)
- Tilemap (타일 기반, 충돌 지원)
```

---

## 1. 애니메이션 상태 머신

### 1.1 개요 및 필요성

**무엇을 하는가**: 여러 애니메이션 클립(Idle, Walk, Run, Jump, Attack 등)을 상태(State)로 관리하고, 조건(파라미터)에 따라 자동 전환(Transition)하는 시스템. 게임 로직에서 `SetBool("isRunning", true)`만 호출하면 애니메이션이 자동으로 적절히 전환된다.

**왜 필수적인가**: 현재 Molga Engine의 Animation 클래스는 단일 애니메이션 시퀀스만 재생 가능하다. 실제 2D 게임에서 캐릭터는 수십 개의 애니메이션 상태를 가지며, 이를 수동으로 if/else 분기하면 코드가 급격히 복잡해진다. 상태 머신은 이를 데이터 주도(data-driven)로 해결한다.

### 1.2 Unity의 구현 방식

**핵심 API 및 워크플로우**:

```
Unity 아키텍처:
Animator (Component)
  └─ AnimatorController (Asset)
       ├─ Layer 0 ("Base Layer")
       │    ├─ State: Idle (default)
       │    │    └─ Motion: idle_animation (AnimationClip)
       │    ├─ State: Walk
       │    │    └─ Motion: walk_animation
       │    ├─ State: Jump
       │    │    └─ Motion: jump_animation / BlendTree
       │    ├─ Transition: Idle → Walk (조건: speed > 0.1)
       │    ├─ Transition: Walk → Idle (조건: speed < 0.1)
       │    └─ AnyState → Hit (조건: trigger "hit")
       ├─ Layer 1 ("Upper Body")  [mask 적용]
       │    └─ 상체만 독립적으로 제어
       └─ Parameters:
            ├─ speed (float)
            ├─ isGrounded (bool)
            ├─ jumpTrigger (trigger)
            └─ attackType (int)
```

**Unity 핵심 개념**:
- **AnimationClip**: 스프라이트 프레임 시퀀스 (현재 Animation 클래스와 대응)
- **AnimatorState**: 하나의 AnimationClip 또는 BlendTree를 포함하는 상태 노드
- **AnimatorTransition**: 상태 간 전환 규칙 (조건 목록, 전환 시간, exit time)
- **Parameter**: bool, float, int, trigger 4가지 타입의 조건 변수
- **Layer**: 독립적인 상태 머신으로 신체 부위별 애니메이션 혼합
- **BlendTree**: 여러 클립을 파라미터 값에 따라 보간 (1D/2D)
- **AnyState**: 어떤 상태에서든 특정 조건 시 전환 가능한 특수 상태
- **Sub-State Machine**: 상태 머신 안의 상태 머신 (복잡도 관리)

**2D 특화 사항**:
- 2D에서는 블렌딩이 보통 "스프라이트 스왑"으로 처리 (3D의 뼈대 보간과 다름)
- BlendTree는 2D에서 주로 방향별 애니메이션 선택에 활용 (8방향 이동 등)
- Sprite Animation은 프레임 단위로 텍스처 교체가 핵심

### 1.3 권장 구현 방법 (C++17 ECS)

**핵심 자료구조**:

```cpp
// ── 파라미터 시스템 ──
enum class AnimParamType { Bool, Int, Float, Trigger };

struct AnimParameter {
    AnimParamType type;
    union {
        bool boolValue;
        int intValue;
        float floatValue;
    };
    std::string name;
};

// ── 전환 조건 ──
enum class CompareOp { Greater, Less, Equal, NotEqual };

struct TransitionCondition {
    std::string paramName;
    CompareOp op;
    AnimParameter threshold;  // 비교 값
};

// ── 전환 (Transition) ──
struct AnimTransition {
    std::string targetState;
    std::vector<TransitionCondition> conditions;  // AND 로직
    float exitTime = 1.0f;       // 0~1, 현재 애니메이션 어디서 전환 허용
    bool hasExitTime = false;     // exitTime 사용 여부
    float transitionDuration = 0.0f;  // 2D에서는 보통 0 (즉시 전환)
    int priority = 0;             // 동시 충족 시 우선순위
};

// ── 상태 (State) ──
struct AnimState {
    std::string name;
    Animation* animation;         // 기존 Animation 클래스 재사용
    float speed = 1.0f;           // 재생 속도 배율
    std::vector<AnimTransition> transitions;

    // 상태 진입/퇴장 콜백
    std::function<void()> onEnter;
    std::function<void()> onExit;
};

// ── 레이어 (Layer) ──
struct AnimLayer {
    std::string name;
    std::unordered_map<std::string, AnimState> states;
    std::string defaultState;
    std::string currentState;
    float weight = 1.0f;          // 레이어 가중치
    // 2D에서 blendMode는 Override 또는 Additive
    enum class BlendMode { Override, Additive } blendMode = BlendMode::Override;
};

// ── 애니메이터 컴포넌트 ──
class Animator : public Component {
    COMPONENT_TYPE(Animator)
public:
    // 파라미터 접근
    void SetBool(const std::string& name, bool value);
    void SetFloat(const std::string& name, float value);
    void SetInt(const std::string& name, int value);
    void SetTrigger(const std::string& name);

    bool GetBool(const std::string& name) const;
    float GetFloat(const std::string& name) const;
    int GetInt(const std::string& name) const;

    // 상태 관리
    void AddState(const std::string& layerName, AnimState state);
    void AddTransition(const std::string& layerName,
                       const std::string& fromState, AnimTransition transition);
    void AddAnyStateTransition(const std::string& layerName,
                               AnimTransition transition);

    // 레이어
    void AddLayer(const std::string& name, float weight = 1.0f);

    // 현재 상태 쿼리
    const std::string& GetCurrentStateName(int layerIndex = 0) const;
    bool IsInTransition(int layerIndex = 0) const;

    void Update(float dt) override;
    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    std::vector<AnimLayer> layers;
    std::unordered_map<std::string, AnimParameter> parameters;
    std::vector<AnimTransition> anyStateTransitions;  // 레이어별로 관리

    void EvaluateTransitions(AnimLayer& layer);
    bool CheckConditions(const std::vector<TransitionCondition>& conditions);
    void TransitionTo(AnimLayer& layer, const std::string& targetState);
    void ConsumeAllTriggers();
};
```

**Update 루프 알고리즘**:

```
매 프레임:
1. 각 레이어에 대해:
   a. AnyState 전환 조건 평가 (우선순위 최고)
   b. 현재 상태의 전환 목록을 우선순위 순서로 평가
   c. exitTime 조건이 있으면 현재 애니메이션 진행률 확인
   d. 모든 조건(AND)이 만족된 첫 번째 전환 실행
   e. 전환 시 onExit → 상태 전환 → onEnter 호출
   f. 현재 상태의 Animation.Update(dt * speed) 호출
2. Trigger 파라미터 자동 소비 (한 프레임만 유효)
3. SpriteRenderer에 현재 프레임 적용
```

**BlendTree (2D 간소화 버전)**:

```cpp
// 2D에서 BlendTree는 주로 방향 선택에 사용
struct BlendTree1D {
    std::string parameterName;
    struct Entry {
        float threshold;       // 파라미터 값 기준점
        Animation* animation;
    };
    std::vector<Entry> entries;  // threshold 순 정렬

    Animation* Evaluate(float paramValue) {
        // 가장 가까운 threshold의 애니메이션 선택
        // (2D에서는 보간이 아닌 선택이 일반적)
    }
};

struct BlendTree2D {
    std::string paramX, paramY;
    struct Entry {
        Vector2 position;      // 2D 파라미터 공간의 위치
        Animation* animation;
    };
    std::vector<Entry> entries;

    Animation* Evaluate(float x, float y) {
        // 가장 가까운 position의 애니메이션 선택
        // 8방향 이동에 최적
    }
};
```

### 1.4 복잡도 및 의존성

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Large** (2~3주) |
| **핵심 파일** | `ECS/Components/Animator.h/.cpp` |
| **의존성** | Animation, SpriteRenderer, Component, SpriteSheet |
| **데이터 구조** | FSM (유한 상태 머신), 조건부 전환 그래프 |
| **알고리즘** | 전환 조건 평가 (AND 로직), 우선순위 정렬, BlendTree nearest-neighbor |
| **직렬화** | 상태 그래프를 JSON으로 저장/로드 (에디터 연동 필수) |

---

## 2. 2D 내비게이션 / 길찾기

### 2.1 개요 및 필요성

**무엇을 하는가**: 게임 오브젝트가 장애물을 피해 목표 지점까지 자동으로 이동하는 시스템. RTS, RPG, 타워디펜스 등 대부분의 2D 게임에서 필요.

**왜 필수적인가**: 적 AI, NPC 행동, 동적 경로 재계산은 게임 개발의 핵심 기능. 없으면 모든 AI 이동 로직을 수동으로 작성해야 하며, 복잡한 맵에서는 사실상 불가능.

### 2.2 Unity의 구현 방식

```
Unity NavMesh 시스템 (3D 기반이나 2D 어댑터 존재):

NavMesh (Asset)             - 이동 가능 영역의 메시
NavMeshSurface (Component)  - NavMesh 생성/베이크 담당
NavMeshAgent (Component)    - 경로 추종하는 에이전트
NavMeshObstacle (Component) - 동적 장애물
NavMeshLink (Component)     - 불연속 영역 연결 (점프 포인트 등)

핵심 API:
- NavMeshAgent.SetDestination(Vector3 target)
- NavMeshAgent.speed / acceleration / stoppingDistance
- NavMeshAgent.remainingDistance / pathPending / hasPath
- NavMesh.CalculatePath(source, target, areaMask, path)
- NavMeshAgent.avoidancePriority (에이전트 간 회피 우선순위)
```

**Unity 2D 내비게이션의 실제 현황**:
- Unity의 NavMesh는 본질적으로 3D 기반 (XZ 평면)
- 2D 게임에서는 보통 커뮤니티 에셋(A* Pathfinding Project)이나 그리드 기반 직접 구현 사용
- 타일맵 기반 2D 게임은 그리드 A*가 가장 적합

### 2.3 권장 구현 방법 (C++17 ECS)

**2D 게임에 최적화된 접근법**: 타일맵 기반 A* + 조향 행동(Steering Behavior)

**핵심 자료구조**:

```cpp
// ── 내비게이션 그리드 ──
struct NavCell {
    int x, y;
    bool walkable = true;
    float cost = 1.0f;        // 이동 비용 (물, 진흙 등)
    uint32_t areaMask = 0xFFFFFFFF;  // 영역 타입 비트마스크
};

class NavGrid {
public:
    NavGrid(int width, int height, float cellSize);

    // Tilemap에서 자동 생성
    void BuildFromTilemap(const Tilemap& tilemap);

    // 동적 업데이트
    void SetWalkable(int x, int y, bool walkable);
    void SetCost(int x, int y, float cost);

    // 쿼리
    bool IsWalkable(int x, int y) const;
    float GetCost(int x, int y) const;
    NavCell* GetCell(int x, int y);

    // 좌표 변환
    Vector2 CellToWorld(int x, int y) const;
    void WorldToCell(const Vector2& world, int& x, int& y) const;

    // 이웃 검색 (4방향 또는 8방향)
    std::vector<NavCell*> GetNeighbors(int x, int y, bool diagonal = true);

private:
    int width, height;
    float cellSize;
    std::vector<NavCell> cells;
};

// ── A* 경로 탐색 ──
struct PathNode {
    int x, y;
    float gCost;        // 시작점에서의 실제 비용
    float hCost;        // 목표까지의 휴리스틱 비용
    float fCost() const { return gCost + hCost; }
    PathNode* parent = nullptr;
};

class Pathfinder {
public:
    explicit Pathfinder(NavGrid* grid);

    // 경로 검색 (결과는 월드 좌표 벡터)
    bool FindPath(const Vector2& start, const Vector2& end,
                  std::vector<Vector2>& outPath,
                  uint32_t areaMask = 0xFFFFFFFF);

    // 경로 최적화 (불필요한 중간점 제거)
    void SmoothPath(std::vector<Vector2>& path);

    // 설정
    void SetMaxIterations(int max) { maxIterations = max; }
    void SetDiagonalMovement(bool allow) { allowDiagonal = allow; }

private:
    NavGrid* grid;
    int maxIterations = 1000;
    bool allowDiagonal = true;

    float Heuristic(int x1, int y1, int x2, int y2);
    // 오프라인 방식: Manhattan/Octile/Euclidean 중 선택
};

// ── NavAgent 컴포넌트 ──
class NavAgent : public Component {
    COMPONENT_TYPE(NavAgent)
public:
    // 목표 설정
    bool SetDestination(const Vector2& target);
    void Stop();

    // 속성
    float speed = 100.0f;
    float acceleration = 200.0f;
    float stoppingDistance = 5.0f;
    float turnSpeed = 360.0f;     // 회전 속도 (도/초)
    int avoidancePriority = 50;   // 0(최고) ~ 99(최저)
    uint32_t areaMask = 0xFFFFFFFF;

    // 상태 쿼리
    bool HasPath() const { return !currentPath.empty(); }
    bool IsPathPending() const { return pathPending; }
    float GetRemainingDistance() const;
    const Vector2& GetVelocity() const { return velocity; }

    void Update(float dt) override;

private:
    std::vector<Vector2> currentPath;
    int currentWaypoint = 0;
    Vector2 velocity;
    bool pathPending = false;

    void FollowPath(float dt);
    void AvoidObstacles(Vector2& desiredVelocity);
};
```

**A* 알고리즘 구현 핵심**:

```
A* 알고리즘 (의사코드):
1. openSet에 시작 노드 추가 (우선순위 큐, fCost 기준)
2. closedSet 초기화 (방문 완료 집합)
3. while openSet이 비어있지 않고 iteration < maxIterations:
   a. current = openSet에서 fCost 최소 노드 꺼내기
   b. current == 목표 → 경로 구성 후 반환
   c. current를 closedSet에 추가
   d. current의 각 이웃 neighbor에 대해:
      - neighbor가 closedSet에 있거나 walkable하지 않으면 건너뛰기
      - tentativeG = current.gCost + neighbor.cost + 이동비용
      - tentativeG < neighbor.gCost이면:
        - neighbor.parent = current
        - neighbor.gCost = tentativeG
        - neighbor.hCost = Heuristic(neighbor, goal)
        - neighbor가 openSet에 없으면 추가
4. 경로 없음 반환

휴리스틱 함수:
- 4방향: Manhattan Distance = |dx| + |dy|
- 8방향: Octile Distance = max(|dx|,|dy|) + (sqrt(2)-1) * min(|dx|,|dy|)
```

**경로 평활화 (Path Smoothing)**:

```cpp
// Funnel Algorithm (Simple String-Pulling)
void Pathfinder::SmoothPath(std::vector<Vector2>& path) {
    if (path.size() <= 2) return;

    std::vector<Vector2> smoothed;
    smoothed.push_back(path[0]);

    int current = 0;
    while (current < (int)path.size() - 1) {
        int farthest = current + 1;
        // 직선 경로가 장애물을 통과하지 않는 가장 먼 점 탐색
        for (int i = current + 2; i < (int)path.size(); i++) {
            if (HasLineOfSight(path[current], path[i])) {
                farthest = i;
            }
        }
        smoothed.push_back(path[farthest]);
        current = farthest;
    }
    path = smoothed;
}
```

**장애물 회피 (Steering Behavior)**:

```cpp
void NavAgent::AvoidObstacles(Vector2& desiredVelocity) {
    // RVO (Reciprocal Velocity Obstacles) 간소화 버전
    // 다른 NavAgent들과의 충돌 회피

    auto& scene = GetScene();
    for (auto* other : scene.GetComponentsOfType<NavAgent>()) {
        if (other == this) continue;
        if (other->avoidancePriority > this->avoidancePriority) continue;

        Vector2 toOther = other->GetPosition() - GetPosition();
        float dist = toOther.Length();
        float minDist = 32.0f;  // 최소 간격

        if (dist < minDist && dist > 0.001f) {
            Vector2 avoidDir = (toOther * -1.0f).Normalized();
            float strength = (minDist - dist) / minDist;
            desiredVelocity += avoidDir * strength * speed;
        }
    }
}
```

### 2.4 성능 최적화 기법

| 기법 | 설명 |
|------|------|
| **JPS (Jump Point Search)** | 균일 비용 그리드에서 A*보다 10~100배 빠름. 대칭 경로를 스킵 |
| **계층적 경로탐색 (HPA*)** | 큰 맵을 섹터로 나누고, 섹터 간 경로를 미리 계산 |
| **Flowfield** | 다수의 에이전트가 같은 목표로 이동할 때 최적. 셀별 방향 벡터 저장 |
| **비동기 경로탐색** | 경로 계산을 별도 스레드로 분리. 프레임별 탐색 iteration 제한 |
| **경로 캐싱** | 동일 출발-도착 쌍의 결과를 캐시 (맵 변경 시 무효화) |

### 2.5 복잡도 및 의존성

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Large** (2~3주) |
| **핵심 파일** | `Navigation/NavGrid.h/.cpp`, `Navigation/Pathfinder.h/.cpp`, `ECS/Components/NavAgent.h/.cpp` |
| **의존성** | Tilemap, Transform, Collision, Component |
| **데이터 구조** | 2D 배열 (그리드), 우선순위 큐 (이진 힙), 연결 리스트 (경로) |
| **알고리즘** | A*, JPS (선택), 경로 평활화, RVO 장애물 회피 |

---

## 3. 태그 & 레이어 시스템

### 3.1 개요 및 필요성

**무엇을 하는가**: 게임 오브젝트에 태그(Tag, 문자열 식별자)와 레이어(Layer, 비트 기반 분류)를 부여하여 식별, 충돌 필터링, 렌더링 순서를 제어하는 시스템.

**왜 필수적인가**: "Player" 태그로 플레이어를 빠르게 찾고, 레이어 충돌 매트릭스로 "PlayerBullet은 Player와 충돌하지 않음" 같은 규칙을 코드 없이 설정. 이 없이는 모든 충돌 필터링을 하드코딩해야 한다.

### 3.2 Unity의 구현 방식

```
Unity Tag 시스템:
- 미리 정의된 태그: "Untagged", "Respawn", "Finish", "EditorOnly", "MainCamera", "Player"
- 커스텀 태그 추가 가능 (프로젝트 설정)
- 오브젝트당 하나의 태그
- API: gameObject.tag = "Enemy"
       gameObject.CompareTag("Enemy")  // GC 없는 비교
       GameObject.FindWithTag("Player")
       GameObject.FindGameObjectsWithTag("Enemy")

Unity Layer 시스템:
- 32개 레이어 (0~31), 비트마스크 기반
- 미리 정의된 레이어: Default(0), TransparentFX(1), Ignore Raycast(2),
                       Water(4), UI(5)
- 커스텀 레이어 8~31 사용 가능
- API: gameObject.layer = LayerMask.NameToLayer("Enemy")
       LayerMask mask = LayerMask.GetMask("Player", "Enemy")

충돌 매트릭스 (Physics2D.Layer Collision Matrix):
- 32x32 비트 매트릭스 (대칭)
- Physics Settings에서 체크박스로 "어떤 레이어가 어떤 레이어와 충돌하는지" 설정
- Physics2D.GetIgnoreLayerCollision(layer1, layer2)
- Physics2D.SetIgnoreLayerCollision(layer1, layer2, ignore)

Sorting Layer (렌더링 전용):
- 렌더링 순서를 결정하는 별도의 레이어 시스템
- SpriteRenderer.sortingLayerName / sortingOrder
- 예: Background(0) → Tilemap(1) → Characters(2) → Effects(3) → UI(4)
```

### 3.3 권장 구현 방법 (C++17 ECS)

```cpp
// ── 태그 시스템 ──
class TagManager {
public:
    static TagManager& Get() {
        static TagManager instance;
        return instance;
    }

    // 태그 등록
    void RegisterTag(const std::string& tag) {
        registeredTags.insert(tag);
    }

    bool IsValidTag(const std::string& tag) const {
        return registeredTags.count(tag) > 0;
    }

    // 태그로 오브젝트 검색 (O(1) 평균)
    GameObject* FindWithTag(const std::string& tag);
    std::vector<GameObject*> FindAllWithTag(const std::string& tag);

    // 내부: 오브젝트 태그 변경 시 인덱스 갱신
    void OnTagChanged(GameObject* obj, const std::string& oldTag,
                      const std::string& newTag);

private:
    std::unordered_set<std::string> registeredTags;
    // 태그 → 오브젝트 목록 역인덱스 (빠른 검색용)
    std::unordered_map<std::string, std::vector<GameObject*>> tagIndex;
};

// ── 레이어 시스템 ──
class LayerManager {
public:
    static constexpr int MAX_LAYERS = 32;

    static LayerManager& Get() {
        static LayerManager instance;
        return instance;
    }

    // 레이어 이름 ↔ 번호 매핑
    void SetLayerName(int layer, const std::string& name);
    int NameToLayer(const std::string& name) const;
    const std::string& LayerToName(int layer) const;

    // 레이어 마스크 헬퍼
    static uint32_t GetMask(std::initializer_list<int> layers) {
        uint32_t mask = 0;
        for (int l : layers) mask |= (1u << l);
        return mask;
    }

    // 충돌 매트릭스
    void SetLayerCollision(int layer1, int layer2, bool collide) {
        if (collide) {
            collisionMatrix[layer1] |= (1u << layer2);
            collisionMatrix[layer2] |= (1u << layer1);
        } else {
            collisionMatrix[layer1] &= ~(1u << layer2);
            collisionMatrix[layer2] &= ~(1u << layer1);
        }
    }

    bool ShouldCollide(int layer1, int layer2) const {
        return (collisionMatrix[layer1] & (1u << layer2)) != 0;
    }

    // 전체 충돌 매트릭스 접근 (에디터 GUI용)
    uint32_t GetCollisionMask(int layer) const { return collisionMatrix[layer]; }

private:
    std::string layerNames[MAX_LAYERS] = { "Default" };
    uint32_t collisionMatrix[MAX_LAYERS];  // 기본값: 모든 레이어 충돌

    LayerManager() {
        // 초기화: 모든 레이어끼리 충돌
        for (int i = 0; i < MAX_LAYERS; i++) {
            collisionMatrix[i] = 0xFFFFFFFF;
        }
        layerNames[0] = "Default";
        layerNames[5] = "UI";
    }
};

// ── 소팅 레이어 (렌더링 순서) ──
struct SortingLayer {
    std::string name;
    int order;  // 낮을수록 먼저 렌더링 (뒤에 그려짐)
};

class SortingLayerManager {
public:
    static SortingLayerManager& Get() {
        static SortingLayerManager instance;
        return instance;
    }

    void AddLayer(const std::string& name, int order);
    void RemoveLayer(const std::string& name);
    int GetLayerOrder(const std::string& name) const;

    // 렌더링 정렬 키 생성
    // sortKey = (sortingLayerOrder << 16) | (sortingOrder & 0xFFFF)
    int32_t GetSortKey(const std::string& layerName, int orderInLayer) const;

private:
    std::vector<SortingLayer> layers = { {"Default", 0} };
};

// ── GameObject 확장 ──
// GameObject에 tag와 layer 필드 추가:
class GameObject {
    // ... 기존 멤버 ...
    std::string tag = "Untagged";
    int layer = 0;  // 0 = Default

public:
    void SetTag(const std::string& newTag);
    const std::string& GetTag() const { return tag; }
    bool CompareTag(const std::string& otherTag) const {
        return tag == otherTag;  // 문자열 비교지만 짧은 태그는 SSO 활용
    }

    void SetLayer(int newLayer) { layer = newLayer; }
    int GetLayer() const { return layer; }
    uint32_t GetLayerMask() const { return 1u << layer; }
};
```

**충돌 시스템 통합**:

```cpp
// 충돌 검사 시 레이어 필터링 적용
void PhysicsSystem::CheckCollisions() {
    auto& layerMgr = LayerManager::Get();

    for (int i = 0; i < colliders.size(); i++) {
        for (int j = i + 1; j < colliders.size(); j++) {
            int layerA = colliders[i]->GetGameObject()->GetLayer();
            int layerB = colliders[j]->GetGameObject()->GetLayer();

            // 레이어 충돌 매트릭스 확인
            if (!layerMgr.ShouldCollide(layerA, layerB)) continue;

            // 실제 충돌 검사 수행
            if (colliders[i]->CheckCollision(colliders[j])) {
                // 충돌 콜백 호출
            }
        }
    }
}
```

### 3.4 복잡도 및 의존성

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Small** (3~5일) |
| **핵심 파일** | `Core/TagManager.h/.cpp`, `Core/LayerManager.h/.cpp`, `Rendering/SortingLayerManager.h/.cpp` |
| **의존성** | GameObject, Collision, SpriteRenderer |
| **데이터 구조** | 비트마스크 (uint32_t), 해시맵 (태그 인덱스), 정렬 배열 |
| **알고리즘** | 비트 연산 (AND, OR, NOT), 해시 기반 검색 |

---

## 4. 추가 빌트인 컴포넌트

### 4.1 Rigidbody2D (물리 바디)

**무엇을 하는가**: 오브젝트에 물리 시뮬레이션을 적용. 중력, 속도, 힘, 토크, 질량, 마찰 등을 처리.

**Unity API**:
```
Rigidbody2D.velocity / angularVelocity
Rigidbody2D.AddForce(force, ForceMode2D.Impulse)
Rigidbody2D.AddTorque(torque)
Rigidbody2D.mass / drag / angularDrag / gravityScale
Rigidbody2D.bodyType: Dynamic / Kinematic / Static
Rigidbody2D.constraints: FreezePositionX/Y, FreezeRotation
Rigidbody2D.interpolation: None / Interpolate / Extrapolate
Rigidbody2D.collisionDetectionMode: Discrete / Continuous
```

**권장 구현**:

```cpp
enum class BodyType { Dynamic, Kinematic, Static };

struct RigidbodyConstraints {
    bool freezePositionX = false;
    bool freezePositionY = false;
    bool freezeRotation = false;
};

class Rigidbody2D : public Component {
    COMPONENT_TYPE(Rigidbody2D)
public:
    // 물리 속성
    BodyType bodyType = BodyType::Dynamic;
    float mass = 1.0f;
    float drag = 0.0f;           // 선형 저항
    float angularDrag = 0.05f;   // 회전 저항
    float gravityScale = 1.0f;
    RigidbodyConstraints constraints;

    // 상태
    Vector2 velocity;
    float angularVelocity = 0.0f;

    // 힘 적용
    void AddForce(const Vector2& force, bool impulse = false);
    void AddTorque(float torque);
    void SetVelocity(const Vector2& vel) { velocity = vel; }

    // 물리 업데이트 (FixedUpdate에서 호출)
    void PhysicsUpdate(float fixedDt);

    // 충돌 응답 (CollisionResolver에서 호출)
    void ResolveCollision(const CollisionResult& result,
                          Rigidbody2D* other);

private:
    Vector2 forceAccumulator;
    float torqueAccumulator = 0.0f;

    void IntegrateForces(float dt);
    void IntegrateVelocity(float dt);
};
```

**물리 업데이트 루프 (Semi-implicit Euler)**:

```
PhysicsUpdate(fixedDt):
1. Static body면 건너뛰기
2. Kinematic body면 속도 기반 이동만 (힘 무시)
3. Dynamic body:
   a. 중력 적용: forceAccumulator += Vector2(0, 9.81 * mass * gravityScale)
   b. 가속도 계산: acceleration = forceAccumulator / mass
   c. 속도 갱신: velocity += acceleration * fixedDt
   d. 저항 적용: velocity *= (1.0 - drag * fixedDt)
   e. 위치 갱신: position += velocity * fixedDt
   f. 회전 처리 (angular)
   g. 제약 조건 적용 (constraints)
   h. 힘 누적기 초기화
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Large** (2~3주) |
| **의존성** | Transform, Collider 계열, LayerManager |

---

### 4.2 추가 콜라이더 (CircleCollider2D, PolygonCollider2D, EdgeCollider2D, CapsuleCollider2D)

**무엇을 하는가**: 다양한 형상의 충돌 감지. BoxCollider2D만으로는 원형, 경사면, 복잡한 형태를 표현 불가.

**Unity API**:
```
CircleCollider2D: center, radius
PolygonCollider2D: points[] (꼭짓점 배열), pathCount
EdgeCollider2D: points[] (선분 체인), edgeRadius
CapsuleCollider2D: size, direction (Vertical/Horizontal)
모든 Collider2D 공통: offset, isTrigger, sharedMaterial (마찰/탄성)
```

**권장 구현**:

```cpp
// 공통 기반 클래스
class Collider2D : public Component {
public:
    Vector2 offset;
    bool isTrigger = false;
    float friction = 0.4f;
    float bounciness = 0.0f;

    virtual AABB GetBoundingBox() const = 0;  // Broad phase용
    virtual bool ContainsPoint(const Vector2& point) const = 0;
};

class CircleCollider2D : public Collider2D {
    COMPONENT_TYPE(CircleCollider2D)
public:
    float radius = 16.0f;

    Circle GetWorldCircle() const;
    AABB GetBoundingBox() const override;
    bool ContainsPoint(const Vector2& point) const override;
};

class PolygonCollider2D : public Collider2D {
    COMPONENT_TYPE(PolygonCollider2D)
public:
    std::vector<Vector2> points;  // 로컬 좌표 꼭짓점 (CCW 순서)

    std::vector<Vector2> GetWorldPoints() const;
    AABB GetBoundingBox() const override;
    bool ContainsPoint(const Vector2& point) const override;
    // SAT (Separating Axis Theorem) 기반 충돌 검사
};

class EdgeCollider2D : public Collider2D {
    COMPONENT_TYPE(EdgeCollider2D)
public:
    std::vector<Vector2> points;  // 선분 체인 꼭짓점
    float edgeRadius = 0.0f;

    AABB GetBoundingBox() const override;
    bool ContainsPoint(const Vector2& point) const override;
};

class CapsuleCollider2D : public Collider2D {
    COMPONENT_TYPE(CapsuleCollider2D)
public:
    Vector2 size = {16.0f, 32.0f};
    enum class Direction { Vertical, Horizontal } direction = Direction::Vertical;

    AABB GetBoundingBox() const override;
    bool ContainsPoint(const Vector2& point) const override;
    // 캡슐 = 2개의 반원 + 1개의 직사각형
};
```

**충돌 검사 알고리즘**:

```
충돌 감지 2단계 (Broad Phase + Narrow Phase):

Broad Phase: AABB 바운딩 박스 비교 (빠른 배제)
  - 모든 콜라이더의 GetBoundingBox()로 AABB 겹침 검사
  - 공간 분할: 균일 그리드(Uniform Grid) 또는 쿼드트리(Quadtree)

Narrow Phase: 형상별 정밀 검사
  - AABB vs AABB: 기존 구현 사용
  - Circle vs Circle: 중심 거리 비교
  - AABB vs Circle: 기존 구현 사용
  - Polygon vs Polygon: SAT (Separating Axis Theorem)
  - Polygon vs Circle: SAT 변형
  - Capsule: 두 반원 + 직사각형으로 분해

SAT (분리축 정리) 핵심:
1. 두 폴리곤의 모든 변(edge)의 법선(normal)을 축으로 사용
2. 각 축에 두 폴리곤을 투영
3. 모든 축에서 투영이 겹치면 충돌, 하나라도 분리되면 비충돌
4. 최소 침투 깊이(MTV, Minimum Translation Vector) 산출
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium~Large** (1.5~2주) |
| **의존성** | Component, Transform, Collision, LayerManager |

---

### 4.3 AudioSource / AudioListener

**무엇을 하는가**: 게임 오브젝트에 부착하여 위치 기반 사운드(Spatial Audio)를 구현. AudioListener는 "귀"의 위치, AudioSource는 "소리원"의 위치.

**Unity API**:
```
AudioSource:
  clip, volume, pitch, loop, playOnAwake
  spatialBlend (0=2D, 1=3D)
  minDistance, maxDistance
  Play(), Stop(), Pause()
  isPlaying, time

AudioListener:
  씬에 하나만 존재 (보통 메인 카메라에 부착)
  volume (전역)
```

**권장 구현**:

```cpp
class AudioSource : public Component {
    COMPONENT_TYPE(AudioSource)
public:
    std::string clipName;     // Audio 시스템에 등록된 사운드 이름
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnAwake = false;
    bool spatial = false;     // true이면 거리 감쇠 적용
    float minDistance = 50.0f;
    float maxDistance = 500.0f;

    void Play();
    void Stop();
    void Pause();
    bool IsPlaying() const;

    void OnAttach() override;
    void Update(float dt) override;

private:
    void UpdateSpatialVolume();  // AudioListener와의 거리 기반 볼륨 조절
};

class AudioListener : public Component {
    COMPONENT_TYPE(AudioListener)
public:
    static AudioListener* GetActive();  // 씬에서 활성 리스너

    void OnAttach() override;
    void OnDetach() override;

private:
    static AudioListener* activeListener;
};
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Small~Medium** (1주) |
| **의존성** | Component, Transform, Audio(기존), Camera2D |

---

### 4.4 Camera 컴포넌트

**무엇을 하는가**: 카메라를 컴포넌트로 만들어 게임 오브젝트에 부착. 다중 카메라, 미니맵, 분할 화면 등 지원.

**현재 상태**: Camera2D가 독립 클래스로 존재하지만 ECS 컴포넌트가 아님.

**Unity API**:
```
Camera:
  orthographic / orthographicSize
  rect (뷰포트 영역, 0~1 정규화)
  depth (렌더링 순서)
  cullingMask (어떤 레이어를 렌더링할지)
  backgroundColor
  targetTexture (렌더 텍스처)
```

**권장 구현**:

```cpp
class CameraComponent : public Component {
    COMPONENT_TYPE(CameraComponent)
public:
    float orthographicSize = 5.0f;  // 세로 절반 크기 (월드 단위)
    Rect viewport = {0, 0, 1, 1};   // 정규화 뷰포트 (0~1)
    int depth = 0;                   // 카메라 렌더링 순서
    uint32_t cullingMask = 0xFFFFFFFF;  // 렌더링할 레이어
    Color backgroundColor;

    // 기존 Camera2D 기능 위임
    void GetViewMatrix(mat4x4 out);
    void GetProjectionMatrix(mat4x4 out);

    // 좌표 변환
    Vector2 ScreenToWorldPoint(const Vector2& screenPos);
    Vector2 WorldToScreenPoint(const Vector2& worldPos);

    void Update(float dt) override;
    void Render() override;

    static CameraComponent* GetMain();  // depth가 가장 낮은 카메라

private:
    static std::vector<CameraComponent*> allCameras;
};
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium** (1주) |
| **의존성** | Component, Transform, Camera2D(기존, 내부 위임), LayerManager |

---

### 4.5 Canvas/UI 컴포넌트

**무엇을 하는가**: 게임 내 UI(체력바, 대화창, 인벤토리 등)를 오브젝트 위에 표시하거나 화면 고정 UI 구현.

**Unity API**:
```
Canvas:
  renderMode: ScreenSpace-Overlay / ScreenSpace-Camera / WorldSpace
  sortingOrder

CanvasScaler:
  uiScaleMode: ConstantPixelSize / ScaleWithScreenSize
  referenceResolution

RectTransform (Transform의 UI 버전):
  anchorMin, anchorMax, pivot
  sizeDelta, anchoredPosition

핵심 UI 컴포넌트:
  Image, Text/TextMeshPro, Button, Slider, ScrollView, InputField, Toggle
```

**권장 구현 (2D 엔진 최소 세트)**:

```cpp
// World Space UI에 집중 (2D 게임에서 가장 실용적)

enum class UIRenderMode {
    ScreenOverlay,    // 화면에 고정 (HUD)
    WorldSpace        // 월드 좌표에 배치 (체력바, 이름표)
};

class Canvas : public Component {
    COMPONENT_TYPE(Canvas)
public:
    UIRenderMode renderMode = UIRenderMode::ScreenOverlay;
    int sortingOrder = 0;
    Vector2 referenceResolution = {1920, 1080};

    void Render() override;
};

// RectTransform은 Transform 확장
class RectTransform : public Transform {
public:
    Vector2 anchorMin = {0.5f, 0.5f};
    Vector2 anchorMax = {0.5f, 0.5f};
    Vector2 pivot = {0.5f, 0.5f};
    Vector2 sizeDelta = {100, 30};
    Vector2 anchoredPosition;

    AABB GetWorldRect() const;
};

// 기본 UI 요소들
class UIImage : public Component { /* 텍스처 렌더링 */ };
class UIText : public Component { /* TextRenderer 활용 */ };
class UIButton : public Component {
    std::function<void()> onClick;
    // 클릭 감지: Input + RectTransform 히트테스트
};
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Very Large** (3~5주, 전체 UI 시스템) |
| **최소 세트만** | **Medium** (1.5주, WorldSpace + 기본 Text/Image) |
| **의존성** | Component, Transform, Renderer, TextRenderer, Input |

---

### 4.6 ParticleSystem 컴포넌트

**현재 상태**: ParticleEmitter가 독립 시스템으로 존재. ECS 컴포넌트로 래핑 필요.

**권장 구현**:

```cpp
class ParticleSystemComponent : public Component {
    COMPONENT_TYPE(ParticleSystemComponent)
public:
    ParticleConfig config;
    bool playOnAwake = true;

    void Play();
    void Stop();
    void Burst(int count);
    bool IsPlaying() const;

    void OnAttach() override;
    void Update(float dt) override;
    void Render() override;

    void Serialize(nlohmann::json& j) const override;
    void Deserialize(const nlohmann::json& j) override;

private:
    std::unique_ptr<ParticleEmitter> emitter;
    // Transform 위치를 emitter에 동기화
};
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Small** (2~3일, 래핑만) |
| **의존성** | Component, Transform, ParticleEmitter(기존) |

---

### 4.7 TilemapCollider2D

**무엇을 하는가**: Tilemap의 타일 정보에서 자동으로 콜라이더를 생성. 개별 타일마다 BoxCollider를 만들지 않고 인접 타일을 병합하여 최적화된 콜라이더 생성.

**Unity API**:
```
TilemapCollider2D: 타일별 개별 콜라이더 생성
CompositeCollider2D: TilemapCollider2D의 인접 콜라이더를 병합
  geometryType: Outlines / Polygons
  generationType: Synchronous / Manual
```

**권장 구현**:

```cpp
class TilemapCollider2D : public Collider2D {
    COMPONENT_TYPE(TilemapCollider2D)
public:
    enum class GeometryType { Individual, Composite };
    GeometryType geometryType = GeometryType::Composite;

    // Tilemap에서 콜라이더 생성
    void GenerateColliders(const Tilemap& tilemap);

    // 충돌 검사
    bool CheckCollision(const AABB& box) const;
    bool CheckCollision(const Circle& circle) const;

    AABB GetBoundingBox() const override;

private:
    // Individual: 타일별 AABB
    std::vector<AABB> tileColliders;

    // Composite: 병합된 폴리곤 외곽선
    std::vector<std::vector<Vector2>> outlines;

    // 병합 알고리즘: Marching Squares
    void GenerateCompositeOutlines(const Tilemap& tilemap);
};
```

**Marching Squares 병합 알고리즘**:

```
Composite 콜라이더 생성 (Marching Squares 변형):
1. solid 타일 비트맵 생성 (Tilemap.IsSolid 활용)
2. 외곽 에지 추출:
   - 각 solid 타일의 4변을 확인
   - 인접 타일이 non-solid이면 해당 변을 외곽 에지로 추가
3. 에지 체인 형성:
   - 연결된 에지를 순서대로 연결하여 폴리곤 생성
   - 여러 독립된 폴리곤(섬)이 생길 수 있음
4. 단순화:
   - 동일 방향의 연속 에지를 하나의 긴 에지로 병합
   - Douglas-Peucker 알고리즘으로 꼭짓점 수 최소화
결과: 수백 개의 개별 AABB 대신 몇 개의 폴리곤으로 충돌 처리
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium** (1~1.5주) |
| **의존성** | Collider2D, Tilemap(기존), PolygonCollider2D |

---

## 5. 오브젝트 라이프사이클

### 5.1 개요 및 필요성

**무엇을 하는가**: 게임 오브젝트의 생성(Instantiate), 파괴(Destroy), 씬 간 보존(DontDestroyOnLoad)을 안전하게 관리. 특히 프레임 중간에 오브젝트를 삭제할 때 발생하는 댕글링 포인터 문제 방지.

**왜 필수적인가**: 총알, 적, 이펙트 등의 동적 생성/파괴는 모든 게임의 기본. 프레임 루프 중간에 삭제하면 반복 중인 컬렉션이 무효화되거나 이미 해제된 메모리에 접근하는 심각한 버그 발생.

### 5.2 Unity의 구현 방식

```
Unity 핵심 API:

Instantiate(prefab):
  - 프리팹(템플릿)을 복제하여 새 오브젝트 생성
  - 컴포넌트, 자식 계층 전부 딥 카피
  - Instantiate(prefab, position, rotation)
  - Instantiate(prefab, parent)

Destroy(obj):
  - 즉시 삭제하지 않음! 현재 프레임 끝에 삭제 (Deferred Destruction)
  - Destroy(obj, delay) 로 지연 삭제 가능
  - 즉시 삭제: DestroyImmediate(obj) - 에디터 전용, 런타임에서 비추천

DontDestroyOnLoad(obj):
  - 씬 전환 시에도 파괴되지 않는 오브젝트 표시
  - 게임 매니저, 오디오 매니저, 네트워크 매니저 등에 사용
  - 별도의 "DontDestroyOnLoad" 씬으로 이동됨

Unity 라이프사이클 순서:
  Awake() → OnEnable() → Start() → FixedUpdate() → Update() →
  LateUpdate() → OnDisable() → OnDestroy()
```

### 5.3 권장 구현 방법 (C++17 ECS)

```cpp
// ── 프리팹 시스템 ──
class Prefab {
public:
    std::string name;

    // 프리팹 생성: 기존 GameObject를 템플릿으로 직렬화
    static std::shared_ptr<Prefab> CreateFromGameObject(GameObject* obj);

    // 프리팹에서 새 오브젝트 인스턴스화
    GameObject* Instantiate();
    GameObject* Instantiate(const Vector2& position, float rotation = 0.0f);
    GameObject* Instantiate(GameObject* parent);

private:
    nlohmann::json serializedData;  // 기존 SceneSerializer 활용
};

// ── 오브젝트 라이프사이클 매니저 ──
class ObjectLifecycleManager {
public:
    static ObjectLifecycleManager& Get() {
        static ObjectLifecycleManager instance;
        return instance;
    }

    // === 즉시 생성 ===
    GameObject* Instantiate(const std::string& name = "GameObject");
    GameObject* Instantiate(std::shared_ptr<Prefab> prefab);
    GameObject* Instantiate(std::shared_ptr<Prefab> prefab,
                            const Vector2& position, float rotation = 0.0f);

    // === 지연 파괴 (프레임 끝에 실행) ===
    void Destroy(GameObject* obj);
    void Destroy(GameObject* obj, float delay);

    // === 즉시 파괴 (주의: 반복 중 사용 금지) ===
    void DestroyImmediate(GameObject* obj);

    // === 씬 간 보존 ===
    void DontDestroyOnLoad(GameObject* obj);
    bool IsPersistent(GameObject* obj) const;

    // === 프레임 끝에 호출 ===
    void ProcessDestroyQueue();

    // === 활성 오브젝트 관리 ===
    const std::vector<std::unique_ptr<GameObject>>& GetAllObjects() const;
    GameObject* FindByID(unsigned int id);
    GameObject* FindByName(const std::string& name);

private:
    // 모든 활성 오브젝트의 소유권
    std::vector<std::unique_ptr<GameObject>> activeObjects;

    // 파괴 대기열
    struct DestroyRequest {
        GameObject* object;
        float remainingDelay;
    };
    std::vector<DestroyRequest> destroyQueue;

    // 씬 전환 시 보존할 오브젝트
    std::unordered_set<GameObject*> persistentObjects;

    // ID 기반 빠른 검색
    std::unordered_map<unsigned int, GameObject*> idLookup;
};
```

**지연 파괴 패턴 상세**:

```
ProcessDestroyQueue() - 매 프레임 끝 (Render 후) 호출:

1. destroyQueue 순회:
   a. remainingDelay > 0이면 dt만큼 감소 후 건너뛰기
   b. remainingDelay <= 0인 오브젝트:
      - 오브젝트의 모든 컴포넌트에 OnDisable() 호출
      - 오브젝트의 모든 컴포넌트에 OnDestroy() 호출 (추가 필요)
      - 자식 오브젝트도 재귀적으로 파괴
      - 부모에서 자식 참조 제거
      - TagManager에서 제거
      - idLookup에서 제거
      - activeObjects에서 제거 (unique_ptr 소멸 → 메모리 해제)
2. 처리 완료된 요청을 destroyQueue에서 제거

주의사항:
- 파괴 중인 오브젝트는 "마킹"하여 다른 시스템이 접근 방지
- GameObject에 bool markedForDestroy 플래그 추가
- GetComponent, Update 등에서 markedForDestroy 체크
```

**오브젝트 풀링 (Object Pooling)**:

```cpp
// 빈번한 생성/파괴 최적화 (총알, 이펙트 등)
class ObjectPool {
public:
    ObjectPool(std::shared_ptr<Prefab> prefab, int initialSize = 10);

    GameObject* Get();                  // 풀에서 비활성 오브젝트 꺼내기
    void Return(GameObject* obj);       // 파괴 대신 비활성화하여 풀에 반환

    void Prewarm(int count);           // 미리 생성해두기

private:
    std::shared_ptr<Prefab> prefab;
    std::vector<GameObject*> pool;
    int activeCount = 0;
};
```

### 5.4 복잡도 및 의존성

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium** (1~1.5주) |
| **핵심 파일** | `Core/ObjectLifecycleManager.h/.cpp`, `Core/Prefab.h/.cpp`, `Core/ObjectPool.h/.cpp` |
| **의존성** | GameObject, Component, SceneSerializer(기존), TagManager |
| **데이터 구조** | 벡터(활성 오브젝트), 큐(파괴 대기열), 해시맵(ID 검색), 풀(재활용) |
| **핵심 패턴** | Deferred Destruction, Object Pooling, Deep Clone |

---

## 6. 씬 관리 시스템

### 6.1 개요 및 필요성

**무엇을 하는가**: 여러 씬(레벨, 메뉴, 로딩 화면)의 로드/언로드, 비동기 로딩, 가산(Additive) 씬 로딩을 관리.

**왜 필수적인가**: 현재 SceneManager는 단일 씬 전환만 지원. 실제 게임에서는 HUD 씬 + 게임월드 씬을 동시에 로드하거나, 로딩 화면을 보여주면서 백그라운드로 씬 로드가 필요.

### 6.2 Unity의 구현 방식

```
Unity SceneManagement:

SceneManager.LoadScene(name, LoadSceneMode.Single):
  - 현재 씬 완전히 언로드 후 새 씬 로드
  - DontDestroyOnLoad 오브젝트는 유지

SceneManager.LoadScene(name, LoadSceneMode.Additive):
  - 현재 씬 유지한 채 추가로 씬 로드
  - HUD, 공유 UI, 서브 레벨에 활용

SceneManager.LoadSceneAsync(name):
  - AsyncOperation 반환
  - progress (0~1), isDone, allowSceneActivation
  - 로딩 바 구현 가능

SceneManager.UnloadSceneAsync(name):
  - 가산 로드된 씬을 비동기 언로드

SceneManager.GetActiveScene():
  - 현재 "활성" 씬 (새 오브젝트가 생성되는 씬)

SceneManager.SetActiveScene(scene):
  - 활성 씬 변경

SceneManager.sceneLoaded += callback:
  - 씬 로드 완료 이벤트
```

### 6.3 권장 구현 방법 (C++17 ECS)

```cpp
enum class LoadSceneMode { Single, Additive };

struct SceneData {
    std::string name;
    std::string filePath;            // JSON 파일 경로
    std::vector<std::unique_ptr<GameObject>> objects;
    bool isLoaded = false;
    bool isActive = false;
};

// 비동기 로드 상태
struct AsyncLoadOperation {
    std::string sceneName;
    float progress = 0.0f;
    bool isDone = false;
    bool allowActivation = true;
    std::function<void()> onComplete;
};

class SceneManagerEx {  // 기존 SceneManager 확장
public:
    static SceneManagerEx& Get() {
        static SceneManagerEx instance;
        return instance;
    }

    // === 동기 로드 ===
    void LoadScene(const std::string& name,
                   LoadSceneMode mode = LoadSceneMode::Single);

    // === 비동기 로드 ===
    AsyncLoadOperation* LoadSceneAsync(const std::string& name,
                                        LoadSceneMode mode = LoadSceneMode::Single);

    // === 언로드 ===
    void UnloadScene(const std::string& name);
    AsyncLoadOperation* UnloadSceneAsync(const std::string& name);

    // === 활성 씬 ===
    SceneData* GetActiveScene();
    void SetActiveScene(const std::string& name);

    // === 로드된 씬 쿼리 ===
    int GetLoadedSceneCount() const;
    SceneData* GetSceneAt(int index);
    SceneData* GetSceneByName(const std::string& name);

    // === 이벤트 ===
    using SceneCallback = std::function<void(const std::string&)>;
    void OnSceneLoaded(SceneCallback callback);
    void OnSceneUnloaded(SceneCallback callback);

    // === 프레임 업데이트 ===
    void Update(float dt);
    void Render(/* renderer params */);

private:
    std::vector<std::unique_ptr<SceneData>> loadedScenes;
    std::string activeSceneName;
    std::vector<AsyncLoadOperation> asyncOps;
    std::vector<SceneCallback> loadedCallbacks;
    std::vector<SceneCallback> unloadedCallbacks;

    // 비동기 로드: 단계별 처리
    void ProcessAsyncLoads();

    // Single 모드: 기존 씬 정리
    void UnloadAllExceptPersistent();
};
```

**비동기 로딩 구현 전략 (싱글 스레드)**:

```
방법 1: 분할 로딩 (Coroutine-like)
- JSON 파싱을 단계별로 분할
- 매 프레임 일정 개수의 오브젝트만 역직렬화
- progress = 처리된 오브젝트 / 전체 오브젝트

프레임별 처리 흐름:
  Frame 1: JSON 파일 읽기, 오브젝트 수 파악 (progress = 0.1)
  Frame 2~N: 프레임당 10~20개 오브젝트 역직렬화 (progress 갱신)
  Frame N+1: 텍스처/리소스 로딩 (progress = 0.8)
  Frame N+2: 오브젝트 초기화, OnAttach 호출 (progress = 1.0)
  Frame N+3: allowActivation이면 씬 활성화

방법 2: 스레드 기반 (고급)
- std::async / std::thread로 JSON 파싱 + 리소스 로딩
- 메인 스레드에서는 로딩 화면 렌더링
- 완료 후 메인 스레드에서 오브젝트 생성/초기화
- 주의: OpenGL 리소스(텍스처 등)는 메인 스레드에서만 생성 가능
```

### 6.4 복잡도 및 의존성

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium~Large** (1.5~2주) |
| **핵심 파일** | `Core/SceneManagerEx.h/.cpp` (기존 SceneManager 대체/확장) |
| **의존성** | SceneSerializer(기존), ObjectLifecycleManager, GameObject |
| **데이터 구조** | 벡터(로드된 씬), 콜백 리스트(이벤트), 비동기 상태 구조체 |

---

## 7. 전역 게임 상태

### 7.1 개요 및 필요성

**무엇을 하는가**: 게임 설정, 플레이어 데이터, 진행 상황 등을 저장/불러오기. 씬 간에 공유되는 데이터 관리.

**왜 필수적인가**: 게임 설정(볼륨, 해상도), 최고 점수, 캐릭터 레벨 등은 씬과 독립적으로 유지되어야 한다. 세이브/로드 시스템은 모든 게임의 기본 기능.

### 7.2 Unity의 구현 방식

```
PlayerPrefs (간단한 키-값 저장):
  PlayerPrefs.SetInt("HighScore", 1000)
  PlayerPrefs.GetInt("HighScore", defaultValue)
  PlayerPrefs.SetFloat("Volume", 0.8f)
  PlayerPrefs.SetString("PlayerName", "Hero")
  PlayerPrefs.Save()  // 디스크에 기록
  PlayerPrefs.DeleteAll()
  - 내부적으로: Windows=레지스트리, Mac=plist, Linux=XML

ScriptableObject (데이터 에셋):
  - 코드로 정의한 데이터 구조를 에셋으로 저장
  - 런타임에 읽기 전용으로 접근 (설정 데이터, 아이템 DB 등)
  - 여러 오브젝트가 같은 데이터를 참조 (메모리 효율)

JsonUtility (커스텀 직렬화):
  - JsonUtility.ToJson(obj) / JsonUtility.FromJson<T>(json)
  - 복잡한 세이브 데이터에 활용
```

### 7.3 권장 구현 방법 (C++17 ECS)

```cpp
// ── PlayerPrefs 대응: GamePrefs ──
class GamePrefs {
public:
    static GamePrefs& Get() {
        static GamePrefs instance;
        return instance;
    }

    // 기본 타입 저장/로드
    void SetInt(const std::string& key, int value);
    int GetInt(const std::string& key, int defaultValue = 0) const;

    void SetFloat(const std::string& key, float value);
    float GetFloat(const std::string& key, float defaultValue = 0.0f) const;

    void SetString(const std::string& key, const std::string& value);
    std::string GetString(const std::string& key,
                          const std::string& defaultValue = "") const;

    void SetBool(const std::string& key, bool value);
    bool GetBool(const std::string& key, bool defaultValue = false) const;

    bool HasKey(const std::string& key) const;
    void DeleteKey(const std::string& key);
    void DeleteAll();

    // 디스크 입출력
    void Save(const std::string& filepath = "prefs.json");
    void Load(const std::string& filepath = "prefs.json");

private:
    nlohmann::json data;  // 내부 저장소
};

// ── ScriptableObject 대응: DataAsset ──
// C++ 구조체를 JSON 직렬화 가능한 데이터 에셋으로 관리
class DataAsset {
public:
    virtual ~DataAsset() = default;
    virtual void Serialize(nlohmann::json& j) const = 0;
    virtual void Deserialize(const nlohmann::json& j) = 0;

    bool SaveToFile(const std::string& filepath) const;
    bool LoadFromFile(const std::string& filepath);

    const std::string& GetName() const { return name; }
    void SetName(const std::string& n) { name = n; }

private:
    std::string name;
};

// 사용 예시: 아이템 데이터베이스
struct ItemData : public DataAsset {
    struct Item {
        int id;
        std::string name;
        std::string description;
        int value;
        std::string spritePath;
    };
    std::vector<Item> items;

    void Serialize(nlohmann::json& j) const override { /* ... */ }
    void Deserialize(const nlohmann::json& j) override { /* ... */ }
};

// ── 세이브/로드 시스템 ──
class SaveSystem {
public:
    static SaveSystem& Get() {
        static SaveSystem instance;
        return instance;
    }

    // 세이브 슬롯 관리
    struct SaveSlot {
        int slotIndex;
        std::string name;
        std::string timestamp;
        nlohmann::json data;
    };

    // 세이브
    bool Save(int slotIndex, const std::string& name = "");
    bool QuickSave();

    // 로드
    bool Load(int slotIndex);
    bool QuickLoad();

    // 슬롯 정보
    std::vector<SaveSlot> GetSaveSlots() const;
    bool HasSaveData(int slotIndex) const;
    void DeleteSave(int slotIndex);

    // 직렬화 등록: 어떤 데이터를 저장할지 등록
    using Serializer = std::function<nlohmann::json()>;
    using Deserializer = std::function<void(const nlohmann::json&)>;
    void RegisterSaveData(const std::string& key,
                          Serializer serializer,
                          Deserializer deserializer);

private:
    std::string saveDirectory = "saves/";
    int maxSlots = 10;

    struct SaveDataEntry {
        Serializer serializer;
        Deserializer deserializer;
    };
    std::unordered_map<std::string, SaveDataEntry> registeredData;

    std::string GetSavePath(int slotIndex) const;
};
```

**세이브 시스템 사용 패턴**:

```cpp
// 게임 초기화 시 등록
void GameInit() {
    auto& save = SaveSystem::Get();

    // 플레이어 데이터 등록
    save.RegisterSaveData("player",
        [&]() -> nlohmann::json {
            return {
                {"health", player->health},
                {"position", {player->x, player->y}},
                {"inventory", player->SerializeInventory()},
                {"level", player->level}
            };
        },
        [&](const nlohmann::json& j) {
            player->health = j["health"];
            player->x = j["position"][0];
            player->y = j["position"][1];
            player->DeserializeInventory(j["inventory"]);
            player->level = j["level"];
        }
    );

    // 월드 상태 등록
    save.RegisterSaveData("world",
        [&]() { return worldState.Serialize(); },
        [&](const nlohmann::json& j) { worldState.Deserialize(j); }
    );
}
```

### 7.4 복잡도 및 의존성

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium** (1~1.5주) |
| **핵심 파일** | `Core/GamePrefs.h/.cpp`, `Core/DataAsset.h/.cpp`, `Core/SaveSystem.h/.cpp` |
| **의존성** | nlohmann/json (기존), 파일 I/O |
| **데이터 구조** | JSON 문서, 키-값 맵, 콜백 레지스트리 |

---

## 8. 트위닝 / 이징 시스템

### 8.1 개요 및 필요성

**무엇을 하는가**: 프로퍼티 값을 시간에 따라 부드럽게 보간. UI 애니메이션, 카메라 이동, 오브젝트 이동/회전/크기 변경, 페이드 인/아웃 등에 사용.

**왜 필수적인가**: 게임의 "주스(Juice)" - 즉, 시각적 피드백과 부드러운 전환을 만드는 핵심 도구. 코루틴이나 수동 보간 코드 없이 한 줄로 복잡한 보간 애니메이션 구현.

### 8.2 Unity의 구현 방식 (DOTween/LeanTween)

```
DOTween API 패턴:
  transform.DOMove(targetPos, duration)
  transform.DORotate(targetRot, duration)
  transform.DOScale(targetScale, duration)
  spriteRenderer.DOColor(targetColor, duration)
  spriteRenderer.DOFade(targetAlpha, duration)

체이닝:
  transform.DOMove(pos, 1f)
    .SetEase(Ease.OutBounce)     // 이징 함수
    .SetDelay(0.5f)              // 시작 지연
    .SetLoops(3, LoopType.Yoyo)  // 반복 (왕복)
    .OnComplete(() => Debug.Log("Done!"))  // 완료 콜백
    .OnUpdate(() => UpdateUI())   // 매 프레임 콜백

시퀀스 (순차/병렬 조합):
  var seq = DOTween.Sequence();
  seq.Append(transform.DOMove(pos1, 1f));   // 1초: 이동
  seq.Append(transform.DOScale(2f, 0.5f));  // 0.5초: 크기 변경
  seq.Join(spriteRenderer.DOFade(0f, 0.5f)); // 위와 동시에: 페이드
  seq.AppendInterval(0.3f);                  // 0.3초 대기
  seq.Append(transform.DOMove(pos2, 1f));    // 1초: 이동

이징 함수 종류:
  Linear, InSine, OutSine, InOutSine,
  InQuad, OutQuad, InOutQuad,
  InCubic, OutCubic, InOutCubic,
  InQuart, OutQuart, InOutQuart,
  InExpo, OutExpo, InOutExpo,
  InCirc, OutCirc, InOutCirc,
  InElastic, OutElastic, InOutElastic,
  InBack, OutBack, InOutBack,
  InBounce, OutBounce, InOutBounce
```

### 8.3 권장 구현 방법 (C++17 ECS)

```cpp
// ── 이징 함수 ──
namespace Easing {
    using EaseFunc = float(*)(float t);  // t: 0~1 → 결과: 0~1

    // 기본 함수들 (Robert Penner 공식)
    float Linear(float t) { return t; }

    float InQuad(float t)    { return t * t; }
    float OutQuad(float t)   { return t * (2.0f - t); }
    float InOutQuad(float t) {
        return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
    }

    float InCubic(float t)    { return t * t * t; }
    float OutCubic(float t)   { float u = t - 1; return u*u*u + 1; }
    float InOutCubic(float t) {
        return t < 0.5f ? 4*t*t*t : (t-1)*(2*t-2)*(2*t-2) + 1;
    }

    float InElastic(float t) {
        if (t == 0 || t == 1) return t;
        return -std::pow(2, 10*(t-1)) * std::sin((t-1.1f) * 5.0f * M_PI);
    }
    float OutElastic(float t) { return 1.0f - InElastic(1.0f - t); }

    float OutBounce(float t) {
        if (t < 1/2.75f) return 7.5625f * t * t;
        if (t < 2/2.75f) { t -= 1.5f/2.75f; return 7.5625f*t*t + 0.75f; }
        if (t < 2.5f/2.75f) { t -= 2.25f/2.75f; return 7.5625f*t*t + 0.9375f; }
        t -= 2.625f/2.75f; return 7.5625f*t*t + 0.984375f;
    }
    float InBounce(float t) { return 1.0f - OutBounce(1.0f - t); }

    float InBack(float t) {
        const float s = 1.70158f;
        return t * t * ((s + 1) * t - s);
    }
    float OutBack(float t) {
        const float s = 1.70158f;
        float u = t - 1;
        return u * u * ((s + 1) * u + s) + 1;
    }

    // ... InSine, OutSine, InExpo, OutExpo, InCirc, OutCirc 등
}

// ── 이징 타입 열거형 ──
enum class EaseType {
    Linear,
    InQuad, OutQuad, InOutQuad,
    InCubic, OutCubic, InOutCubic,
    InQuart, OutQuart, InOutQuart,
    InSine, OutSine, InOutSine,
    InExpo, OutExpo, InOutExpo,
    InCirc, OutCirc, InOutCirc,
    InElastic, OutElastic, InOutElastic,
    InBack, OutBack, InOutBack,
    InBounce, OutBounce, InOutBounce
};

Easing::EaseFunc GetEaseFunction(EaseType type);  // 룩업 테이블

// ── 트윈 (Tween) ──
enum class LoopType { None, Restart, Yoyo, Incremental };

class Tween {
public:
    // float 프로퍼티 트윈
    static Tween* To(float* target, float endValue, float duration);

    // Vector2 프로퍼티 트윈
    static Tween* To(Vector2* target, const Vector2& endValue, float duration);

    // Color 프로퍼티 트윈
    static Tween* To(Color* target, const Color& endValue, float duration);

    // 커스텀 getter/setter 트윈
    static Tween* To(std::function<float()> getter,
                     std::function<void(float)> setter,
                     float endValue, float duration);

    // 체이닝 API
    Tween* SetEase(EaseType ease);
    Tween* SetDelay(float delay);
    Tween* SetLoops(int loops, LoopType loopType = LoopType::Restart);
    Tween* OnComplete(std::function<void()> callback);
    Tween* OnUpdate(std::function<void()> callback);
    Tween* OnStart(std::function<void()> callback);

    // 제어
    void Kill(bool complete = false);
    void Pause();
    void Resume();

    // 상태
    bool IsPlaying() const;
    bool IsComplete() const;
    float GetProgress() const;

private:
    friend class TweenManager;

    enum class State { Delayed, Playing, Paused, Complete };
    State state = State::Delayed;

    float elapsed = 0.0f;
    float delay = 0.0f;
    float duration;
    EaseType easeType = EaseType::Linear;
    Easing::EaseFunc easeFunc = Easing::Linear;

    int loops = 1;            // -1 = 무한
    int completedLoops = 0;
    LoopType loopType = LoopType::None;

    std::function<void(float)> applyFunc;  // 보간값 적용 함수
    std::function<void()> onComplete;
    std::function<void()> onUpdate;
    std::function<void()> onStart;

    bool Update(float dt);  // true이면 완료
};

// ── 시퀀스 (Sequence) ──
class TweenSequence {
public:
    TweenSequence* Append(Tween* tween);     // 순차 추가
    TweenSequence* Join(Tween* tween);        // 이전과 동시 실행
    TweenSequence* AppendInterval(float dur);  // 대기 시간
    TweenSequence* AppendCallback(std::function<void()> callback);

    TweenSequence* SetLoops(int loops, LoopType type = LoopType::Restart);
    TweenSequence* OnComplete(std::function<void()> callback);

    void Kill(bool complete = false);

private:
    struct SequenceEntry {
        enum class Type { Tween, Interval, Callback };
        Type type;
        Tween* tween = nullptr;
        float interval = 0.0f;
        std::function<void()> callback;
        float startTime;     // 시퀀스 내 시작 시간
    };
    std::vector<SequenceEntry> entries;
    float totalDuration = 0.0f;
};

// ── 트윈 매니저 (전역 관리) ──
class TweenManager {
public:
    static TweenManager& Get() {
        static TweenManager instance;
        return instance;
    }

    // 매 프레임 호출
    void Update(float dt);

    // 유틸리티
    void KillAll(bool complete = false);
    void KillTweensOf(void* target);  // 특정 대상의 모든 트윈 제거
    int ActiveCount() const;

    // 내부: 트윈 등록
    void Register(std::unique_ptr<Tween> tween);

private:
    std::vector<std::unique_ptr<Tween>> activeTweens;
};
```

**편의 확장 함수 (Transform/SpriteRenderer에 직접 부착)**:

```cpp
// Transform 확장
namespace TweenExtensions {
    Tween* DOMove(Transform* t, const Vector2& target, float duration);
    Tween* DOMoveX(Transform* t, float targetX, float duration);
    Tween* DOMoveY(Transform* t, float targetY, float duration);
    Tween* DORotate(Transform* t, float targetAngle, float duration);
    Tween* DOScale(Transform* t, const Vector2& targetScale, float duration);
    Tween* DOScaleUniform(Transform* t, float targetScale, float duration);
}

// SpriteRenderer 확장
namespace TweenExtensions {
    Tween* DOColor(SpriteRenderer* sr, const Color& target, float duration);
    Tween* DOFade(SpriteRenderer* sr, float targetAlpha, float duration);
}
```

### 8.4 복잡도 및 의존성

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium** (1~1.5주) |
| **핵심 파일** | `Core/Tween.h/.cpp`, `Core/TweenManager.h/.cpp`, `Core/Easing.h` |
| **의존성** | Transform, SpriteRenderer, Vector2, Color (기존 Types.h) |
| **데이터 구조** | 벡터(활성 트윈), 함수 포인터(이징), std::function(콜백) |
| **알고리즘** | 이징 함수(수학적 보간 곡선), 시퀀스 타이밍 관리 |

---

## 9. 2D 특화 시스템

### 9.1 Sprite Shape (변형 가능 스프라이트)

**무엇을 하는가**: 스플라인 곡선으로 정의된 형태를 따라 스프라이트 텍스처를 늘리거나 타일링하여 지형, 로프, 파이프 등을 유연하게 생성.

**Unity 구현**:
```
SpriteShapeController:
  - spline: 제어점(Control Point) 배열
  - 각 제어점에 코너 설정 (Linear/Continuous/Broken)
  - 스프라이트를 스플라인을 따라 배치/스트레치
  - 내부에 채우기(Fill) 텍스처 적용 가능
  - SpriteShapeRenderer로 렌더링

SpriteShapeProfile:
  - 사용할 스프라이트 에셋 정의
  - 각도 범위별 스프라이트 매핑 (경사면 대응)
```

**권장 구현**:

```cpp
struct SplinePoint {
    Vector2 position;
    Vector2 leftTangent;   // 베지어 제어점
    Vector2 rightTangent;
    enum class Mode { Linear, Smooth, Broken } mode = Mode::Smooth;
};

class SpriteShape : public Component {
    COMPONENT_TYPE(SpriteShape)
public:
    std::vector<SplinePoint> splinePoints;
    bool closedShape = false;
    int quality = 10;          // 보간 세분화 수준
    float fillPixelsPerUnit = 16.0f;

    // 스플라인에서 메시 생성
    void RebuildMesh();

    void Render() override;

private:
    // Catmull-Rom 또는 Cubic Bezier 보간으로 메시 생성
    std::vector<Vector2> GenerateSplinePoints();
    void BuildEdgeMesh(const std::vector<Vector2>& points);
    void BuildFillMesh(const std::vector<Vector2>& points);

    // 생성된 메시 데이터
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    unsigned int VAO, VBO, EBO;
};
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Large** (2~3주) |
| **의존성** | Renderer, Texture, Component |
| **알고리즘** | 베지어/Catmull-Rom 스플라인, 메시 생성, 삼각분할(Triangulation) |

---

### 9.2 2D Effectors (물리 효과 영역)

**무엇을 하는가**: 특정 영역 안의 물리 오브젝트에 힘/효과를 적용. 바람, 물 부력, 원-웨이 플랫폼, 컨베이어 벨트 등 구현.

**Unity Effector 종류**:
```
SurfaceEffector2D:  표면을 따라 미끄러지는 힘 (컨베이어 벨트)
  - speed, speedVariation, forceScale

PointEffector2D:    중심점에서 끌어당기거나 밀어내는 힘 (블랙홀, 자석)
  - forceMagnitude, forceVariation, distanceScale
  - forceMode: Constant / InverseLinear / InverseSquared

AreaEffector2D:     영역 내 일정 방향의 힘 (바람, 물살)
  - forceAngle, forceMagnitude
  - drag, angularDrag

PlatformEffector2D: 원-웨이 플랫폼 (아래에서 위로 통과 가능)
  - surfaceArc, oneWay, sideFriction

BuoyancyEffector2D: 부력 (물 표면)
  - surfaceLevel, density, linearDrag, flowAngle
```

**권장 구현**:

```cpp
// 기반 클래스
class Effector2D : public Component {
public:
    bool useColliderMask = true;
    uint32_t colliderMask = 0xFFFFFFFF;  // 영향받는 레이어

    virtual void ApplyEffector(Rigidbody2D* body, const CollisionResult& contact) = 0;
};

class AreaEffector2D : public Effector2D {
    COMPONENT_TYPE(AreaEffector2D)
public:
    float forceAngle = 0.0f;       // 힘 방향 (도)
    float forceMagnitude = 10.0f;
    float drag = 0.0f;

    void ApplyEffector(Rigidbody2D* body, const CollisionResult& contact) override {
        float rad = forceAngle * (M_PI / 180.0f);
        Vector2 force(std::cos(rad) * forceMagnitude,
                      std::sin(rad) * forceMagnitude);
        body->AddForce(force);
        if (drag > 0) {
            body->velocity *= (1.0f - drag);
        }
    }
};

class PointEffector2D : public Effector2D {
    COMPONENT_TYPE(PointEffector2D)
public:
    float forceMagnitude = 10.0f;
    enum class ForceMode { Constant, InverseLinear, InverseSquared }
        forceMode = ForceMode::InverseSquared;

    void ApplyEffector(Rigidbody2D* body, const CollisionResult& contact) override;
};

class PlatformEffector2D : public Effector2D {
    COMPONENT_TYPE(PlatformEffector2D)
public:
    float surfaceArc = 180.0f;     // 플랫폼 표면 범위 (도)
    bool oneWay = true;

    // 충돌 필터링: 아래에서 위로의 충돌만 허용
    bool ShouldCollide(const Vector2& contactNormal, const Vector2& velocity);
};

class BuoyancyEffector2D : public Effector2D {
    COMPONENT_TYPE(BuoyancyEffector2D)
public:
    float surfaceLevel = 0.0f;     // 수면 Y 좌표
    float density = 1.0f;
    float linearDrag = 1.0f;
    float flowAngle = 0.0f;
    float flowMagnitude = 0.0f;

    void ApplyEffector(Rigidbody2D* body, const CollisionResult& contact) override;
    // 수면 아래 잠긴 비율에 따라 부력 적용
};
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium** (1~1.5주) |
| **의존성** | Rigidbody2D, Collider2D, LayerManager |

---

### 9.3 Composite Collider 2D

**무엇을 하는가**: 여러 개의 BoxCollider2D/PolygonCollider2D를 하나의 최적화된 콜라이더로 병합.

이미 위의 TilemapCollider2D 섹션(4.7)에서 Marching Squares 기반 병합 알고리즘을 기술하였으므로, 동일 접근법을 일반 콜라이더에도 적용.

```cpp
class CompositeCollider2D : public Collider2D {
    COMPONENT_TYPE(CompositeCollider2D)
public:
    enum class GeometryType { Outlines, Polygons };
    GeometryType geometryType = GeometryType::Polygons;

    // 자식 오브젝트의 Collider2D를 병합
    void GenerateGeometry();

    // 병합 결과
    const std::vector<std::vector<Vector2>>& GetPaths() const { return paths; }

private:
    std::vector<std::vector<Vector2>> paths;

    // Sutherland-Hodgman 또는 Clipper 라이브러리로 폴리곤 병합
    void MergePolygons(const std::vector<std::vector<Vector2>>& polygons);
};
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium~Large** (1.5~2주) |
| **의존성** | PolygonCollider2D, BoxCollider2D |
| **알고리즘** | 폴리곤 클리핑(Clipper lib), 외곽선 추출 |

---

### 9.4 Sprite Mask

**무엇을 하는가**: 스프라이트의 특정 영역만 보이거나 숨기는 마스킹. 시야각(fog of war), 포탈 효과, UI 스크롤 등에 활용.

**Unity 구현**:
```
SpriteMask:
  - sprite (마스크 형태)
  - alphaCutoff (알파 임계값)
  - 렌더링 범위 설정 (isCustomRangeActive, frontSortingLayer, backSortingLayer)

SpriteRenderer:
  - maskInteraction: None / VisibleInsideMask / VisibleOutsideMask
```

**권장 구현**:

```cpp
class SpriteMask : public Component {
    COMPONENT_TYPE(SpriteMask)
public:
    Texture* maskSprite = nullptr;
    float alphaCutoff = 0.5f;

    // 소팅 레이어 범위 (이 범위 내의 스프라이트만 마스킹)
    std::string frontSortingLayer;
    std::string backSortingLayer;

    void Render() override;
    // 스텐실 버퍼를 활용한 마스킹
};

// SpriteRenderer에 maskInteraction 필드 추가
enum class SpriteMaskInteraction {
    None,
    VisibleInsideMask,
    VisibleOutsideMask
};
```

**렌더링 구현 (OpenGL Stencil Buffer)**:

```
마스킹 렌더 파이프라인:
1. 스텐실 버퍼 초기화
2. SpriteMask 렌더 패스:
   a. 컬러 쓰기 비활성화 (glColorMask(false))
   b. 스텐실 쓰기 활성화 (glStencilFunc(GL_ALWAYS), glStencilOp(GL_REPLACE))
   c. 마스크 스프라이트 렌더 (알파 > cutoff인 부분만 스텐실에 기록)
3. 마스킹된 스프라이트 렌더 패스:
   a. 컬러 쓰기 활성화
   b. 스텐실 테스트 활성화
   c. VisibleInsideMask: glStencilFunc(GL_EQUAL, 1)
   d. VisibleOutsideMask: glStencilFunc(GL_NOTEQUAL, 1)
   e. 해당 스프라이트 렌더
4. 스텐실 테스트 비활성화
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium** (1주) |
| **의존성** | SpriteRenderer, Renderer(OpenGL), SortingLayerManager |

---

### 9.5 2D IK (Inverse Kinematics)

**무엇을 하는가**: 뼈대 기반 2D 애니메이션에서 끝점(손, 발)의 목표 위치를 지정하면 중간 관절 각도를 자동 계산. 캐릭터가 마우스를 향해 총을 겨누거나 지면에 발을 맞추는 등의 기능.

**Unity 구현**:
```
IKManager2D: IK 체인 관리
Limb Solver 2D: 간단한 2관절 IK (팔, 다리)
  - target (목표 Transform)
  - flip
CCD Solver 2D: 다관절 IK (촉수, 꼬리)
  - target, tolerance, maxIterations
FABRIK Solver 2D: Forward And Backward Reaching Inverse Kinematics
  - 더 자연스러운 결과, 빠른 수렴
```

**권장 구현**:

```cpp
// ── 2관절 IK (가장 일반적) ──
class IKLimb2D : public Component {
    COMPONENT_TYPE(IKLimb2D)
public:
    Transform* target = nullptr;       // 목표 위치
    Transform* rootBone = nullptr;     // 어깨/엉덩이
    Transform* midBone = nullptr;      // 팔꿈치/무릎
    Transform* tipBone = nullptr;      // 손/발
    bool flip = false;

    void Update(float dt) override {
        if (!target || !rootBone || !midBone || !tipBone) return;
        SolveTwoBoneIK();
    }

private:
    void SolveTwoBoneIK() {
        // 해석적 해법 (Analytical Two-Bone IK)
        Vector2 rootPos = rootBone->GetWorldPosition();
        Vector2 targetPos = target->GetWorldPosition();

        float upperLength = Vector2::Distance(rootPos, midBone->GetWorldPosition());
        float lowerLength = Vector2::Distance(midBone->GetWorldPosition(),
                                               tipBone->GetWorldPosition());
        float targetDist = Vector2::Distance(rootPos, targetPos);

        // 코사인 법칙으로 관절 각도 계산
        targetDist = std::min(targetDist, upperLength + lowerLength - 0.01f);

        float cosAngle = (upperLength*upperLength + targetDist*targetDist
                          - lowerLength*lowerLength)
                         / (2 * upperLength * targetDist);
        cosAngle = std::clamp(cosAngle, -1.0f, 1.0f);

        float angle = std::acos(cosAngle);
        float baseAngle = std::atan2(targetPos.y - rootPos.y,
                                      targetPos.x - rootPos.x);

        if (flip) angle = -angle;

        rootBone->SetRotation(baseAngle + angle);

        // mid bone 각도도 유사하게 계산
        // ...
    }
};

// ── CCD IK (다관절) ──
class IKCCD2D : public Component {
    COMPONENT_TYPE(IKCCD2D)
public:
    Transform* target = nullptr;
    std::vector<Transform*> chain;  // root → tip 순서
    int maxIterations = 10;
    float tolerance = 0.01f;

    void Update(float dt) override {
        if (!target || chain.size() < 2) return;
        SolveCCD();
    }

private:
    void SolveCCD() {
        for (int iter = 0; iter < maxIterations; iter++) {
            // 끝에서부터 root 방향으로 순회
            for (int i = (int)chain.size() - 2; i >= 0; i--) {
                Vector2 jointPos = chain[i]->GetWorldPosition();
                Vector2 tipPos = chain.back()->GetWorldPosition();
                Vector2 targetPos = target->GetWorldPosition();

                // joint → tip 벡터와 joint → target 벡터 사이의 각도 계산
                Vector2 toTip = tipPos - jointPos;
                Vector2 toTarget = targetPos - jointPos;

                float angle = std::atan2(toTarget.y, toTarget.x)
                            - std::atan2(toTip.y, toTip.x);

                chain[i]->SetRotation(chain[i]->GetRotation() + angle);
            }

            // 수렴 확인
            float dist = Vector2::Distance(chain.back()->GetWorldPosition(),
                                            target->GetWorldPosition());
            if (dist < tolerance) break;
        }
    }
};
```

| 항목 | 내용 |
|------|------|
| **예상 복잡도** | **Medium** (1~1.5주) |
| **의존성** | Transform (parent-child 계층), Component |
| **알고리즘** | Two-Bone Analytical IK, CCD (Cyclic Coordinate Descent), FABRIK |

---

## 10. 구현 우선순위 로드맵

### 우선순위 기준

시스템 간 **의존성 그래프**와 **게임 개발 시 실제 필요도**를 기반으로 분류.

### Tier 1: 기반 시스템 (다른 시스템의 전제조건)

| 순서 | 시스템 | 복잡도 | 예상 기간 | 이유 |
|------|--------|--------|-----------|------|
| 1 | **태그 & 레이어** | Small | 3~5일 | 충돌 필터링, 렌더링 순서, 오브젝트 식별의 기초. 거의 모든 시스템이 의존 |
| 2 | **오브젝트 라이프사이클** | Medium | 1~1.5주 | Instantiate/Destroy/Prefab은 게임 로직의 핵심. 씬 관리의 전제조건 |
| 3 | **Rigidbody2D** | Large | 2~3주 | 물리 시뮬레이션은 플랫포머, 액션 등 대부분의 2D 게임에 필수. Effector/추가 콜라이더의 전제조건 |

### Tier 2: 핵심 게임플레이 시스템

| 순서 | 시스템 | 복잡도 | 예상 기간 | 이유 |
|------|--------|--------|-----------|------|
| 4 | **애니메이션 상태 머신** | Large | 2~3주 | 캐릭터 애니메이션 관리의 핵심. 기존 Animation 클래스 위에 구축 |
| 5 | **추가 콜라이더** | Medium~Large | 1.5~2주 | CircleCollider2D, PolygonCollider2D. 다양한 형태의 충돌 처리 |
| 6 | **트위닝 / 이징** | Medium | 1~1.5주 | UI 애니메이션, 카메라 효과, 게임 피드백. 독립적으로 구현 가능 |
| 7 | **씬 관리 확장** | Medium~Large | 1.5~2주 | 비동기 로딩, 가산 씬. 오브젝트 라이프사이클에 의존 |

### Tier 3: 고급 게임플레이 시스템

| 순서 | 시스템 | 복잡도 | 예상 기간 | 이유 |
|------|--------|--------|-----------|------|
| 8 | **전역 게임 상태** | Medium | 1~1.5주 | 세이브/로드, 설정. 독립적으로 구현 가능 |
| 9 | **Camera 컴포넌트** | Medium | 1주 | 기존 Camera2D를 ECS 컴포넌트로 래핑. 다중 카메라 지원 |
| 10 | **AudioSource/Listener** | Small~Medium | 1주 | 기존 Audio를 ECS 컴포넌트로. 위치 기반 사운드 |
| 11 | **ParticleSystem 컴포넌트** | Small | 2~3일 | 기존 ParticleEmitter를 ECS로 래핑 |

### Tier 4: 전문 시스템 (필요 시 구현)

| 순서 | 시스템 | 복잡도 | 예상 기간 | 이유 |
|------|--------|--------|-----------|------|
| 12 | **2D 내비게이션** | Large | 2~3주 | AI가 필요한 게임에서만 필수 |
| 13 | **2D Effectors** | Medium | 1~1.5주 | Rigidbody2D 필요. 물리 기반 게임에서 활용 |
| 14 | **TilemapCollider2D** | Medium | 1~1.5주 | 타일맵 기반 게임에서 최적화에 중요 |
| 15 | **Sprite Mask** | Medium | 1주 | 시각 효과. 스텐실 버퍼 이해 필요 |
| 16 | **2D IK** | Medium | 1~1.5주 | 뼈대 기반 애니메이션 게임에서만 필요 |

### Tier 5: 고급 전문 시스템 (장기 목표)

| 순서 | 시스템 | 복잡도 | 예상 기간 | 이유 |
|------|--------|--------|-----------|------|
| 17 | **Canvas/UI 시스템** | Very Large | 3~5주 | 복잡도가 매우 높음. 에디터 UI(ImGui)로 대체 가능 |
| 18 | **Sprite Shape** | Large | 2~3주 | 메시 생성/삼각분할 등 고급 그래픽스 지식 필요 |
| 19 | **Composite Collider** | Medium~Large | 1.5~2주 | 폴리곤 클리핑 알고리즘 필요 |

### 의존성 그래프

```
태그 & 레이어 ──→ 충돌 필터링 ──→ Rigidbody2D ──→ Effectors
     │                                   │              │
     │                                   ↓              ↓
     │                           추가 콜라이더 ──→ Composite Collider
     │                                   │
     ↓                                   ↓
오브젝트 라이프사이클 ──→ 씬 관리 확장    TilemapCollider2D
     │
     ↓
 프리팹 시스템 ──→ 오브젝트 풀링

애니메이션 상태 머신 (독립, Animation 클래스 위에 구축)
트위닝 / 이징 (독립)
전역 게임 상태 (독립)
Camera 컴포넌트 (Camera2D 위에 구축)
AudioSource/Listener (Audio 위에 구축)
ParticleSystem 컴포넌트 (ParticleEmitter 위에 구축)
2D 내비게이션 (Tilemap, Transform에 의존)
Sprite Mask (Renderer/OpenGL에 의존)
2D IK (Transform parent-child에 의존)
Sprite Shape (Renderer에 의존)
```

### 총 예상 기간

| 범위 | 기간 | 포함 시스템 |
|------|------|------------|
| **MVP (최소)** | 6~8주 | Tier 1 + Tier 2 핵심 (태그/레이어, 라이프사이클, Rigidbody2D, Animator, 추가 콜라이더, 트위닝) |
| **표준** | 10~14주 | Tier 1~3 전체 |
| **전체** | 18~26주 | Tier 1~5 전체 |

---

## 부록: 현재 엔진에서 즉시 개선 가능한 사항

현재 코드베이스를 분석한 결과, 새로운 시스템 구현 전에 선행되면 좋은 개선사항:

### A. Component에 OnDestroy() 추가

현재 Component 기반 클래스에 OnDestroy 콜백이 없음. 오브젝트 라이프사이클 구현 전 필요.

```cpp
// Component.h에 추가
virtual void OnDestroy() {}
```

### B. Collider2D 기반 클래스 추출

현재 BoxCollider2D가 독립적으로 존재. 추가 콜라이더 구현 전에 Collider2D 공통 기반 클래스를 먼저 만들면 좋음.

```
현재:  Component → BoxCollider2D
권장:  Component → Collider2D → BoxCollider2D
                              → CircleCollider2D
                              → PolygonCollider2D
```

### C. FixedUpdate 루프 정비

현재 Script 클래스에 FixedUpdate가 선언되어 있지만, 메인 루프에서 고정 시간 간격 호출이 구현되어 있는지 확인 필요. Rigidbody2D 물리 시뮬레이션에 필수.

```cpp
// 메인 루프에 필요한 패턴:
float fixedDeltaTime = 1.0f / 60.0f;  // 60Hz 물리
float accumulator = 0.0f;

while (running) {
    float dt = GetDeltaTime();
    accumulator += dt;

    // 물리 업데이트 (고정 간격)
    while (accumulator >= fixedDeltaTime) {
        PhysicsUpdate(fixedDeltaTime);  // Rigidbody2D, 충돌 검사
        FixedUpdate(fixedDeltaTime);    // 스크립트
        accumulator -= fixedDeltaTime;
    }

    // 일반 업데이트 (매 프레임)
    Update(dt);
    LateUpdate(dt);
    Render();

    // 지연 파괴 처리
    ProcessDestroyQueue();
}
```

### D. 공간 분할 자료구조

현재 충돌 검사가 O(n^2) brute-force로 추정됨. 오브젝트 수가 늘어나면 성능 문제. Broad Phase를 위한 공간 분할 자료구조 준비가 필요.

```
권장: Uniform Grid (구현 간단, 2D에 적합)
대안: Quadtree (다양한 크기의 오브젝트에 적합)

Uniform Grid:
- 월드를 고정 크기 셀로 분할
- 각 셀에 해당 영역의 콜라이더 목록 유지
- 충돌 검사: 같은 셀 + 인접 셀의 콜라이더만 비교
- 시간 복잡도: O(n) 평균 (vs O(n^2) brute-force)
```
