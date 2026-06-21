# UX-4: Script Iteration (스크립트 이터레이션)

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 구현 시 `superpowers:test-driven-development`, 완료 선언 전 `superpowers:verification-before-completion`을 사용한다. 체크박스(`- [ ]`)로 진행을 추적한다.
>
> **의존성:** 이 작업은 **UX-2**(`03_ux2_console_and_tasks.md`)가 도입하는 `EditorTaskService`와 Console sink **위에** 직접 쌓인다. UX-2가 task/console 표면을 만들고, **UX-4는 그것을 비동기 스크립트 컴파일·리로드 전 흐름까지 확장**한다. 또한 UX-1(`02_ux1…`)의 dirty/command 계약을 사용한다. UX-2 미완 상태에서 이 문서를 구현하려면, 아래 Task A가 정의하는 `EditorTaskService` 최소 API를 UX-2와 호환되게 먼저 세운다.

**Goal:** 사용자가 에디터를 켜둔 채 게임플레이 스크립트를 수정하고, **UI를 멈추지 않고** 컴파일하며, 성공 시 동적 라이브러리를 **안전한 지점에서** 교체하고, 씬 오브젝트의 직렬화된 스크립트 필드 값을 보존한 채 리로드를 끝낼 수 있게 한다. 실패한 컴파일은 현재 실행 가능한 스크립트 런타임(마지막 정상 라이브러리)을 파괴하지 않는다.

**Architecture:** 컴파일은 **메인 스레드 밖**에서 별도 프로세스(`cmake --build`)로 실행되고, stdout/stderr는 Console로 스트리밍된다. 컴파일러 프로세스 실행은 주입 가능한 `IProcessRunner` 심(seam)으로 감싸 정책/상태 머신을 결정적으로 단위 테스트한다. 라이브러리 교체(unload/reload)와 stale 포인터 무효화는 **에디터 프레임의 문서화된 안전 지점**(Play가 아닌 Edit 모드, 프레임 사이, 스택에 스크립트 `Update`가 없는 시점)에서만 수행한다. 직렬화된 스크립트 필드는 unload **전에** 각 살아있는 `Script`를 JSON으로 스냅샷하고, reload 후 동일한 `RegisterFields` 기반 `Deserialize`로 복원한다. 계층 경계(`phase-1-3_roadmap.md` §2.1)를 따른다: Runtime(`ScriptManager`)은 ImGui/Editor singleton을 모르고, 정책/오케스트레이션은 Editor 계층(`EditorTaskService`, `ScriptReloadService`)이 소유한다.

**Tech Stack:** C++17, doctest, ImGui, 플랫폼 동적 라이브러리(`Platform::LoadDynamicLibrary`/`GetSymbol`/`CloseDynamicLibrary`), 새 `Platform::RunProcess` 프로세스 심.

**닫는 결함 (갭 분석 §4):**

- UI가 `compiler.Compile()`을 **동기 호출**한다 (`ScriptWindow.cpp:66` — Compile 버튼 클릭이 메인 스레드를 블로킹).
- 백그라운드 task 진행 모델·취소가 없다 (UX-2가 도입, UX-4가 스크립트 흐름으로 확장).
- UI 수준에서 **last-good 동적 라이브러리 정책**이 없다 (실패해도 현재 런타임을 깨뜨리지 않는 보증 부재).
- 컴파일러 출력이 Console로 가지 않고 팝업 문자열로만 보인다 (`ScriptWindow.cpp:251`).
- Play 중 compile/reload 정책이 불명확하다.

---

## 현재 상태 (검증된 사실)

