# Task 0-5b: Playable Editor Vertical Slice 스모크 테스트

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:test-driven-development`, `superpowers:systematic-debugging`, `superpowers:verification-before-completion`

**Goal:** 프로젝트 생성부터 Scene 저장, Play/Stop, 빌드, 독립 런타임 실행까지 Vertical Slice 전체 흐름을 자동 검증하고 CI의 필수 통과 조건으로 만든다.

**Architecture:** 빠른 피드백을 주는 프로세스 내부 스모크 테스트와 실제 실행 파일을 연결하는 End-to-End 스모크 테스트를 분리한다. 에디터 실행 파일은 GUI 초기화 전에 `--smoke-build` 자동화 경로를 처리하고, 런타임은 숨김 GLFW 창에서 제한된 프레임을 렌더링한 뒤 JSON 리포트를 남긴다. 테스트가 내부 구현이나 픽셀 값에 의존하지 않도록 저장된 Scene, 빌드 산출물, 종료 코드, 리포트의 의미 있는 상태만 검증한다.

**Tech Stack:** C++17, doctest, CTest, CMake script mode, GLFW, nlohmann/json

**선행 작업:** `0-5a → 0-1 → 0-4 → 0-2 → 0-3`이 완료되어 있어야 한다.

**닫는 결함:** P0-6. 핵심 사용자 흐름을 수동 확인에만 의존하며, 에디터와 빌드 런타임 사이의 계약 파손을 CI에서 탐지하지 못한다.

---

## 1. 완료 상태에서 보장할 사용자 흐름

이 문서가 완료되면 다음 흐름을 하나의 명령으로 검증할 수 있어야 한다.

1. 최소 프로젝트와 텍스처 Asset을 생성한다.
2. `SceneDocument`에서 Sprite GameObject를 저장한다.
3. Play 진입 후 Play World만 변경되는지 확인한다.
4. Stop 후 Edit World가 원래 상태로 복원되는지 확인한다.
5. 에디터 자동화 모드가 프로젝트를 독립 실행 패키지로 빌드한다.
6. 빌드된 런타임이 자기 실행 파일 기준으로 Scene과 Asset을 찾는다.
7. 런타임이 숨김 창에서 최소 3프레임을 렌더링하고 정상 종료한다.
8. CI가 위 과정 중 하나라도 실패하면 실패한다.

스모크 테스트는 프레임 이미지의 픽셀 일치 테스트가 아니다. 운영체제와 GPU 드라이버에 따라 달라질 수 있는 렌더 결과 대신 아래 계약을 검증한다.

- 프로세스 종료 코드가 `0`이다.
- Scene 파일을 로드했고 GameObject 수가 기대값과 같다.
- 경로가 설정된 SpriteRenderer의 Texture가 해석되었다.
- 지정한 프레임 수만큼 Update/Render 루프가 실행되었다.
- 빌드 산출물에 실행 파일, `game.json`, Scene, Assets, Shaders가 있다.
- 에디터의 Edit World는 Play World 변경에 오염되지 않는다.

---

## 2. 변경 파일

### 생성

```text
src/Core/SmokeReport.h
src/Core/SmokeReport.cpp
src/Core/PackageLayout.h
src/Core/PackageLayout.cpp
tests/SmokeTestSupport.h
tests/test_editor_smoke.cpp
tests/test_runtime_smoke.cpp
tests/test_build_smoke.cpp
tests/smoke/create_fixture.cmake
tests/smoke/run_end_to_end.cmake
```

### 수정

```text
CMakeLists.txt
tests/CMakeLists.txt
src/Core/Bootstrap.h
src/Core/Bootstrap.cpp
src/Editor/GameBuilder.cpp
src/main.cpp
src/runtime_main.cpp
.github/workflows/ci.yml
```

---

## 3. 테스트 계층과 실패 해석

| 계층 | CTest 이름 | 검증 범위 | 실패 시 우선 확인 |
|---|---|---|---|
| 모델 스모크 | `editor_smoke` | Scene 저장, Play/Stop 복제 격리 | `SceneDocument`, `World::Clone`, 직렬화 |
| 로드 스모크 | `runtime_smoke` | 런타임 Scene 계약과 리포트 | Component factory, Scene schema |
| 패키지 스모크 | `build_smoke` | 패키지 필수 파일 검증 | `GameBuilder`, 패키지 레이아웃 |
| 프로세스 E2E | `smoke_end_to_end` | 실제 에디터 빌드와 독립 런타임 실행 | 경로, 실행 파일 복사, GLFW/OpenGL |

`smoke_end_to_end` 실패를 단위 테스트 실패와 섞지 않는다. 로컬에서 아래처럼 계층별로 재현할 수 있어야 한다.

```bash
ctest --preset debug -L unit -LE smoke --output-on-failure
ctest --preset debug -L smoke --output-on-failure
```

---

## Task A: 실패 리포트와 패키지 계약부터 고정

### A-1. 실패하는 테스트 작성

`tests/test_build_smoke.cpp`를 먼저 만든다.

```cpp
#include "doctest.h"

