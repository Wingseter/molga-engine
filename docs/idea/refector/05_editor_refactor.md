# Phase 4: 에디터 구조 개선

## 목표
Editor God Class 분리, 하드코딩된 문자열/경로 상수화, UI 코드 중복 제거, 에러 처리 일관성 확보.

---

## 4.1 Editor God Class 분리

### 현재 문제

Editor 클래스가 15개 이상의 책임을 보유:

```
Editor 현재 책임:
├── 메뉴바 렌더링 (File, Edit, GameObject, Window, Scripting, Build)
├── Play/Pause/Stop 컨트롤
├── 씬 관리 (NewScene, SaveScene, SaveSceneAs, OpenScene)
├── GameObject 생성 (CreateGameObject)
├── 빌드 윈도우 (RenderBuildWindow)
├── 빌드 실행 (BuildGame)
├── 스크립팅 메뉴 (RenderScriptingMenu)
├── 도킹 레이아웃 관리 (BeginDockSpace, SetupDefaultLayout)
├── 씬 뷰 렌더링 (RenderSceneView)
├── 통계 윈도우 (RenderStatsWindow)
├── 윈도우 가시성 관리 (8개 bool 플래그)
├── HierarchyWindow 소유
├── InspectorWindow 소유
├── ProjectBrowserWindow 소유
└── ScriptWindow 소유
```

### 변경 계획: 책임별 클래스 분리

```
Editor (조율자)
├── WindowManager         ← 윈도우 등록/가시성/라이프사이클
├── MenuBarManager        ← 메뉴바 렌더링 (File, Edit, Window 메뉴)
├── SceneOperations       ← NewScene, SaveScene, OpenScene
├── GameObjectFactory     ← CreateGameObject
├── PlayControlManager    ← Play, Pause, Stop 상태 전환
├── BuildManager          ← 빌드 설정 UI + 빌드 실행
└── DockspaceManager      ← 도킹 레이아웃 초기화/관리
```

### 단계적 분리 (권장 순서)

**Step 1: WindowManager 추출**
```cpp
class WindowManager {
    std::map<std::string, std::unique_ptr<EditorWindow>> windows;
    std::map<std::string, bool> visibility;

public:
    void Register(const std::string& name, std::unique_ptr<EditorWindow> window);
    void Toggle(const std::string& name);
    bool IsVisible(const std::string& name) const;
    void RenderAll();
    void RenderWindowMenu();  // Window 메뉴 아이템 자동 생성
};
```

**Step 2: SceneOperations 추출**
```cpp
class SceneOperations {
    std::string currentScenePath;
    bool sceneModified = false;

public:
    void NewScene(std::vector<std::shared_ptr<GameObject>>& gameObjects);
    bool SaveScene(const std::vector<std::shared_ptr<GameObject>>& gameObjects);
    bool SaveSceneAs(const std::vector<std::shared_ptr<GameObject>>& gameObjects);
    bool OpenScene(std::vector<std::shared_ptr<GameObject>>& gameObjects);
    const std::string& GetCurrentPath() const;
};
```

**Step 3: BuildManager 추출**
```cpp
class BuildManager {
    BuildSettings settings;
    bool showBuildWindow = false;

public:
    void RenderBuildWindow();
    void Build();
};
```

### 대상 파일
- `src/Editor/Editor.h` / `src/Editor/Editor.cpp`
- 새 파일: `src/Editor/WindowManager.h/cpp`
- 새 파일: `src/Editor/SceneOperations.h/cpp`
- 새 파일: `src/Editor/BuildManager.h/cpp`

---

## 4.2 하드코딩된 문자열/경로 상수화

### 현재 문제: 수집된 하드코딩 목록