- **동기 컴파일 호출처:** `ScriptWindow::DrawToolbar`의 "Compile" 버튼이 메인 스레드에서 `compiler.Compile()`을 직접 호출하고 그 자리에서 결과를 받는다 (`src/Editor/Windows/ScriptWindow.cpp:61-75`). `Compile()` 내부는 `cmake -S . -B build`와 `cmake --build build`를 `popen`으로 **블로킹** 실행한다 (`src/Scripting/ScriptCompiler.cpp:227-272`, `ExecuteCommand` `:441-467`).
- **프로세스 실행 심 부재:** `Platform.h`에는 동적 라이브러리 API만 있고 프로세스 실행 API가 없다 (`src/Platform/Platform.h:7-11`). 컴파일은 `ScriptCompiler::ExecuteCommand`가 `popen`/`pclose`로 직접 수행한다 (`src/Scripting/ScriptCompiler.cpp:444-466`). 즉 컴파일 호출을 테스트에서 가짜로 바꿀 이음새가 아직 없다.
- **컴파일 출력 캡처:** `ScriptCompiler::compileOutput`(전체 stdout/stderr 누적)·`lastError`가 멤버로 보존되고 `GetCompileOutput()`/`GetLastError()`로 노출된다 (`src/Scripting/ScriptCompiler.h:31-32`, `:63-64`). 현재는 ImGui 팝업 텍스트로만 표시된다 (`ScriptWindow.cpp:240-255`).
- **라이브러리 로드/언로드:** `ScriptManager::LoadScriptLibrary`는 `Platform::LoadDynamicLibrary` 후 `RegisterScripts` 심볼을 찾아 호출한다 (`src/Scripting/ScriptManager.cpp:62-91`). `UnloadScriptLibrary`는 `Platform::CloseDynamicLibrary` 후 핸들·경로를 지운다 (`:93-107`). `ReloadScriptLibraries`는 전부 unload → `dynamicFactories.clear()` → 전부 reload 한다 (`:109-127`).
- **Hot Reload 진입:** `ScriptWindow`의 "Hot Reload" 버튼이 `GetCompiledLibraryPath()`로 경로를 얻어 `LoadScriptLibrary`를 호출한다 (`src/Editor/Windows/ScriptWindow.cpp:80-96`). **언로드/필드 보존 없이 같은 경로를 재로드**할 뿐이라, 이미 로드된 경우 `LoadScriptLibrary`가 즉시 `true`를 반환한다 (`ScriptManager.cpp:63-67`).
- **last-good 정책 부재:** `ReloadScriptLibraries`는 reload가 실패해도 `dynamicFactories`를 이미 비운 뒤다 → 실패 시 **현재 실행 가능한 스크립트 런타임이 사라진다**. 갭 분석 §4 "failure keeps last-good library active"를 만족하지 못한다.
- **스크립트 인스턴스 바인딩:** 씬 로드 시 `SceneSerializer`가 컴포넌트 타입을 팩토리에서 못 찾으면 `ScriptManager::Get().CreateScript(type)`로 동적 스크립트를 생성한다 (`src/Core/SceneSerializer.cpp:199-201`, `:307`, `:487`). `CreateScript`는 `dynamicFactories`를 먼저, 그다음 `builtinFactories`를 찾는다 (`ScriptManager.cpp:27-40`). **동적 팩토리가 가리키는 함수 포인터는 라이브러리가 unload되면 무효**가 된다.
- **직렬화된 스크립트 필드가 보존되는 방식 (오늘):** `Script::RegisterFields(ScriptFieldRegistry&)`로 등록된 멤버 포인터를, `Script::Serialize`가 `j["fields"]` 하위 객체에 **필드명을 키로** 기록하고(`src/Scripting/Script.cpp:17-50`), `Script::Deserialize`가 같은 키로 되읽어 멤버에 써넣는다(`:52-94`). 타입은 `ScriptFieldType`(Float/Int/Bool/String/Vector2/Color/ObjectRef/PrefabRef, `src/Scripting/ScriptField.h:29-38`). 즉 **필드는 라이브러리 핸들이 아니라 필드명 키 JSON으로 저장**되므로, 같은 클래스명·필드명을 가진 새 인스턴스로 그대로 복원 가능하다 — 이것이 reload 시 필드 보존의 토대다.
- **Edit/Play World 분리:** `SceneDocument`는 권위 사본 `editWorld_`와 Play 중에만 존재하는 휘발 사본 `playWorld_`를 구분한다 (`src/Editor/SceneDocument.h:10-43`). `EnterPlay()`는 `editWorld_.Clone()`으로 playWorld를 만들고, `ExitPlay()`는 reset한다. `IsPlaying()`은 `playWorld_ != nullptr`.
- **에디터 모드:** `EditorState`가 `Edit | Play | Pause`를 가지며 `IsPlayMode()`/`IsEditMode()`를 노출한다 (`src/Editor/EditorState.h:4-23`).
- **프레임 루프 (안전 지점 근거):** 메인 루프는 `Time::Update` → (Play일 때만) `ActiveWorld().Update/LateUpdate/FlushDeferred` → 메인 클리어 → `ImGuiLayer::BeginFrame` → `Editor::Update`/`RenderGUI` → `ImGuiLayer::EndFrame` → `EventBus::ProcessQueue()` → `glfwSwapBuffers` → `glfwPollEvents` 순서다 (`src/main.cpp:220-290`). Play 시뮬레이션(스크립트 `Update` 호출)은 `:246-257`에서만 일어난다.
- **VS Code 연동:** `VSCodeIntegration::OpenFileInVSCode(path)`가 존재하고 스크립트 생성/더블클릭 시 사용된다 (`ScriptWindow.cpp:137`, `:203`). 컴파일 오류 줄을 외부 에디터로 여는 데 재사용한다.
- **빌드 배치:** `ScriptManager.cpp`/`Script.cpp`는 `molga_core`(테스트 링크 대상)에 있다 (`CMakeLists.txt:85-86`). `ScriptCompiler.cpp`는 에디터 전용 `EDITOR_SOURCES`에 있다 (`CMakeLists.txt:166`). 따라서 **reload 정책 상태 머신은 core에**, 프로세스 오케스트레이션은 editor에 두면 core 링크만으로 단위 테스트가 가능하다.
- **테스트 등록:** `tests/CMakeLists.txt`의 `molga_add_test(name src)`는 `molga_core doctest_main molga_warnings`에 링크한다 (`tests/CMakeLists.txt:9-13`). 즉 테스트 대상 타입은 `molga_core`에 있어야 한다.
- **`EditorTaskService` 부재:** 현재 코드베이스에 task service/`TaskId`/task 상태 enum이 전혀 없다 (`grep` 결과 0건). UX-2가 처음 도입한다.

---

## 파일 구조

**Create:**
- `src/Core/EditorTaskService.h` — `EditorTaskService`, `TaskId`, `TaskCategory`, `TaskState`, `TaskInfo`(헤더 온리 상태/스레드 안전 스냅샷). **UX-2와 공유** — UX-2가 이미 만들었다면 이 파일을 *확장*만 한다.
- `src/Core/EditorTaskService.cpp` — 비-헤더 구현이 필요할 경우(스레드/뮤텍스). UX-2가 만든 것을 확장.
- `src/Platform/Process.h` — `IProcessRunner` 인터페이스 + `ProcessResult`.
- `src/Platform/Process.cpp` — `SystemProcessRunner`(POSIX `popen`/Windows `_popen` 줄단위 스트리밍 콜백).
- `src/Scripting/ScriptReloadService.h` — last-good 정책 + 필드 스냅샷/복원 + safe-point 큐(상태 머신, 헤더 온리에 가깝게).
- `src/Scripting/ScriptReloadService.cpp` — 구현(`molga_core`).
- `tests/test_editor_task_service.cpp`
- `tests/test_process_runner.cpp`
- `tests/test_script_field_snapshot.cpp`
- `tests/test_script_reload_service.cpp`
- `tests/test_play_mode_compile_policy.cpp`

**Modify:**
- `src/Platform/Platform.cpp` / `src/Platform/Platform.h` — (선택) 프로세스 심을 Platform 네임스페이스에 둘 경우. 기본은 별도 `Process.h`.
- `src/Scripting/ScriptManager.h` / `.cpp` — `LoadScriptLibrary`가 핸들 반환·`RegisterScripts` 검증을 노출(`bool Validate`), unload-전-파괴 보증, last-good 보존을 위한 분리된 `SwapLibrary`.
- `src/Scripting/Script.h` / `.cpp` — `SnapshotFields()`/`RestoreFields()` 공개 헬퍼(기존 Serialize/Deserialize 재사용).
- `src/Editor/Windows/ScriptWindow.cpp` — Compile 버튼이 `EditorTaskService`에 비동기 task를 큐잉하고, Hot Reload가 `ScriptReloadService`를 거치도록 교체.
- `src/Editor/Editor.h` / `.cpp` — `EditorTaskService`/`ScriptReloadService` 소유, 매 프레임 safe-point 펌프 호출.
- `src/main.cpp` — 프레임 끝의 문서화된 safe point에서 `ScriptReloadService::PumpPendingReload(...)` 호출 추가.
- `CMakeLists.txt` — `EDITOR_SOURCES`에 `src/Platform/Process.cpp`, `ENGINE_SOURCES`에 `src/Core/EditorTaskService.cpp`·`src/Scripting/ScriptReloadService.cpp` 등록.
- `tests/CMakeLists.txt` — 새 테스트 5개 등록.

> **배치 원칙:** 상태 머신/정책(`EditorTaskService`, `ScriptReloadService`, 필드 스냅샷)은 `molga_core`에 둬 `molga_add_test`로 단위 테스트한다. 실제 `cmake --build` 호출은 editor 전용 `SystemProcessRunner`로만 수행하고, core 테스트에는 `FakeProcessRunner`를 주입한다.

