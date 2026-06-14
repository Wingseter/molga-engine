# Task 0-3: PathService + 신뢰성 있는 빌드 + 런타임 에셋 resolve

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 선행: [task-0-5a](task-0-5a_test_framework.md), [task-0-2](task-0-2_scene_document_and_play_world.md). 체크박스로 추적.

**Goal:** 모든 경로를 현재 작업 디렉터리(cwd) 의존에서 벗어나 절대 경로로 관리하고, 프로젝트 에셋·셰이더·씬을 신뢰성 있게 빌드한다. 필수 파일 누락 시 빌드를 실패시키고, 위험한 출력 경로 삭제를 막는다. 빌드된 런타임이 텍스처를 에디터와 동일하게 표시하게 한다(P0-5).

**Architecture:** `PathService`(molga_core)가 실행 파일 위치를 기준으로 엔진 리소스/프로젝트/에셋 경로를 절대화한다. 에셋 로딩은 `Component::ResolveAssets()`(지연 로드)로 GL 컨텍스트가 있는 시점에만 수행한다. `TextureManager`를 `molga_core`로 옮겨 런타임도 텍스처를 로드한다. 빌드는 `BuildManifest`로 필수 파일을 검증한 뒤 진행한다.

**Tech Stack:** C++17 `<filesystem>`, 플랫폼 실행 파일 경로 API, doctest

**닫는 결함:** 갭 분석 P0-3(경로/빌드 신뢰성), P0-5(런타임 에셋) (`docs/plan/2026-06-06_project_gap_analysis.md` §3 P0-3·P0-5, §5.2)

---

## 현재 상태 (검증된 사실)

- 셰이더는 `"Shaders/default.vert"` cwd 상대 경로로 로드(`main.cpp:58`, `runtime_main.cpp:81`). 엔진 루트는 `current_path().parent_path()`로 추정(`Editor.cpp:50-54`).
- `SceneOperations::SaveSceneAs/OpenScene`는 `EditorConstants::DEFAULT_SCENE_FILE="scene.json"`(cwd 상대)을 하드코딩. `Project::GetScenesPath()` 미사용.
- `GameBuilder`:
  - `CreateOutputDirectory`가 검증 없이 `fs::remove_all(settings.outputPath)` (기본 `"build/export"`, 사용자 편집 가능) 실행(`GameBuilder.cpp:72`).
  - `CopyAssets`는 cwd 상대 `"assets"` 복사, 없어도 `return true`(`:84,91`). `CopyShaders`는 cwd 상대 `"src/Shaders"`, 없어도 `return true`(`:100,107`). `CopyScenes`는 `settings.mainScene` 없어도 `return true`(`:120,134`).
  - `CopyExecutable`는 `"build/molga_runtime"`(cwd 상대), 없으면 **에디터 바이너리** `"build/molga_engine"`로 폴백(`:177,181`).
- 런타임은 `game.json`/`Shaders/`/`scenes/main.json`/`assets`를 전부 cwd 상대로 찾음(`runtime_main.cpp`).
- `SpriteRenderer::Deserialize`는 `texturePath`만 저장하고 **텍스처를 로드하지 않음**(`SpriteRenderer.cpp:84-86`). 텍스처 로드는 `OnInspectorGUI`(MOLGA_EDITOR)와 드롭 타깃에서만 발생(`:126,145`). `SpriteRenderer.cpp:13`이 `Editor/Project.h`를 무조건 include(core ↔ editor 경계 위반).
- `TextureManager`(`src/Core/TextureManager.h`)는 헤더는 Core에 있으나 **구현이 Editor 타깃에만** 컴파일됨. 런타임은 텍스처를 로드하지 않음.
- `PathConstants.h`: project는 `"Assets"/"Scenes"`(대문자), build는 `"assets"/"scenes"`(소문자) — **케이싱 불일치**.
- GameBuilder/Project 테스트 없음. 둘 다 editor 타깃 소스.

---

## 파일 구조

