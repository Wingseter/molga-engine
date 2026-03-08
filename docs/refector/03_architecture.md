# Phase 2: 아키텍처 통일 및 부트스트랩 정리

## 목표
부트스트랩 3중 분리 해소, Layer/LayerStack 아키텍처 도입, EngineContext 기반 서비스 관리, 경로 규칙 정규화.

---

## 2.1 Application vs main.cpp 이중 초기화 해소

### 현재 문제

Application 클래스가 완전한 초기화 로직을 구현하지만, main.cpp에서 독자적으로 GLFW를 초기화하여 Application을 완전히 무시.

| 기능 | Application.cpp | main.cpp | 상태 |
|------|----------------|----------|------|
| GLFW Init | Line 23 | Line 53 | 중복 |
| Window Hints | Lines 29-35 | Lines 54-60 | 중복 (힌트 불일치) |
| Window 생성 | Line 47 | Line 62 | 중복 |
| GLAD 로딩 | Line 56 | Line 72 | 중복 |
| 콜백 설정 | Lines 59-62 | Line 70 | Application 것은 미사용 |
| Maximized 힌트 | Line 42 | 없음 | main.cpp에서 누락 |

### 변경 계획

**Option A: Application 클래스를 진입점으로 사용 (권장)**

```cpp
// main.cpp - 간소화
int main() {
    Application::Config config;
    config.title = "Molga Engine";
    config.width = 1280;
    config.height = 720;

    auto& app = Application::Get();
    if (!app.Init(config)) return -1;

    // Editor 초기화
    Editor::Get().Init();

    app.Run();  // 메인 루프를 Application이 관리
    app.Shutdown();
    return 0;
}
```

**Option B: Application 클래스 제거하고 main.cpp 유지**
- Application.h/cpp를 삭제하고 main.cpp의 초기화 로직을 정리
- 단, Application이 제공하는 콜백 관리 등의 기능 손실

**권장: Option A** - Application 클래스가 더 완성도 높은 초기화 로직을 보유

### 공통 초기화 코드 추출 (Bootstrap)

> 출처: Codex 분석 - main.cpp와 runtime_main.cpp의 공통 코드 추출

main.cpp와 runtime_main.cpp는 GLFW/GLAD 초기화, Input/Time/Audio 초기화 등 대부분의 부트스트랩 코드가 동일하다. 이를 공통 함수 또는 클래스로 추출:

```cpp
// src/Core/Bootstrap.h
struct BootstrapConfig {
    std::string title;
    int width, height;
    bool fullscreen;
    bool enableEditor;  // ImGui/Editor 초기화 여부
};

struct BootstrapResult {
    GLFWwindow* window;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Shader> shader;
    std::unique_ptr<Camera2D> camera;
};

// 공통 초기화 (editor와 runtime 모두 사용)
std::optional<BootstrapResult> Bootstrap(const BootstrapConfig& config);
void Teardown(BootstrapResult& result);
```

### 대상 파일
- `src/Core/Application.h` / `src/Core/Application.cpp`
- `src/main.cpp`
- `src/runtime_main.cpp`
- 새 파일: `src/Core/Bootstrap.h` / `src/Core/Bootstrap.cpp` (선택)

---

## 2.2 Layer/LayerStack 아키텍처

> 출처: Gemini 분석 - 에디터와 런타임의 강한 결합 해소

### 현재 문제

Application 메인 루프 내에 ImGui/에디터 코드가 하드코딩되어 있어, 런타임 빌드 시 에디터 코드를 깔끔하게 분리하기 어렵다.

### 변경 계획

```cpp
// Layer 인터페이스
class Layer {
public:
    virtual ~Layer() = default;
    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate(float dt) {}
    virtual void OnRender() {}
    virtual void OnImGuiRender() {}  // 에디터 전용
};

// LayerStack - Application이 소유
class LayerStack {
    std::vector<std::unique_ptr<Layer>> layers;
public:
    void PushLayer(std::unique_ptr<Layer> layer);
    void PopLayer(Layer* layer);
    // iterator 지원
};

// Application 메인 루프
void Application::Run() {
    while (running) {
        float dt = Time::Get().GetDeltaTime();
        for (auto& layer : layerStack) {
            layer->OnUpdate(dt);
        }
        for (auto& layer : layerStack) {
            layer->OnRender();
        }
        // ImGui는 MOLGA_EDITOR일 때만
        #ifdef MOLGA_EDITOR
        ImGuiLayer::Get().BeginFrame();
        for (auto& layer : layerStack) {
            layer->OnImGuiRender();
        }
        ImGuiLayer::Get().EndFrame();
        #endif
    }
}

// 에디터 빌드
app.PushLayer(std::make_unique<EditorLayer>());

// 런타임 빌드
app.PushLayer(std::make_unique<GameLayer>());
```