**에디터 UI 문자열:**
| 파일 | 위치 | 값 | 용도 |
|------|------|-----|------|
| Editor.cpp | Line 81 | `"MolgaDockSpace"` | Dockspace ID |
| Editor.cpp | Lines 107-112 | `"Hierarchy"`, `"Inspector"` 등 | 도킹 윈도우 이름 |
| Editor.cpp | Line 397 | `"scene.json"` | 기본 씬 이름 |

**빌드/스크립팅 경로:**
| 파일 | 위치 | 값 | 용도 |
|------|------|-----|------|
| ScriptCompiler.cpp | Line 20 | `"Scripts"` | 스크립트 디렉토리 |
| ScriptCompiler.cpp | Line 224 | `"Debug"` | 빌드 타입 |
| GameBuilder.cpp | Line 83 | `"assets"` | 에셋 디렉토리 |
| GameBuilder.cpp | Line 99 | `"src/Shaders"` | 셰이더 경로 |
| GameBuilder.cpp | Lines 176-180 | `"build/molga_runtime"` | 런타임 경로 |
| Project.cpp | Line 143 | `"project.molga"` | 프로젝트 파일 |
| Project.cpp | Line 255 | `".molga"` | 설정 디렉토리 |

**UI 스타일 상수:**
| 파일 | 위치 | 값 | 용도 |
|------|------|-----|------|
| Editor.cpp | Line 282 | `ImVec4(0.2f, 0.6f, 0.2f, 1.0f)` | Play 버튼 색상 |
| Editor.cpp | Line 288 | `ImVec4(0.8f, 0.6f, 0.2f, 1.0f)` | Pause 버튼 색상 |
| Editor.cpp | Line 294 | `ImVec4(0.7f, 0.2f, 0.2f, 1.0f)` | Stop 버튼 색상 |
| ProjectBrowserWindow.cpp | Lines 221-228 | 파일 타입별 색상 | 파일 아이콘 색상 |

### 변경 계획

```cpp
// src/Editor/EditorConstants.h
namespace EditorConstants {
    // Dockspace
    constexpr const char* DOCKSPACE_ID = "MolgaDockSpace";

    // Window Names
    constexpr const char* WIN_HIERARCHY = "Hierarchy";
    constexpr const char* WIN_INSPECTOR = "Inspector";
    constexpr const char* WIN_PROJECT_BROWSER = "Project Browser";
    constexpr const char* WIN_SCENE = "Scene";
    constexpr const char* WIN_SCRIPT = "Script Editor";
    constexpr const char* WIN_STATS = "Stats";

    // Default Files
    constexpr const char* DEFAULT_SCENE = "scene.json";
    constexpr const char* PROJECT_FILE_EXT = ".molga";
}

// src/Core/PathConstants.h
namespace Paths {
    constexpr const char* SCRIPTS_DIR = "Scripts";
    constexpr const char* ASSETS_DIR = "assets";
    constexpr const char* SHADERS_DIR = "src/Shaders";
    constexpr const char* BUILD_DIR = "build";
    constexpr const char* SCENES_DIR = "scenes";
    constexpr const char* CONFIG_DIR = ".molga";
    constexpr const char* PROJECT_FILE = "project.molga";
    constexpr const char* RECENT_PROJECTS_FILE = "recent_projects.json";
}

// src/Editor/EditorTheme.h
namespace EditorTheme {
    const ImVec4 PLAY_BUTTON = ImVec4(0.2f, 0.6f, 0.2f, 1.0f);
    const ImVec4 PAUSE_BUTTON = ImVec4(0.8f, 0.6f, 0.2f, 1.0f);
    const ImVec4 STOP_BUTTON = ImVec4(0.7f, 0.2f, 0.2f, 1.0f);
    // 파일 타입 색상 등
}
```

### 대상 파일
- 새 파일: `src/Editor/EditorConstants.h`
- 새 파일: `src/Core/PathConstants.h`
- 새 파일: `src/Editor/EditorTheme.h`
- 위 테이블의 모든 파일에서 상수 참조로 교체

---

## 4.3 UI 코드 중복 제거