**Files:**
- Create: `src/Core/PathService.h` / `src/Core/PathService.cpp`
- Create: `src/Core/BuildManifest.h` / `src/Core/BuildManifest.cpp` (필수 파일 검증, core)
- Create: `tests/test_path_service.cpp`
- Create: `tests/test_game_builder.cpp`
- Modify: `src/ECS/Component.h` (`virtual void ResolveAssets() {}`)
- Modify: `src/ECS/GameObject.h` / `GameObject.cpp` (`ResolveAssets()`)
- Modify: `src/Core/World.h` / `World.cpp` (`ResolveAssets()`)
- Modify: `src/ECS/Components/SpriteRenderer.h` / `SpriteRenderer.cpp` (지연 텍스처 로드, Project.h 가드)
- Modify: `src/main.cpp` (PathService init, 셰이더 경로, 프로젝트 열기 시 asset root, ResolveAssets 호출)
- Modify: `src/runtime_main.cpp` (PathService init, 모든 경로 exe 기준, ResolveAssets)
- Modify: `src/Editor/Editor.cpp` (engine path를 PathService로)
- Modify: `src/Editor/SceneOperations.cpp` (프로젝트 Scenes 아래 저장/열기)
- Modify: `src/Editor/GameBuilder.cpp` / `GameBuilder.h` (절대 경로, 검증, 안전 삭제, manifest)
- Modify: `CMakeLists.txt` (`PathService.cpp`/`BuildManifest.cpp` + `TextureManager.cpp`를 ENGINE_SOURCES로)
- Modify: `tests/CMakeLists.txt` (test_path_service, test_game_builder)

---

## Task A. PathService (TDD)

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_path_service.cpp`:
```cpp
#include "Core/PathService.h"
#include "doctest.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("Resolve passes absolute through and joins relative under root") {
    CHECK(PathService::Resolve("/proj", "/abs/x.png") == "/abs/x.png");
    CHECK(PathService::Resolve("/proj", "Assets/x.png") ==
          fs::path("/proj/Assets/x.png").string());
    CHECK(PathService::Resolve("/proj", "").empty());
}

TEST_CASE("IsSafeOutputPath rejects dangerous targets") {
    std::string why;
    CHECK_FALSE(PathService::IsSafeOutputPath("", why));
    CHECK_FALSE(PathService::IsSafeOutputPath(fs::path("/"), why));
    CHECK_FALSE(PathService::IsSafeOutputPath(fs::path("."), why));
}

TEST_CASE("IsSafeOutputPath accepts a normal nested export dir") {
    std::string why;
    CHECK(PathService::IsSafeOutputPath(fs::path("/tmp/molga_export_abc/dist"), why));
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_path_service test_path_service.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_path_service -j4
```
Expected: FAIL — `PathService.h` 없음.

- [ ] **Step 3: PathService.h 작성**

Create `src/Core/PathService.h`:
```cpp
#pragma once

#include <filesystem>
#include <string>

// 실행 파일 위치를 기준으로 모든 경로를 절대화하는 서비스(싱글톤).
class PathService {
public:
    static PathService& Get();

    // argv[0]로 실행 파일 디렉터리를 확정한다(플랫폼별).
    void InitFromExecutable(const char* argv0);

    const std::filesystem::path& ExecutableDir() const { return executableDir_; }

    // 실행 파일 옆에 배포되는 엔진 리소스(Shaders/ 등).
    std::filesystem::path EngineResource(const std::string& rel) const {
        return executableDir_ / rel;
    }

    // 에셋 경로 해석의 기준 루트(에디터=프로젝트 루트, 런타임=실행 파일 디렉터리).
    void SetAssetRoot(const std::filesystem::path& root) { assetRoot_ = root; }
    const std::filesystem::path& AssetRoot() const { return assetRoot_; }

    // 저장된 에셋 경로(상대/절대)를 절대 경로 문자열로 해석.
    std::string ResolveAsset(const std::string& stored) const {
        const std::filesystem::path& root = assetRoot_.empty() ? executableDir_ : assetRoot_;
        return Resolve(root, stored);
    }

    // ── 순수 헬퍼(테스트 대상) ──────────────────────────────────────────────
    static std::string Resolve(const std::filesystem::path& root, const std::string& stored);
    static bool IsSafeOutputPath(const std::filesystem::path& path, std::string& reason);

private:
    PathService() = default;
    std::filesystem::path executableDir_;
    std::filesystem::path assetRoot_;
};
```

- [ ] **Step 4: PathService.cpp 작성**

Create `src/Core/PathService.cpp`:
```cpp
#include "Core/PathService.h"
#include <cstdlib>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

PathService& PathService::Get() {
    static PathService instance;
    return instance;
}

void PathService::InitFromExecutable(const char* argv0) {
    fs::path exePath;

#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        exePath = fs::path(buf.data());
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; exePath = fs::path(buf); }
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0) exePath = fs::path(std::wstring(buf, n));
#endif

    if (exePath.empty() && argv0) {
        exePath = fs::path(argv0);          // 폴백
    }

    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(exePath, ec);
    if (ec) canonical = exePath;
    executableDir_ = canonical.parent_path();
}