---

## Task A. EditorTaskService 확장 (스크립트 카테고리 + 스레드)

> UX-2가 `EditorTaskService` 기본 골격(task 등록·진행·Console 로그 스트림)을 만든다. 여기서는 **`ScriptCompile`/`ScriptReload` 카테고리와 백그라운드 스레드에서 안전하게 상태/로그를 갱신하는 경로**를 보장하고, UX-4가 의존하는 정확한 API를 못 박는다.

**Files:**
- Create/Modify: `src/Core/EditorTaskService.h`, `src/Core/EditorTaskService.cpp`
- Create: `tests/test_editor_task_service.cpp`
- Modify: `CMakeLists.txt`(`ENGINE_SOURCES`), `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성** — `tests/test_editor_task_service.cpp`

```cpp
#include "Core/EditorTaskService.h"
#include "doctest.h"
#include <thread>

using molga::EditorTaskService;
using molga::TaskCategory;
using molga::TaskState;
using molga::TaskId;

TEST_CASE("queued task is observable and transitions to running then succeeded") {
    EditorTaskService svc;
    TaskId id = svc.Begin("Compile UserScripts", TaskCategory::ScriptCompile);
    CHECK(svc.Get(id).state == TaskState::Queued);

    svc.MarkRunning(id);
    CHECK(svc.Get(id).state == TaskState::Running);

    svc.AppendLog(id, "=== CMake Build ===\n");
    svc.Complete(id, TaskState::Succeeded);

    auto info = svc.Get(id);
    CHECK(info.state == TaskState::Succeeded);
    CHECK(info.category == TaskCategory::ScriptCompile);
    CHECK(info.log.find("CMake Build") != std::string::npos);
}

TEST_CASE("logs appended from a worker thread are visible on the main thread") {
    EditorTaskService svc;
    TaskId id = svc.Begin("Compile", TaskCategory::ScriptCompile);
    svc.MarkRunning(id);

    std::thread worker([&]{
        for (int i = 0; i < 100; ++i) svc.AppendLog(id, "line\n");
        svc.Complete(id, TaskState::Failed);
    });
    worker.join();

    auto info = svc.Get(id);
    CHECK(info.state == TaskState::Failed);
    CHECK(info.log.size() >= 500);   // 100 * "line\n"
}

TEST_CASE("cancel before run yields Cancelled and skips work") {
    EditorTaskService svc;
    TaskId id = svc.Begin("Compile", TaskCategory::ScriptCompile);
    svc.RequestCancel(id);
    CHECK(svc.IsCancelRequested(id));
    svc.Complete(id, TaskState::Cancelled);
    CHECK(svc.Get(id).state == TaskState::Cancelled);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_editor_task_service test_editor_task_service.cpp)
```
```bash
cmake --build --preset debug --target test_editor_task_service -j4
```
Expected: FAIL — `EditorTaskService` 헤더/심볼 없음(또는 UX-2 버전에 `ScriptCompile` 카테고리·worker-safe `AppendLog` 부재).

- [ ] **Step 3: API 정의/확장** — `src/Core/EditorTaskService.h`

```cpp
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace molga {

using TaskId = uint64_t;

enum class TaskCategory { Build, ScriptCompile, ScriptReload, Import, Package, ShaderReload };
enum class TaskState    { Queued, Running, Succeeded, Failed, Cancelled };

// 메인 스레드에 건네는 불변 스냅샷(직접 ImGui를 건드리지 않는다).
struct TaskInfo {
    TaskId       id = 0;
    std::string  name;
    TaskCategory category = TaskCategory::Build;
    TaskState    state    = TaskState::Queued;
    float        progress = 0.0f;     // 0..1, 불확정이면 -1
    std::string  log;                 // 누적 stdout/stderr
    std::string  result;              // 결과 payload(예: 라이브러리 경로)
};

// 스레드 안전 task 레지스트리. 워커 스레드가 상태/로그를 갱신하고,
// 메인 스레드(ImGui)는 Get()/Snapshot()으로 복사본만 읽는다.
class EditorTaskService {
public:
    TaskId Begin(const std::string& name, TaskCategory cat);
    void   MarkRunning(TaskId id);
    void   SetProgress(TaskId id, float p);
    void   AppendLog(TaskId id, const std::string& chunk);   // 워커 스레드 호출 OK
    void   SetResult(TaskId id, const std::string& payload);
    void   Complete(TaskId id, TaskState terminal);

    void   RequestCancel(TaskId id);
    bool   IsCancelRequested(TaskId id) const;

    TaskInfo               Get(TaskId id) const;
    std::vector<TaskInfo>  Snapshot() const;                 // UI 패널용

private:
    mutable std::mutex mtx_;
    std::atomic<TaskId> next_{1};
    std::unordered_map<TaskId, TaskInfo> tasks_;
    std::unordered_map<TaskId, bool>     cancel_;
};

} // namespace molga
```

- [ ] **Step 4: 구현** — `src/Core/EditorTaskService.cpp`: 모든 변경을 `std::lock_guard<std::mutex>`로 보호. `Begin`은 `next_++`로 id 발급 후 `Queued`로 삽입. `AppendLog`는 `tasks_[id].log += chunk`. `IsCancelRequested`는 락 하에 `cancel_` 조회.

- [ ] **Step 5: CMake 등록 + 통과 확인**

`CMakeLists.txt`의 `ENGINE_SOURCES`에 `src/Core/EditorTaskService.cpp` 추가.
```bash
cmake --preset debug
cmake --build --preset debug --target test_editor_task_service -j4
ctest --preset debug -R test_editor_task_service --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: 커밋**
```bash
git add src/Core/EditorTaskService.h src/Core/EditorTaskService.cpp \
        tests/test_editor_task_service.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(tasks): EditorTaskService with ScriptCompile/Reload categories + worker-safe log stream"
```

---

## Task B. 프로세스 심 + 비동기 컴파일 (stdout/stderr → Console)

> 목표: `cmake --build`를 **메인 스레드 밖**에서 돌리고 줄단위로 task 로그(→ Console)에 스트리밍. 테스트는 `FakeProcessRunner`로 결정적으로 한다(실제 빌드 미수행).

**Files:**
- Create: `src/Platform/Process.h`, `src/Platform/Process.cpp`
- Create: `tests/test_process_runner.cpp`
- Modify: `CMakeLists.txt`(`EDITOR_SOURCES`에 `Process.cpp`; 단 인터페이스/Fake는 `molga_core` 테스트에서 쓰도록 헤더 온리), `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성** — `tests/test_process_runner.cpp`

```cpp
#include "Platform/Process.h"
#include "doctest.h"
#include <vector>
#include <string>