### 현재 문제

**파일 타입 아이콘/색상 매핑 2중 정의:**
- `ProjectBrowserWindow.cpp:221-228` - DrawFileGrid()에서 확장자→색상
- `ProjectBrowserWindow.cpp:310-321` - GetFileIcon()에서 확장자→아이콘

**컴포넌트 아이콘 매핑 2중 정의:**
- `InspectorWindow.cpp:119-123` - 컴포넌트 타입→아이콘
- `HierarchyWindow.cpp:75-78` - TODO로만 기재 (구현 대기)

### 변경 계획

```cpp
// src/Editor/UIRegistry.h
class UIRegistry {
public:
    struct FileTypeInfo {
        const char* icon;
        ImVec4 color;
        const char* description;
    };

    struct ComponentInfo {
        const char* icon;
        const char* displayName;
    };

    static const FileTypeInfo& GetFileTypeInfo(const std::string& extension);
    static const ComponentInfo& GetComponentInfo(const std::string& typeName);

private:
    static const std::unordered_map<std::string, FileTypeInfo> fileTypes;
    static const std::unordered_map<std::string, ComponentInfo> componentTypes;
};
```

### 대상 파일
- 새 파일: `src/Editor/UIRegistry.h` / `src/Editor/UIRegistry.cpp`
- `src/Editor/Windows/ProjectBrowserWindow.cpp`
- `src/Editor/Windows/InspectorWindow.cpp`
- `src/Editor/Windows/HierarchyWindow.cpp`

---

## 4.4 에러 처리 일관성 확보

### 현재 문제: 에러 처리 패턴 혼재

| 파일 | 패턴 | 문제 |
|------|------|------|
| Editor.cpp:206 | MenuItem "Exit" 구현 없음 | 무시됨 |
| Editor.cpp:212-216 | Undo/Redo 빈 구현 | 사용자 혼란 |
| ProjectBrowserWindow.cpp:179-181 | `catch` 블록 무시 | 에러 로깅 없음 |
| ScriptCompiler.cpp:228 | `"error"` 문자열 검색으로 실패 판정 | 불안정 |
| SceneSerializer.cpp | `std::cerr` + `return bool` | 일관된 패턴 |
| GameBuilder.cpp | `lastError` 문자열 + `return bool` | 일관된 패턴 |

### 변경 계획: 통일된 로깅 및 에러 패턴

```cpp
// 로깅 유틸리티
namespace Log {
    void Info(const std::string& msg);
    void Warn(const std::string& msg);
    void Error(const std::string& msg);
}

// 미구현 기능 명시
if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
    Log::Warn("Undo is not yet implemented");
}

// 예외 로깅 통일
} catch (const std::exception& e) {
    Log::Error("Failed to scan directory: " + std::string(e.what()));
}
```

### 대상 파일
- 새 파일: `src/Common/Log.h` / `src/Common/Log.cpp` (또는 기존 로깅에 통합)
- 에러 처리가 불일치한 모든 파일

---

## 4.5 ScriptCompiler 명령 실행 안정화

### 현재 문제

```cpp
// ScriptCompiler.cpp:224-228
std::string configureCmd = "cd \"" + scriptsPath +
    "\" && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug 2>&1";

if (configResult != 0 && output.find("error") != std::string::npos) {
    // "error" 문자열이 없으면 실패해도 성공 처리
}
```

- 빌드 타입 "Debug" 하드코딩
- 에러 판정이 `"error"` 문자열 검색에 의존 (불안정)
- exit code만으로 판정해야 함

### 변경 계획

```cpp
// exit code 기반 판정
if (configResult != 0) {
    lastError = "CMake configuration failed (exit code: " +
                std::to_string(configResult) + ")\n" + output;
    isCompiling = false;
    return false;
}
```

### 대상 파일
- `src/Scripting/ScriptCompiler.cpp`

---