std::string PathService::Resolve(const fs::path& root, const std::string& stored) {
    if (stored.empty()) return "";
    fs::path p(stored);
    if (p.is_absolute()) return stored;
    return (root / p).string();
}

bool PathService::IsSafeOutputPath(const fs::path& path, std::string& reason) {
    if (path.empty()) { reason = "empty path"; return false; }
    fs::path p = path.lexically_normal();
    if (p == p.root_path()) { reason = "filesystem root"; return false; }
    if (p == fs::path(".") || p == fs::path("..")) { reason = "current/parent directory"; return false; }

    if (const char* home = std::getenv("HOME")) {
        if (!std::string(home).empty() && p == fs::path(home).lexically_normal()) {
            reason = "home directory"; return false;
        }
    }
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (!ec && p == cwd.lexically_normal()) { reason = "current working directory"; return false; }

    return true;
}
```

- [ ] **Step 5: ENGINE_SOURCES에 추가 + 테스트 통과 + 커밋**

`CMakeLists.txt`의 `ENGINE_SOURCES`에 추가:
```cmake
    src/Core/PathService.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_path_service -j4
ctest --preset debug -R test_path_service --output-on-failure
```
Expected: PASS, `3 | 3 passed`.
```bash
git add src/Core/PathService.h src/Core/PathService.cpp tests/test_path_service.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(core): PathService for executable-relative path resolution"
```

---

## Task B. 셰이더/엔진 경로를 PathService로

- [ ] **Step 1: 에디터/런타임에서 실행 파일 기준 init + 셰이더 경로**

`src/main.cpp`의 `int main(int argc, char** argv)` 시그니처를 확인하고(필요 시 `argc, argv`를 받도록 수정), 초기화 가장 앞부분에 추가:
```cpp
    PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);
```
상단 include에 추가:
```cpp
#include "Core/PathService.h"
```
셰이더 로드(`main.cpp:58`)
```cpp
    auto shader = std::make_unique<Shader>("Shaders/default.vert", "Shaders/default.frag");
```
를 다음으로 교체:
```cpp
    auto vertPath = PathService::Get().EngineResource("Shaders/default.vert").string();
    auto fragPath = PathService::Get().EngineResource("Shaders/default.frag").string();
    auto shader = std::make_unique<Shader>(vertPath.c_str(), fragPath.c_str());
```
`src/runtime_main.cpp`도 동일하게: `main` 시작에 `PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);` 추가(시그니처에 `argc/argv` 필요), include 추가, 셰이더 경로(`runtime_main.cpp:81`)를 위와 같은 `EngineResource(...)` 방식으로 교체.

- [ ] **Step 2: Editor.cpp 엔진 경로를 PathService로**

`src/Editor/Editor.cpp`의 엔진 경로 추정(`:50-54`)
```cpp
    std::string enginePath =
        std::filesystem::current_path().parent_path().string();
```
를 다음으로 교체:
```cpp
    std::string enginePath = PathService::Get().ExecutableDir().string();
