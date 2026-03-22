# Phase 1: 메모리 안전성 및 RAII ✅ 완료

> 의존: Phase 0 (`d4d4988`)
> 완료일: 2026-03-08

## 목표
Raw pointer를 smart pointer로 전환하고, RAII 원칙을 적용하여 메모리 누수와 예외 안전성 문제를 해결한다.

---

## 1.1 main.cpp - Raw Pointer 제거 + goto 제거 ✅

### 이전 문제
```cpp
// main.cpp:38-40 - Raw new 할당
Renderer* g_renderer = nullptr;
Shader* g_shader = nullptr;
Camera2D* g_camera = nullptr;

// main.cpp:87-90
g_renderer = new Renderer();
g_shader = new Shader("src/Shaders/default.vert", "src/Shaders/default.frag");
g_camera = new Camera2D(800.0f, 600.0f);

// main.cpp:138
goto cleanup;  // 프로젝트 선택 중 창 닫힐 때

// main.cpp:252-254 - 수동 delete
delete g_camera;
delete g_shader;
delete g_renderer;
```

- 예외 발생 시 cleanup 코드에 도달하지 못함
- GLAD 실패 시 window 미해제 (`return -1` 직전 cleanup 누락)
- `goto cleanup` 패턴은 RAII 미적용의 증상

### 적용 결과

**unique_ptr 전환** (Phase 1):
```cpp
// Phase 1 시점: 글로벌 스코프 unique_ptr
std::unique_ptr<Renderer> g_renderer;
std::unique_ptr<Shader> g_shader;
std::unique_ptr<Camera2D> g_camera;
```

> **Note**: Phase 2에서 이 글로벌 변수들은 `main()` 로컬 스코프로 이동되었고,
> GLFW/GLAD 초기화 코드는 `Bootstrap.h/cpp`의 `EngineInit()`으로 추출되었다.
> 셰이더 경로도 `"src/Shaders/"` → `"Shaders/"`로 통일.
> 현재 코드는 아래 형태:
> ```cpp
> auto renderer = std::make_unique<Renderer>();
> auto shader = std::make_unique<Shader>("Shaders/default.vert", "Shaders/default.frag");
> auto camera = std::make_unique<Camera2D>(...);
> ```

**goto 제거 전략** - 조건 반전으로 자연스러운 흐름 제어:
```cpp
// Before:
if (glfwWindowShouldClose(window)) { goto cleanup; }
// ... editor setup + main loop ...
cleanup:
    // teardown

// After:
if (!glfwWindowShouldClose(window)) {
    // ... editor setup + main loop ...
}
// cleanup (라벨 없이 직접 실행, unique_ptr가 자동 해제)
```

**GLAD 실패 시 cleanup 추가**:
```cpp
if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
}
```

**호출부 .get() 추가**: `g_renderer->Begin(g_shader.get(), g_camera.get())`, `sr->RenderSprite(g_renderer.get(), g_shader.get(), g_camera.get())`, `SceneManager::Render(g_renderer.get(), g_shader.get(), g_camera.get())`

### 변경 파일
- `src/main.cpp`

---

## 1.2 runtime_main.cpp - 동일 패턴 적용 ✅

### 이전 문제
```cpp
// runtime_main.cpp:38-40
Renderer* g_renderer = nullptr;
Shader* g_shader = nullptr;
Camera2D* g_camera = nullptr;

// runtime_main.cpp:174-176
delete g_camera;
delete g_shader;
delete g_renderer;
```

### 적용 결과
main.cpp와 동일하게 `std::unique_ptr` 전환. GLAD 실패 시 `glfwDestroyWindow(window); glfwTerminate();` 추가. 모든 호출부에 `.get()` 추가.

`GLFWwindow* g_window`은 유지 (GLFW가 수명 관리).

### 변경 파일
- `src/runtime_main.cpp`

---

## 1.3 Audio.cpp - 리소스 RAII 래핑 ✅

### 이전 문제
```cpp
// Audio.h - Raw pointer 멤버
static ma_engine* engine;
static ma_sound* musicSound;
static std::unordered_map<std::string, ma_sound*> sounds;

// Audio.cpp - 수동 uninit + delete
for (auto& pair : sounds) {
    ma_sound_uninit(pair.second);
    delete pair.second;
}
```

- Shutdown() 이중 호출 시 double-free 위험
- 예외 발생 시 사운드 리소스 누수

### 적용 결과

**Custom deleter 정의** (Audio.h):
```cpp
struct MaEngineDeleter {
    void operator()(ma_engine* e);
};

struct MaSoundDeleter {
    void operator()(ma_sound* s);
};

static std::unique_ptr<ma_engine, MaEngineDeleter> engine;
static std::unique_ptr<ma_sound, MaSoundDeleter> musicSound;
static std::unordered_map<std::string, std::unique_ptr<ma_sound, MaSoundDeleter>> sounds;
```

