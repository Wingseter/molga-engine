# Phase 6: 기반 인프라 구현 계획서

> 작성일: 2026-03-23 · 개정: 2026-03-23 (외부 리뷰 반영)
> 기반: MASTER_PLAN.md Phase 6 + 설계 문서 6종
> 선행 작업: P0-1 (라이프사이클), P0-2 (Collider2D), P0-3 (FixedUpdate) 모두 완료
> 목표: 다른 모든 시스템의 전제 조건이 되는 기반 시스템 구축

---

## 변경 이력

- **v1** (2026-03-23): 초안 작성 (7개 시스템)
- **v2** (2026-03-23): Gemini/Codex 리뷰 반영
  - D2 Clone: JSON roundtrip → C++ deep copy (`virtual Component* Clone()`)
  - A2 ObjectPool: O(n) 선형 탐색 → O(1) free-list + IPoolable reset 콜백
  - A1 EventBus: `handled` 취소 기능, 비템플릿 `Unsubscribe()`, `ScopedSubscription` 추가
  - D1: 2단계 분할 (D1.1 필드+직렬화, D1.2 글로벌 인덱스는 D2 이후)
  - D1 SortKey: 음수 orderInLayer 대응 bias offset 적용
  - B0 추가: B1 전 렌더러 Begin/End 소유권 정리 (SpriteRenderer 중첩 호출 버그 수정)
  - B1 Material 직렬화: shaderName/texturePath 문자열 키로 저장, 매니저 통해 resolve
  - A1+D2: 이벤트 payload에 GameObjectID 사용, 처리 순서 명시
  - D2: 모놀리식 Manager → Registry + DestroyQueue + CloneService 분리
  - D2: 중앙 소유권 이전 대신 기존 소유 구조 위 서비스 레이어
  - D2: 부모-자식 cascade destroy 정책 정의
  - D2 Clone: ID 복제 버그 수정 (preserveIDs 옵션)
  - E1: brew install 불필요 확인, GL 테스트 분리, 아티팩트 개선
  - 실행 순서 변경: E1 → A1 → D1.1 → B0 → B1 → B2 → A2 → D2 → D1.2
  - 스크립트 현황 보수적 재기술 (Start/LateUpdate 미호출 상태)

---

## 목차

