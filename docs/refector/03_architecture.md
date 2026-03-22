# Phase 2: 아키텍처 통일 및 부트스트랩 정리 ✅ 완료

> 의존: Phase 1 (`654df76`)
> 완료일: 2026-03-08

## 목표

중복 제거, 데드 코드 삭제, 경로 상수화, 글로벌 변수 스코프 축소.

## 범위 결정

원래 Phase 2 계획에는 Layer/LayerStack, 싱글톤 통일(Time/Input/Audio → Meyer's), EngineContext 등이 포함되어 있었으나, 변경 범위가 너무 넓어 아래 항목만 이 Phase에서 실행. 나머지는 Phase 4 (에디터 리팩토링)로 이관.

**이번 Phase에서 실행한 것**:
- PathConstants.h 생성 (경로 상수화)
- CMake 셰이더 복사 규칙 + 에디터 셰이더 경로 통일
- GameBuilder 경로 상수화
- 존재하지 않는 `external/glm` 참조 제거
- Application 클래스 삭제 (데드 코드)
- Bootstrap 모듈 추출 (`EngineInit`/`EngineShutdown`)
- 글로벌 변수 → `main()` 로컬 스코프 이동

**Phase 4로 보류한 것**:
- Layer/LayerStack 아키텍처
- 싱글톤 패턴 통일 (Time/Input/Audio → Meyer's)
- EngineContext / Service Locator
- 플랫폼 추상화 레이어 강화
- 초기화/종료 순서 명시화

---

## 2.1 PathConstants.h 생성 ✅

**신규 파일**: `src/Core/PathConstants.h`

```cpp
namespace Paths {
    namespace Project {
        constexpr const char* ASSETS = "Assets";
        constexpr const char* TEXTURES = "Assets/Textures";
        constexpr const char* AUDIO = "Assets/Audio";
        constexpr const char* SCENES = "Scenes";
        constexpr const char* SCRIPTS = "Scripts";
        constexpr const char* SCRIPTS_BUILD = "Scripts/build";
        constexpr const char* SETTINGS = "ProjectSettings";
    }
    namespace Build {
        constexpr const char* ASSETS = "assets";
        constexpr const char* SCENES = "scenes";
        constexpr const char* SHADERS = "Shaders";
    }
    namespace Engine {
        constexpr const char* SHADER_VERT = "Shaders/default.vert";
        constexpr const char* SHADER_FRAG = "Shaders/default.frag";
        constexpr const char* SHADER_SRC_DIR = "src/Shaders";
    }
}
```

순수 추가. 기존 파일 변경 없음.

> **케이싱 버그 메모**: `GameBuilder::CopyAssets()`는 소문자 `"assets"`를 소스로 사용하지만, `Project` 클래스는 대문자 `"Assets"` 디렉토리를 생성한다. PathConstants에서 `Build::ASSETS = "assets"`, `Project::ASSETS = "Assets"`로 불일치가 명시적으로 드러남. 실제 수정은 Phase 4 에디터 리팩토링에서 GameBuilder가 Project 경로를 참조하도록 재설계할 때 진행.

### 변경 파일
- `src/Core/PathConstants.h` (신규)

---

## 2.2 CMake 셰이더 복사 규칙 + 에디터 셰이더 경로 통일 ✅

### 이전 문제

- `build/Shaders/` 디렉토리가 빌드 시 생성되지 않음
- 에디터(`main.cpp`)는 `"src/Shaders/default.vert"`를 참조 → CWD=프로젝트 루트에서만 작동
- 런타임(`runtime_main.cpp`)은 `"Shaders/default.vert"` 참조 → 빌드 디렉토리에 복사되지 않아 항상 실패

### 적용 결과

**CMakeLists.txt** — POST_BUILD 복사 규칙 추가:
```cmake
add_custom_command(TARGET molga_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/src/Shaders $<TARGET_FILE_DIR:molga_engine>/Shaders
    COMMENT "Copying shaders to build directory"
)
add_custom_command(TARGET molga_runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/src/Shaders $<TARGET_FILE_DIR:molga_runtime>/Shaders
    COMMENT "Copying shaders to build directory (runtime)"
)
```

**main.cpp** — 셰이더 경로 통일:
```cpp
// Before:
g_shader = std::make_unique<Shader>("src/Shaders/default.vert", "src/Shaders/default.frag");
// After:
auto shader = std::make_unique<Shader>("Shaders/default.vert", "Shaders/default.frag");
```

`runtime_main.cpp`는 이미 `"Shaders/default.vert"` 사용 중 — 변경 불필요.

### 변경 파일
- `CMakeLists.txt`
- `src/main.cpp`

---

## 2.3 GameBuilder 경로 상수화 ✅

### 적용 결과

`GameBuilder.cpp`의 하드코딩된 문자열을 `Paths::` 상수로 교체:

| 이전 | 이후 |
|------|------|
| `"assets"` | `Paths::Build::ASSETS` |
| `"/assets"` | `"/" + Paths::Build::ASSETS` |
| `"src/Shaders"` | `Paths::Engine::SHADER_SRC_DIR` |
| `"/Shaders"` | `"/" + Paths::Build::SHADERS` |
| `"/scenes"` | `"/" + Paths::Build::SCENES` |
| `"scenes/main.json"` | `Paths::Build::SCENES + "/main.json"` |
| `"scenes/"` | `Paths::Build::SCENES + "/"` |

### 변경 파일
- `src/Core/GameBuilder.cpp`

---

## 2.4 `external/glm` 참조 제거 ✅

`external/glm` 디렉토리는 존재하지 않음. 두 파일에서 참조 제거:

**ScriptCompiler.cpp** — `include_directories(${MOLGA_ENGINE_PATH}/external/glm)` 행 제거 (코멘트로 대체).

**VSCodeIntegration.cpp** — `GetEngineIncludePaths()`에서 `external/glm` 경로 제거.

### 변경 파일
- `src/Scripting/ScriptCompiler.cpp`
- `src/Editor/VSCodeIntegration.cpp`

---

## 2.5 Application 클래스 삭제 ✅

### 검증 결과

- `Application.h`를 include하는 파일은 `Application.cpp`뿐
- `Application::Get()` 호출 없음 (codebase 전체 검색)
- `CMakeLists.txt:88`에 `EDITOR_SOURCES`로 컴파일되었으나 어디서도 사용되지 않는 데드 코드

### 적용 결과

1. `src/Core/Application.h` 삭제
2. `src/Core/Application.cpp` 삭제
3. `CMakeLists.txt`: `EDITOR_SOURCES`에서 `src/Core/Application.cpp` 제거
4. `src/Scenes/MenuScene.h:28`: 주석 `// Screen dimensions (will be passed or obtained from Application)` → `// Screen dimensions`

### Application이 제공하던 기능과 현재 상태

| Application 기능 | 현재 대체 |
|---|---|
| GLFW/GLAD Init | `Bootstrap.cpp`의 `EngineInit()` |
| Window 콜백 | `main.cpp` / `runtime_main.cpp`에서 직접 설정 |
| 메인 루프 | `main.cpp` / `runtime_main.cpp`에서 직접 관리 |
| ImGui 메뉴/Stats | 실제 사용되지 않았음 (하드코딩 placeholder) |
| Maximized 힌트 | main.cpp에서 사용하지 않았음 (향후 필요시 WindowConfig 확장) |

### 삭제 파일
- `src/Core/Application.h`
- `src/Core/Application.cpp`

### 변경 파일
- `CMakeLists.txt`
- `src/Scenes/MenuScene.h`

---

## 2.6 Bootstrap 모듈 추출 ✅

공통 GLFW/GLAD/subsystem 초기화 코드를 `EngineInit()`/`EngineShutdown()`으로 추출.

### 신규 파일

**`src/Core/Bootstrap.h`**:
```cpp
struct WindowConfig {
    std::string title = "Molga Engine";
    int width = 800;
    int height = 600;
    bool fullscreen = false;
};

GLFWwindow* EngineInit(const WindowConfig& config);
void EngineShutdown();
```

**`src/Core/Bootstrap.cpp`** — 추출된 코드:
- `glfwInit()` + 5개 WindowHint (`#ifdef __APPLE__` 포함)
- `glfwCreateWindow()` (fullscreen monitor 지원)
- `glfwMakeContextCurrent()` + `glfwSetFramebufferSizeCallback()`
- `gladLoadGLLoader()` (실패 시 window 정리)
- `glEnable(GL_BLEND)` + `glBlendFunc()`
- `Time::Init()`, `Input::Init(window)`, `Audio::Init()`
- `EngineShutdown()`: `Audio::Shutdown()` + `glfwTerminate()`
- `static FramebufferSizeCallback()` 내부 정의

### main.cpp 변경

```cpp
// Before (53-86행, ~33줄):
glfwInit();
glfwWindowHint(...);
// ... GLFW, GLAD, blending, Time, Input, Audio 초기화 ...

// After (4줄):
WindowConfig wc;
wc.title = "Molga Engine";
wc.width = SCR_WIDTH;
wc.height = SCR_HEIGHT;
GLFWwindow* window = EngineInit(wc);
if (!window) return -1;
```

`ImGuiLayer::Init(window)`는 에디터 전용이므로 `EngineInit` 이후 별도 호출.

파일 끝 cleanup: `Audio::Shutdown(); glfwTerminate();` → `EngineShutdown();`

### runtime_main.cpp 변경

```cpp
// Before (76-112행, ~36줄):
glfwInit();
// ... 동일 초기화 ...

// After (6줄):
WindowConfig wc;
wc.title = config.gameName;
wc.width = config.windowWidth;
wc.height = config.windowHeight;
wc.fullscreen = config.fullscreen;
GLFWwindow* window = EngineInit(wc);
if (!window) return -1;
```

### 초기화 순서 보존

`Time::Init()` → `Input::Init(window)` → `Audio::Init()` (양쪽 원본과 동일)

### 변경 파일
- `src/Core/Bootstrap.h` (신규)
- `src/Core/Bootstrap.cpp` (신규)
- `CMakeLists.txt` (`ENGINE_SOURCES`에 `Bootstrap.cpp` 추가)
- `src/main.cpp`
- `src/runtime_main.cpp`

---

## 2.7 글로벌 변수 → main() 로컬 스코프 ✅

### 이전 상태

```cpp
// main.cpp 파일 스코프
std::unique_ptr<Renderer> g_renderer;
std::unique_ptr<Shader> g_shader;
std::unique_ptr<Camera2D> g_camera;
std::vector<std::shared_ptr<GameObject>> g_editorObjects;

// runtime_main.cpp 파일 스코프
std::unique_ptr<Renderer> g_renderer;
std::unique_ptr<Shader> g_shader;
std::unique_ptr<Camera2D> g_camera;
std::vector<std::shared_ptr<GameObject>> g_gameObjects;
```

### 적용 결과

외부 파일에서 `extern`으로 참조하지 않음을 확인. `main()` 내 로컬 변수로 이동하고 `g_` 접두사 제거:

```cpp
int main(int argc, char* argv[]) {
    // ...
    auto renderer = std::make_unique<Renderer>();
    auto shader = std::make_unique<Shader>(...);
    auto camera = std::make_unique<Camera2D>(...);
    std::vector<std::shared_ptr<GameObject>> editorObjects;
    // ...
}
```

**`g_window` 유지**: `MenuScene.cpp`가 `extern GLFWwindow* g_window`를 2곳에서 사용 (lines 112, 124). 파일 스코프 글로벌로 유지. Phase 4 에디터 리팩토링에서 Scene이 window 접근 방식을 개선할 때 제거 예정.

### 변경 파일
- `src/main.cpp`
- `src/runtime_main.cpp`

---

## 검증 결과

- Clean rebuild: `rm -rf build && cmake -B build && cmake --build build` — 성공
- `ctest --output-on-failure` — 4/4 테스트 통과
- `ls build/Shaders/` — `default.vert`, `default.frag` 존재 확인

---

## 체크리스트

- [x] PathConstants.h 생성
- [x] CMake 셰이더 복사 규칙 추가 (molga_engine + molga_runtime)
- [x] main.cpp 셰이더 경로 `"src/Shaders/"` → `"Shaders/"` 수정
- [x] GameBuilder.cpp 경로 하드코딩 → `Paths::` 상수 치환
- [x] ScriptCompiler.cpp `external/glm` 참조 제거
- [x] VSCodeIntegration.cpp `external/glm` 참조 제거
- [x] Application.h / Application.cpp 삭제
- [x] CMakeLists.txt에서 Application.cpp 제거
- [x] MenuScene.h 주석 정리
- [x] Bootstrap.h / Bootstrap.cpp 생성
- [x] CMakeLists.txt에 Bootstrap.cpp 추가
- [x] main.cpp: 부트스트랩 코드 → `EngineInit()` 호출로 대체
- [x] main.cpp: `framebuffer_size_callback()` 삭제 (Bootstrap 내부로 이동)
- [x] main.cpp: cleanup → `EngineShutdown()` 호출로 대체
- [x] runtime_main.cpp: 동일 패턴 적용
- [x] main.cpp: `g_renderer`/`g_shader`/`g_camera`/`g_editorObjects` → 로컬 변수
- [x] runtime_main.cpp: `g_renderer`/`g_shader`/`g_camera`/`g_gameObjects` → 로컬 변수
- [x] `g_window` 유지 (MenuScene.cpp extern 참조)
- [x] Clean rebuild 성공
- [x] CTest 4/4 통과

## 변경 파일 요약

| 파일 | 변경 내용 |
|------|-----------|
| `src/Core/PathConstants.h` | 신규 — 경로 상수 정의 |
| `src/Core/Bootstrap.h` | 신규 — `WindowConfig`, `EngineInit()`, `EngineShutdown()` |
| `src/Core/Bootstrap.cpp` | 신규 — GLFW/GLAD/subsystem 초기화 구현 |
| `src/Core/Application.h` | 삭제 |
| `src/Core/Application.cpp` | 삭제 |
| `CMakeLists.txt` | 셰이더 복사 규칙, Application 제거, Bootstrap 추가 |
| `src/main.cpp` | Bootstrap 사용, 셰이더 경로 통일, 글로벌→로컬 |
| `src/runtime_main.cpp` | Bootstrap 사용, 글로벌→로컬 |
| `src/Core/GameBuilder.cpp` | 경로 상수화 |
| `src/Scripting/ScriptCompiler.cpp` | `external/glm` 참조 제거 |
| `src/Editor/VSCodeIntegration.cpp` | `external/glm` 참조 제거 |
| `src/Scenes/MenuScene.h` | 주석 정리 |

## Phase 4로 이관된 항목

| 항목 | 이유 |
|------|------|
| Layer/LayerStack | 에디터/런타임 분리 전략과 함께 설계 필요 |
| 싱글톤 통일 (Time/Input/Audio → Meyer's) | 호출부 변경이 codebase 전체에 파급 |
| EngineContext / Service Locator | Layer 아키텍처와 함께 도입해야 의미 있음 |
| `g_window` 제거 | MenuScene이 Scene의 window 접근 방식 개선 후 가능 |
| 플랫폼 추상화 강화 | 우선순위 낮음, Phase 5에서도 가능 |
| 초기화/종료 순서 명시화 | EngineContext 도입 시 자연스럽게 해결 |
| Assets/assets 케이싱 버그 | GameBuilder가 Project 경로를 참조하도록 재설계 필요 |