**Deleter 구현** (Audio.cpp) - `uninit` 후 `delete`:
```cpp
void MaEngineDeleter::operator()(ma_engine* e) {
    if (e) { ma_engine_uninit(e); delete e; }
}

void MaSoundDeleter::operator()(ma_sound* s) {
    if (s) { ma_sound_uninit(s); delete s; }
}
```

**init-then-wrap 패턴** - init 실패 시 uninit 호출 방지:
```cpp
auto raw = new ma_engine();
if (ma_engine_init(nullptr, raw) != MA_SUCCESS) {
    delete raw;  // uninit 없이 delete만 (init 실패)
    return false;
}
engine.reset(raw);  // 성공 시에만 deleter 연결
```

**Shutdown() 간소화** - deleter가 모든 cleanup 처리:
```cpp
void Audio::Shutdown() {
    if (!initialized) return;
    sounds.clear();      // deleter가 각 사운드의 uninit+delete 처리
    musicSound.reset();
    engine.reset();
    initialized = false;
}
```

### 계획 대비 차이점
- Shutdown() 이중 호출 안전성: `unique_ptr::reset()`은 이미 nullptr인 경우 no-op이므로 자동 보장됨
- 모든 내부 접근에 `.get()` 추가 (예: `ma_sound_init_from_file(engine.get(), ...)`, `ma_sound_set_volume(it->second.get(), ...)`)

### 변경 파일
- `src/Audio.h`
- `src/Audio.cpp`

---

## 1.4 Renderer - currentShader Dangling Pointer 방지 ✅

### 이전 문제
```cpp
// Renderer.h:30
Shader* currentShader;  // Non-owning, 문서화 없음

// Renderer.cpp - Begin()에서 nullptr 검증 없음
void Renderer::Begin(Shader* shader, Camera2D* camera) {
    currentShader = shader;
    currentShader->Use();  // shader가 nullptr이면 크래시
}
```

### 적용 결과

```cpp
// Renderer.cpp - assert 추가
#include <cassert>

void Renderer::Begin(Shader* shader, Camera2D* camera) {
    assert(shader != nullptr && "Renderer::Begin called with null shader");
    currentShader = shader;
    currentShader->Use();
    // ...
}
```

```cpp
// Renderer.h - non-owning 주석 추가
Shader* currentShader; // non-owning; lifetime managed by caller
```

### 계획 대비 차이점
- `camera`에 대한 assert는 추가하지 않음 — `Begin()`이 `camera == nullptr`일 때 `mat4x4_identity(view)`를 호출하는 유효한 코드 경로가 존재하므로

### 변경 파일
- `src/Renderer.cpp`
- `src/Renderer.h`

---

## 1.5 UI 시스템 - 보류 (현재 모델 유효)

### 판단 근거
`GameScene`에서 스택 할당 `ProgressBar`를 `AddElement(&healthBar)`로 전달하므로 non-owning 모델이 현재 정확함. `unique_ptr`로 전환하면 `GameScene`이 스택 객체의 소유권을 이전할 수 없어 재설계 필요.

Phase 3 (ECS/씬 리팩토링)에서 GameScene 구조 변경 시 재평가 예정.

---

## 1.6 REGISTER_SCRIPT 매크로 + ScriptManager - unique_ptr factory ✅

### 이전 문제
```cpp
// ScriptManager.h
using ScriptFactory = std::function<Script*()>;
Script* CreateScript(const std::string& name);

// REGISTER_SCRIPT 매크로
#define REGISTER_SCRIPT(cls) \
    ScriptManager::Get().RegisterScript(#cls, []() -> Script* { return new cls(); })

// BuiltinScripts.cpp - 3개 수동 등록
ScriptManager::Get().RegisterScript("PlayerController", []() -> Script* {
    return new PlayerController();
});
```

- 팩토리 람다가 raw `new` 반환, 호출 측에서 소유권 관리 불명확

### 적용 결과

**ScriptManager.h**:
```cpp
using ScriptFactory = std::function<std::unique_ptr<Script>()>;

std::unique_ptr<Script> CreateScript(const std::string& name);

#define REGISTER_SCRIPT(ScriptClass) \
    namespace { \
        struct ScriptClass##Registrar { \
            ScriptClass##Registrar() { \
                ScriptManager::Get().RegisterScript(#ScriptClass, \
                    []() -> std::unique_ptr<Script> { \
                        return std::make_unique<ScriptClass>(); \
                    }); \
            } \
        }; \
        static ScriptClass##Registrar g_##ScriptClass##Registrar; \
    }
```

**BuiltinScripts.cpp** - 3개 람다 동일 변경:
```cpp
ScriptManager::Get().RegisterScript("PlayerController", []() -> std::unique_ptr<Script> {
    return std::make_unique<PlayerController>();
});
```

**InspectorWindow.cpp** - `release()`로 소유권 전달:
```cpp
auto script = ScriptManager::Get().CreateScript(scriptName);
if (script) {
    target->AddComponentRaw(script.release());
}
```