#include "Core/PackageLayout.h"
#include "Core/SmokeReport.h"
#include "SmokeTestSupport.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("build package requires executable config scene assets and shaders") {
    test_support::TempDirectory temp{"build-smoke"};
    const fs::path root = temp.Path();

    fs::create_directories(root / "Scenes");
    fs::create_directories(root / "Assets");
    test_support::WriteText(root / "SmokeGame", "runtime");
    test_support::WriteText(root / "game.json", "{}");
    test_support::WriteText(root / "Scenes/main.json", "{}");

    std::string error;
    CHECK_FALSE(PackageLayout::Validate(root, "SmokeGame", error));
    CHECK(error.find("Shaders") != std::string::npos);

    test_support::WriteText(root / "Shaders/sprite.vert", "shader");
    CHECK(PackageLayout::Validate(root, "SmokeGame", error));
}

TEST_CASE("smoke report round trips through json") {
    test_support::TempDirectory temp{"smoke-report"};
    const fs::path path = temp.Path() / "report.json";

    SmokeReport written;
    written.executable = "molga_runtime";
    written.status = "ok";
    written.scenePath = "Scenes/main.json";
    written.objectCount = 1;
    written.frames = 3;
    written.assetsResolved = true;
    REQUIRE(written.Save(path));

    SmokeReport loaded;
    REQUIRE(SmokeReport::Load(path, loaded));
    CHECK(loaded.status == "ok");
    CHECK(loaded.objectCount == 1);
    CHECK(loaded.frames == 3);
    CHECK(loaded.assetsResolved);
}
```

`tests/SmokeTestSupport.h`는 테스트가 공용 임시 경로와 텍스트 파일 쓰기를 재사용하도록 만든다. 임시 경로는 테스트 종료 시 삭제해야 한다.

```cpp
#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace test_support {
namespace fs = std::filesystem;

class TempDirectory {
public:
    explicit TempDirectory(const std::string& prefix) {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = fs::temp_directory_path() /
                ("molga-" + prefix + "-" + std::to_string(stamp));
        fs::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& Path() const { return path_; }

private:
    fs::path path_;
};

inline void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Could not write " + path.string());
    }
    output << text;
}

}  // namespace test_support
```

### A-2. 테스트가 실패하는지 확인

```bash
cmake --build --preset debug -j4
ctest --preset debug -R build_smoke --output-on-failure
```

예상 결과:

```text
fatal error: 'Core/PackageLayout.h' file not found
```

### A-3. `SmokeReport` 구현

`src/Core/SmokeReport.h`:

```cpp
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct SmokeReport {
    std::string executable;
    std::string status;
    std::string scenePath;
    std::string message;
    std::size_t objectCount = 0;
    int frames = 0;
    bool assetsResolved = false;

    bool Save(const std::filesystem::path& path) const;
    static bool Load(const std::filesystem::path& path, SmokeReport& out);
};
```

`src/Core/SmokeReport.cpp`:

```cpp
#include "Core/SmokeReport.h"

#include <fstream>
#include <nlohmann/json.hpp>

bool SmokeReport::Save(const std::filesystem::path& path) const {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::ofstream output(path);
    if (!output) {
        return false;
    }

    output << nlohmann::json{
        {"executable", executable},
        {"status", status},
        {"scenePath", scenePath},
        {"message", message},
        {"objectCount", objectCount},
        {"frames", frames},
        {"assetsResolved", assetsResolved},
    }.dump(2);
    return output.good();
}