## 4.6 도킹 윈도우 이름 불일치 수정

> 출처: Codex 분석 - Editor.cpp:110 vs ProjectBrowserWindow.cpp:10

### 현재 문제

```cpp
// Editor.cpp:110 - 도킹에서 사용하는 이름
ImGui::DockBuilderDockWindow("Project Browser", ...);

// ProjectBrowserWindow.cpp:10 - 실제 창 이름
ImGui::Begin("Project");  // "Project Browser"가 아님!
```

도킹 시스템이 "Project Browser"를 찾지만 실제 창은 "Project"로 생성되어, 도킹이 올바르게 작동하지 않을 수 있음.

### 변경 계획
- EditorConstants.h의 상수를 도킹과 윈도우 생성 모두에서 동일하게 참조
- 현재 불일치하는 모든 윈도우 이름을 대조하여 통일

### 대상 파일
- `src/Editor/Editor.cpp`
- `src/Editor/Windows/ProjectBrowserWindow.cpp`

---

## 4.7 Scene View 실제 렌더 타깃 구현

> 출처: Codex 분석 - Editor.cpp:157

### 현재 문제

Scene View가 placeholder 사각형으로만 표시. 실제 씬을 렌더링하지 않음.

### 변경 계획

Framebuffer Object(FBO)를 사용하여 씬을 오프스크린 렌더링 후 ImGui 텍스처로 표시:

```cpp
// 1. FBO 생성 및 씬 렌더링
glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
renderer->Clear(...);
renderer->Begin(shader, camera);
// 게임 오브젝트 렌더링
renderer->End();
glBindFramebuffer(GL_FRAMEBUFFER, 0);

// 2. ImGui에서 FBO 텍스처 표시
ImGui::Image((void*)(intptr_t)sceneTexture, ImGui::GetContentRegionAvail());
```

### 대상 파일
- `src/Editor/Editor.cpp` (RenderSceneView)
- 새 파일 또는 Renderer 확장: FBO 관리

---

## 4.8 HierarchyWindow TODO 항목 구현

> 출처: Codex 분석 - HierarchyWindow.cpp:22, 43, 91

### 현재 문제

HierarchyWindow에서 다음 기능이 TODO로 남아 있음:
- 게임 오브젝트 생성 (컨텍스트 메뉴)
- 이름 변경 (더블 클릭)
- 복제
- 삭제

### 변경 계획

컨텍스트 메뉴에 실제 명령 연결:

```cpp
if (ImGui::MenuItem("Create Empty")) {
    auto obj = std::make_shared<GameObject>("New GameObject");
    obj->AddComponent<Transform>();
    gameObjects->push_back(obj);
}
if (ImGui::MenuItem("Delete") && selectedObject) {
    // gameObjects에서 제거
}
if (ImGui::MenuItem("Duplicate") && selectedObject) {
    // 딥 카피 생성
}
```

### 대상 파일
- `src/Editor/Windows/HierarchyWindow.cpp`

---

## 4.9 ScriptManager 핫리로드 builtin 보호

> 출처: Codex 분석 - ScriptManager.cpp:87

### 현재 문제

```cpp
// ScriptManager.cpp:87
void ScriptManager::ReloadScriptLibraries() {
    scriptFactories.clear();  // builtin 스크립트도 함께 삭제!
    // ...
}
```

`ReloadScriptLibraries()` 호출 시 `scriptFactories`를 전부 비워서 `RegisterBuiltinScripts()`로 등록한 PlayerController, Rotator, Oscillator까지 사라짐.

### 변경 계획

builtin과 dynamic 레지스트리를 분리:

```cpp
class ScriptManager {
    // builtin: 엔진에 내장된 스크립트 (절대 삭제되지 않음)
    std::unordered_map<std::string, ScriptFactory> builtinFactories;

    // dynamic: 사용자 스크립트 (핫리로드 시 교체)
    std::unordered_map<std::string, ScriptFactory> dynamicFactories;

public:
    void RegisterBuiltin(const std::string& name, ScriptFactory factory);
    void RegisterDynamic(const std::string& name, ScriptFactory factory);

    void ReloadScriptLibraries() {
        dynamicFactories.clear();  // dynamic만 삭제
        // 라이브러리 리로드 후 dynamic 재등록
    }

    Script* CreateScript(const std::string& name) {
        // dynamic 우선, 없으면 builtin 탐색
        auto it = dynamicFactories.find(name);
        if (it != dynamicFactories.end()) return it->second();
        it = builtinFactories.find(name);
        if (it != builtinFactories.end()) return it->second();
        return nullptr;
    }
};
```

### 대상 파일
- `src/Scripting/ScriptManager.h` / `src/Scripting/ScriptManager.cpp`
- `src/Scripting/BuiltinScripts.cpp` (RegisterBuiltin 호출로 변경)

---

## 4.10 Project/GameBuilder의 에디터 패키지 분류

> 출처: Gemini 분석 - 코어 로직과 에디터 전용 도구 분리

### 현재 문제

`Project`와 `GameBuilder`는 `src/Core/`에 있지만, 런타임에서는 사용하지 않는 에디터 전용 도구.

### 변경 계획

```
// 현재
src/Core/
├── Application.h/cpp    ← 코어 (에디터+런타임)
├── Project.h/cpp        ← 에디터 전용
├── GameBuilder.h/cpp    ← 에디터 전용
├── SceneSerializer.h/cpp ← 코어 (에디터+런타임)
└── TextureManager.h/cpp  ← 코어 (에디터+런타임)

// 변경
src/Core/                ← 에디터+런타임 공통
├── Application.h/cpp
├── SceneSerializer.h/cpp
└── TextureManager.h/cpp

src/Editor/              ← 에디터 전용
├── Project.h/cpp        ← 이동
├── GameBuilder.h/cpp    ← 이동
└── ...
```

CMakeLists.txt의 `EDITOR_SOURCES`에만 포함되도록 이동.

### 대상 파일
- `src/Core/Project.h/cpp` → `src/Editor/Project.h/cpp`
- `src/Core/GameBuilder.h/cpp` → `src/Editor/GameBuilder.h/cpp`
- `CMakeLists.txt`

---

## 체크리스트

- [x] EditorConstants.h 생성 및 문자열 상수화 ✅
- [x] EditorTheme.h 생성 및 스타일 상수화 ✅
- [x] PathConstants.h 확장 (Config 네임스페이스 추가) ✅
- [x] 에러 처리 패턴 통일 (Log 유틸리티: `src/Common/Log.h/cpp`) ✅
- [x] 미구현 메뉴 아이템에 경고 메시지 (Undo, Redo, Exit) ✅
- [x] ScriptCompiler 에러 판정을 exit code 기반으로 변경 ✅
- [x] ScriptManager builtin/dynamic 레지스트리 분리 (핫리로드 버그 수정) ✅
- [x] 도킹 윈도우 이름 불일치 수정 ("Project" → "Project Browser") ✅
- [x] UIRegistry 구현 (파일 타입/컴포넌트 아이콘 통합) ✅
- [x] Project, GameBuilder를 Editor 디렉토리로 이동 ✅
- [x] WindowManager 클래스 추출 (윈도우 등록/가시성/렌더링) ✅
- [x] SceneViewWindow, StatsWindow를 EditorWindow 서브클래스로 전환 ✅
- [x] SceneOperations 클래스 추출 (씬 CRUD) ✅
- [x] BuildManager 클래스 추출 (빌드 설정/실행) ✅
- [x] HierarchyWindow 생성/삭제/복제/이름변경 구현 ✅
- [ ] Scene View FBO 기반 실제 렌더링 구현 → Phase 5로 이관 (렌더 파이프라인 경계 변경 필요)