```
상단 include에 `#include "Core/PathService.h"` 추가.

- [ ] **Step 3: 빌드 + 임의 디렉터리 실행 확인 + 커밋**

Run:
```bash
cmake --build --preset debug -j4
# 임의 디렉터리에서 실행해도 셰이더가 로드되어야 한다(에디터는 창이 뜨므로 즉시 종료):
( cd /tmp && /Users/kwon/Workspace/C/molga-engine/build/debug/molga_engine & sleep 2; kill %1 2>/dev/null )
```
Expected: `ERROR::SHADER::FILE_NOT_FOUND` 가 **나오지 않는다**.
```bash
git add src/main.cpp src/runtime_main.cpp src/Editor/Editor.cpp
git commit -m "fix(paths): load shaders relative to the executable (P0-3)"
```

---

## Task C. Save/Open을 프로젝트 Scenes 아래로

- [ ] **Step 1: SceneOperations가 프로젝트 경로 사용**

`src/Editor/SceneOperations.cpp` 상단 include에 추가:
```cpp
#include "Editor/Project.h"
#include <filesystem>
```
`SaveSceneAs`(현재 `:26-35`)의 첫 줄
```cpp
    currentScenePath = EditorConstants::DEFAULT_SCENE_FILE;
```
을 다음으로 교체:
```cpp
    namespace fs = std::filesystem;
    if (Project::Get().IsOpen()) {
        fs::path dir = Project::Get().GetScenesPath();
        fs::create_directories(dir);
        currentScenePath = (dir / "main.json").string();
    } else {
        currentScenePath = EditorConstants::DEFAULT_SCENE_FILE;  // 프로젝트 없을 때 폴백
    }
```
`OpenScene`(현재 `:37-46`)의
```cpp
    std::string filepath = EditorConstants::DEFAULT_SCENE_FILE;
```
을 다음으로 교체:
```cpp
    namespace fs = std::filesystem;
    std::string filepath = EditorConstants::DEFAULT_SCENE_FILE;
    if (Project::Get().IsOpen()) {
        filepath = (fs::path(Project::Get().GetScenesPath()) / "main.json").string();
    }
```
> 정식 파일 선택 dialog는 Phase 1. 슬라이스에서는 프로젝트당 `Scenes/main.json` 한 개를 사용한다.

- [ ] **Step 2: 빌드 + 커밋**

Run:
```bash
cmake --build --preset debug --target molga_engine -j4
```
```bash
git add src/Editor/SceneOperations.cpp
git commit -m "fix(editor): save/open scene under the project's Scenes directory"
```

---

## Task D. 런타임 텍스처 resolve (P0-5)

> 텍스처 로드는 GL이 필요하므로 **Deserialize에서 하지 않고**, GL 컨텍스트가 있는 시점에 호출하는 `ResolveAssets()` 단계로 분리한다(단위 테스트는 GL 없이 Deserialize만 수행).

- [ ] **Step 1: Component 베이스에 ResolveAssets 훅 추가**

`src/ECS/Component.h`의 public 가상 메서드들 근처(예: `virtual void OnInspectorGUI() {}` 아래)에 추가:
```cpp
    // 직렬화 이후, GL 컨텍스트가 있는 시점에 에셋(텍스처 등)을 지연 로드한다.
    virtual void ResolveAssets() {}
```

- [ ] **Step 2: GameObject/World에 ResolveAssets 추가**

`src/ECS/GameObject.h`의 `void StartScripts();` 아래에 추가:
```cpp
    void ResolveAssets();
```
`src/ECS/GameObject.cpp`에 추가:
```cpp
void GameObject::ResolveAssets() {
    for (auto& [id, comp] : componentMap) {
        if (comp) comp->ResolveAssets();
    }
}
```
`src/Core/World.h`의 `void LateUpdate(float dt);` 아래에 추가:
```cpp
    void ResolveAssets();   // 모든 컴포넌트의 지연 에셋 로드
```
`src/Core/World.cpp`에 추가:
```cpp
void World::ResolveAssets() {
    for (auto& o : objects_) if (o) o->ResolveAssets();
}
```