bool SmokeReport::Load(const std::filesystem::path& path, SmokeReport& out) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    try {
        const nlohmann::json json = nlohmann::json::parse(input);
        out.executable = json.value("executable", "");
        out.status = json.value("status", "");
        out.scenePath = json.value("scenePath", "");
        out.message = json.value("message", "");
        out.objectCount = json.value("objectCount", 0U);
        out.frames = json.value("frames", 0);
        out.assetsResolved = json.value("assetsResolved", false);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

```

### A-4. `PackageLayout` 구현

`src/Core/PackageLayout.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>

class PackageLayout {
public:
    static bool Validate(
        const std::filesystem::path& root,
        const std::string& executableName,
        std::string& errorOut);
};
```

`src/Core/PackageLayout.cpp`:

```cpp
#include "Core/PackageLayout.h"

#include <array>

bool PackageLayout::Validate(
    const std::filesystem::path& root,
    const std::string& executableName,
    std::string& errorOut) {
    const std::array required{
        root / executableName,
        root / "game.json",
        root / "Scenes/main.json",
        root / "Assets",
        root / "Shaders",
    };

    for (const auto& path : required) {
        if (!std::filesystem::exists(path)) {
            errorOut = "Missing package entry: " + path.string();
            return false;
        }
    }

    errorOut.clear();
    return true;
}

```

`GameBuilder::Build`의 마지막 단계에서 같은 계약을 검증한다. 검증 실패는 성공 다이얼로그를 띄우기 전에 빌드 실패로 반환해야 한다.

```cpp
std::string packageError;
if (!PackageLayout::Validate(
        settings.outputPath,
        RuntimeOutputName(settings.gameName),
        packageError)) {
    lastError = packageError;
    return false;
}
```

운영체제별 런타임 이름은 Windows에만 `.exe`를 붙인다.

```cpp
std::string RuntimeOutputName(const std::string& gameName) {
#ifdef _WIN32
    return gameName + ".exe";
#else
    return gameName;
#endif
}
```

### A-5. CMake 등록과 통과 확인

`src/Core/SmokeReport.cpp`, `src/Core/PackageLayout.cpp`를 `molga_core` 소스에 추가한다. 먼저 `tests/CMakeLists.txt`의 기존 `molga_add_test` 함수가 모든 테스트에 기본 `unit` 라벨을 지정하도록 보강한다.

```cmake
function(molga_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE molga_core doctest_main molga_warnings)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES LABELS "unit")
endfunction()
```

그 뒤 새 테스트를 등록한다.

```cmake
molga_add_test(build_smoke
    test_build_smoke.cpp
)
target_include_directories(build_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(build_smoke PROPERTIES LABELS "unit;smoke")
```

검증:

```bash
cmake --build --preset debug -j4
ctest --preset debug -R build_smoke --output-on-failure
```

예상 결과:

```text
100% tests passed, 0 tests failed
```

### A-6. 커밋

```bash
git add src/Core/SmokeReport.h src/Core/SmokeReport.cpp \
  src/Core/PackageLayout.h src/Core/PackageLayout.cpp \
  src/Editor/GameBuilder.cpp tests/SmokeTestSupport.h \
  tests/test_build_smoke.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "test: define smoke report and package contracts"
```

---

## Task B: Edit World와 Play World 경계 스모크 테스트

### B-1. 실패하는 에디터 스모크 테스트 작성

`tests/test_editor_smoke.cpp`:

```cpp
#include "doctest.h"

#include "Core/World.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/GameObject.h"
#include "Editor/SceneDocument.h"
#include "SmokeTestSupport.h"

TEST_CASE("editor save play stop keeps edit world authoritative") {
    test_support::TempDirectory temp{"editor-smoke"};
    const auto scenePath = temp.Path() / "Scenes/main.json";

    SceneDocument document;
    auto spriteObject = std::make_shared<GameObject>("SmokeSprite");
    auto* sprite = spriteObject->AddComponent<SpriteRenderer>();
    sprite->SetTexturePath("Assets/Textures/smoke.ppm");
    document.EditWorld().Add(spriteObject);
    document.SetPath(scenePath.string());

    REQUIRE(document.EditWorld().SaveToFile(scenePath.string()));
    REQUIRE(document.EditWorld().Objects().size() == 1);

    document.EnterPlay();
    REQUIRE(document.IsPlaying());
    REQUIRE(document.ActiveWorld().Objects().size() == 1);

    document.ActiveWorld().Objects().front()->SetName("PlayOnlyName");
    document.ActiveWorld().Add(std::make_shared<GameObject>("PlayOnlyObject"));
    CHECK(document.ActiveWorld().Objects().size() == 2);

    document.ExitPlay();
    CHECK_FALSE(document.IsPlaying());
    REQUIRE(document.EditWorld().Objects().size() == 1);
    CHECK(document.EditWorld().Objects().front()->GetName() == "SmokeSprite");

    World reloaded;
    REQUIRE(reloaded.LoadFromFile(scenePath.string()));
    REQUIRE(reloaded.Objects().size() == 1);
    CHECK(reloaded.Objects().front()->GetName() == "SmokeSprite");
}
```

### B-2. 실패 확인

```bash
cmake --build --preset debug -j4
ctest --preset debug -R editor_smoke --output-on-failure
```

이 단계에서 실패하면 `task-0-2_scene_document_and_play_world.md`의 구현을 먼저 수정한다. 스모크 테스트를 통과시키기 위해 Play World의 변경을 Edit World로 복사하는 예외 처리를 추가하면 안 된다.

### B-3. CMake 등록

```cmake
molga_add_test(editor_smoke
    test_editor_smoke.cpp
)
target_include_directories(editor_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(editor_smoke PROPERTIES LABELS "unit;smoke")
```

### B-4. 통과 확인

```bash
ctest --preset debug -R editor_smoke --output-on-failure
```

예상 결과:

```text
1/1 Test #...: editor_smoke ................ Passed
```

### B-5. 커밋

```bash
git add tests/test_editor_smoke.cpp tests/CMakeLists.txt
git commit -m "test: cover editor save play stop workflow"
```

---

## Task C: 런타임 Scene 로드 계약 스모크 테스트

### C-1. 실패하는 테스트 작성

`tests/test_runtime_smoke.cpp`:

```cpp
#include "doctest.h"

#include "Core/PathService.h"
#include "Core/World.h"
#include "ECS/Components/SpriteRenderer.h"
#include "SmokeTestSupport.h"

TEST_CASE("runtime world loads a packaged scene with project relative assets") {
    test_support::TempDirectory temp{"runtime-smoke"};
    const auto root = temp.Path();
    const auto scenePath = root / "Scenes/main.json";

    test_support::WriteText(root / "Assets/Textures/smoke.ppm",
                            "P6\n1 1\n255\n@ `");
    test_support::WriteText(
        scenePath,
        R"({
  "version": "1.0",
  "name": "Smoke Scene",
  "gameObjects": [{
    "name": "SmokeSprite",
    "id": 1001,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "Transform",
      "enabled": true,
      "position": [32.0, 48.0],
      "rotation": 0.0,
      "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer",
      "enabled": true,
      "texturePath": "Assets/Textures/smoke.ppm",
      "color": [1.0, 1.0, 1.0, 1.0],
      "size": [1.0, 1.0],
      "flipX": false,
      "flipY": false,
      "sortingOrder": 0
    }]
  }]
})");

    PathService::Get().SetAssetRoot(root);

    World world;
    REQUIRE(world.LoadFromFile(scenePath.string()));
    REQUIRE(world.Objects().size() == 1);

    auto* sprite = world.Objects().front()->GetComponent<SpriteRenderer>();
    REQUIRE(sprite != nullptr);
    CHECK(sprite->GetTexturePath() == "Assets/Textures/smoke.ppm");
    CHECK(PathService::Get().ResolveAsset(sprite->GetTexturePath()) ==
          (root / "Assets/Textures/smoke.ppm").string());

    world.StartPending();
    world.FixedStep(1.0F / 60.0F);
    world.Update(1.0F / 60.0F);
    world.LateUpdate(1.0F / 60.0F);
}
```

이 테스트는 OpenGL Context 없이 실행한다. Texture 실제 업로드는 Task E의 프로세스 E2E가 검증한다.

### C-2. CMake 등록과 통과 확인

```cmake
molga_add_test(runtime_smoke
    test_runtime_smoke.cpp
)
target_include_directories(runtime_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(runtime_smoke PROPERTIES LABELS "unit;smoke")
```

```bash
cmake --build --preset debug -j4
ctest --preset debug -R runtime_smoke --output-on-failure
```

### C-3. 커밋

```bash
git add tests/test_runtime_smoke.cpp tests/CMakeLists.txt
git commit -m "test: cover runtime scene loading contract"
```

---

## Task D: 에디터의 Headless 빌드 자동화 경로

### D-1. CLI 계약

에디터 실행 파일은 다음 명령을 지원한다.

```bash
molga_engine --smoke-build <project-root> <output-root> <report-path>
```

계약:

- GLFW, OpenGL, ImGui를 초기화하기 전에 실행한다.
- `<project-root>/Scenes/main.json`을 메인 Scene으로 사용한다.
- 게임 이름은 `SmokeGame`으로 고정한다.
- `GameBuilder`의 실제 빌드 경로를 사용한다.
- 성공과 실패 모두 `SmokeReport`를 기록한다.
- 성공은 `0`, 입력 오류는 `2`, 빌드 실패는 `3`으로 종료한다.

### D-2. `src/main.cpp`에 옵션 파서와 자동화 함수 추가

익명 namespace에 다음 구조를 추가한다.

```cpp
namespace {

struct SmokeBuildOptions {
    std::filesystem::path projectRoot;
    std::filesystem::path outputRoot;
    std::filesystem::path reportPath;
};

std::optional<SmokeBuildOptions> ParseSmokeBuild(int argc, char** argv) {
    if (argc != 5 || std::string_view(argv[1]) != "--smoke-build") {
        return std::nullopt;
    }
    return SmokeBuildOptions{argv[2], argv[3], argv[4]};
}

int RunSmokeBuild(const SmokeBuildOptions& options) {
    SmokeReport report;
    report.executable = "molga_engine";
    report.scenePath = "Scenes/main.json";

    if (!Project::Get().Open(options.projectRoot.string())) {
        report.status = "error";
        report.message = "Could not open smoke project";
        report.Save(options.reportPath);
        return 3;
    }

    World world;
    const auto scenePath = options.projectRoot / "Scenes/main.json";
    if (!world.LoadFromFile(scenePath.string())) {
        report.status = "error";
        report.message = "Could not load smoke scene";
        report.Save(options.reportPath);
        return 3;
    }

    BuildSettings settings;
    settings.gameName = "SmokeGame";
    settings.outputPath = options.outputRoot.string();
    settings.mainScene = scenePath.string();

    if (!GameBuilder::Get().Build(settings)) {
        report.status = "error";
        report.message = GameBuilder::Get().GetLastError();
        report.Save(options.reportPath);
        return 3;
    }

    report.status = "ok";
    report.message = "Build completed";
    report.objectCount = world.Objects().size();
    report.Save(options.reportPath);
    return 0;
}

}  // namespace
```

`main` 진입 직후 `PathService`를 초기화하고, 일반 GUI 초기화 전에 자동화 분기를 실행한다.

```cpp
int main(int argc, char** argv) {
    PathService::Get().InitFromExecutable(argc > 0 ? argv[0] : nullptr);

    if (argc > 1 && std::string_view(argv[1]) == "--smoke-build") {
        const auto options = ParseSmokeBuild(argc, argv);
        if (!options) {
            std::cerr
                << "Usage: molga_engine --smoke-build "
                << "<project-root> <output-root> <report-path>\n";
            return 2;
        }
        return RunSmokeBuild(*options);
    }

    // 기존 GUI 에디터 초기화와 루프
```

실제 `BuildSettings`, `Project::Open`, `GameBuilder` 시그니처가 다르면 해당 타입의 현재 API를 따른다. 단, CLI 인자와 종료 코드 계약은 변경하지 않는다.

### D-3. 인자 오류 경로 검증

```bash
./build/debug/molga_engine --smoke-build
echo $?
```

예상 결과:

```text
Usage: molga_engine --smoke-build <project-root> <output-root> <report-path>
2
```

성공 경로는 Task F에서 생성하는 고정 fixture로 검증한다.

### D-4. 커밋

```bash
git add src/main.cpp src/Editor/GameBuilder.cpp
git commit -m "feat: add headless editor build smoke mode"
```

---

## Task E: 런타임 숨김 창 스모크 모드

### E-1. CLI 계약

빌드된 런타임은 다음 명령을 지원한다.

```bash
SmokeGame --smoke --frames 3 --report <report-path>
```

계약:

- 평소와 같은 `game.json`, Scene, Asset 로드 경로를 사용한다.
- GLFW 창은 생성하지만 화면에는 표시하지 않는다.
- 지정 프레임 수만큼 Fixed/Update/LateUpdate/Render를 실행한다.
- Texture 경로가 있는 모든 SpriteRenderer의 Texture가 해석되어야 성공한다.
- 성공과 실패 모두 가능한 범위에서 리포트를 기록한다.
- 성공은 `0`, 인자 오류는 `2`, 초기화·로드·Asset 실패는 `4`로 종료한다.

### E-2. 숨김 창 설정 추가

`src/Core/Bootstrap.h`의 설정 구조에 기본값이 `true`인 필드를 추가한다.

```cpp
struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string title = "Molga Engine";
    bool visible = true;
};
```

`src/Core/Bootstrap.cpp`에서 GLFW 창 생성 전에 적용한다.

```cpp
glfwWindowHint(GLFW_VISIBLE, config.visible ? GLFW_TRUE : GLFW_FALSE);
```

기본값이 `true`이므로 기존 에디터와 런타임 동작은 바뀌지 않는다.

### E-3. `src/runtime_main.cpp` 옵션과 종료 조건 추가

```cpp
namespace {

struct RuntimeSmokeOptions {
    bool enabled = false;
    int frames = 3;
    std::filesystem::path reportPath;
};

std::optional<RuntimeSmokeOptions> ParseRuntimeSmoke(int argc, char** argv) {
    RuntimeSmokeOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--smoke") {
            options.enabled = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            try {
                options.frames = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        } else if (arg == "--report" && i + 1 < argc) {
            options.reportPath = argv[++i];
        } else {
            return std::nullopt;
        }
    }
    if (options.enabled &&
        (options.frames < 1 || options.reportPath.empty())) {
        return std::nullopt;
    }
    return options;
}

bool AllSpriteAssetsResolved(const World& world) {
    for (const auto& object : world.Objects()) {
        const auto* sprite = object->GetComponent<SpriteRenderer>();
        if (sprite != nullptr &&
            !sprite->GetTexturePath().empty() &&
            sprite->GetTexture() == nullptr) {
            return false;
        }
    }
    return true;
}

}  // namespace
```

일반 런타임 경로와 동일하게 World를 로드하고 `ResolveAssets()`까지 수행한다. 스모크 모드일 때만 창을 숨기고 프레임 제한을 적용한다.

```cpp
const auto smoke = ParseRuntimeSmoke(argc, argv);
if (!smoke) {
    std::cerr << "Usage: runtime [--smoke --frames N --report PATH]\n";
    return 2;
}