### 기대 효과
- 에디터/런타임 완전 분리
- 빌드 타깃에 따라 Layer 조합으로 구성
- 런타임 바이너리에서 ImGui/에디터 코드 완전 배제 가능

### 대상 파일
- 새 파일: `src/Core/Layer.h`
- `src/Core/Application.h` / `src/Core/Application.cpp`
- 새 파일: `src/Editor/EditorLayer.h/cpp`
- 새 파일: `src/Scenes/GameLayer.h/cpp`

---

## 2.3 싱글톤 패턴 통일 → EngineContext로 이행

### 현재 상태 - 3가지 패턴 혼재

| 패턴 | 클래스 | 특징 |
|------|--------|------|
| Meyer's Singleton (`Get()`) | Application, Editor, Project, TextureManager, ScriptManager, ScriptCompiler, TextRenderer, EditorState, VSCodeIntegration, FontManager, GameBuilder | 인스턴스 접근에 `Get()` 호출 |
| Static-Only | Time, Input, ImGuiLayer, Collision, Audio | 모든 멤버가 static. 인스턴스 없음 |
| 혼합 | SceneManager | static 멤버 + static 메서드 |

### 변경 계획: EngineContext / Service Locator 패턴

> 출처: Gemini 분석 - 싱글톤 의존성 지옥에서의 탈출

단순히 Meyer's Singleton으로 통일하는 것보다, **EngineContext**에 서브시스템을 등록하는 방식이 장기적으로 우수하다. 테스트 시 mock 주입이 가능하고, 의존성이 명시적이며, 멀티스레드 확장에 유리하다.

```cpp
// 단계적 접근: 우선 Meyer's Singleton 통일 후 → EngineContext로 이행

// Step 1 (즉시): Static-Only → Meyer's Singleton 통일
class Time {
public:
    static Time& Get() {
        static Time instance;
        return instance;
    }
    void Init();
    void Update();
    float GetDeltaTime() const;
    Time(const Time&) = delete;
    Time& operator=(const Time&) = delete;
private:
    Time() = default;
    float deltaTime = 0.0f;
    float lastFrameTime = 0.0f;
};

// Step 2 (후속): EngineContext로 이행
class EngineContext {
    std::unique_ptr<Time> time;
    std::unique_ptr<Input> input;
    std::unique_ptr<Audio> audio;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<TextureManager> textureManager;
    std::unique_ptr<ScriptManager> scriptManager;
    // ...
public:
    Time& GetTime() { return *time; }
    Input& GetInput() { return *input; }
    Audio& GetAudio() { return *audio; }
    // ...
    bool Init(const Application::Config& config);
    void Shutdown();
};
```

### Step 1 전환 대상 (즉시)

| 클래스 | 현재 | 변경 | 호출 변경 |
|--------|------|------|-----------|
| Time | Static-Only | `Get()` 싱글톤 | `Time::Update()` → `Time::Get().Update()` |
| Input | Static-Only | `Get()` 싱글톤 | `Input::GetKey()` → `Input::Get().GetKey()` |
| Audio | Static-Only | `Get()` 싱글톤 | `Audio::Init()` → `Audio::Get().Init()` |
| Collision | Static-Only | 유틸리티 유지 (상태 없음) | 변경 없음 |
| ImGuiLayer | Static-Only | `Get()` 싱글톤 | `ImGuiLayer::Init()` → `ImGuiLayer::Get().Init()` |
| SceneManager | Static 혼합 | `Get()` 싱글톤 | `SceneManager::Update()` → `SceneManager::Get().Update()` |

> Collision은 상태가 없는 순수 유틸리티이므로 static 메서드 유지.

### Step 2 고려사항 (EngineContext 이행)
- Project, GameBuilder는 에디터 전용이므로 EditorContext에 별도 배치 (Gemini 제안)
- EngineContext는 Application이 소유하고, Layer들에 참조로 전달
- 기존 `::Get()` 호출은 점진적으로 EngineContext 경유로 전환