using molga::IProcessRunner;
using molga::ProcessResult;

namespace {
// 정해진 줄을 콜백으로 흘리고 정해진 exit code를 돌려주는 가짜 러너.
struct FakeProcessRunner : IProcessRunner {
    std::vector<std::string> lines;
    int exitCode = 0;
    ProcessResult Run(const std::string& /*cmd*/, const std::string& /*workdir*/,
                      const std::function<void(const std::string&)>& onLine,
                      const std::function<bool()>& isCancelled) override {
        for (auto& l : lines) {
            if (isCancelled && isCancelled()) return { -1, true };
            onLine(l);
        }
        return { exitCode, false };
    }
};
}

TEST_CASE("runner streams each line then reports exit code") {
    FakeProcessRunner r;
    r.lines = { "configuring\n", "building\n", "done\n" };
    r.exitCode = 0;

    std::string captured;
    ProcessResult res = r.Run("cmake --build build", "/proj/Scripts",
        [&](const std::string& l){ captured += l; },
        []{ return false; });

    CHECK(res.exitCode == 0);
    CHECK_FALSE(res.cancelled);
    CHECK(captured == "configuring\nbuilding\ndone\n");
}

TEST_CASE("cancellation stops streaming and marks cancelled") {
    FakeProcessRunner r;
    r.lines = { "a\n", "b\n", "c\n" };

    int seen = 0;
    ProcessResult res = r.Run("cmd", "/wd",
        [&](const std::string&){ ++seen; },
        [&]{ return seen >= 1; });   // 첫 줄 뒤 취소

    CHECK(res.cancelled);
    CHECK(seen == 1);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_process_runner test_process_runner.cpp)
```
```bash
cmake --build --preset debug --target test_process_runner -j4
```
Expected: FAIL — `Process.h` 없음.

- [ ] **Step 3: 인터페이스 작성** — `src/Platform/Process.h`

```cpp
#pragma once
#include <string>
#include <functional>

namespace molga {

struct ProcessResult {
    int  exitCode  = 0;
    bool cancelled = false;
};

// 외부 프로세스를 줄단위로 실행하는 심. 테스트는 Fake로 주입한다.
class IProcessRunner {
public:
    virtual ~IProcessRunner() = default;
    // onLine: stdout/stderr 한 줄마다 호출(워커 스레드에서). isCancelled가
    // true를 반환하면 가능한 한 빨리 중단한다.
    virtual ProcessResult Run(const std::string& command,
                              const std::string& workdir,
                              const std::function<void(const std::string&)>& onLine,
                              const std::function<bool()>& isCancelled) = 0;
};

// 실제 popen/_popen 기반 러너(EDITOR_SOURCES).
class SystemProcessRunner : public IProcessRunner {
public:
    ProcessResult Run(const std::string& command,
                      const std::string& workdir,
                      const std::function<void(const std::string&)>& onLine,
                      const std::function<bool()>& isCancelled) override;
};

} // namespace molga
```

- [ ] **Step 4: SystemProcessRunner 구현** — `src/Platform/Process.cpp`: 기존 `ScriptCompiler::ExecuteCommand`(`ScriptCompiler.cpp:441-467`) 패턴을 재사용하되 `fgets` 루프에서 **줄 단위로 `onLine` 호출**하고, 매 줄 사이 `isCancelled()`를 점검한다. `workdir`은 `cd "<workdir>" && <command> 2>&1` 형태로 합성(기존 `Compile()`의 `cd ... && cmake` 방식과 동일, `ScriptCompiler.cpp:246`,`:259`).

- [ ] **Step 5: ScriptCompiler를 러너 주입형으로 분해**

`src/Scripting/ScriptCompiler.h`에 task/runner 친화 API 추가:
```cpp
    // 동기 Compile()을 분해: 빌드 커맨드 2개와 작업 디렉터리를 노출한다.
    std::string ConfigureCommand() const;   // "cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug"
    std::string BuildCommand() const;       // "cmake --build build"
    const std::string& ScriptsDir() const { return scriptsPath; }
```
구현은 `Compile()` 안의 문자열(`ScriptCompiler.cpp:246`,`:259`)을 그대로 옮긴다. 기존 `Compile()`은 호환을 위해 남기되, 새 경로는 `EditorTaskService`+`IProcessRunner`로 두 커맨드를 순차 실행한다.

- [ ] **Step 6: ScriptWindow Compile 버튼을 비동기로 교체**

`src/Editor/Windows/ScriptWindow.cpp:61-75`의 동기 블록을 task 큐잉으로:
```cpp
if (ImGui::Button("Compile")) {
    ScriptCompiler& c = ScriptCompiler::Get();
    c.SetProjectPath(project.GetPath());
    c.GenerateCMakeLists();                       // 메인 스레드(파일 I/O)에서 1회
    TaskId id = Editor::Get().Tasks().Begin("Compile UserScripts", TaskCategory::ScriptCompile);
    Editor::Get().LaunchScriptCompile(id, c.ScriptsDir(),
                                      c.ConfigureCommand(), c.BuildCommand());
}
```
`Editor::LaunchScriptCompile`(아래 Task C에서 정의)은 **별도 `std::thread`**에서 `SystemProcessRunner`로 두 커맨드를 돌리고, `onLine`마다 `Tasks().AppendLog(id, line)`(→ Console sink로도 미러)하고, 종료 시 `Tasks().Complete(id, ...)`를 호출한다. UI는 어떤 프레임도 블로킹하지 않는다.

- [ ] **Step 7: CMake 등록 + 통과/빌드 확인**

`CMakeLists.txt` `EDITOR_SOURCES`에 `src/Platform/Process.cpp` 추가.
```bash
cmake --preset debug
cmake --build --preset debug --target test_process_runner -j4
ctest --preset debug -R test_process_runner --output-on-failure
cmake --build --preset debug --target molga_engine -j4
```
Expected: 테스트 PASS, 에디터 빌드 성공.

- [ ] **Step 8: 커밋**
```bash
git add src/Platform/Process.h src/Platform/Process.cpp \
        src/Scripting/ScriptCompiler.h src/Scripting/ScriptCompiler.cpp \
        src/Editor/Windows/ScriptWindow.cpp CMakeLists.txt tests/CMakeLists.txt \
        tests/test_process_runner.cpp