WindowConfig windowConfig;
windowConfig.visible = !smoke->enabled;

// 기존 Engine, World 초기화

int renderedFrames = 0;
while (!glfwWindowShouldClose(window)) {
    // 기존 Fixed/Update/LateUpdate/Render 루프
    ++renderedFrames;
    if (smoke->enabled && renderedFrames >= smoke->frames) {
        break;
    }
}

int exitCode = 0;
if (smoke->enabled) {
    SmokeReport report;
    report.executable = "molga_runtime";
    report.status = AllSpriteAssetsResolved(world) ? "ok" : "error";
    report.scenePath = config.mainScene;
    report.objectCount = world.Objects().size();
    report.frames = renderedFrames;
    report.assetsResolved = AllSpriteAssetsResolved(world);
    report.message = report.assetsResolved
        ? "Runtime smoke completed"
        : "One or more sprite assets failed to resolve";
    report.Save(smoke->reportPath);
    exitCode = report.assetsResolved ? 0 : 4;
}

world.Clear();
TextRenderer::Get().Shutdown();
camera.reset();
shader.reset();
renderer.reset();
EngineShutdown();
return exitCode;
```

예외나 조기 실패에서도 `--report` 경로를 알고 있다면 `"status": "error"` 리포트를 남긴다. 리포트 기록 실패가 원래 오류를 숨기면 안 된다.
일반 모드는 기존 정책을 유지할 수 있지만, 스모크 모드에서 `game.json` 또는 Scene 로드가 실패하면 빈 World로 계속하지 않고 리포트 기록 후 `4`로 종료한다.

### E-4. 수동 검증

```bash
/tmp/molga-smoke-build/SmokeGame \
  --smoke --frames 3 --report /tmp/molga-runtime-report.json