### 계획 대비 차이점
- 기존 `AddComponentRaw(Component*)` 인터페이스는 유지 — Phase 3 ECS 리팩토링에서 `AddComponent<T>(unique_ptr<T>)` 도입 시 개선 예정
- `release()`로 소유권을 명시적으로 이전하여 기존 API와 호환

### 변경 파일
- `src/Scripting/ScriptManager.h`
- `src/Scripting/ScriptManager.cpp`
- `src/Scripting/BuiltinScripts.cpp`
- `src/Editor/Windows/InspectorWindow.cpp`

---

## 1.7 TextRenderer - fontTexture unique_ptr ✅

### 이전 문제
```cpp
// TextRenderer.h
Texture* fontTexture = nullptr;

// TextRenderer.cpp
fontTexture = new Texture(texWidth, texHeight, textureData, 4);
delete fontTexture;
```

### 적용 결과
```cpp
// TextRenderer.h
std::unique_ptr<Texture> fontTexture;

// TextRenderer.cpp - 생성
fontTexture = std::make_unique<Texture>(texWidth, texHeight, textureData, 4);

// Shutdown
fontTexture.reset();

// 사용처
sprite.SetTexture(fontTexture.get());
```

### 변경 파일
- `src/TextRenderer.h`
- `src/TextRenderer.cpp`

---

## 검증 결과

- `cmake --build build` — 양쪽 타깃 (molga_engine, molga_runtime) 컴파일 성공
- `ctest --output-on-failure` — 4/4 테스트 통과 (test_types, test_collision, test_ecs, test_scene_serializer)

---

## 체크리스트

- [x] runtime_main.cpp: `new Renderer/Shader/Camera2D` → `std::make_unique`
- [x] runtime_main.cpp: GLAD 실패 시 `glfwDestroyWindow` + `glfwTerminate` 추가
- [x] runtime_main.cpp: `delete` → `reset()`, 호출부 `.get()` 추가
- [x] main.cpp: `new Renderer/Shader/Camera2D` → `std::make_unique`
- [x] main.cpp: GLAD 실패 시 cleanup 코드 추가
- [x] main.cpp: `goto cleanup` 패턴 제거 (조건 반전)
- [x] main.cpp: `delete` → `reset()`, 호출부 `.get()` 추가
- [x] Renderer.cpp: `Begin()`에 `assert(shader != nullptr)` 추가
- [x] Renderer.h: `currentShader` non-owning 주석 추가
- [x] Audio.h: `MaEngineDeleter`, `MaSoundDeleter` custom deleter 정의
- [x] Audio.h: `engine`, `musicSound`, `sounds` → `unique_ptr` with custom deleter
- [x] Audio.cpp: init-then-wrap 패턴 적용
- [x] Audio.cpp: `Shutdown()` 간소화 (`clear/reset/reset`)
- [x] TextRenderer.h: `fontTexture` → `std::unique_ptr<Texture>`
- [x] TextRenderer.cpp: `new Texture` → `make_unique`, `delete` → `reset()`, `.get()` 추가
- [x] ScriptManager.h: `ScriptFactory` → `std::function<std::unique_ptr<Script>()>`
- [x] ScriptManager.h: `CreateScript()` → `std::unique_ptr<Script>` 반환
- [x] ScriptManager.h: `REGISTER_SCRIPT` 매크로 `make_unique` 전환
- [x] BuiltinScripts.cpp: 3개 람다 `make_unique` 전환
- [x] InspectorWindow.cpp: `script.release()`로 소유권 전달
- [ ] UI 시스템: 보류 (Phase 3에서 재평가)
- [x] 빌드 성공 확인 (molga_engine + molga_runtime)
- [x] CTest 4/4 통과 확인

## 변경 파일 요약

| 파일 | 변경 내용 |
|------|-----------|
| `src/runtime_main.cpp` | unique_ptr 전환, GLAD cleanup, `.get()` 추가 |
| `src/main.cpp` | unique_ptr 전환, goto 제거, GLAD cleanup, `.get()` 추가 |
| `src/Renderer.cpp` | `#include <cassert>`, `assert(shader)` 추가 |
| `src/Renderer.h` | non-owning 주석 추가 |
| `src/Audio.h` | custom deleter 구조체, unique_ptr 멤버 전환 |
| `src/Audio.cpp` | deleter 구현, init-then-wrap, Shutdown 간소화 |
| `src/TextRenderer.h` | `unique_ptr<Texture>` 전환 |
| `src/TextRenderer.cpp` | `make_unique`, `reset()`, `.get()` |
| `src/Scripting/ScriptManager.h` | ScriptFactory/CreateScript unique_ptr, REGISTER_SCRIPT 매크로 |
| `src/Scripting/ScriptManager.cpp` | CreateScript 반환 타입 변경 |
| `src/Scripting/BuiltinScripts.cpp` | 3개 람다 make_unique 전환 |
| `src/Editor/Windows/InspectorWindow.cpp` | `script.release()` 소유권 전달 |