git commit -m "feat(script): injectable process runner; async compile streams stdout/stderr to task log"
```

---

## Task C. 라이브러리 검증 + last-good 보존 (실패 시 현재 런타임 유지)

> 목표: 컴파일 성공 시에만 새 라이브러리를 검증(로드 가능 + `RegisterScripts` 존재)하고, 검증 실패면 **현재 라이브러리를 그대로 유지**한다. `ReloadScriptLibraries`가 reload 실패 시 런타임을 비우는 결함(`ScriptManager.cpp:109-127`)을 닫는다.

**Files:**
- Modify: `src/Scripting/ScriptManager.h`, `src/Scripting/ScriptManager.cpp`
- Test: `tests/test_script_reload_service.cpp`(일부) — 단, 실제 dlopen 없는 검증 경로는 fake 핸들 정책으로 테스트.

- [ ] **Step 1: 실패 케이스부터** — last-good 보존 단위 테스트를 Task E의 `ScriptReloadService` 테스트로 통합(아래). 여기서는 `ScriptManager`에 검증/스왑 API를 추가한다.

`src/Scripting/ScriptManager.h`에 추가:
```cpp
    // path를 로드하되 RegisterScripts 심볼 존재까지 검증한다. 성공 시 핸들을
    // outHandle로 돌려주고 등록은 호출자가 SwapLibrary로 확정한다.
    bool ValidateLibrary(const std::string& path, void*& outHandle, std::string& error);

    // newHandle/newPath를 활성으로 채택하고, 직전 활성 라이브러리는 unload한다.
    // 채택 전에 모든 동적 팩토리를 비우고 RegisterScripts를 재호출한다.
    void SwapToValidatedLibrary(void* newHandle, const std::string& newPath);

    // 현재 활성(마지막 정상) 라이브러리 경로. 없으면 빈 문자열.
    const std::string& ActiveLibraryPath() const { return activeLibraryPath_; }
```
멤버 추가:
```cpp
    std::string activeLibraryPath_;
```

- [ ] **Step 2: 구현**

`ValidateLibrary`: `Platform::LoadDynamicLibrary` → 실패면 `error = Platform::GetDynamicLibraryError(); return false;`. 핸들에서 `Platform::GetSymbol(handle,"RegisterScripts")`가 null이면 `Platform::CloseDynamicLibrary(handle)` 후 `error = "RegisterScripts not found"; return false;`. 성공이면 `outHandle = handle; return true;` (아직 등록은 하지 않는다 — 검증만).

`SwapToValidatedLibrary`: 직전 `activeLibraryPath_`가 있으면 그 핸들을 `UnloadScriptLibrary`로 닫고, `dynamicFactories.clear()` 후 `outHandle`의 `RegisterScripts`를 호출, `libraryHandles[newPath]=newHandle`·`loadedLibraries` 갱신·`activeLibraryPath_=newPath`. **검증된 핸들만 들어오므로** 이 시점에서 실패하지 않는다 → last-good 보장.

> 핵심: 기존 `ReloadScriptLibraries`의 "비우고 → 다시 로드" 순서를 뒤집어, **새 라이브러리가 검증을 통과한 다음에야** 과거 것을 버린다.

- [ ] **Step 3: 빌드만 확인(테스트는 Task E에서)**
```bash
cmake --build --preset debug --target molga_core -j4
```
Expected: 빌드 성공.

- [ ] **Step 4: 커밋**
```bash
git add src/Scripting/ScriptManager.h src/Scripting/ScriptManager.cpp
git commit -m "feat(script): validate library before swap; keep last-good active on failure"
```

---

## Task D. 직렬화된 스크립트 필드 스냅샷/복원

> 목표: reload 전에 살아있는 각 `Script` 인스턴스의 등록 필드를 JSON으로 스냅샷하고, reload 후 동일 클래스·필드명으로 재생성된 인스턴스에 복원. 기존 `Serialize`/`Deserialize`(`Script.cpp:17-94`)를 그대로 재사용한다.

**Files:**
- Modify: `src/Scripting/Script.h`, `src/Scripting/Script.cpp`
- Create: `tests/test_script_field_snapshot.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성** — `tests/test_script_field_snapshot.cpp`

```cpp
#include "Scripting/Script.h"
#include "doctest.h"
#include <nlohmann/json.hpp>

namespace {
// reload 시 "같은 클래스의 새 인스턴스"를 흉내내는 두 객체.
struct DummyScript : Script {
    float speed = 0.0f;
    int   lives = 0;
    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Float("Speed", &speed).Int("Lives", &lives);
    }
    const char* GetScriptName() const override { return "DummyScript"; }
};
}

TEST_CASE("field values survive a snapshot/restore round trip (reload simulation)") {
    DummyScript before;
    before.speed = 250.0f;
    before.lives = 3;

    nlohmann::json snap = before.SnapshotFields();   // unload 직전

    DummyScript after;                               // reload 후 새 인스턴스
    CHECK(after.speed == 0.0f);
    after.RestoreFields(snap);                       // 복원

    CHECK(after.speed == doctest::Approx(250.0f));
    CHECK(after.lives == 3);
}

TEST_CASE("snapshot of a script with no registered fields is empty but safe") {
    struct Bare : Script { const char* GetScriptName() const override { return "Bare"; } };
    Bare b;
    nlohmann::json snap = b.SnapshotFields();
    Bare b2;
    b2.RestoreFields(snap);   // no-op, must not throw
    CHECK(true);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_script_field_snapshot test_script_field_snapshot.cpp)
```
```bash
cmake --build --preset debug --target test_script_field_snapshot -j4
```
Expected: FAIL — `SnapshotFields`/`RestoreFields` 미정의.

- [ ] **Step 3: 구현** — `src/Scripting/Script.h` public에 추가:
```cpp
    // reload 경계에서 사용하는 필드 스냅샷/복원. Serialize/Deserialize의
    // "fields" 페이로드를 그대로 재사용한다.
    nlohmann::json SnapshotFields() const;
    void RestoreFields(const nlohmann::json& snapshot);
```
`src/Scripting/Script.cpp`:
```cpp
nlohmann::json Script::SnapshotFields() const {
    nlohmann::json j;
    Serialize(j);            // j["fields"] = {...} 채움(없으면 비움)
    return j;
}

void Script::RestoreFields(const nlohmann::json& snapshot) {
    Deserialize(snapshot);   // "fields"가 없으면 즉시 반환(Script.cpp:53)
}
```

- [ ] **Step 4: 통과 + 커밋**
```bash
cmake --build --preset debug --target test_script_field_snapshot -j4
ctest --preset debug -R test_script_field_snapshot --output-on-failure
git add src/Scripting/Script.h src/Scripting/Script.cpp \
        tests/test_script_field_snapshot.cpp tests/CMakeLists.txt
git commit -m "feat(script): SnapshotFields/RestoreFields reuse field serialization for reload"
```

---

## Task E. reload-safe 포인터 무효화 (safe-point 상태 머신)