```

예상 리포트 핵심 필드:

```json
{
  "status": "ok",
  "objectCount": 1,
  "frames": 3,
  "assetsResolved": true
}
```

### E-5. 커밋

```bash
git add src/Core/Bootstrap.h src/Core/Bootstrap.cpp src/runtime_main.cpp
git commit -m "feat: add hidden runtime smoke mode"
```

---

## Task F: 실제 실행 파일을 연결하는 End-to-End CTest

### F-1. Fixture 생성 스크립트

`tests/smoke/create_fixture.cmake`는 소스 트리를 수정하지 않고 빌드 디렉터리에 테스트 프로젝트를 만든다.

```cmake
if(NOT DEFINED FIXTURE_ROOT)
    message(FATAL_ERROR "FIXTURE_ROOT is required")
endif()

file(REMOVE_RECURSE "${FIXTURE_ROOT}")
file(MAKE_DIRECTORY
    "${FIXTURE_ROOT}/Assets/Textures"
    "${FIXTURE_ROOT}/Scenes"
)

file(WRITE "${FIXTURE_ROOT}/project.molga" [=[
{
  "name": "SmokeProject",
  "version": "1.0",
  "mainScene": "Scenes/main.json"
}
]=])

# stb_image의 PNM loader는 binary P6만 지원한다.
# header 뒤의 세 문자는 RGB bytes 64, 32, 96이다.
file(WRITE "${FIXTURE_ROOT}/Assets/Textures/smoke.ppm"
    "P6\n1 1\n255\n@ `")

