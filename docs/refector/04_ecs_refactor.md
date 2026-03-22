# Phase 3: ECS / 씬 데이터 모델 개선 ✅ 완료

## 목표
씬 직렬화 데이터 손실을 먼저 해결하고, 레거시+ECS 혼합 모델을 정리한 후, 컴포넌트 조회 성능과 구조를 개선한다. Codex 원칙: "ECS 성능 개선은 씬 데이터 모델을 먼저 고정한 뒤 진행한다."

## 완료 요약

| 항목 | 변경 내용 | 핵심 파일 |
|------|----------|----------|
| 생명주기 콜백 | `SetEnabled()` → `OnEnable()`/`OnDisable()` 자동 호출, 중복 방지 | `Component.h`, `Script.h` |
| O(1) 컴포넌트 조회 | `vector` → `unordered_map<size_t, unique_ptr>`, `dynamic_cast` → `static_cast` | `GameObject.h/cpp`, `Component.h` |
| ID 복원 | `SetID()` 추가, `nextID` 충돌 방지, 로드 시 원본 ID 복원 | `GameObject.h`, `SceneSerializer.cpp` |
| enabled 직렬화 | `enabled` 필드 저장/로드, 기존 씬 파일 하위 호환 | `SceneSerializer.cpp` |
| ComponentFactory | `REGISTER_COMPONENT` 매크로 자동 등록, `ScriptManager` fallback | `ComponentFactory.h` (신규), `*.cpp` |
| 부모-자식 직렬화 | `parentId` 저장, 2-pass 로드로 계층 복원 | `SceneSerializer.cpp` |

**테스트**: 기존 4개 + 신규 4개 테스트 모두 통과 (lifecycle, ID, enabled, hierarchy).
**빌드**: 0 errors, 0 warnings (clean rebuild 검증 완료).

---

## 3.1 컴포넌트 조회 성능 개선 - dynamic_cast 제거

### 현재 문제

```cpp
// GameObject.h:37-44 - O(n) 선형 탐색 + RTTI 비용
template<typename T>
T* GetComponent() {
    for (auto& comp : components) {
        T* result = dynamic_cast<T*>(comp.get());
        if (result) return result;
    }
    return nullptr;
}
```

- `dynamic_cast`는 RTTI(Runtime Type Information) 의존, 성능 비용 높음
- 컴포넌트 수에 비례하는 O(n) 탐색
- 매 프레임 렌더링 시 SpriteRenderer 조회에서 반복 호출

### 변경 계획: 타입 인덱스 기반 O(1) 조회

```cpp
// 컴파일 타임 타입 ID 시스템
class ComponentTypeID {
    static size_t nextID;
public:
    template<typename T>
    static size_t Get() {
        static size_t id = nextID++;
        return id;
    }
};

// GameObject - 타입별 맵으로 O(1) 조회
class GameObject {
    std::unordered_map<size_t, std::unique_ptr<Component>> componentMap;

public:
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        auto id = ComponentTypeID::Get<T>();
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        comp->SetGameObject(this);
        comp->OnAttach();
        T* ptr = comp.get();
        componentMap[id] = std::move(comp);
        return ptr;
    }

    template<typename T>
    T* GetComponent() {
        auto id = ComponentTypeID::Get<T>();
        auto it = componentMap.find(id);
        if (it != componentMap.end()) {
            return static_cast<T*>(it->second.get());  // static_cast (안전)
        }
        return nullptr;
    }

    template<typename T>
    bool HasComponent() const {
        return componentMap.count(ComponentTypeID::Get<T>()) > 0;
    }
};
```

### 주의사항
- 기존 `AddComponentRaw(Component*)` 인터페이스도 타입 ID 기반으로 전환 필요
- `GetComponents()` (전체 목록 반환)은 맵 순회로 변경
- 한 GameObject에 동일 타입 컴포넌트 복수 추가 불가 (현재도 실질적으로 하나만 사용)

### 대상 파일
- `src/ECS/GameObject.h` / `src/ECS/GameObject.cpp`
- `src/ECS/Component.h`

---

## 3.2 Transform 월드 좌표 캐싱

### 현재 문제

```cpp
// Transform.cpp:10-35 - 매 호출마다 부모 체인 전체 재계산
Vector2 Transform::GetWorldPosition() const {
    // 루트까지 재귀적으로 부모 체인 순회
    // 삼각함수(cos, sin) 매번 계산
}
```

- 부모-자식 깊이에 비례하는 O(depth) 비용
- SpriteRenderer::Render()에서 매 프레임 호출
- 동일 프레임 내 여러 자식이 같은 부모 Transform을 중복 계산

### 변경 계획: Dirty Flag 패턴

