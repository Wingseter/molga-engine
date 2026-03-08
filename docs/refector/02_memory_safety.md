# Phase 1: 메모리 안전성 및 RAII

## 목표
Raw pointer를 smart pointer로 전환하고, RAII 원칙을 적용하여 메모리 누수와 예외 안전성 문제를 해결한다.

---

## 1.1 main.cpp - Raw Pointer 제거

### 현재 문제
```cpp
// main.cpp:87-90 - Raw new 할당
g_renderer = new Renderer();
g_shader = new Shader("Shaders/default.vert", "Shaders/default.frag");
g_camera = new Camera2D(800.0f, 600.0f);

// main.cpp:252-254 - 수동 delete
delete g_camera;
delete g_shader;
delete g_renderer;
```

- 예외 발생 시 cleanup 코드에 도달하지 못함
- main.cpp:63-66의 early return에서 이미 초기화된 시스템 cleanup 누락
- main.cpp:72-74 GLAD 실패 시 window 미해제

### 변경 계획
```cpp
// Before
Renderer* g_renderer = nullptr;
Shader* g_shader = nullptr;
Camera2D* g_camera = nullptr;

// After
std::unique_ptr<Renderer> g_renderer;
std::unique_ptr<Shader> g_shader;
std::unique_ptr<Camera2D> g_camera;
```

### 에러 경로 수정
```cpp
// Before (main.cpp:72-74)
if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;  // window 미해제, GLFW 미종료
}

// After
if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    glfwDestroyWindow(window);
    glfwTerminate();
    return -1;
}
```

### 대상 파일
- `src/main.cpp`

---

## 1.2 runtime_main.cpp - 동일 패턴 적용

### 현재 문제
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

### 변경 계획
main.cpp와 동일하게 `std::unique_ptr` 전환. 게임 루프 중 예외 발생 시에도 자동 cleanup 보장.

### 대상 파일
- `src/runtime_main.cpp`

---

## 1.3 Audio.cpp - 리소스 RAII 래핑

### 현재 문제
```cpp
// Audio.cpp:15-18
engine = new ma_engine();
if (ma_engine_init(nullptr, engine) != MA_SUCCESS) {
    delete engine;  // 수동 cleanup
    engine = nullptr;
    return false;
}

// Audio.cpp:65-68 - 사운드 로딩
ma_sound* sound = new ma_sound();
if (ma_sound_init_from_file(...) != MA_SUCCESS) {
    delete sound;  // 수동 cleanup
    return false;
}

// Audio.cpp:38-47 - Shutdown에서 수동 해제
for (auto& pair : sounds) {
    ma_sound_uninit(pair.second);
    delete pair.second;  // double-free 위험
}
```

- Shutdown() 이중 호출 시 double-free 위험
- 예외 발생 시 사운드 리소스 누수
- `musicSound` nullptr 체크 불완전

### 변경 계획
```cpp
// Custom deleter로 RAII 래핑
struct MaEngineDeleter {
    void operator()(ma_engine* e) {
        if (e) { ma_engine_uninit(e); delete e; }
    }
};

struct MaSoundDeleter {
    void operator()(ma_sound* s) {
        if (s) { ma_sound_uninit(s); delete s; }
    }
};

// Before
static ma_engine* engine;
static std::unordered_map<std::string, ma_sound*> sounds;

// After
static std::unique_ptr<ma_engine, MaEngineDeleter> engine;
static std::unordered_map<std::string, std::unique_ptr<ma_sound, MaSoundDeleter>> sounds;
```

### 대상 파일
- `src/Audio.h`
- `src/Audio.cpp`

---

## 1.4 Renderer - currentShader Dangling Pointer 방지

### 현재 문제
```cpp
// Renderer.h:30
Shader* currentShader;  // Non-owning, 문서화 없음

// Renderer.cpp Begin/End
void Renderer::Begin(Shader* shader, Camera2D* camera) {
    currentShader = shader;
    currentShader->Use();  // shader가 nullptr이면 크래시
}
```