file(WRITE "${FIXTURE_ROOT}/Scenes/main.json" [=[
{
  "version": "1.0",
  "name": "Smoke Scene",
  "gameObjects": [{
    "name": "SmokeSprite",
    "id": 1001,
    "active": true,
    "parentId": -1,
    "components": [{
      "type": "Transform",
      "enabled": true,
      "position": [32.0, 48.0],
      "rotation": 0.0,
      "scale": [1.0, 1.0]
    }, {
      "type": "SpriteRenderer",
      "enabled": true,
      "texturePath": "Assets/Textures/smoke.ppm",
      "color": [1.0, 1.0, 1.0, 1.0],
      "size": [1.0, 1.0],
      "flipX": false,
      "flipY": false,
      "sortingOrder": 0
    }]
  }]
}
]=])
```

Scene JSON 필드는 현재 serializer가 출력하는 schema와 정확히 맞춰야 한다. serializer 변경 시 fixture를 별도 schema로 유지하지 말고 같은 변경에서 함께 갱신한다.

### F-2. End-to-End 실행 스크립트

`tests/smoke/run_end_to_end.cmake`:

```cmake
foreach(required EDITOR FIXTURE_SCRIPT WORK_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(PROJECT_ROOT "${WORK_ROOT}/project")
set(PACKAGE_ROOT "${WORK_ROOT}/package")
set(EDITOR_REPORT "${WORK_ROOT}/editor-report.json")
set(RUNTIME_REPORT "${WORK_ROOT}/runtime-report.json")
set(TEST_HOME "${WORK_ROOT}/home")

file(REMOVE_RECURSE "${WORK_ROOT}")
file(MAKE_DIRECTORY "${WORK_ROOT}" "${TEST_HOME}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DFIXTURE_ROOT=${PROJECT_ROOT}"
        -P "${FIXTURE_SCRIPT}"
    RESULT_VARIABLE fixture_result
)
if(NOT fixture_result EQUAL 0)
    message(FATAL_ERROR "Could not create smoke fixture")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "HOME=${TEST_HOME}"
        "${EDITOR}" --smoke-build
        "${PROJECT_ROOT}" "${PACKAGE_ROOT}" "${EDITOR_REPORT}"
    RESULT_VARIABLE editor_result
    OUTPUT_VARIABLE editor_stdout
    ERROR_VARIABLE editor_stderr
)
if(NOT editor_result EQUAL 0)
    message(FATAL_ERROR
        "Editor smoke build failed (${editor_result})\n"
        "${editor_stdout}\n${editor_stderr}")