```cpp
class Transform : public Component {
    // 로컬 값
    Vector2 position;
    float rotation;
    Vector2 scale;

    // 캐시된 월드 값
    mutable Vector2 cachedWorldPosition;
    mutable float cachedWorldRotation;
    mutable Vector2 cachedWorldScale;
    mutable bool isDirty = true;

    void MarkDirty() {
        isDirty = true;
        // 자식들도 dirty 전파
        if (auto* go = GetGameObject()) {
            for (auto* child : go->GetChildren()) {
                if (auto* t = child->GetComponent<Transform>()) {
                    t->MarkDirty();
                }
            }
        }
    }

public:
    void SetPosition(const Vector2& pos) {
        position = pos;
        MarkDirty();
    }

    Vector2 GetWorldPosition() const {
        if (isDirty) {
            RecalculateWorldTransform();
        }
        return cachedWorldPosition;
    }

private:
    void RecalculateWorldTransform() const {
        // 부모 체인 계산 (기존 로직)
        // 결과를 캐시에 저장
        isDirty = false;
    }
};
```

### 대상 파일
- `src/ECS/Components/Transform.h` / `src/ECS/Components/Transform.cpp`

---

## 3.3 Component Factory 패턴 추가

### 현재 문제

역직렬화 시 컴포넌트 타입 이름(문자열)으로 인스턴스를 생성하는 팩토리가 없다. SceneSerializer에서 `if/else` 체인으로 처리 중.

```cpp
// Component.h:28 - 런타임 문자열 기반 타입 이름
virtual std::string GetTypeName() const = 0;

// SceneSerializer.cpp - 수동 if/else 체인 (추정)
if (typeName == "Transform") { ... }
else if (typeName == "SpriteRenderer") { ... }
else if (typeName == "BoxCollider2D") { ... }
```

- 새 컴포넌트 추가 시 SceneSerializer 수정 필요
- 타입 이름 오타 시 컴파일 에러 없이 런타임 실패

### 변경 계획: 자동 등록 Factory

```cpp
class ComponentFactory {
    using Creator = std::function<std::unique_ptr<Component>()>;
    std::unordered_map<std::string, Creator> creators;

public:
    static ComponentFactory& Get() {
        static ComponentFactory instance;
        return instance;
    }

    template<typename T>
    void Register(const std::string& typeName) {
        creators[typeName] = []() {
            return std::make_unique<T>();
        };
    }

    std::unique_ptr<Component> Create(const std::string& typeName) {
        auto it = creators.find(typeName);
        if (it != creators.end()) {
            return it->second();
        }
        return nullptr;
    }
};

// 등록 (프로그램 시작 시)
ComponentFactory::Get().Register<Transform>("Transform");
ComponentFactory::Get().Register<SpriteRenderer>("SpriteRenderer");
ComponentFactory::Get().Register<BoxCollider2D>("BoxCollider2D");

// SceneSerializer에서 사용
auto comp = ComponentFactory::Get().Create(typeName);
if (comp) {
    comp->Deserialize(jsonData);
    gameObject->AddComponentRaw(comp.release());
}
```

### 대상 파일
- 새 파일: `src/ECS/ComponentFactory.h` / `src/ECS/ComponentFactory.cpp`
- `src/Core/SceneSerializer.cpp`
- `src/ECS/Component.h`

---

## 3.4 Component 생명주기 콜백 보강

### 현재 문제

```cpp
// Component.h:43-44 - Enable/Disable에 콜백 없음
bool IsEnabled() const { return enabled; }
void SetEnabled(bool value) { enabled = value; }  // 단순 setter
```

- 컴포넌트 비활성화 시 정리 로직 실행 불가
- 재활성화 시 초기화 로직 실행 불가
- Script 클래스에만 OnEnable/OnDisable 존재 (Component 기본 클래스에는 없음)

### 변경 계획

```cpp
class Component {
public:
    virtual void OnEnable() {}
    virtual void OnDisable() {}

    void SetEnabled(bool value) {
        if (enabled == value) return;
        enabled = value;
        if (enabled) OnEnable();
        else OnDisable();
    }
};
```

### 대상 파일
- `src/ECS/Component.h` / `src/ECS/Component.cpp`

---

## 3.5 Component::enabled 직렬화 누락

### 현재 문제

```cpp
// BoxCollider2D.cpp:50-54
void BoxCollider2D::Serialize(nlohmann::json& j) const {
    j["size"] = { size.x, size.y };
    j["offset"] = { offset.x, offset.y };
    j["isTrigger"] = isTrigger;
    // enabled 상태 미저장
}
```

모든 컴포넌트에서 `Component::enabled` 필드가 직렬화되지 않음. 씬 저장/로드 시 비활성 상태가 손실.

### 변경 계획

Component 기본 클래스에서 처리:

```cpp
// Component.h
void SerializeBase(nlohmann::json& j) const {
    j["enabled"] = enabled;
}

void DeserializeBase(const nlohmann::json& j) {
    if (j.contains("enabled")) {
        enabled = j["enabled"];
    }
}
```

SceneSerializer에서 `SerializeBase()`/`DeserializeBase()` 호출을 자동화.

### 대상 파일
- `src/ECS/Component.h` / `src/ECS/Component.cpp`
- `src/Core/SceneSerializer.cpp`

---

## 3.6 직렬화 데이터 손실 해결

> 출처: Codex 분석 - 씬 저장/로드 반복 시 정보 유실

### 현재 문제

SceneSerializer에 세 가지 데이터 손실 지점이 존재:

1. **id 미복원** - 저장 시 `id`를 기록하지만(`SceneSerializer.cpp:38`), 로드 시 복원하지 않음(`SceneSerializer.cpp:107`). 새 id가 할당되어 참조가 깨짐.

2. **부모-자식 관계 미저장** - `GameObject`는 계층 구조를 지원하지만(`GameObject.h:82`), 직렬화는 이름, id, active, components만 저장. 부모-자식 관계는 유실.

3. **스크립트 컴포넌트 복원 불가** - 인스펙터에서 스크립트를 추가할 수 있지만(`InspectorWindow.cpp:92`), 로드 시 해당 타입을 복원할 경로가 없음. SceneSerializer는 Transform, SpriteRenderer, BoxCollider2D 3가지만 하드코딩.

### 변경 계획

```cpp
// 1. id 복원
void SceneSerializer::DeserializeGameObject(const json& j, ...) {
    auto obj = std::make_shared<GameObject>(j["name"]);
    if (j.contains("id")) {
        obj->SetID(j["id"]);  // id 복원 메서드 추가 필요
    }
    // ...
}

// 2. 부모-자식 관계 저장
json SceneSerializer::SerializeGameObject(const GameObject& obj) {
    json j;
    j["id"] = obj.GetID();
    j["name"] = obj.GetName();
    j["active"] = obj.IsActive();
    j["parentId"] = obj.GetParent() ? obj.GetParent()->GetID() : -1;
    // components...
    return j;
}

// 로드 후 2-pass로 부모-자식 연결
// Pass 1: 모든 GameObject 생성
// Pass 2: parentId 기반으로 관계 복원

// 3. 스크립트 컴포넌트 - ComponentFactory (3.3)와 연동
// ScriptManager에 등록된 스크립트도 ComponentFactory에 등록
```

### 대상 파일
- `src/Core/SceneSerializer.cpp` / `src/Core/SceneSerializer.h`
- `src/ECS/GameObject.h` (SetID 추가)

---

## 3.7 GameScene 레거시+ECS 이중 상태 제거

> 출처: Codex 분석 - 씬/게임플레이 계층의 혼합 모델

### 현재 문제

```cpp
// GameScene.cpp:152-185
// playerSprite를 직접 움직인 뒤 ECS Transform에 다시 동기화
playerSprite.x += dx;
playerSprite.y += dy;
playerObject->GetComponent<Transform>()->SetPosition(playerSprite.x, playerSprite.y);
```

- `playerSprite`(레거시)와 ECS `Transform`이 이중 소스
- 타일맵, UI, 파티클도 레거시 객체로 직접 관리
- "진짜 데이터 소스"가 어디인지 불명확

### 변경 계획

1. 모든 게임 오브젝트를 ECS `GameObject` 기반으로 통일
2. `playerSprite` → `SpriteRenderer` 컴포넌트로 대체
3. 타일맵, 파티클도 컴포넌트화 검토 (TilemapRenderer, ParticleEmitter 컴포넌트)
4. GameScene은 GameObject 생성/배치만 담당, 로직은 Script에 위임

### 대상 파일
- `src/Scenes/GameScene.cpp` / `src/Scenes/GameScene.h`

---

## 3.8 Serialize/Deserialize 외부 분리 (장기)

> 출처: Gemini 분석 - Component SRP 보완

### 현재 문제

Component에 `Serialize()`/`Deserialize()`가 직접 포함되어 nlohmann/json 의존이 엔진 코어에 침투.

### 변경 계획 (장기 옵션)

```cpp
// 컴포넌트에서 Serialize/Deserialize 제거
// → ComponentSerializer에서 전담

class ComponentSerializer {
public:
    static void Serialize(const Transform& t, nlohmann::json& j);
    static void Deserialize(Transform& t, const nlohmann::json& j);

    static void Serialize(const SpriteRenderer& sr, nlohmann::json& j);
    static void Deserialize(SpriteRenderer& sr, const nlohmann::json& j);
    // ...
};
```

> 이 변경은 3.3 ComponentFactory와 함께 진행하면 자연스럽다.
> 단기적으로는 현재 패턴 유지하면서 enabled 직렬화(3.5)만 우선 해결.