1. [현재 상태 진단](#1-현재-상태-진단)
2. [Phase 6 시스템 목록](#2-phase-6-시스템-목록)
3. [E1: CI/CD 파이프라인](#e1-cicd-파이프라인)
4. [A1: 이벤트/메시징 시스템](#a1-이벤트메시징-시스템)
5. [D1.1: 태그 & 레이어 — 필드 + 직렬화](#d11-태그--레이어--필드--직렬화)
6. [B0: 렌더러 소유권 정리](#b0-렌더러-소유권-정리)
7. [B1: 커스텀 셰이더/머티리얼 시스템](#b1-커스텀-셰이더머티리얼-시스템)
8. [B2: 디버그/기즈모 렌더링](#b2-디버그기즈모-렌더링)
9. [A2: 오브젝트 풀링](#a2-오브젝트-풀링)
10. [D2: 오브젝트 라이프사이클](#d2-오브젝트-라이프사이클)
11. [D1.2: 태그 & 레이어 — 글로벌 인덱스](#d12-태그--레이어--글로벌-인덱스)
12. [실행 순서 및 의존성](#12-실행-순서-및-의존성)
13. [검증 계획](#13-검증-계획)

---

## 1. 현재 상태 진단

### P0 완료 사항

| 작업 | 상태 | 핵심 산출물 |
|------|------|------------|
| P0-1 라이프사이클 | ✅ 완료 | OnDestroy, NotifyDestroy, 스냅샷 순회, FixedUpdateScripts/LateUpdateScripts |
| P0-2 Collider2D | ✅ 완료 | Collider2D 추상 클래스, GetWorldBounds 계약, NormalizeBounds |
| P0-3 FixedUpdate | ✅ 완료 | Time::AccumulateFixedTime, HasPendingFixedStep, ConsumeFixedStep, deltaTime 0.25s clamp |

### 스크립트 시스템 현황 (보수적 기술)

```
보유:
  - Script.h에 Start, FixedUpdate, LateUpdate, OnCollisionEnter 등 시그니처 선언됨
  - GameObject::FixedUpdateScripts() 구현 완료 (P0-1)
  - GameObject::LateUpdateScripts() 구현 완료 (P0-1)
  - main.cpp/runtime_main.cpp에서 FixedUpdateScripts() 호출 중 (P0-3)

미호출 (선언만 존재):
  - Script::Start() — 호출 엔트리포인트 없음
  - LateUpdateScripts() — main.cpp/runtime_main.cpp에서 호출하지 않음
  - OnCollision/OnTrigger 콜백 — 물리 시스템 미구현 (Phase 9)

⚠️ Phase 6에서 Start() 훅 호출을 추가하거나, D2 Instantiate 이후 초기화 타이밍을 정의해야 함
```

### 렌더링 시스템 현황

```
현재 구조:
  Shader (1개: default.vert/frag) → Renderer (Begin/End) → DrawSprite(Sprite*)

⚠️ 소유권 충돌:
  - main.cpp: renderer->Begin() → 루프 내 SpriteRenderer::RenderSprite() → renderer->End()
  - SpriteRenderer::RenderSprite() 내부에서도 Begin()/End() 호출
  - Renderer::Begin()은 assert(state == Idle) → 중첩 호출 시 assert 실패
  → B0에서 소유권 정리 필수

셰이더 유니폼:
  model(mat4), projection(mat4), uUV(vec4), uColor(vec4),
  uTexture(sampler2D), useTexture(bool)

Shader 클래스:
  - uniform location 캐싱 보유 (std::unordered_map<std::string, GLint>)
  → Material::Apply()에서 Shader::SetFloat() 등 호출 시 O(1) 캐시 활용 가능
```

### ECS 시스템 현황

```
보유:
  - ComponentTypeID (컴파일 타임 O(1) 룩업)
  - ComponentFactory (문자열→타입 팩토리)
  - SceneSerializer (JSON 직렬화/역직렬화, 2-pass 로딩)
  - 부모-자식 계층 (raw pointer)

부재:
  - EventBus: 시스템 간 디커플링 통신 없음
  - Tag/Layer: 오브젝트 분류/필터링 없음
  - ObjectPool: 빈번한 생성/파괴 최적화 없음
  - Instantiate/Destroy: 지연 파괴, 프리팹, DontDestroyOnLoad 없음
  - 오브젝트 검색 API: FindByTag, FindByName 등 없음

소유권 구조 (분산):
  - main.cpp: std::vector<std::shared_ptr<GameObject>> editorObjects
  - runtime_main.cpp: std::vector<std::shared_ptr<GameObject>> gameObjects
  - GameScene 내부: std::vector<std::shared_ptr<GameObject>> gameObjects
  - SceneManager: 씬만 관리, 씬 내부 오브젝트는 모름
  → D2는 이 분산 소유 구조 위에 서비스 레이어로 동작해야 함
```

---

## 2. Phase 6 시스템 목록

| 순서 | ID | 시스템 | 기간 | 의존성 |
|------|-----|--------|------|--------|
| 1 | E1 | CI/CD 파이프라인 | 2-3일 | 없음 |
| 2 | A1 | 이벤트/메시징 | 2-3일 | 없음 |
| 3 | D1.1 | 태그 & 레이어 (필드+직렬화) | 2-3일 | 없음 |
| 4 | B0 | 렌더러 소유권 정리 | 1-2일 | 없음 |
| 5 | B1 | 셰이더/머티리얼 | 1-1.5주 | B0 |
| 6 | B2 | 디버그 렌더링 | 3-5일 | B1 |
| 7 | A2 | 오브젝트 풀링 | 1-2일 | 없음 |
| 8 | D2 | 오브젝트 라이프사이클 | 1-1.5주 | A1 |
| 9 | D1.2 | 태그 & 레이어 (글로벌 인덱스) | 2-3일 | D2 |

---

## E1: CI/CD 파이프라인

### 예상 기간: 2-3일

### 필요 이유

초반에 깔아야 이후 작업의 회귀를 잡는다.

### 설계

`.github/workflows/ci.yml`:

```yaml
name: CI

on:
  push:
    branches: [main, phase4, phase6]
  pull_request:
    branches: [main]

jobs:
  build-macos:
    runs-on: macos-latest
    strategy:
      matrix:
        build-type: [Debug, Release]

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      # GLFW는 external/glfw에서 add_subdirectory로 빌드됨
      # brew install 불필요

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=${{ matrix.build-type }}

      - name: Build
        run: cmake --build build --config ${{ matrix.build-type }}

      - name: Run unit tests
        working-directory: build
        run: ctest --build-config ${{ matrix.build-type }} --output-on-failure --timeout 30

      - name: Upload artifacts
        if: matrix.build-type == 'Release'
        uses: actions/upload-artifact@v4
        with:
          name: molga-engine-macos
          path: |
            build/molga_engine
            build/Shaders/
            build/assets/
```

### CI에서 GL 테스트 처리

GitHub Actions macOS 러너에는 윈도우 시스템이 없어 `glfwInit()` 및 GL 컨텍스트 생성이 실패할 수 있다.

**전략**: 테스트를 두 카테고리로 분리:
- **unit tests** (GL 없음): 이벤트, 태그/레이어, 풀링, 라이프사이클, accumulator, 머티리얼 직렬화 등
- **integration tests** (GL 필요): 셰이더 컴파일, Material::Apply(), DebugDraw 스모크 테스트

CTest에서 LABEL로 구분:
```cmake
set_tests_properties(test_shader_smoke PROPERTIES LABELS "integration")
```

CI에서는 unit tests만 실행:
```yaml
- name: Run unit tests
  run: ctest --label-exclude integration --output-on-failure
```

### 산출물

```
.github/workflows/ci.yml
```

---

## A1: 이벤트/메시징 시스템

### 예상 기간: 2-3일

### 필요 이유

물리 충돌, 씬 전환, 입력, 게임 이벤트 등 모든 시스템 간 디커플링 통신의 기반.

### 설계

#### 핵심 개선 (리뷰 반영)

1. **이벤트 취소 (Consume)**: `event.handled = true`로 후속 핸들러 전파 차단
2. **비템플릿 Unsubscribe**: 이벤트 타입을 몰라도 SubscriptionID만으로 해제 가능
3. **ScopedSubscription**: RAII 기반 자동 해제
4. **이벤트 payload**: raw `GameObject*` 대신 `unsigned int objectID` 사용 (D2 지연 파괴와의 안전성)
5. **Publish 중 subscribe/unsubscribe 안전성**: snapshot 또는 pending mutation 큐

#### Event 기반 (`src/Core/Event.h`)

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

class EventTypeID {
    static inline size_t nextID = 0;
public:
    template<typename T>
    static size_t Get() {
        static size_t id = nextID++;
        return id;
    }
};

using SubscriptionID = uint64_t;

// 이벤트 기본 구조 — handled 필드로 취소 지원
struct EventBase {
    bool handled = false;
};
```

#### EventBus (`src/Core/EventBus.h`)

```cpp
class EventBus {
public:
    // 구독: 우선순위 지원 (높을수록 먼저 호출)
    template<typename EventT>
    static SubscriptionID Subscribe(std::function<void(EventT&)> callback,
                                     int priority = 0);

    // 비템플릿 해제 (이벤트 타입 불필요)
    static void Unsubscribe(SubscriptionID id);

    // 즉시 발행 — handled == true이면 후속 핸들러 중단
    template<typename EventT>
    static void Publish(EventT& event);

    // 지연 발행 (프레임 끝 일괄 처리)
    template<typename EventT>
    static void QueueEvent(EventT event);

    // 지연 이벤트 처리 (프레임 끝, DestroyQueue 전에 호출)
    static void ProcessQueue();

    static void Clear();
};

// RAII 자동 해제 핸들
class ScopedSubscription {
public:
    ScopedSubscription() = default;
    explicit ScopedSubscription(SubscriptionID id) : id(id) {}
    ~ScopedSubscription() { if (id != 0) EventBus::Unsubscribe(id); }

    ScopedSubscription(ScopedSubscription&& other) noexcept
        : id(other.id) { other.id = 0; }
    ScopedSubscription& operator=(ScopedSubscription&& other) noexcept;

    ScopedSubscription(const ScopedSubscription&) = delete;
    ScopedSubscription& operator=(const ScopedSubscription&) = delete;

private:
    SubscriptionID id = 0;
};
```

#### 이벤트 타입 (`src/Core/Events/`)

```cpp
// PhysicsEvents.h — raw pointer 대신 ID 사용
struct CollisionEvent : EventBase {
    unsigned int objectA_ID = 0;
    unsigned int objectB_ID = 0;
    Vector2 contactPoint;
    Vector2 normal;
    float penetration = 0.0f;
};

struct TriggerEvent : EventBase {
    unsigned int trigger_ID = 0;
    unsigned int other_ID = 0;
    bool entered = true;
};

// SceneEvents.h
struct SceneLoadEvent : EventBase {
    std::string sceneName;
};

struct SceneUnloadEvent : EventBase {
    std::string sceneName;
};
```

### 프레임 처리 순서 계약

```
매 프레임 끝:
  1. EventBus::ProcessQueue()        ← 지연 이벤트 먼저
  2. GameObjectRegistry::ProcessDestroyQueue()  ← 파괴는 이벤트 이후
```

이벤트 payload에 `objectID`를 사용하므로, 이벤트 처리 시 ID로 오브젝트를 조회하면 이미 파괴 예약된 오브젝트도 안전하게 감지 가능.

### 테스트 계획

`tests/test_event.cpp`:

1. **test_subscribe_publish**: 이벤트 발행 → 콜백 호출
2. **test_unsubscribe**: 해제 후 콜백 미호출
3. **test_priority_order**: 우선순위 순서 검증
4. **test_handled_cancellation**: `handled = true` → 후속 핸들러 미호출
5. **test_queue_deferred**: QueueEvent → ProcessQueue 후 호출
6. **test_scoped_subscription**: 스코프 종료 시 자동 해제
7. **test_unsubscribe_during_publish**: Publish 중 Unsubscribe 안전성

### 산출물

```
src/Core/Event.h
src/Core/EventBus.h (템플릿 구현 포함)
src/Core/EventBus.cpp
src/Core/Events/PhysicsEvents.h
src/Core/Events/SceneEvents.h
tests/test_event.cpp
```

---

## D1.1: 태그 & 레이어 — 필드 + 직렬화

### 예상 기간: 2-3일

### 설계 근거

D1을 2단계로 분할하는 이유: `TagManager` 글로벌 인덱스는 `GameObject*` raw pointer를 저장하므로, 중앙 수명 관리(D2)가 없으면 씬 종료/오브젝트 파괴 시 dangling pointer가 발생한다. 1단계에서는 데이터 필드와 직렬화만 안전하게 추가한다.

### 변경 사항

#### GameObject 확장

```cpp
// GameObject.h에 추가
class GameObject {
public:
    void SetTag(const std::string& newTag) { tag = newTag; }
    const std::string& GetTag() const { return tag; }
    bool CompareTag(const std::string& otherTag) const { return tag == otherTag; }

    void SetLayer(int newLayer) { layer = newLayer; }
    int GetLayer() const { return layer; }
    uint32_t GetLayerMask() const { return 1u << layer; }

private:
    std::string tag = "Untagged";
    int layer = 0;
};
```

#### LayerManager (`src/Core/LayerManager.h/cpp`)

레이어 이름 관리 + 충돌 매트릭스 (인덱스 없음, 순수 설정 데이터):

```cpp
class LayerManager {
public:
    static LayerManager& Get();
    static constexpr int MAX_LAYERS = 32;

    void SetLayerName(int layer, const std::string& name);
    const std::string& GetLayerName(int layer) const;
    int NameToLayer(const std::string& name) const;

    void SetLayerCollision(int layer1, int layer2, bool shouldCollide);
    bool ShouldCollide(int layer1, int layer2) const;

    static uint32_t GetMask(std::initializer_list<int> layers);

private:
    std::string layerNames[MAX_LAYERS] = {"Default"};
    uint32_t collisionMatrix[MAX_LAYERS];  // 비트마스크, 기본 all-collide
};
```

#### SortingLayerManager (`src/Rendering/SortingLayerManager.h/cpp`)

```cpp
class SortingLayerManager {
public:
    static SortingLayerManager& Get();

    int GetLayerOrder(const std::string& name) const;

    // 정렬 키: 음수 orderInLayer 안전 처리
    // bias offset: orderInLayer + 32768 → 항상 양수
    int32_t GetSortKey(const std::string& layerName, int orderInLayer) const {
        int layerOrd = GetLayerOrder(layerName);
        return (layerOrd << 16) | ((orderInLayer + 32768) & 0xFFFF);
    }

private:
    std::vector<SortingLayer> layers = {
        {"Background", 0}, {"Default", 1}, {"Foreground", 2}, {"UI", 3}
    };
};
```

#### 직렬화 (`SceneSerializer`)

```json
{
  "name": "Player",
  "tag": "Player",
  "layer": 0,
  "components": [...]
}
```

### 산출물

```
src/Core/LayerManager.h/cpp
src/Rendering/SortingLayerManager.h/cpp
tests/test_tag_layer.cpp
수정: src/ECS/GameObject.h/cpp (tag, layer 필드)
수정: src/Core/SceneSerializer.cpp (tag, layer 직렬화)
수정: src/ECS/Components/SpriteRenderer.h (sortingLayerName 추가)
수정: CMakeLists.txt
```

---

## B0: 렌더러 소유권 정리

### 예상 기간: 1-2일

### 필요 이유

현재 `SpriteRenderer::RenderSprite()` 내부에서 `renderer->Begin()`/`renderer->End()`를 호출하는데, `main.cpp`에서도 외부에서 `Begin()`/`End()`를 감싸고 있다. `Renderer::Begin()`은 `assert(state == Idle)`을 가지고 있으므로, 중첩 호출 시 디버그 빌드에서 즉시 크래시한다.

B1(Material)과 B2(DebugDraw)를 안전하게 추가하려면 이 소유권 계약을 먼저 정리해야 한다.

### 정리 계약

```
SpriteRenderer::RenderSprite()는 draw command만 준비한다.
  → Sprite 데이터(position, scale, rotation, color, texture, UV)를 Sprite 객체에 설정
  → renderer->DrawSprite(&sprite) 호출 (Begin/End는 호출하지 않음)

Begin/End는 호출자(main.cpp, runtime_main.cpp)가 소유한다.
  → renderer->Begin(shader, camera)
  → for each obj: sr->RenderSprite(renderer, shader, camera)
  → renderer->End()

Renderer::DrawSprite()가 uniform 바인딩의 최종 소유자다.
  → Material이 있으면 material->Apply() 후 DrawSprite
  → Material이 없으면 기존 직접 uniform 설정
```

### 변경 대상

- `src/ECS/Components/SpriteRenderer.cpp`: `RenderSprite()` 내부의 `Begin()`/`End()` 호출 제거
- `src/main.cpp`: 렌더링 루프에서 `Begin()/End()` 한 번만 호출하도록 정리 (이미 구조가 맞으나 확인)
- `src/runtime_main.cpp`: 동일

### 산출물

```
수정: src/ECS/Components/SpriteRenderer.cpp
수정: src/main.cpp (필요 시)
수정: src/runtime_main.cpp (필요 시)
```

---

## B1: 커스텀 셰이더/머티리얼 시스템

### 예상 기간: 1-1.5주

### 설계

#### ShaderManager (`src/Rendering/ShaderManager.h/cpp`)

```cpp
class ShaderManager {
public:
    static ShaderManager& Get();

    Shader* LoadShader(const std::string& name,
                       const std::string& vertPath,
                       const std::string& fragPath);
    Shader* GetShader(const std::string& name);
    bool HasShader(const std::string& name) const;

    void InitDefaults();  // "default", "debug", "unlit"
    bool ReloadShader(const std::string& name);
    void Clear();

private:
    std::unordered_map<std::string, std::unique_ptr<Shader>> shaders;
};
```

#### Material (`src/Rendering/Material.h/cpp`)

```cpp
class Material {
public:
    explicit Material(const std::string& shaderName = "default");

    void SetShaderName(const std::string& name);
    const std::string& GetShaderName() const { return shaderName; }
    Shader* GetShader() const;  // ShaderManager 통해 resolve

    // 프로퍼티 설정
    void SetFloat(const std::string& name, float value);
    void SetColor(const std::string& name, const Color& color);
    void SetTexturePath(const std::string& name, const std::string& path);
    void SetInt(const std::string& name, int value);

    // 셰이더에 모든 프로퍼티 바인딩
    void Apply() const;

    // 직렬화 — 문자열 키 기반 (포인터 아님)
    void Serialize(nlohmann::json& j) const;
    void Deserialize(const nlohmann::json& j);

private:
    std::string shaderName = "default";
    std::unordered_map<std::string, float> floats;
    std::unordered_map<std::string, Color> colors;
    std::unordered_map<std::string, std::string> texturePaths;  // name → path
    std::unordered_map<std::string, int> ints;
};
```

**직렬화 계약**: Material은 `shaderName`과 `texturePath` 문자열만 저장. 로드 시 `ShaderManager::GetShader(shaderName)` 및 `TextureManager::LoadTexture(path)`로 resolve.

**소유권 계약**: Material은 Shader와 Texture를 소유하지 않음 (매니저가 소유). SpriteRenderer가 `Material*`를 가지면 이것은 씬/에디터 공유 에셋을 가리킴. 객체별 인스턴스가 필요하면 복제.

#### SpriteRenderer 통합

```cpp
// SpriteRenderer.h 추가
void SetMaterial(Material* mat);
Material* GetMaterial() const { return material; }

private:
    Material* material = nullptr;  // nullptr → 기본 머티리얼 사용 (하위 호환)
```

#### 새 셰이더 파일

- `src/Shaders/debug.vert` — 위치 + 색상 정점
- `src/Shaders/debug.frag` — 정점 색상 패스스루

### 테스트 계획

**Unit tests** (GL 불필요):
- `test_material_properties`: float/color/int 설정 및 조회
- `test_material_serialization`: JSON 왕복 (shaderName, texturePaths 포함)
- `test_shader_manager_registry`: 이름으로 등록/조회

**Integration tests** (GL 필요, CI에서 제외):
- `test_shader_compile_smoke`: 셰이더 컴파일 성공
- `test_material_apply_smoke`: Apply() 호출 후 uniform 설정 검증

### 산출물

```
src/Rendering/ShaderManager.h/cpp
src/Rendering/Material.h/cpp
src/Shaders/debug.vert
src/Shaders/debug.frag
tests/test_material.cpp
수정: src/ECS/Components/SpriteRenderer.h/cpp (Material 통합)
수정: src/Core/Bootstrap.cpp (ShaderManager::Get().InitDefaults())
수정: CMakeLists.txt
```

---

## B2: 디버그/기즈모 렌더링

### 예상 기간: 3-5일

### 설계

#### DebugDraw (`src/Rendering/DebugDraw.h/cpp`)

```cpp
namespace DebugDraw {
    void Line(const Vector2& start, const Vector2& end,
              const Color& color = Color::Green(), float duration = 0.0f);
    void WireRect(const Vector2& center, const Vector2& size,
                  const Color& color = Color::Green(), float duration = 0.0f);
    void Circle(const Vector2& center, float radius,
                const Color& color = Color::Green(),
                int segments = 32, float duration = 0.0f);
    void Point(const Vector2& position, float size = 4.0f,
               const Color& color = Color::Red(), float duration = 0.0f);
    void AABB(const ::AABB& bounds,
              const Color& color = Color::Green(), float duration = 0.0f);

    void Init();       // VAO/VBO 생성
    void Shutdown();
    void Update(float dt);  // duration 기반 만료
    void Render(Camera2D* camera);  // 디버그 셰이더로 일괄 렌더링
    void Clear();

    // 커맨드 카운트 (테스트용)
    size_t GetCommandCount();
}
```

**렌더링**: 동적 VBO에 정점(x, y, r, g, b, a) 업로드 → `GL_LINES`로 일괄 드로우. 디버그 셰이더(B1에서 생성) 사용.

**통합**: `main.cpp` 렌더링 끝에 `DebugDraw::Render(camera)` + `DebugDraw::Update(dt)` 추가. `End()` 이후, ImGui 전에.

### 테스트 계획

**Unit tests** (GL 불필요):
- `test_debug_command_queue`: 커맨드 추가 후 카운트 확인
- `test_debug_duration_expire`: duration 경과 후 제거
- `test_debug_clear`: Clear 후 빈 큐

### 산출물

```
src/Rendering/DebugDraw.h/cpp
tests/test_debug_draw.cpp
수정: src/main.cpp (렌더 루프에 DebugDraw)
수정: CMakeLists.txt
```

---

## A2: 오브젝트 풀링

### 예상 기간: 1-2일

### 설계 (리뷰 반영: free-list + IPoolable)

#### IPoolable 인터페이스

```cpp
// src/Core/IPoolable.h
class IPoolable {
public:
    virtual ~IPoolable() = default;
    virtual void OnAcquire() {}   // 풀에서 꺼낼 때
    virtual void OnRelease() {}   // 풀에 반환할 때
};
```

#### ObjectPool (`src/Core/ObjectPool.h`) — 헤더 전용, O(1) free-list

```cpp
template<typename T>
class ObjectPool {
public:
    void Preallocate(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            auto obj = std::make_unique<T>();
            freeList.push_back(obj.get());
            storage.push_back(std::move(obj));
        }
    }

    T* Acquire() {
        if (freeList.empty()) {
            auto obj = std::make_unique<T>();
            freeList.push_back(obj.get());
            storage.push_back(std::move(obj));
        }
        T* obj = freeList.back();
        freeList.pop_back();
        activeCount++;
        if constexpr (std::is_base_of_v<IPoolable, T>) {
            obj->OnAcquire();
        }
        return obj;
    }

    void Release(T* obj) {
        if constexpr (std::is_base_of_v<IPoolable, T>) {
            obj->OnRelease();
        }
        freeList.push_back(obj);
        activeCount--;
    }

    size_t GetActiveCount() const { return activeCount; }
    size_t GetPoolSize() const { return storage.size(); }
    void Clear() { storage.clear(); freeList.clear(); activeCount = 0; }

private:
    std::vector<std::unique_ptr<T>> storage;
    std::vector<T*> freeList;  // O(1) acquire/release
    size_t activeCount = 0;
};
```

### 테스트 계획

`tests/test_object_pool.cpp`:

1. **test_preallocate**: 사전 할당 후 풀 크기 확인
2. **test_acquire_release**: 획득→활성 카운트 증가, 반환→감소
3. **test_reuse**: 반환된 객체 재획득 가능
4. **test_auto_expand**: 풀 소진 시 자동 확장
5. **test_poolable_callbacks**: IPoolable 구현 시 OnAcquire/OnRelease 호출 확인
6. **test_clear**: 전체 클리어

### 산출물

```
src/Core/IPoolable.h
src/Core/ObjectPool.h (헤더 전용)
tests/test_object_pool.cpp
```

---

## D2: 오브젝트 라이프사이클

### 예상 기간: 1-1.5주

### 설계 (리뷰 반영: 서비스 분리, 소유권 비침범, cascade destroy)

현재 오브젝트 소유권은 분산되어 있다 (`editorObjects`, `gameObjects`, `GameScene::gameObjects`). D2는 이 소유 구조를 변경하지 않고 **그 위에 서비스 레이어**로 동작한다.

3개 서비스로 분리:

#### 1. GameObjectRegistry (`src/Core/GameObjectRegistry.h/cpp`)

읽기 전용 인덱스. 오브젝트를 소유하지 않음.

```cpp
class GameObjectRegistry {
public:
    static GameObjectRegistry& Get();

    // 등록/해제 (오브젝트 생성/파괴 시 호출)
    void Register(GameObject* obj);
    void Unregister(GameObject* obj);

    // 검색
    GameObject* FindByID(unsigned int id);
    GameObject* FindByName(const std::string& name);

    void Clear();

private:
    std::unordered_map<unsigned int, GameObject*> idIndex;
};
```

#### 2. DestroyQueue (`src/Core/DestroyQueue.h/cpp`)

지연 파괴 서비스. 프레임 끝 일괄 처리.

```cpp
class DestroyQueue {
public:
    static DestroyQueue& Get();

    // 지연 파괴 예약
    void Destroy(GameObject* obj);
    void Destroy(GameObject* obj, float delay);

    // DontDestroyOnLoad 마킹
    void DontDestroyOnLoad(GameObject* obj);
    bool IsPersistent(GameObject* obj) const;

    // 비영속 오브젝트 전부 파괴 예약 (씬 전환 시)
    void DestroyAllNonPersistent(std::vector<std::shared_ptr<GameObject>>& container);

    // 프레임 끝 처리 (EventBus::ProcessQueue 이후 호출)
    // container: 실제 오브젝트를 소유하는 벡터 (remove 책임)
    void ProcessQueue(float dt, std::vector<std::shared_ptr<GameObject>>& container);

private:
    struct Request {
        GameObject* target;
        float delay;
    };

    std::vector<Request> queue;
    std::unordered_set<GameObject*> persistent;
};
```

#### 부모-자식 파괴 정책

```
정책: Cascade Destroy (Unity와 동일)
  - 부모 파괴 시 모든 자식도 함께 파괴 예약
  - DestroyQueue::Destroy(parent) 호출 시:
    1. parent의 모든 children을 재귀적으로 파괴 큐에 추가
    2. persistent child가 있으면 자동 orphan 처리 (parent 해제)
  - 처리 순서: 자식 먼저 → 부모 나중 (leaf-first)
```

#### 3. CloneService (`src/Core/CloneService.h/cpp`)

C++ 깊은 복사 기반 (JSON roundtrip 아님).

```cpp
class CloneService {
public:
    // 오브젝트 깊은 복사 (새 ID 할당)
    static std::shared_ptr<GameObject> Clone(GameObject* original);

    // 위치/회전 지정 클론
    static std::shared_ptr<GameObject> Clone(GameObject* original,
                                              const Vector2& position,
                                              float rotation = 0.0f);
};
```

**Component 확장**: 깊은 복사를 위해 `Component`에 `Clone()` 가상 메서드 추가.

```cpp
// Component.h에 추가
virtual std::unique_ptr<Component> Clone() const { return nullptr; }
```

각 컴포넌트가 `Clone()`을 override:

```cpp
// Transform.h
std::unique_ptr<Component> Clone() const override {
    auto clone = std::make_unique<Transform>(GetX(), GetY());
    clone->SetRotation(GetRotation());
    clone->SetScale(GetScale().x, GetScale().y);
    return clone;
}
```

**Clone 프로세스**:
1. 새 `GameObject` 생성 (새 ID 자동 할당)
2. 원본의 name, tag, layer 복사
3. 각 컴포넌트의 `Clone()` 호출 → `AddComponentRaw()`로 추가
4. 자식 재귀 복사 (ID remap 포함)
5. `GameObjectRegistry`에 등록

> **참고**: `Clone()`을 구현하지 않은 컴포넌트(nullptr 반환)는 복사에서 제외된다. 모든 기본 컴포넌트(Transform, SpriteRenderer, BoxCollider2D)에 Clone() 구현을 추가한다.

#### 편의 함수 (`src/Core/GameObjectUtils.h`)

```cpp
namespace GameObjectUtils {
    // Instantiate: 빈 오브젝트 생성 + Registry 등록
    std::shared_ptr<GameObject> Instantiate(const std::string& name = "GameObject");

    // Clone: CloneService 위임 + Registry 등록
    std::shared_ptr<GameObject> Clone(GameObject* original);

    // Destroy: DestroyQueue 위임
    void Destroy(GameObject* obj);
    void Destroy(GameObject* obj, float delay);
}
```

### 테스트 계획

`tests/test_lifecycle.cpp`:

1. **test_registry_find_by_id**: ID 검색
2. **test_registry_find_by_name**: 이름 검색
3. **test_destroy_deferred**: Destroy 후 ProcessQueue 전 존재, 후 제거
4. **test_destroy_delayed**: 딜레이 타이머 정확성
5. **test_dont_destroy_on_load**: 영속 오브젝트 생존
6. **test_cascade_destroy_children**: 부모 파괴 시 자식도 파괴
7. **test_cascade_persistent_child_orphan**: 영속 자식은 orphan 처리
8. **test_clone_new_id**: 클론 후 독립적 ID
9. **test_clone_components**: 클론 후 동일 컴포넌트 구성
10. **test_clone_children**: 자식 포함 재귀 복사

### 산출물

```
src/Core/GameObjectRegistry.h/cpp
src/Core/DestroyQueue.h/cpp
src/Core/CloneService.h/cpp
src/Core/GameObjectUtils.h
tests/test_lifecycle.cpp
수정: src/ECS/Component.h (virtual Clone())
수정: src/ECS/Components/Transform.h (Clone 구현)
수정: src/ECS/Components/SpriteRenderer.h (Clone 구현)
수정: src/ECS/Components/BoxCollider2D.h (Clone 구현)
수정: CMakeLists.txt
```

---

## D1.2: 태그 & 레이어 — 글로벌 인덱스

### 예상 기간: 2-3일

### 선행 조건: D2 (GameObjectRegistry + DestroyQueue)

D2의 수명 관리가 있으므로 raw pointer 인덱스가 안전하다.

### 설계

#### TagManager (`src/Core/TagManager.h/cpp`)

```cpp
class TagManager {
public:
    static TagManager& Get();

    GameObject* FindWithTag(const std::string& tag);
    std::vector<GameObject*> FindAllWithTag(const std::string& tag);

    // 자동 등록/해제 (GameObject::SetTag, 소멸자에서 호출)
    void Register(GameObject* obj, const std::string& tag);
    void Unregister(GameObject* obj, const std::string& tag);
    void OnTagChanged(GameObject* obj, const std::string& oldTag, const std::string& newTag);

    void Clear();

private:
    std::unordered_map<std::string, std::vector<GameObject*>> index;
};
```

#### GameObject 확장

`SetTag()`를 TagManager 연동으로 변경:

```cpp
void GameObject::SetTag(const std::string& newTag) {
    std::string oldTag = tag;
    tag = newTag;
    TagManager::Get().OnTagChanged(this, oldTag, newTag);
}
```

소멸자에서 `TagManager::Get().Unregister(this, tag)` 호출.

### 테스트 계획

기존 `test_tag_layer.cpp`에 추가:

1. **test_find_with_tag**: 태그로 단일 오브젝트 검색
2. **test_find_all_with_tag**: 동일 태그 복수 검색
3. **test_tag_change_reindex**: 태그 변경 시 인덱스 갱신
4. **test_tag_unregister_on_destroy**: 오브젝트 파괴 시 자동 해제

### 산출물

```
src/Core/TagManager.h/cpp
수정: src/ECS/GameObject.h/cpp (SetTag 연동, 소멸자 해제)
수정: tests/test_tag_layer.cpp (인덱스 테스트 추가)
수정: CMakeLists.txt
```

---

## 12. 실행 순서 및 의존성

```
Week 1              Week 2              Week 3              Week 4-5
┌──────────┐       ┌──────────┐       ┌──────────────┐    ┌──────────────┐
│E1 CI/CD  │       │D1.1 태그 │       │B1 셰이더/    │    │D2 라이프사이클│
│(2-3일)   │       │필드+직렬화│       │머티리얼      │    │(1-1.5주)     │
└──────────┘       │(2-3일)   │       │(1-1.5주)     │    └──────┬───────┘
┌──────────┐       └──────────┘       └──────┬───────┘           │
│A1 이벤트 │       ┌──────────┐              │              ┌────┴────────┐
│(2-3일)   │       │B0 렌더러 │              │              │D1.2 태그    │
└──────────┘       │정리      │       ┌──────┴───────┐      │글로벌 인덱스│
                   │(1-2일)   │       │B2 디버그렌더링│      │(2-3일)      │
                   └──────────┘       │(3-5일)       │      └─────────────┘
                   ┌──────────┐       └──────────────┘
                   │A2 풀링   │
                   │(1-2일)   │
                   └──────────┘
```

### 병렬 전략

- **트랙 1 (게임플레이)**: E1 → A1 → D1.1 → D2 → D1.2
- **트랙 2 (렌더링)**: B0 → B1 → B2
- **독립**: A2 (아무 시점)

트랙 1과 트랙 2는 **완전 병렬** 가능.

### 권장 순서 (단일 실행 시)

| 순서 | 시스템 | 기간 | 비고 |
|------|--------|------|------|
| 1 | E1 CI/CD | 2-3일 | 최우선. 이후 회귀 방지 |
| 2 | A1 이벤트 | 2-3일 | D2가 이벤트에 의존 |
| 3 | D1.1 태그/레이어 필드 | 2-3일 | 데이터만. 인덱스는 나중 |
| 4 | B0 렌더러 정리 | 1-2일 | B1 전 필수 |
| 5 | B1 셰이더/머티리얼 | 1-1.5주 | B0 완료 후 |
| 6 | A2 풀링 | 1-2일 | 독립. 틈새 시간 |
| 7 | B2 디버그 렌더링 | 3-5일 | B1 완료 후 |
| 8 | D2 라이프사이클 | 1-1.5주 | A1 완료 후 |
| 9 | D1.2 태그 인덱스 | 2-3일 | D2 완료 후 |

---

## 13. 검증 계획

### Phase 6 완료 체크리스트

| # | 검증 항목 | 방법 | 통과 기준 |
|---|----------|------|----------|
| 1 | CI 빌드 | GitHub Actions | macOS Debug/Release 빌드 + unit CTest 통과 |
| 2 | 이벤트 발행/구독 | 유닛 테스트 | Subscribe → Publish → 콜백 호출 |
| 3 | 이벤트 취소 | 유닛 테스트 | handled=true → 후속 핸들러 미호출 |
| 4 | ScopedSubscription | 유닛 테스트 | 스코프 종료 시 자동 해제 |
| 5 | 지연 이벤트 | 유닛 테스트 | QueueEvent → ProcessQueue 후 호출 |
| 6 | 태그/레이어 필드 | 유닛 테스트 | tag, layer 설정/조회 |
| 7 | 레이어 충돌 매트릭스 | 유닛 테스트 | SetLayerCollision → ShouldCollide |
| 8 | 소팅 키 음수 안전 | 유닛 테스트 | orderInLayer -1이 0보다 작은 키 생성 |
| 9 | 태그/레이어 직렬화 | 유닛 테스트 | 씬 저장/로드 왕복 |
| 10 | 렌더러 중첩 Begin 해소 | 수동 테스트 | 에디터 Play 모드에서 크래시 없음 |
| 11 | 셰이더 관리 | 유닛 테스트 | LoadShader → GetShader 정상 |
| 12 | 머티리얼 직렬화 | 유닛 테스트 | shaderName/texturePath JSON 왕복 |
| 13 | 디버그 라인 렌더링 | 수동 테스트 | DebugDraw::Line() 화면 표시 |
| 14 | 콜라이더 시각화 | 수동 테스트 | BoxCollider2D 경계 와이어프레임 |
| 15 | 오브젝트 풀 O(1) | 유닛 테스트 | Acquire/Release + IPoolable 콜백 |
| 16 | Clone 깊은 복사 | 유닛 테스트 | 새 ID, 동일 컴포넌트, 독립 수정 |
| 17 | 지연 파괴 | 유닛 테스트 | ProcessQueue 후 제거, NotifyDestroy 호출 |
| 18 | Cascade destroy | 유닛 테스트 | 부모 파괴 → 자식도 파괴 |
| 19 | DontDestroyOnLoad | 유닛 테스트 | DestroyAllNonPersistent에서 생존 |
| 20 | TagManager 검색 | 유닛 테스트 | FindWithTag, FindAllWithTag |
| 21 | TagManager 파괴 해제 | 유닛 테스트 | 오브젝트 파괴 시 인덱스 자동 제거 |
| 22 | 전체 CTest | CI | 기존 5개 + 새 ~8개 스위트 모두 통과 |

### 테스트 파일 (unit / integration 분리)

```
[Unit Tests — GL 불필요]
tests/test_event.cpp           (A1: 7개)
tests/test_tag_layer.cpp       (D1.1+D1.2: 12개)
tests/test_material.cpp        (B1: 3개 unit + 별도 integration)
tests/test_debug_draw.cpp      (B2: 3개 큐잉 로직)
tests/test_object_pool.cpp     (A2: 6개)
tests/test_lifecycle.cpp       (D2: 10개)

[Integration Tests — GL 필요, CI에서 label로 제외]
tests/test_shader_smoke.cpp    (B1: 2개)
tests/test_debug_render_smoke.cpp  (B2: 1개)
```

### 프레임 끝 처리 순서 (최종 계약)

```
매 프레임 끝:
  1. EventBus::ProcessQueue()         ← 지연 이벤트 먼저
  2. DestroyQueue::ProcessQueue(dt)   ← 파괴는 이벤트 이후
  3. DebugDraw::Update(dt)            ← 디버그 커맨드 만료 처리
```