- main.cpp에서 Shader를 delete한 후 Renderer가 dangling pointer를 가질 수 있음
- Begin()에서 nullptr 검증 없음

### 변경 계획
```cpp
// Non-owning pointer는 유지하되, 안전 검증 추가
void Renderer::Begin(Shader* shader, Camera2D* camera) {
    assert(shader != nullptr && "Shader must not be null");
    assert(camera != nullptr && "Camera must not be null");
    currentShader = shader;
    currentShader->Use();
}
```

### 대상 파일
- `src/Renderer.cpp`

---

## 1.5 UI 시스템 - 소유권 모델 명확화

### 현재 문제
```cpp
// UI.h:104
std::vector<UIElement*> elements;  // Raw pointer 저장

// UI.cpp:231-233
UIManager::~UIManager() {
    // "does not own elements" 주석 - 소유권 불명확
}
```

### 변경 계획
```cpp
// Option A: UIManager가 소유 (권장)
std::vector<std::unique_ptr<UIElement>> elements;

// Option B: 비소유 유지 시 명시적 문서화
// Non-owning: 외부에서 UIElement 수명 관리 필수
std::vector<UIElement*> elements;  // non-owning, caller manages lifetime
```

### 대상 파일
- `src/UI.h`
- `src/UI.cpp`

---

## 1.6 REGISTER_SCRIPT 매크로 - raw new 제거

> 출처: Codex 분석 - ScriptManager.h:53

### 현재 문제
```cpp
// ScriptManager.h:53 - REGISTER_SCRIPT 매크로
#define REGISTER_SCRIPT(cls) \
    ScriptManager::Get().RegisterScript(#cls, []() -> Script* { return new cls(); })
```

- 팩토리 람다가 raw `new`를 반환
- 호출 측에서 소유권 관리가 불명확

### 변경 계획
```cpp
// smart pointer 반환으로 변경
#define REGISTER_SCRIPT(cls) \
    ScriptManager::Get().RegisterScript(#cls, []() -> std::unique_ptr<Script> { \
        return std::make_unique<cls>(); \
    })
```

- `ScriptFactory` 타입도 `std::function<std::unique_ptr<Script>()>`로 변경
- `CreateScript()` 반환 타입도 `std::unique_ptr<Script>`로 변경

### 대상 파일
- `src/Scripting/ScriptManager.h`
- `src/Scripting/ScriptManager.cpp`

---

## 1.7 main.cpp - goto cleanup 패턴 제거

> 출처: Codex 분석 - main.cpp:138

### 현재 문제
```cpp
// main.cpp:138
goto cleanup;  // 에러 시 goto 사용
```

- `goto` 기반 cleanup은 RAII 미적용의 증상
- smart pointer 전환 후 자연스럽게 제거됨

### 변경 계획
- Phase 1.1의 smart pointer 전환 완료 시 자동 해소
- `goto cleanup` 레이블 및 관련 코드 삭제
- 모든 리소스가 스코프 기반 수명을 가지므로 별도 cleanup 블록 불필요

### 대상 파일
- `src/main.cpp`

---

## 체크리스트

- [ ] main.cpp: `new Renderer/Shader/Camera2D` → `std::make_unique`
- [ ] main.cpp: early return 경로에 cleanup 코드 추가
- [ ] main.cpp: `goto cleanup` 패턴 제거
- [ ] runtime_main.cpp: 동일 smart pointer 전환
- [ ] Audio.cpp: RAII custom deleter 적용
- [ ] Audio.cpp: Shutdown() 이중 호출 안전성 보장
- [ ] Renderer.cpp: Begin()에 nullptr assertion 추가
- [ ] UI.h/cpp: 소유권 모델 결정 및 적용
- [ ] REGISTER_SCRIPT: raw new → std::make_unique 전환
- [ ] ScriptFactory 반환 타입 std::unique_ptr 전환