---

## 3.9 EnTT 도입 검토 (장기 마일스톤)

> 출처: Gemini 분석 - Data-Oriented ECS

### 현재 아키텍처 한계

- 포인터 배열 기반 → 힙 메모리에 흩어져 Cache Miss 빈번
- 대량 오브젝트(총알, 파티클 등) 처리 시 구조적 성능 한계

### 검토 사항

**EnTT 라이브러리** (header-only, MIT 라이선스):
- 엔티티를 순수 ID(`uint32_t`)로 취급
- 컴포넌트를 Contiguous Memory(Packed Array)에 배치
- System 로직을 별도 함수/클래스로 분리

```cpp
// EnTT 스타일 예시
entt::registry registry;
auto entity = registry.create();
registry.emplace<Transform>(entity, 0.0f, 0.0f);
registry.emplace<SpriteRenderer>(entity, "player.png");

// System
auto view = registry.view<Transform, SpriteRenderer>();
for (auto [entity, transform, sprite] : view.each()) {
    // 배치 처리 - 캐시 친화적
}
```

> **현 단계 판단**: 현재 엔진 규모에서는 3.1의 타입 인덱스 개선으로 충분. EnTT는 대규모 오브젝트 처리가 필요해지는 시점에 검토. 도입 시 기존 ECS API와의 호환 레이어 필요.

---

## 3.10 InspectorGUI 커플링 분리

### 현재 문제

각 컴포넌트의 `.cpp` 파일에 `#ifdef MOLGA_EDITOR`로 ImGui 코드가 직접 포함:

- `Transform.cpp:83-100` - ImGui DragFloat2, DragFloat
- `SpriteRenderer.cpp:86-189` - 텍스처 로딩 UI, 색상 피커 등
- `BoxCollider2D.cpp:68-85` - ImGui DragFloat2, Checkbox

### 변경 계획 (장기)

Visitor 패턴으로 UI 렌더링을 컴포넌트에서 분리:

```cpp
// 컴포넌트는 데이터만 노출
class Component {
    // OnInspectorGUI() 제거
};

// 에디터 전용 Inspector 클래스
class TransformInspector {
public:
    static void Draw(Transform* transform);
};

class SpriteRendererInspector {
public:
    static void Draw(SpriteRenderer* sr);
};
```

> 이 변경은 대규모이므로 Phase 5 이후 진행 검토.
> 단기적으로는 현재 `#ifdef MOLGA_EDITOR` 방식을 유지하되, SpriteRenderer의 텍스처 로딩 로직(109-144)을 InspectorWindow 쪽으로 이동하는 것만 우선 진행.

### 대상 파일
- `src/ECS/Components/SpriteRenderer.cpp` (텍스처 로딩 로직 이동)
- `src/Editor/Windows/InspectorWindow.cpp`

---

## 체크리스트

### 우선 (데이터 모델 고정)
- [x] SceneSerializer: id 저장/복원 구현 — `SetID()` 추가, `LoadScene`/`DeserializeGameObject`에서 복원
- [x] SceneSerializer: 부모-자식 관계 저장/복원 (2-pass 로딩) — `parentId` 직렬화, Pass 2에서 `idMap` 기반 복원
- [x] SceneSerializer: 스크립트 컴포넌트 타입 저장/복원 — `ComponentFactory` + `ScriptManager` fallback
- [x] Component::enabled 직렬화 추가 — SceneSerializer에서 `enabled` 필드 저장/로드
- [ ] GameScene 레거시 객체를 ECS 기반으로 통합 → **보류** (Phase 4 범위)

### 후순 (성능/구조 개선)
- [x] ComponentTypeID 시스템 구현 — `ComponentTypeID::Get<T>()` inline 정적 ID 생성
- [x] GameObject::GetComponent를 타입 맵 기반 O(1) 조회로 변경 — `unordered_map<size_t, unique_ptr<Component>>`
- [ ] Transform에 dirty flag 패턴 적용 → **보류** (데이터 모델 안정 후 진행)
- [x] ComponentFactory 구현 및 SceneSerializer 연동 — `REGISTER_COMPONENT` 매크로 자동 등록
- [x] Component::SetEnabled에 OnEnable/OnDisable 콜백 추가 — 중복 호출 방지 포함
- [ ] SpriteRenderer 인스펙터 내 텍스처 로딩 로직을 에디터 레이어로 이동 → **보류** (Phase 5+)

### 장기 (선택)
- [ ] Serialize/Deserialize를 외부 ComponentSerializer로 분리
- [ ] OnInspectorGUI를 에디터 레이어로 완전 분리
- [ ] EnTT 도입 검토 (대규모 오브젝트 처리 필요 시)