endif()

if(WIN32)
    set(GAME_EXECUTABLE "${PACKAGE_ROOT}/SmokeGame.exe")
else()
    set(GAME_EXECUTABLE "${PACKAGE_ROOT}/SmokeGame")
endif()

foreach(required_path
    "${GAME_EXECUTABLE}"
    "${PACKAGE_ROOT}/game.json"
    "${PACKAGE_ROOT}/Scenes/main.json"
    "${PACKAGE_ROOT}/Assets/Textures/smoke.ppm"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Missing package output: ${required_path}")
    endif()
endforeach()

execute_process(
    COMMAND "${GAME_EXECUTABLE}"
        --smoke --frames 3 --report "${RUNTIME_REPORT}"
    RESULT_VARIABLE runtime_result
    OUTPUT_VARIABLE runtime_stdout
    ERROR_VARIABLE runtime_stderr
    TIMEOUT 20
)
if(NOT runtime_result EQUAL 0)
    message(FATAL_ERROR
        "Runtime smoke failed (${runtime_result})\n"
        "${runtime_stdout}\n${runtime_stderr}")
endif()

file(READ "${RUNTIME_REPORT}" runtime_report)
foreach(expected
    [["status": "ok"]]
    [["objectCount": 1]]
    [["frames": 3]]
    [["assetsResolved": true]]
)
    string(FIND "${runtime_report}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Runtime report is missing ${expected}\n${runtime_report}")
    endif()
endforeach()
```

### F-3. CTest 등록

루트 `CMakeLists.txt`에서 에디터와 런타임 Target이 만들어진 후 등록한다.

```cmake
add_test(
    NAME smoke_end_to_end
    COMMAND "${CMAKE_COMMAND}"
        "-DEDITOR=$<TARGET_FILE:molga_engine>"
        "-DFIXTURE_SCRIPT=${CMAKE_SOURCE_DIR}/tests/smoke/create_fixture.cmake"
        "-DWORK_ROOT=${CMAKE_BINARY_DIR}/smoke-e2e"
        -P "${CMAKE_SOURCE_DIR}/tests/smoke/run_end_to_end.cmake"
)
set_tests_properties(smoke_end_to_end PROPERTIES
    LABELS "smoke;e2e"
    TIMEOUT 30
    RUN_SERIAL TRUE
)
```

`RUN_SERIAL`은 같은 빌드 디렉터리의 고정 fixture 경로를 보호한다. 병렬 실행이 필요해지면 CTest resource group이나 테스트별 고유 경로로 교체한다.

### F-4. 실패부터 확인

첫 실행은 아직 발견하지 못한 실제 패키징·경로 문제를 드러낼 가능성이 높다.

```bash
cmake --preset debug
cmake --build --preset debug -j4
ctest --preset debug -R smoke_end_to_end --output-on-failure
```

실패 시 아래 순서로 원인을 분리한다.

1. `editor-report.json`이 없으면 CLI 파싱 또는 조기 크래시를 확인한다.
2. 에디터 리포트가 error이면 `GameBuilder::GetLastError()`를 확인한다.
3. 패키지 파일 누락이면 `PackageLayout`과 복사 경로를 확인한다.
4. 런타임 리포트가 없으면 실행 파일 권한, `game.json`, GLFW 초기화를 확인한다.
5. `assetsResolved=false`면 Asset root와 SpriteRenderer texture path를 확인한다.
6. sanitizer에서만 실패하면 수명·종료 순서와 OpenGL resource 해제를 확인한다.

테스트를 통과시키기 위해 Asset 해석 검사를 끄거나 런타임 실행을 생략하면 안 된다.

### F-5. 통과 확인

```bash
ctest --preset debug -L smoke --output-on-failure
```

예상 결과:

```text
editor_smoke ............. Passed
runtime_smoke ............ Passed
build_smoke .............. Passed
smoke_end_to_end ......... Passed
100% tests passed
```

### F-6. 커밋

```bash
git add tests/smoke/create_fixture.cmake tests/smoke/run_end_to_end.cmake \
  CMakeLists.txt
git commit -m "test: add editor build runtime end to end smoke"
```

---

## Task G: CI 필수 게이트로 승격

### G-1. Debug와 Release에서 스모크 실행

`.github/workflows/ci.yml`의 Debug/Release 테스트 단계를 구분해 실패 위치를 명확히 한다.

```yaml
- name: Run unit tests
  run: ctest --preset ${{ matrix.preset }} -L unit -LE smoke --output-on-failure

- name: Run playable editor smoke
  run: ctest --preset ${{ matrix.preset }} -L smoke --output-on-failure
```

macOS runner에서 숨김 GLFW 창 생성이 실패하면 먼저 runner의 디스플레이와 OpenGL 초기화 로그를 확보한다. E2E를 무조건 제외하거나 `continue-on-error`로 낮추지 않는다.

### G-2. 실패 산출물 업로드

E2E 실패 시 원인을 확인할 수 있도록 리포트와 빌드 산출물을 실패한 Job에서만 업로드한다.

```yaml
- name: Upload smoke diagnostics
  if: failure()
  uses: actions/upload-artifact@v4
  with:
    name: smoke-${{ matrix.preset }}-${{ runner.os }}
    path: |
      build/${{ matrix.preset }}/smoke-e2e/editor-report.json
      build/${{ matrix.preset }}/smoke-e2e/runtime-report.json
      build/${{ matrix.preset }}/smoke-e2e/package/game.json
    if-no-files-found: ignore
```

### G-3. Sanitizer 범위

ASan/UBSan Job에서는 우선 `unit`과 프로세스 내부 `smoke` 테스트를 실행한다. 플랫폼 OpenGL 드라이버와 sanitizer 충돌이 확인된 경우에만 `smoke_end_to_end`를 별도 라벨로 제외하고, 그 사유와 추적 이슈를 workflow 주석에 남긴다.

```bash
ctest --preset asan -L smoke --output-on-failure
ctest --preset ubsan -L smoke --output-on-failure
```

### G-4. 커밋

```bash
git add .github/workflows/ci.yml
git commit -m "ci: require playable editor smoke workflow"
```

---

## 4. 전체 검증 절차

### 정적 구성 확인

```bash
cmake --preset debug
cmake --preset release
```

예상 결과: 두 preset 모두 configure 성공.

### Debug 전체 테스트

```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```

### Release 스모크

```bash
cmake --build --preset release -j4
ctest --preset release -L smoke --output-on-failure
```

### Sanitizer

```bash
cmake --build --preset asan -j4
ctest --preset asan -L smoke --output-on-failure
cmake --build --preset ubsan -j4
ctest --preset ubsan -L smoke --output-on-failure
```

### 수동 GUI 확인

자동화 테스트는 UI 배치와 조작성까지 판정하지 않는다. 최종 병합 전에 한 번은 일반 에디터 모드에서 아래를 확인한다.

1. 프로젝트를 열었을 때 Editor 창이 표시된다.
2. Hierarchy에서 Sprite GameObject가 보인다.
3. Scene 저장 후 dirty marker가 사라진다.
4. Play 중 생성한 Object가 Stop 후 사라진다.
5. Build 후 생성된 실행 파일을 Finder 또는 터미널에서 직접 실행할 수 있다.

---

## 5. 완료 기준

- [ ] `editor_smoke`가 Scene 저장과 Play/Stop 격리를 검증한다.
- [ ] `runtime_smoke`가 패키지 기준 Scene과 Asset 경로 계약을 검증한다.
- [ ] `build_smoke`가 필수 패키지 레이아웃과 JSON 리포트를 검증한다.
- [ ] `molga_engine --smoke-build`가 GLFW 초기화 없이 실제 GameBuilder를 실행한다.
- [ ] `molga_runtime --smoke`가 숨김 창에서 실제 Asset 해석과 Render 루프를 실행한다.
- [ ] `smoke_end_to_end`가 실제 에디터 실행 파일로 빌드하고 빌드된 런타임을 실행한다.
- [ ] Debug와 Release CI에서 모든 smoke 라벨 테스트가 필수 통과 조건이다.
- [ ] 실패 시 종료 코드와 JSON 리포트만으로 어느 단계가 실패했는지 구분할 수 있다.
- [ ] 기본 에디터와 기본 런타임 실행 동작은 CLI 옵션을 주지 않았을 때 바뀌지 않는다.
- [ ] 전체 `ctest --preset debug --output-on-failure`가 통과한다.

---

## 6. 비목표

아래 항목은 이 Task에서 구현하지 않는다.

- 픽셀 단위 Golden Image 테스트
- ImGui 위젯 자동 클릭 테스트
- 다중 운영체제 패키지 설치 프로그램
- 성능 회귀 측정
- 대규모 프로젝트 로딩 테스트
- 네트워크 또는 원격 Asset 테스트

이 Task의 결과는 Phase 0의 회귀 방지 바닥선이다. UI 상호작용 자동화와 성능 회귀 테스트는 [phase-1-3_roadmap.md](phase-1-3_roadmap.md)의 후속 단계에서 추가한다.