### 대상 파일
- `src/Time.h` / `src/Time.cpp`
- `src/Input.h` / `src/Input.cpp`
- `src/Audio.h` / `src/Audio.cpp`
- `src/Editor/ImGuiLayer.h` / `src/Editor/ImGuiLayer.cpp`
- `src/Scene.h` / `src/Scene.cpp`
- 이들을 호출하는 모든 파일

---

## 2.3 글로벌 변수 제거

### 현재 문제

GLFWwindow 포인터가 5곳에 중복 저장:

```
main.cpp:41         → GLFWwindow* g_window
Application.h:51    → GLFWwindow* window
ImGuiLayer.h:23     → static GLFWwindow* currentWindow
Input.h:32          → static GLFWwindow* window
runtime_main.cpp:41 → GLFWwindow* g_window
```

하나를 파괴하면 나머지 4개가 dangling pointer.

### 변경 계획

Application이 window를 소유하고, 다른 시스템은 참조로 접근:

```cpp
class Application {
public:
    GLFWwindow* GetWindow() const { return window; }
private:
    GLFWwindow* window = nullptr;  // 유일한 소유자
};

// 다른 시스템은 초기화 시 참조 전달
Input::Get().Init(Application::Get().GetWindow());
ImGuiLayer::Get().Init(Application::Get().GetWindow());
```

main.cpp / runtime_main.cpp의 글로벌 변수:
```cpp
// Before
Renderer* g_renderer = nullptr;
Shader* g_shader = nullptr;
Camera2D* g_camera = nullptr;
GLFWwindow* g_window = nullptr;
std::vector<std::shared_ptr<GameObject>> g_gameObjects;

// After - Application 또는 main() 스코프 내 로컬 변수로 이동
int main() {
    auto renderer = std::make_unique<Renderer>();
    auto shader = std::make_unique<Shader>(...);
    auto camera = std::make_unique<Camera2D>(...);
    std::vector<std::shared_ptr<GameObject>> gameObjects;
    // ...
}
```

### 대상 파일
- `src/main.cpp`
- `src/runtime_main.cpp`
- `src/Core/Application.h`

---

## 2.4 초기화/종료 순서 명시화

### 현재 문제

초기화 순서가 암묵적이고 문서화되지 않음. 종료 순서가 초기화 역순이 아님.

```
초기화 순서: Time → Input → Audio → ImGui → Renderer → Shader → Camera
종료 순서:   Project → Editor → Objects → ImGui → Camera/Shader/Renderer → Audio
             ↑ 불일치: 역순이 아님
```

### 변경 계획

```cpp
// 초기화 순서 (의존성 그래프 기반)
// Level 0: 의존성 없음
Time::Get().Init();

// Level 1: Window 필요
Input::Get().Init(window);

// Level 2: OpenGL 필요
renderer = std::make_unique<Renderer>();
renderer->Init();
shader = std::make_unique<Shader>(...);
camera = std::make_unique<Camera2D>(...);
TextRenderer::Get().Init();

// Level 3: Renderer 필요
ImGuiLayer::Get().Init(window);
Audio::Get().Init();

// Level 4: 모든 시스템 필요
Editor::Get().Init();
RegisterBuiltinScripts();

// 종료: 역순 (LIFO)
Editor::Get().Shutdown();
Audio::Get().Shutdown();
ImGuiLayer::Get().Shutdown();
TextRenderer::Get().Shutdown();
// camera, shader, renderer는 unique_ptr로 자동 해제
Input::Get().Shutdown();  // 추가 필요
```

### Application의 empty 콜백 정리

```cpp
// Application.cpp:236-249 - 빈 콜백 제거
// 현재: 등록만 하고 아무 동작 안 함
void Application::KeyCallback(...) { /* empty */ }
void Application::MouseButtonCallback(...) { /* empty */ }
void Application::CursorPosCallback(...) { /* empty */ }

// 변경: Input 시스템이 콜백 직접 처리하거나, 콜백 제거
```

### 대상 파일
- `src/Core/Application.cpp`
- `src/main.cpp`
- `src/runtime_main.cpp`

---

## 2.5 플랫폼 추상화 레이어

### 현재 문제

플랫폼별 코드가 여러 파일에 `#ifdef` 로 산재:

| 파일 | 위치 | 내용 |
|------|------|------|
| ScriptCompiler.cpp | Lines 390-413 | `_popen` vs `popen` |
| ScriptCompiler.cpp | Lines 373-379 | `.dylib` vs `.dll` vs `.so` |
| GameBuilder.cpp | Lines 188-192 | 실행파일 확장자 |
| Project.cpp | Line 252 | `getenv("HOME")` (Unix only) |