- [ ] **Step 3: TextureManager를 molga_core로 이동**

`src/Core/TextureManager.cpp`를 읽어 Editor 의존이 없는지 확인한다(Texture/stb만 사용해야 함). `CMakeLists.txt`에서 `EDITOR_SOURCES`에 있는 `src/Core/TextureManager.cpp` 줄을 **삭제**하고 `ENGINE_SOURCES`에 추가:
```cmake
    src/Core/TextureManager.cpp
```

- [ ] **Step 4: SpriteRenderer가 PathService로 텍스처를 지연 로드**

`src/ECS/Components/SpriteRenderer.cpp`에서:
- 상단의 `#include "../../Editor/Project.h"`(현재 `:13`)를 `#ifdef`로 가드:
  ```cpp
  #ifdef MOLGA_EDITOR
  #include "../../Editor/Project.h"
  #endif
  ```
- 상단 include에 추가:
  ```cpp
  #include "../../Core/PathService.h"
  #include "../../Common/Log.h"
  ```
- `Deserialize` 뒤(파일 어딘가, OnInspectorGUI 위)에 추가:
  ```cpp
  void SpriteRenderer::ResolveAssets() {
      if (texturePath.empty() || texture) return;
      std::string abs = PathService::Get().ResolveAsset(texturePath);
      texture = TextureManager::Get().Load(abs);
      if (!texture) {
          Log::Warn("SpriteRenderer", "Texture not found: " + abs);
      } else if (width == 32.0f && height == 32.0f) {
          width = static_cast<float>(texture->GetWidth());
          height = static_cast<float>(texture->GetHeight());
      }
  }
  ```
`src/ECS/Components/SpriteRenderer.h`에 override 선언 추가(`Deserialize` 선언 근처):
```cpp
    void ResolveAssets() override;
```

- [ ] **Step 5: 에디터/런타임에서 ResolveAssets 호출 + asset root 설정**

`src/main.cpp`에서, 프로젝트를 여는 지점(Project::Open 직후) 또는 씬을 로드한 직후에 추가:
```cpp
    if (Project::Get().IsOpen()) {
        PathService::Get().SetAssetRoot(Project::Get().GetPath());
    }
    sceneDoc.EditWorld().ResolveAssets();
```
또한 task-0-2에서 만든 `onEnterPlay` 콜백 끝에 추가(클론된 playWorld 텍스처 로드):
```cpp
            sceneDoc.ActiveWorld().ResolveAssets();
```
`src/runtime_main.cpp`에서 `world.LoadFromFile(...)` 직후에 추가:
```cpp
    PathService::Get().SetAssetRoot(PathService::Get().ExecutableDir());
    world.ResolveAssets();
```

- [ ] **Step 6: sortingOrder를 렌더 순서에 적용**

에디터(`main.cpp`)와 런타임(`runtime_main.cpp`)의 렌더 루프에서, `ActiveWorld().Objects()`(또는 `world.Objects()`)를 직접 순회하기 전에 sortingOrder로 정렬한 포인터 목록을 만든다. 렌더 블록을 다음 패턴으로 교체:
```cpp
            // sortingOrder 오름차순으로 그릴 스프라이트 수집
            std::vector<std::pair<int, SpriteRenderer*>> drawList;
            for (auto& obj : sceneDoc.ActiveWorld().Objects()) {   // 런타임은 world.Objects()
                if (obj && obj->IsActive()) {
                    if (auto sr = obj->GetComponent<SpriteRenderer>()) {
                        drawList.emplace_back(sr->GetSortingOrder(), sr);
                    }
                }
            }
            std::stable_sort(drawList.begin(), drawList.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);
            {
                molga::RenderPass pass(*renderer, shader.get(), camera.get());
                for (auto& [order, sr] : drawList) {
                    sr->RenderSprite(renderer.get());
                }
            }
```
`#include <algorithm>` 와 `#include "ECS/Components/SpriteRenderer.h"`가 필요하면 추가. `SpriteRenderer::GetSortingOrder()` 접근자가 없으면 `SpriteRenderer.h`에 `int GetSortingOrder() const { return sortingOrder; }`를 추가한다.