> 목표: 라이브러리 unload **전에** 모든 살아있는 `Script` 인스턴스를 파괴(또는 필드 스냅샷 후 제거)하고, swap 후 동일 오브젝트에 새 인스턴스를 재생성·복원한다. **모든 작업은 문서화된 안전 지점에서만** 수행하도록 큐잉한다. `IProcessRunner`/`ScriptManager`를 주입해 dlopen 없이 정책을 테스트한다.

**Files:**
- Create: `src/Scripting/ScriptReloadService.h`, `src/Scripting/ScriptReloadService.cpp`
- Create: `tests/test_script_reload_service.cpp`
- Modify: `CMakeLists.txt`(`ENGINE_SOURCES`), `tests/CMakeLists.txt`, `src/main.cpp`, `src/Editor/Editor.h/.cpp`

- [ ] **Step 1: 안전 지점 계약 명시(문서 + 테스트로 표현)**

**Reload 안전 지점 정의:** reload(unload+swap+재바인딩)는 *프레임 사이*에만, 다음 조건이 **모두** 참일 때 실행한다.
1. 에디터가 **Edit 모드**다(`EditorState::IsEditMode()`, `EditorState.h:17`). Play 중에는 `playWorld_`가 살아있어 스크립트 인스턴스가 시뮬레이션에 참여하므로 reload하지 않는다.
2. 현재 콜스택에 스크립트 `Update`/`FixedUpdate`가 **없다** — 메인 루프에서 Play 시뮬레이션 구간(`main.cpp:246-257`) 밖이라는 뜻.
3. 프레임 마무리 지점, 즉 `EventBus::ProcessQueue()` 이후·`glfwSwapBuffers` 직전(`main.cpp:286-288`)에서 `PumpPendingReload`를 호출한다.

이 지점에서 모든 동적 `Script`는 메인 스레드 소유의 `editWorld_` 오브젝트에 매달려 있고, 어떤 활성 호출 프레임에도 잡혀 있지 않으므로 안전하게 파괴/재생성할 수 있다.

- [ ] **Step 2: 실패하는 테스트 작성** — `tests/test_script_reload_service.cpp`

```cpp
#include "Scripting/ScriptReloadService.h"
#include "doctest.h"

using molga::ScriptReloadService;
using molga::ReloadOutcome;

namespace {
// 검증 성공/실패와 스왑 횟수를 추적하는 가짜 라이브러리 포트.
struct FakeLibraryPort : molga::ILibraryPort {
    bool validateOk = true;
    int  swaps = 0;
    std::string active = "lib.v1";
    bool Validate(const std::string&, std::string& err) override {
        if (!validateOk) { err = "bad library"; return false; }
        return true;
    }
    void Swap(const std::string& path) override { active = path; ++swaps; }
    std::string Active() const override { return active; }
};
}

TEST_CASE("successful validation swaps to the new library") {
    FakeLibraryPort lib;
    ScriptReloadService svc(&lib);
    ReloadOutcome out = svc.PerformReload("lib.v2");
    CHECK(out == ReloadOutcome::Reloaded);
    CHECK(lib.active == "lib.v2");
    CHECK(lib.swaps == 1);
}

TEST_CASE("failed validation keeps the last-good library (no swap)") {
    FakeLibraryPort lib;
    lib.validateOk = false;
    ScriptReloadService svc(&lib);
    ReloadOutcome out = svc.PerformReload("lib.broken");
    CHECK(out == ReloadOutcome::ValidationFailed);
    CHECK(lib.active == "lib.v1");   // 변하지 않음 = last-good 유지
    CHECK(lib.swaps == 0);
}

TEST_CASE("reload requested during play mode is deferred until edit mode") {
    FakeLibraryPort lib;
    ScriptReloadService svc(&lib);
    svc.RequestReload("lib.v2");

    // play 중: pump는 아무 것도 하지 않는다
    CHECK(svc.PumpPendingReload(/*isEditMode=*/false) == ReloadOutcome::Deferred);
    CHECK(lib.swaps == 0);

    // stop 후 edit 모드: 큐된 reload가 수행된다
    CHECK(svc.PumpPendingReload(/*isEditMode=*/true) == ReloadOutcome::Reloaded);
    CHECK(lib.active == "lib.v2");
}

TEST_CASE("pump with no pending reload is a no-op") {
    FakeLibraryPort lib;
    ScriptReloadService svc(&lib);
    CHECK(svc.PumpPendingReload(true) == ReloadOutcome::Idle);
}
```

- [ ] **Step 3: 등록 + 실패 확인**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_script_reload_service test_script_reload_service.cpp)
```
```bash
cmake --build --preset debug --target test_script_reload_service -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 4: 인터페이스/상태 머신 작성** — `src/Scripting/ScriptReloadService.h`

```cpp
#pragma once
#include <string>
#include <functional>

namespace molga {

enum class ReloadOutcome { Idle, Deferred, ValidationFailed, Reloaded };

// dlopen/필드 재바인딩을 추상화한 포트(테스트는 Fake로 주입).
class ILibraryPort {
public:
    virtual ~ILibraryPort() = default;
    virtual bool        Validate(const std::string& path, std::string& error) = 0;
    // 검증된 path를 활성으로 채택(직전 라이브러리 unload + 인스턴스 재바인딩).
    virtual void        Swap(const std::string& path) = 0;
    virtual std::string Active() const = 0;
};

// reload 정책 + safe-point 큐. UI/프레임 루프에서 호출한다.
class ScriptReloadService {
public:
    explicit ScriptReloadService(ILibraryPort* lib) : lib_(lib) {}

    // 즉시 검증→스왑 시도(이미 safe-point라고 가정). 테스트/동기 경로용.
    ReloadOutcome PerformReload(const std::string& path);

    // 다음 안전 지점에 수행하도록 큐잉(예: 컴파일 task 성공 콜백에서).
    void RequestReload(const std::string& path) { pending_ = path; hasPending_ = true; }

    // 프레임 끝 safe point에서 호출. Edit 모드가 아니면 Deferred를 돌려준다.
    ReloadOutcome PumpPendingReload(bool isEditMode);

    bool HasPending() const { return hasPending_; }

private:
    ILibraryPort* lib_;
    std::string   pending_;
    bool          hasPending_ = false;
};

} // namespace molga
```

- [ ] **Step 5: 구현** — `src/Scripting/ScriptReloadService.cpp`