### 변경 계획

`src/Platform/Platform.h`에 플랫폼 추상화 집중:

```cpp
namespace Platform {
    std::string GetLibraryExtension();   // .dylib, .dll, .so
    std::string GetLibraryPrefix();      // lib, "", lib
    std::string GetExecutableName(const std::string& name);
    std::string GetConfigDirectory();    // ~/.molga, %APPDATA%/molga
    int ExecuteCommand(const std::string& cmd, std::string& output);
}
```

### 대상 파일
- `src/Platform/Platform.h` / `src/Platform/Platform.cpp` (확장)
- `src/Scripting/ScriptCompiler.cpp`
- `src/Core/GameBuilder.cpp`
- `src/Core/Project.cpp`

---

## 2.7 경로 규칙 정규화

> 출처: Codex 분석 - 경로 규칙 불일치 (Linux CI에서 즉시 실패 가능)

### 현재 문제

| 시스템 | 사용 경로 | 문제 |
|--------|----------|------|
| Project.cpp:84-162 | `Assets`, `Scenes`, `ProjectSettings`, `Scripts` (대문자) | 프로젝트 내부 경로 |
| GameBuilder.cpp:81-176 | `assets`, `scenes` (소문자) | 패키징 산출물 경로 |
| main.cpp:89 | `src/Shaders/default.vert` | 에디터 셰이더 경로 |
| runtime_main.cpp:115 | `Shaders/default.vert` | 런타임 셰이더 경로 (불일치!) |
| ProjectBrowserWindow.cpp:23 | `"Assets"` 가정 | 프로젝트 루트 |
| ScriptCompiler.cpp:143 | `external/glm` include | 존재하지 않는 디렉토리 참조 |

macOS는 대소문자 비엄격이라 숨겨져 있지만, Linux에서는 즉시 실패.

### 변경 계획

```cpp
// src/Core/PathConstants.h
namespace Paths {
    // 프로젝트 내부 디렉토리 (대문자 통일)
    namespace Project {
        constexpr const char* ASSETS = "Assets";
        constexpr const char* SCENES = "Scenes";
        constexpr const char* SCRIPTS = "Scripts";
        constexpr const char* SETTINGS = "ProjectSettings";
    }

    // 빌드/패키징 산출물
    namespace Build {
        constexpr const char* ASSETS = "assets";
        constexpr const char* SCENES = "scenes";
        constexpr const char* SHADERS = "Shaders";
    }

    // 엔진 내부
    namespace Engine {
        constexpr const char* SHADERS = "Shaders";  // 에디터/런타임 통일
        constexpr const char* PROJECT_FILE = "project.molga";
        constexpr const char* CONFIG_DIR = ".molga";
    }
}
```

### 핵심 수정 사항
1. 에디터 셰이더 경로를 `Shaders/default.vert`로 통일 (현재 `src/Shaders/`)
2. GameBuilder에서 셰이더 복사 시 올바른 소스 경로 사용
3. ScriptCompiler의 `external/glm` 참조 제거 (존재하지 않는 디렉토리)
4. 모든 경로를 PathConstants를 통해 참조

### 대상 파일
- 새 파일: `src/Core/PathConstants.h`
- `src/main.cpp` (셰이더 경로)
- `src/Core/Project.cpp`
- `src/Core/GameBuilder.cpp`
- `src/Scripting/ScriptCompiler.cpp`
- `src/Editor/Windows/ProjectBrowserWindow.cpp`

---

## 체크리스트

- [ ] Application 클래스를 main.cpp의 진입점으로 활용
- [ ] main.cpp / runtime_main.cpp 공통 부트스트랩 코드 추출
- [ ] Layer/LayerStack 인터페이스 구현
- [ ] EditorLayer, GameLayer 분리
- [ ] Time, Input, Audio, ImGuiLayer → Meyer's Singleton 전환
- [ ] EngineContext 설계 (장기 - Step 2)
- [ ] 글로벌 변수 → 로컬 스코프 또는 Application 멤버로 이동
- [ ] GLFWwindow 저장 위치를 Application 단일 소유로 통일
- [ ] 초기화/종료 순서 문서화 및 역순 보장
- [ ] Application의 빈 콜백 정리
- [ ] 플랫폼별 #ifdef를 Platform 모듈로 집중
- [ ] 경로 상수 파일 생성 (PathConstants.h)
- [ ] 에디터/런타임 셰이더 경로 통일
- [ ] ScriptCompiler의 존재하지 않는 external/glm 참조 제거