- [ ] **Step 7: 빌드 + 테스트 + 커밋**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 통과(텍스처 로드는 GL이 필요하므로 단위 테스트는 Deserialize까지만 검증하고 ResolveAssets는 호출하지 않는다 — 회귀 없음).
```bash
git add src/ECS/Component.h src/ECS/GameObject.h src/ECS/GameObject.cpp \
        src/Core/World.h src/Core/World.cpp src/ECS/Components/SpriteRenderer.h \
        src/ECS/Components/SpriteRenderer.cpp src/main.cpp src/runtime_main.cpp CMakeLists.txt
git commit -m "feat(assets): deferred texture resolve via PathService in core + runtime (P0-5)"
```

---

## Task E. 신뢰성 있는 GameBuilder (TDD)

- [ ] **Step 1: BuildManifest 테스트 작성**

Create `tests/test_game_builder.cpp`:
```cpp
#include "Core/BuildManifest.h"
#include "Core/PathService.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("BuildManifest fails and names a missing required file") {
    BuildManifest m;
    m.requiredFiles = { "/nonexistent/molga_missing_zzz.txt" };
    std::string err;
    CHECK_FALSE(m.Validate(err));
    CHECK(err.find("molga_missing_zzz") != std::string::npos);
}

TEST_CASE("BuildManifest passes when every required file exists") {
    fs::path tmp = fs::temp_directory_path() / "molga_manifest_present.txt";
    { std::ofstream f(tmp); f << "x"; }
    BuildManifest m;
    m.requiredFiles = { tmp.string() };
    std::string err;
    CHECK(m.Validate(err));
    fs::remove(tmp);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_game_builder test_game_builder.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_game_builder -j4
```
Expected: FAIL — `BuildManifest.h` 없음.

- [ ] **Step 3: BuildManifest 작성**

Create `src/Core/BuildManifest.h`:
```cpp
#pragma once

#include <string>
#include <vector>

// 빌드에 반드시 존재해야 하는 파일/디렉터리 목록과 검증.
struct BuildManifest {
    std::vector<std::string> requiredFiles;

    // 누락된 항목 목록을 반환.
    std::vector<std::string> FindMissing() const;

    // 누락이 하나라도 있으면 false + errorOut에 원인.
    bool Validate(std::string& errorOut) const;
};
```
Create `src/Core/BuildManifest.cpp`:
```cpp
#include "Core/BuildManifest.h"
#include <filesystem>

namespace fs = std::filesystem;

std::vector<std::string> BuildManifest::FindMissing() const {
    std::vector<std::string> missing;
    for (const auto& f : requiredFiles) {
        std::error_code ec;
        if (!fs::exists(f, ec)) missing.push_back(f);
    }
    return missing;
}

bool BuildManifest::Validate(std::string& errorOut) const {
    auto missing = FindMissing();
    if (missing.empty()) return true;
    errorOut = "Build aborted; missing required files:";
    for (const auto& m : missing) errorOut += "\n  - " + m;
    return false;
}
```
`CMakeLists.txt`의 `ENGINE_SOURCES`에 추가:
```cmake
    src/Core/BuildManifest.cpp
```

- [ ] **Step 4: 테스트 통과 + 커밋**

Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_game_builder -j4
ctest --preset debug -R test_game_builder --output-on-failure
```
Expected: PASS.
```bash
git add src/Core/BuildManifest.h src/Core/BuildManifest.cpp tests/test_game_builder.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(build): BuildManifest required-file validation with tests"
```

- [ ] **Step 5: GameBuilder 절대 경로 + 검증 + 안전 삭제로 재구성**

`src/Editor/GameBuilder.cpp`를 읽고 다음을 적용한다. 상단 include에 추가:
```cpp
#include "Core/PathService.h"
#include "Core/BuildManifest.h"
#include "Editor/Project.h"
```
`CreateOutputDirectory`(현재 `:68-80`)에서 `fs::remove_all(path)` 앞에 안전 검사를 넣는다. 다음 블록
```cpp
        if (fs::exists(path)) {
            // Clean existing directory
            fs::remove_all(path);
        }