```cpp
#include "Scripting/ScriptReloadService.h"

namespace molga {

ReloadOutcome ScriptReloadService::PerformReload(const std::string& path) {
    std::string err;
    if (!lib_->Validate(path, err)) {
        return ReloadOutcome::ValidationFailed;   // last-good 유지(Swap 호출 안 함)
    }
    lib_->Swap(path);
    return ReloadOutcome::Reloaded;
}

ReloadOutcome ScriptReloadService::PumpPendingReload(bool isEditMode) {
    if (!hasPending_)  return ReloadOutcome::Idle;
    if (!isEditMode)   return ReloadOutcome::Deferred;   // Play 중이면 미룬다
    std::string path = pending_;
    hasPending_ = false;
    pending_.clear();
    return PerformReload(path);
}

} // namespace molga
```

- [ ] **Step 6: 실 ILibraryPort 구현 (editor 측, 필드 스냅샷/복원 결합)**

`Editor`(또는 `ScriptManager` 어댑터)에 `ILibraryPort` 구현을 둔다. `Swap(path)`은 safe point에서:
1. `editWorld_`를 순회해 모든 동적 `Script*`를 모으고, 각자 `SnapshotFields()`(Task D)로 JSON 저장 + 소속 GameObject·스크립트 클래스명(`GetScriptName()`) 기록.
2. 해당 `Script` 컴포넌트를 제거(이전 라이브러리의 함수 포인터·vtable 참조 제거 → stale 포인터 무효화).
3. `ScriptManager::ValidateLibrary` 통과분으로 `SwapToValidatedLibrary`(Task C) 호출 → 동적 팩토리 재등록.
4. 기록한 (GameObject, 클래스명) 쌍마다 `ScriptManager::CreateScript(name)`로 새 인스턴스 생성·`AddComponentRaw`로 부착 후 `RestoreFields(snap)`.

`Validate(path,error)`는 `ScriptManager::ValidateLibrary`로 위임.

> **위험 통제(로드맵 §Milestone 1-5):** 동적 라이브러리 unload 전에 모든 스크립트 인스턴스를 파괴하고(2단계), 함수 포인터·RTTI 객체를 unload 이후 보관하지 않는다(`dynamicFactories.clear()` in `SwapToValidatedLibrary`).

- [ ] **Step 7: 프레임 루프 + 컴파일 성공 연결**

- `src/Editor/Editor.h`: `EditorTaskService tasks_; ScriptReloadService reload_;` (또는 포인터) 멤버와 `EditorTaskService& Tasks();`/`ScriptReloadService& ScriptReload();` 접근자, `void PumpScriptReload(bool isEditMode);`, 그리고 Task B Step 6이 호출하는 `void LaunchScriptCompile(TaskId id, const std::string& scriptsDir, const std::string& configureCmd, const std::string& buildCmd);`를 선언한다. `LaunchScriptCompile`은 별도 `std::thread`에서 `SystemProcessRunner`로 두 커맨드를 순차 실행하고 `Tasks().AppendLog`/`Tasks().Complete`를 호출하며, 성공 시 `ScriptReload().RequestReload(...)`를 건다.
- `src/main.cpp`의 safe point(`EventBus::ProcessQueue();` 직후, `glfwSwapBuffers` 직전, `:286-288`)에 추가:
```cpp
            EventBus::ProcessQueue();
            Editor::Get().PumpScriptReload(EditorState::Get().IsEditMode());  // ← safe point
            glfwSwapBuffers(window);
```
- 컴파일 task가 `Succeeded`로 끝나는 콜백(Task B의 워커 종료부)에서 `Editor::Get().ScriptReload().RequestReload(compiler.GetCompiledLibraryPath())` 호출 → 다음 safe point에 reload된다.

- [ ] **Step 8: CMake 등록 + 통과/빌드 확인**

`CMakeLists.txt` `ENGINE_SOURCES`에 `src/Scripting/ScriptReloadService.cpp` 추가.
```bash
cmake --preset debug
cmake --build --preset debug --target test_script_reload_service -j4
ctest --preset debug -R test_script_reload_service --output-on-failure
cmake --build --preset debug --target molga_engine -j4
```
Expected: 테스트 PASS, 에디터 빌드 성공.

- [ ] **Step 9: ASan reload 반복 확인 (로드맵 1-5 완료 기준)**

가능하면 reload 정책+필드 복원을 반복하는 통합 테스트(`FakeLibraryPort`로 N회 swap)를 ASan에서 돌려 use-after-free가 없음을 확인한다.
```bash
cmake --preset asan && cmake --build --preset asan --target test_script_reload_service -j4
ctest --preset asan -R test_script_reload_service --output-on-failure
```

- [ ] **Step 10: 커밋**
```bash
git add src/Scripting/ScriptReloadService.h src/Scripting/ScriptReloadService.cpp \
        src/Editor/Editor.h src/Editor/Editor.cpp src/main.cpp \
        tests/test_script_reload_service.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(script): safe-point reload service; invalidate stale pointers, preserve fields"
```

---

## Task F. Play 모드 컴파일 정책 강제

> 목표: Play 중 컴파일/리로드 동작을 **명시적**으로 만든다. 채택 정책: **컴파일은 Play 중에도 허용(백그라운드 task)하되, reload(라이브러리 스왑)는 Stop 후 Edit 모드까지 큐잉**한다. (갭 분석 §4: "blocked / queued until Stop / allowed with reload" 중 *queued-until-Stop reload* 채택.)

**Files:**
- Create: `tests/test_play_mode_compile_policy.cpp`
- Modify: `src/Editor/Windows/ScriptWindow.cpp`(버튼 가용성/툴팁), `src/Editor/Editor.cpp`(정책 적용 지점)

- [ ] **Step 1: 실패하는 테스트 작성** — `tests/test_play_mode_compile_policy.cpp`

이미 Task E의 `ScriptReloadService`가 Play 중 reload를 `Deferred`로 미루는 것을 검증했다. 여기서는 **컴파일은 허용하되 reload만 미뤄지는** 전체 정책을 결정적으로 본다.

```cpp
#include "Scripting/ScriptReloadService.h"
#include "doctest.h"

using molga::ScriptReloadService;
using molga::ReloadOutcome;

namespace {
struct FakeLibraryPort : molga::ILibraryPort {
    int swaps = 0; std::string active = "v1";
    bool Validate(const std::string&, std::string&) override { return true; }
    void Swap(const std::string& p) override { active = p; ++swaps; }
    std::string Active() const override { return active; }
};
}

TEST_CASE("compile during play queues reload until Stop") {
    FakeLibraryPort lib;
    ScriptReloadService svc(&lib);

    // 컴파일 성공 → reload 요청(엔진은 Play 중)
    svc.RequestReload("v2");

    // Play 동안 여러 프레임 pump: swap 없음
    for (int frame = 0; frame < 5; ++frame)
        CHECK(svc.PumpPendingReload(/*isEditMode=*/false) == ReloadOutcome::Deferred);
    CHECK(lib.swaps == 0);
    CHECK(svc.HasPending());

    // Stop → Edit: 정확히 한 번 reload
    CHECK(svc.PumpPendingReload(true) == ReloadOutcome::Reloaded);
    CHECK(lib.swaps == 1);
    CHECK(lib.active == "v2");
    CHECK_FALSE(svc.HasPending());
}
```

- [ ] **Step 2: 등록 + 실패 확인 → (Task E 구현으로 이미 통과해야 함)**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_play_mode_compile_policy test_play_mode_compile_policy.cpp)
```
```bash
cmake --build --preset debug --target test_play_mode_compile_policy -j4
ctest --preset debug -R test_play_mode_compile_policy --output-on-failure
```
Expected: PASS(상태 머신이 이미 정책을 구현). FAIL이면 `PumpPendingReload`가 pending을 유지하는지(Deferred가 큐를 비우지 않는지) 점검.

- [ ] **Step 3: UI에 정책을 가시화**

`src/Editor/Windows/ScriptWindow.cpp`:
- Compile 버튼은 Play 중에도 활성(백그라운드 task). 단 Play 중이면 버튼 옆/툴팁에 "Reload will apply after Stop" 안내를 표시한다.
- "Hot Reload" 버튼은 Play 중 비활성화(`ImGui::BeginDisabled(EditorState::Get().IsPlayMode())`)하고, 비활성 사유 툴팁을 단다. Edit 모드에서는 즉시 `ScriptReload().RequestReload(...)`로 다음 safe point reload를 건다.

- [ ] **Step 4: 전체 빌드/테스트 + 수동 검증**
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
수동 검증(에디터): (1) 스크립트 필드값 설정 → Compile 클릭 → **UI가 멈추지 않고** Console에 빌드 로그가 흐름 → 성공 후 다음 프레임에 reload → 씬 오브젝트의 스크립트 필드 값이 유지됨. (2) 일부러 컴파일 오류를 내고 Compile → Console에 오류 표시 + 기존 스크립트 런타임이 계속 동작(last-good). (3) Play 중 Compile → reload는 Stop 후 적용됨.

- [ ] **Step 5: 커밋**
```bash
git add src/Editor/Windows/ScriptWindow.cpp src/Editor/Editor.cpp \
        tests/test_play_mode_compile_policy.cpp tests/CMakeLists.txt
git commit -m "feat(script): explicit play-mode policy — compile allowed, reload queued until Stop"
```

---

## 완료 기준

- [ ] 스크립트 컴파일이 에디터 렌더링을 블로킹하지 않는다 (Compile은 task+워커 스레드; UI는 매 프레임 진행). — 갭 분석 §4
- [ ] 컴파일 상태가 Console과 task UI에 보인다 (stdout/stderr가 `EditorTaskService` 로그로 스트리밍 → Console sink). — §4
- [ ] 실패한 컴파일이 현재 스크립트 런타임을 사용 가능하게 둔다 (검증 실패 시 `Swap` 미호출 = last-good 유지). — §4, `test_script_reload_service` "failed validation keeps last-good"
- [ ] reload가 stale 스크립트 포인터를 **문서화된 안전 지점**에서 무효화한다 (unload 전 인스턴스 파괴 + 프레임 끝·Edit 모드에서만 swap). — §4
- [ ] Play 모드 컴파일 동작이 명시적이다 (컴파일 허용, reload는 Stop까지 큐잉; `test_play_mode_compile_policy`). — §4
- [ ] **Exit 시나리오:** 에디터를 켠 채 게임플레이 스크립트를 수정 → UI 블로킹 없이 컴파일 → 성공적으로 reload → 씬 오브젝트의 필드 값이 그대로 유지됨 (`test_script_field_snapshot` + 수동 검증).
- [ ] reload 반복이 ASan에서 통과한다 (use-after-free 없음). — 로드맵 1-5

---

## 의존성 / 순서

- **선행:** **UX-2**(`03_ux2_console_and_tasks.md`)가 `EditorTaskService`와 Console sink를 먼저 도입해야 한다. UX-4는 그 위에서 `ScriptCompile`/`ScriptReload` 카테고리와 비동기 스크립트 흐름을 *확장*한다. Task A는 UX-2가 정의한 API와 **호환**되도록 작성한다(같은 `EditorTaskService`/`TaskId`/`TaskCategory`/`TaskState` 이름). UX-1(`02_ux1…`)의 dirty/command 계약을 사용한다.
- **권장 순서:** A(task 확장) → B(프로세스 심 + 비동기 컴파일) → C(라이브러리 검증/last-good) → D(필드 스냅샷) → E(safe-point reload) → F(Play 정책). C·D는 E의 부품이므로 E보다 먼저 끝낸다.

### Reload 안전 지점 (정확한 정의)

프레임 루프(`src/main.cpp:220-290`) 안에서, reload(unload + 라이브러리 swap + 인스턴스 재바인딩)는 다음 한 지점에서만 수행한다:

```text
... Editor::Update / RenderGUI ...
ImGuiLayer::EndFrame();
EventBus::ProcessQueue();
Editor::Get().PumpScriptReload(EditorState::Get().IsEditMode());   // ← 안전 지점
glfwSwapBuffers(window);
glfwPollEvents();
```

조건: (1) **Edit 모드일 때만** (`playWorld_`가 null이라 시뮬레이션 중 스크립트 인스턴스가 잡혀있지 않음 — `SceneDocument::IsPlaying()`/`EditorState::IsEditMode()`), (2) **프레임 사이**(스택에 스크립트 `Update`/`FixedUpdate` 없음 — Play 시뮬레이션 구간 `main.cpp:246-257` 밖), (3) GPU/플랫폼 호출과 분리된 `swapBuffers` 직전. Play 중에 들어온 reload 요청은 `ScriptReloadService::PumpPendingReload`가 `Deferred`로 미뤄, Stop으로 Edit 모드가 된 다음 첫 안전 지점에서 적용한다.

### 직렬화된 스크립트 필드가 보존되는 방식 (오늘 → reload)

오늘 필드는 `Script::RegisterFields`로 등록한 멤버를 `Serialize`가 `j["fields"]`에 **필드명 키 JSON**으로 기록하고 `Deserialize`가 같은 키로 되읽어 보존한다(`src/Scripting/Script.cpp:17-94`; 키는 `ScriptField.h`의 `ScriptFieldRegistry`). 라이브러리 핸들이 아니라 이름 기반 JSON이므로, reload 시 `SnapshotFields()`(=`Serialize`)로 unload 전에 떠두고, 같은 클래스명(`GetScriptName()`)·필드명을 가진 새 인스턴스를 `ScriptManager::CreateScript`로 만든 뒤 `RestoreFields()`(=`Deserialize`)로 동일 값을 복원한다.