```
을 다음으로 교체:
```cpp
        std::string reason;
        if (!PathService::IsSafeOutputPath(path, reason)) {
            lastError = "Refusing to use output path '" + path + "': " + reason;
            return false;
        }
        if (fs::exists(path)) {
            fs::remove_all(path);
        }
```
`CopyAssets`(현재 `:82-96`) 전체를 다음으로 교체(프로젝트 Assets, 누락 시 실패):
```cpp
bool GameBuilder::CopyAssets(const std::string& outputPath) {
    namespace fs = std::filesystem;
    if (!Project::Get().IsOpen()) {
        lastError = "No project open; cannot locate Assets to build.";
        return false;
    }
    fs::path src = Project::Get().GetAssetsPath();      // 절대 프로젝트 Assets
    if (!fs::exists(src)) {
        lastError = "Project Assets folder not found: " + src.string();
        return false;
    }
    fs::path dest = fs::path(outputPath) / "Assets";    // 케이싱 보존
    fs::create_directories(dest);
    fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    return true;
}
```
`CopyShaders`(현재 `:98-112`) 전체를 다음으로 교체(엔진 셰이더를 실행 파일 기준으로, 누락 시 실패):
```cpp
bool GameBuilder::CopyShaders(const std::string& outputPath) {
    namespace fs = std::filesystem;
    fs::path src = PathService::Get().EngineResource("Shaders");  // exe 옆 Shaders
    if (!fs::exists(src)) {
        lastError = "Engine Shaders folder not found next to the editor: " + src.string();
        return false;
    }
    fs::path dest = fs::path(outputPath) / "Shaders";
    fs::create_directories(dest);
    fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    return true;
}
```
`CopyScenes`(현재 `:114-139`)에서 `settings.mainScene`이 없으면 실패하도록, `if (fs::exists(settings.mainScene)) { ... }` 블록을 다음으로 바꾸고 그 뒤의 `return true;` 전에 실패 처리를 추가:
```cpp
    if (!fs::exists(settings.mainScene)) {
        lastError = "Main scene not found: " + settings.mainScene;
        return false;
    }
    fs::copy_file(settings.mainScene, scenesPath + "/main.json",
                  fs::copy_options::overwrite_existing);
```
`CopyExecutable`(현재 `:174-210`)에서 런타임 경로 결정부
```cpp
    std::string runtimePath = "build/molga_runtime";
    if (!fs::exists(runtimePath)) {
        runtimePath = "build/molga_engine";
    }
    if (!fs::exists(runtimePath)) {
        lastError = "Runtime executable not found. Please build the runtime first.";
        return false;
    }
```
을 다음으로 교체(에디터 실행 파일 디렉터리 기준, 에디터 바이너리 폴백 제거):
```cpp
    fs::path runtimePath = PathService::Get().ExecutableDir() / "molga_runtime";
    if (!fs::exists(runtimePath)) {
        lastError = "Runtime executable not found next to the editor: " + runtimePath.string()
                  + ". Build the molga_runtime target first.";
        return false;
    }
```
> 이하 `fs::copy_file(runtimePath, ...)`은 `runtimePath.string()`을 쓰도록 필요 시 조정.

- [ ] **Step 6: Build() 시작에 manifest 검증 추가**

`src/Editor/GameBuilder.cpp`의 `Build(...)`(현재 `:15-66`)에서 `CreateOutputDirectory` 호출 **전에** 다음을 추가:
```cpp
    // 필수 입력 검증(절대 경로)
    {
        BuildManifest manifest;
        if (Project::Get().IsOpen()) {
            manifest.requiredFiles.push_back(Project::Get().GetAssetsPath());
        }
        manifest.requiredFiles.push_back(PathService::Get().EngineResource("Shaders").string());
        manifest.requiredFiles.push_back(settings.mainScene);
        manifest.requiredFiles.push_back((PathService::Get().ExecutableDir() / "molga_runtime").string());

        std::string err;
        if (!manifest.Validate(err)) {
            lastError = err;
            return false;
        }
    }
```

- [ ] **Step 7: 빌드 + 커밋**

Run:
```bash
cmake --build --preset debug --target molga_engine -j4
ctest --preset debug --output-on-failure
```
Expected: 빌드 성공, 테스트 통과.
```bash
git add src/Editor/GameBuilder.cpp src/Editor/GameBuilder.h
git commit -m "fix(build): absolute paths, manifest validation, safe delete, real runtime (P0-3)"
```

---

## Task F. 런타임이 자기 위치 기준으로 리소스 탐색

- [ ] **Step 1: runtime_main.cpp 경로를 exe 기준으로**

`src/runtime_main.cpp`에서(Task B에서 셰이더는 이미 처리):
- `LoadGameConfig("game.json", config)`(현재 `:65`)를 다음으로:
  ```cpp
      std::string configPath = (PathService::Get().ExecutableDir() / "game.json").string();
      LoadGameConfig(configPath, config);
  ```
- 씬 로드 경로(`config.mainScene`, 현재 기본 `"scenes/main.json"`)를 exe 기준으로:
  ```cpp
      std::string scenePath = (PathService::Get().ExecutableDir() / config.mainScene).string();
      if (!world.LoadFromFile(scenePath)) { ... }
  ```
- asset root는 Task D Step 5에서 이미 `ExecutableDir()`로 설정 → `"Assets/..."` 텍스처가 `<exe>/Assets/...`로 해석된다(빌드가 `<out>/Assets`에 복사하므로 일치).

- [ ] **Step 2: 빌드 + 커밋**

Run:
```bash
cmake --build --preset debug --target molga_runtime -j4
```
```bash
git add src/runtime_main.cpp
git commit -m "fix(runtime): resolve game.json/scene/assets relative to the executable (P0-3)"
```

---

## 수동 종단 검증 (빌드 → 임의 위치 실행)

> 0-5b의 build smoke test가 이를 자동화한다. 여기서는 수동으로 한 번 확인한다.

1. 에디터를 실행해 텍스처가 지정된 스프라이트가 있는 씬을 프로젝트에 저장한다.
2. Build를 실행한다. 필수 파일이 모두 있으면 `<output>/`에 `Assets/`, `Shaders/`, `scenes/main.json`, `game.json`, `<gameName>` 실행 파일이 생긴다.
3. 임의 디렉터리에서 실행:
   ```bash
   ( cd /tmp && /path/to/output/MyGame & sleep 2; kill %1 2>/dev/null )
   ```
   텍스처가 에디터와 동일하게 보여야 하고, 셰이더/씬 누락 오류가 없어야 한다.
4. 일부러 프로젝트 Assets를 비우고 Build → **실패하고 원인이 표시**되어야 한다(이전엔 성공으로 처리).

---

## 작업 완료 기준

- [ ] 저장소 루트/`build`/임의 디렉터리 어디서 실행해도 에디터·런타임이 셰이더를 로드한다(`test_path_service` + 수동 실행).
- [ ] Save/Open이 프로젝트 `Scenes/main.json`을 사용한다.
- [ ] 프로젝트 텍스처가 에디터/Play/빌드 런타임에서 동일하게 보인다(P0-5).
- [ ] 프로젝트 Assets/엔진 셰이더/메인 씬/런타임 실행 파일 중 하나라도 없으면 Build가 실패하고 원인을 표시한다(`test_game_builder` + 수동).
- [ ] 위험한 출력 경로(빈 값/루트/홈/cwd)에 대한 `remove_all`이 거부된다.
- [ ] sortingOrder가 실제 렌더 순서에 적용된다.

## 다음 작업

[task-0-5b_smoke_tests.md](task-0-5b_smoke_tests.md) — editor/runtime/build smoke test로 슬라이스 전체를 CI에서 자동 검증한다.
