# UX-2: Console and Task Status (콘솔 및 작업 상태)

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 구현은 `superpowers:test-driven-development`로, 완료 선언 전에는 `superpowers:verification-before-completion`으로 검증한다. 모든 단계를 `- [ ]` 체크박스로 추적한다. **선행:** UX-1(`02_ux1...`)이 도입하는 *에디터 서비스 패턴*(에디터가 매 프레임 main thread에서 서비스 상태를 pull하고, 패널은 서비스를 직접 소유하지 않는다) 위에 쌓는다. **후속:** UX-4(`05_ux4...`)가 여기서 정의한 `EditorTaskService`를 비동기 컴파일 수명주기로 확장하고, UX-5(`06_ux5...`)가 build/asset 타이밍을 이 Console로 라우팅한다.

**Goal:** 엔진·에디터·사용자 Script의 로그와 오류를 **단일 구조화 로그 파이프라인**으로 통합하고, 검색·필터·source 링크를 갖춘 `ConsoleWindow`에서 탐색할 수 있게 한다. 동시에 build/script 같은 느린 작업을 `EditorTaskService`의 **작업 상태**로 노출하고, 그 출력(stdout/stderr/진행)을 Console로 라우팅한다. background task의 로그가 ImGui 상태를 **직접 변경하지 못하도록** thread-safe sink 경계를 세운다.

**Architecture:**

```text
Core (Runtime/Editor 양쪽이 의존)
  └─ Log: Emit(LogMessage) → 등록된 ILogSink* 들에 fan-out
            ├─ StdoutSink        (즉시 stdout/stderr — 항상 켜짐, headless/CI 동일)
            ├─ RingBufferSink    (lock 하에 고정 용량 링버퍼; 메모리 상한)
            ├─ FileSink          (세션 로그 파일)
            ├─ SmokeReportSink   (smoke 실패를 구조화 메시지로 stdout+report에)
            └─ EditorConsoleSink (thread-safe 큐에 push만; ImGui 미접근)

Editor (main thread)
  └─ ConsoleWindow: 매 프레임 EditorConsoleSink.Drain() 으로 큐를 비워
                    자신의 표시 모델로 옮긴 뒤(여기서만 ImGui 접근) 렌더
  └─ EditorTaskService: TaskId/name/category/state/progress/logStream/result
                        build/script 실행을 Task로 감싸고 그 출력을 Log::Emit로 라우팅
```

**핵심 thread-safe sink 계약 (다운스트림이 반드시 지킬 것):**

1. `Log::Emit(const LogMessage&)`는 **어느 thread에서도** 호출 가능하다.
2. 모든 `ILogSink` 구현은 자기 내부에서 동기화를 책임진다(`std::mutex`). sink는 **ImGui, Editor singleton, Runtime World를 절대 접근하지 않는다.**
3. `EditorConsoleSink`는 들어온 메시지를 mutex로 보호되는 큐에 **복사 push만** 한다. 표시 컨테이너(필터된 row 목록, 문자열 풀)는 `ConsoleWindow`가 소유하며, `ConsoleWindow::OnGUI()`가 main thread에서 `Drain()`을 호출해 큐를 옮긴 **뒤에만** 갱신된다.
4. 따라서 worker thread(빌드/컴파일 프로세스 reader)는 `Log::Emit`만 호출하고, **ImGui 위젯이나 에디터 컨테이너를 절대 직접 만지지 않는다.**

**Tech Stack:** C++17, doctest, ImGui. 새 외부 의존성 없음(표준 라이브러리 `<mutex>`/`<atomic>`/`<thread>`/`<deque>`만 사용; thread는 Task C/F에서만 도입).

**닫는 결함:** 갭 분석 §3(Console/Logs/Diagnostics, lines 146-209) 전부와 §4(lines 211-263)의 **작업-상태 표면 + 라우팅 부분**. *비동기 compile 수명주기, last-good 라이브러리, field 보존, play-mode 정책은 UX-4로 미룬다.* UX-2는 "구조화 로그 + Console + 작업 상태/라우팅" 계층까지만 책임진다.

---

## 현재 상태 (검증된 사실)

- `Log`는 **`namespace Log`** 의 자유 함수 3개뿐이다: `Info/Warn/Error(const std::string& tag, const std::string& msg)`. (`src/Common/Log.h:5-9`)
- 구현은 곧장 stream에 쓴다: `Info`/`Warn`은 `std::cout`, `Error`는 `std::cerr`. **sink 모델·severity enum·category·timestamp·thread·source 위치가 전혀 없다.** (`src/Common/Log.cpp:6-16`)
- `Log.cpp`는 `molga_core`(`ENGINE_SOURCES`)에 포함되어 Core/Runtime/Editor/테스트가 모두 링크한다. (`CMakeLists.txt:93`, `tests/CMakeLists.txt`의 `molga_add_test`가 `molga_core` 링크)
- 시스템 전반이 이미 `Log`를 통해 보고한다(호출처 약 17개 파일). 가장 많은 곳: `Project.cpp`(16), `ProjectBrowserWindow.cpp`(12), `ScriptManager.cpp`(9). 경고/오류 site 예: `SpriteRenderer.cpp:113`(missing texture), `Renderer.cpp:81/85/102/129`(pass 상태 오류), `Framebuffer.cpp:46`, `ScriptManager.cpp:71`(라이브러리 로드 실패), `BuildManager.cpp:60/159/180`(빌드 오류).
- `ScriptCompiler`는 compile 출력을 **단일 문자열** `compileOutput`에 누적하고(`=== CMake Configure ===`/`=== CMake Build ===` 헤더로 구분), `lastError` 한 줄을 둔다. `GetCompileOutput()`/`GetLastError()`로 노출한다. (`src/Scripting/ScriptCompiler.h:31-32`, `ScriptCompiler.cpp:248,261,266`)
- `ScriptCompiler::Compile()`은 **동기**다. `ExecuteCommand()`가 `popen`/`_popen`으로 프로세스를 돌려 stdout/stderr(`2>&1`)를 통째로 읽고 종료 코드를 반환한다. 구조화·파일/줄 파싱 없음. (`ScriptCompiler.cpp:227-272, 441-467`)
- `ScriptWindow`는 `compiler.GetCompileOutput()`을 `ImGui::TextWrapped`로 그대로 덤프하고, 상태 문자열 `compileStatus`만 둔다. (`src/Editor/Windows/ScriptWindow.cpp:240-251, 66-70`)
- compile은 에디터 메뉴에서도 동기로 호출된다: `compiler.Compile()` 후 성공/실패를 `Log::Info`/`Log::Error`로 남길 뿐. (`src/Editor/Editor.cpp:387-396`)
- `GameBuilder`는 이미 `GetProgress()`(0.0–1.0)와 `GetCurrentStep()`을 가지고 있다 — 작업 진행 표면의 자연스러운 source. 단 `Build()`는 동기이고 진행은 Console과 연결돼 있지 않다. (`src/Editor/GameBuilder.h:22-24,36-38`)
- `BuildManager::Build()`는 `GameBuilder`를 호출하고 실패 시 `Log::Error("Editor", ...)`만 남긴다. (`src/Editor/BuildManager.cpp:180`)
- **Console 창이 없다.** `find src -iname '*console*'`는 결과 0개. `EditorWindow`(`src/Editor/Windows/EditorWindow.h:5-25`)를 상속하고 `OnGUI()`를 구현하는 패널 패턴, `WindowManager::Register(name, ...)`/`GetAs<T>()`(`src/Editor/WindowManager.h`), `EditorConstants::WIN_*` 상수(`src/Editor/EditorConstants.h:8-16`)로 등록한다.
- 패널 렌더 진입점은 **main thread**의 `Editor::RenderGUI()` → `windowManager.RenderAll()`이다. (`src/Editor/Editor.cpp:130-145`) 메인 루프는 `src/main.cpp:283`의 `Editor::Get().RenderGUI()`. → **여기가 Console이 매 프레임 sink 큐를 pull하는 지점.**
- **현재 코드에 thread가 전혀 없다.** `grep -rln 'std::thread|std::mutex|std::atomic'` → 0건. background task/동기화 인프라 전무.
- source 파일 열기 수단은 `VSCodeIntegration::OpenFileInVSCode(filePath)` 뿐이며 **줄 번호 인자가 없다**(`code "<path>"`만 실행). (`src/Editor/VSCodeIntegration.cpp:237-244`) ScriptWindow가 이미 사용 중(`ScriptWindow.cpp:137,203`). 줄 이동(`code -g path:line`)은 이번에 추가한다.
- smoke 리포트 구조체 `SmokeReport`{executable,status,scenePath,message,objectCount,frames,assetsResolved} + `Save/Load`가 이미 있다(`src/Core/SmokeReport.h`). smoke 테스트는 `tests/test_*_smoke.cpp` + `tests/SmokeTestSupport.h`(TempDirectory/WriteText). CI/stdout 파리티의 기준.

---

## 파일 구조

**Create:**
- `src/Common/LogMessage.h` — `Severity`/`LogContext` enum + `LogMessage` 구조체(헤더 온리)
- `src/Common/LogSink.h` — `ILogSink` 인터페이스(헤더 온리)
- `src/Common/StdoutSink.h` / `.cpp` — 즉시 stream 출력 sink
- `src/Common/RingBufferSink.h` / `.cpp` — lock-보호 고정 용량 링버퍼 sink
- `src/Common/FileSink.h` / `.cpp` — 세션 로그 파일 sink
- `src/Common/SmokeReportSink.h` / `.cpp` — smoke 실패 구조화 출력 sink
- `src/Editor/EditorConsoleSink.h` / `.cpp` — thread-safe 큐 + `Drain()` (ImGui 미접근)
- `src/Editor/Windows/ConsoleWindow.h` / `.cpp` — 콘솔 패널(필터/가상화/detail)
- `src/Editor/EditorTaskService.h` / `.cpp` — TaskId/state/progress/logStream 모델 + build/script 래핑
- `tests/test_log_message.cpp` — LogMessage + Log::Emit fan-out
- `tests/test_log_sinks.cpp` — Stdout/RingBuffer/File/SmokeReport sink + thread 안전성
- `tests/test_editor_console_sink.cpp` — 큐 push/Drain, ImGui 미접근, thread 안전성
- `tests/test_editor_task_service.cpp` — Task 상태 전이 + 출력 라우팅

**Modify:**
- `src/Common/Log.h` / `.cpp` — `Severity`/`LogContext` 인지, `Log::Emit(LogMessage)` 추가, 기존 `Info/Warn/Error(tag,msg)`는 `Emit`으로 위임(호출처 17파일 무변경), sink 등록 API 추가
- `src/Editor/VSCodeIntegration.h` / `.cpp` — `OpenFileInVSCode(path, line)` 오버로드(`code -g path:line`)
- `src/Editor/Editor.cpp` — `ConsoleWindow` 등록, compile/build 호출을 `EditorTaskService`를 통해 라우팅
- `src/Scripting/ScriptCompiler.cpp` — `lastError`를 구조화 진단(파일:줄)으로도 `Log::Emit`
- `src/Editor/BuildManager.cpp` / `GameBuilder.cpp` — 빌드 단계/진행을 `EditorTaskService` Task로 보고
- `src/Editor/EditorConstants.h` — `WIN_CONSOLE` 상수 추가
- `CMakeLists.txt` — 새 `src/Common/*Sink.cpp`를 `ENGINE_SOURCES`에, `EditorConsoleSink.cpp`/`ConsoleWindow.cpp`/`EditorTaskService.cpp`를 `EDITOR_SOURCES`에
- `tests/CMakeLists.txt` — 새 테스트 4개 등록

---

## 결정 고정 (모든 Task가 동일 명칭 사용)

- 발행 함수: **`void Log::Emit(const LogMessage& m)`** (단수, push 아님). 기존 `Info/Warn/Error`는 보존하되 내부에서 `Emit`을 호출.
- sink 인터페이스 메서드: **`void ILogSink::Write(const LogMessage& m)`**.
- 등록: **`Log::AddSink(std::shared_ptr<ILogSink>)`**, **`Log::RemoveSink(const std::shared_ptr<ILogSink>&)`**, **`Log::ClearSinks()`**(테스트 격리용).
- severity: `enum class Severity { Trace, Info, Warning, Error, Fatal }`.
- context: `enum class LogContext { Editor, Runtime, Build, ScriptCompiler, Importer }`.
- 콘솔 sink drain: **`std::vector<LogMessage> EditorConsoleSink::Drain()`**.
- task 발행: **`TaskId EditorTaskService::Begin(name, category)`**, **`Update(id, progress, line)`**, **`Finish(id, TaskState)`**.

---

## Task A. LogMessage + 구조화 Log core (TDD)

> `Log`는 Core(`molga_core`)에 있어 Runtime도 링크한다. 따라서 LogMessage/sink 인터페이스는 **ImGui/Editor 의존이 0**이어야 한다(계층 경계 §2.1). 이 Task는 헤더 + Core .cpp만 건드린다.

**Files:**
- Create: `src/Common/LogMessage.h`, `src/Common/LogSink.h`, `tests/test_log_message.cpp`
- Modify: `src/Common/Log.h`, `src/Common/Log.cpp`, `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_log_message.cpp`:
```cpp
#include "Common/Log.h"
#include "Common/LogMessage.h"
#include "Common/LogSink.h"
#include "doctest.h"
#include <memory>
#include <vector>

namespace {
// 받은 메시지를 모으는 테스트용 sink.
struct CapturingSink : Log::ILogSink {
    std::vector<Log::LogMessage> received;
    void Write(const Log::LogMessage& m) override { received.push_back(m); }
};
}

TEST_CASE("Emit fans a structured message out to every registered sink") {
    Log::ClearSinks();
    auto sink = std::make_shared<CapturingSink>();
    Log::AddSink(sink);

    Log::LogMessage m;
    m.severity = Log::Severity::Error;
    m.context  = Log::LogContext::ScriptCompiler;
    m.category = "ScriptCompiler";
    m.message  = "expected ';'";
    m.externalPath = "Scripts/Player.cpp";
    m.externalLine = 42;
    Log::Emit(m);

    REQUIRE(sink->received.size() == 1);
    CHECK(sink->received[0].message == "expected ';'");
    CHECK(sink->received[0].externalLine == 42);
    CHECK(sink->received[0].severity == Log::Severity::Error);
    Log::ClearSinks();
}

TEST_CASE("Emit stamps a monotonically increasing sequence number") {
    Log::ClearSinks();
    auto sink = std::make_shared<CapturingSink>();
    Log::AddSink(sink);

    Log::LogMessage a; a.message = "first";
    Log::LogMessage b; b.message = "second";
    Log::Emit(a);
    Log::Emit(b);

    REQUIRE(sink->received.size() == 2);
    CHECK(sink->received[1].sequence > sink->received[0].sequence);
    Log::ClearSinks();
}

TEST_CASE("legacy Info/Warn/Error delegate to Emit with mapped severity") {
    Log::ClearSinks();
    auto sink = std::make_shared<CapturingSink>();
    Log::AddSink(sink);

    Log::Info("TagI", "info-line");
    Log::Warn("TagW", "warn-line");
    Log::Error("TagE", "err-line");

    REQUIRE(sink->received.size() == 3);
    CHECK(sink->received[0].severity == Log::Severity::Info);
    CHECK(sink->received[0].category == "TagI");
    CHECK(sink->received[1].severity == Log::Severity::Warning);
    CHECK(sink->received[2].severity == Log::Severity::Error);
    CHECK(sink->received[2].message == "err-line");
    Log::ClearSinks();
}
```

- [ ] **Step 2: tests/CMakeLists.txt에 등록**

```cmake
molga_add_test(test_log_message      test_log_message.cpp)
```

- [ ] **Step 3: 컴파일/실행 실패 확인**

```bash
cmake --build --preset debug --target test_log_message -j4
```
Expected: FAIL — `LogMessage`/`ILogSink`/`Severity`/`LogContext`/`Emit`/`AddSink`/`ClearSinks` 미정의.

- [ ] **Step 4: LogMessage.h 작성**

Create `src/Common/LogMessage.h`:
```cpp
#pragma once

#include <cstdint>
#include <string>
#include <thread>

namespace Log {

enum class Severity { Trace, Info, Warning, Error, Fatal };
enum class LogContext { Editor, Runtime, Build, ScriptCompiler, Importer };

// 모든 sink가 받는 구조화 로그 레코드. 값 타입(복사 가능) — thread 간 안전 전달용.
struct LogMessage {
    std::uint64_t   sequence   = 0;          // Emit이 채움(단조 증가)
    std::int64_t    timestampMs = 0;         // Emit이 채움(epoch ms)
    std::thread::id threadId{};              // Emit이 채움
    Severity        severity   = Severity::Info;
    LogContext      context    = LogContext::Editor;
    std::string     category;                // 기존 "tag" (예: "Renderer")
    std::string     message;
    std::string     sourceFile;              // 엔진 소스 위치(선택, __FILE__)
    int             sourceLine = 0;
    std::string     externalPath;            // 사용자 스크립트/에셋 경로(컴파일 진단)
    int             externalLine = 0;
    std::string     stack;                   // 선택적 멀티라인 stack/detail
    std::uint32_t   repeatCount = 1;         // 콘솔 collapse가 채움(발행 시 항상 1)
};

} // namespace Log
```

- [ ] **Step 5: LogSink.h 작성**

Create `src/Common/LogSink.h`:
```cpp
#pragma once

#include "Common/LogMessage.h"

namespace Log {

// 하나의 로그 목적지. 구현은 자기 내부 동기화를 책임지며,
// ImGui/Editor/Runtime 어떤 것도 접근하지 않는다. (thread-safe sink 계약)
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogMessage& m) = 0;
};

} // namespace Log
```

- [ ] **Step 6: Log.h 확장**

`src/Common/Log.h` 전체를 다음으로 교체:
```cpp
#pragma once

#include "Common/LogMessage.h"
#include <memory>
#include <string>

namespace Log {

class ILogSink;

// 구조화 발행: 어느 thread에서도 호출 가능. sequence/timestamp/threadId를 채운 뒤
// 등록된 모든 sink로 fan-out 한다.
void Emit(const LogMessage& m);

// sink 레지스트리 (등록은 main thread에서 한다고 가정; fan-out 자체는 thread-safe).
void AddSink(std::shared_ptr<ILogSink> sink);
void RemoveSink(const std::shared_ptr<ILogSink>& sink);
void ClearSinks();

// 기존 호출처(17파일)를 깨지 않는 편의 함수 — 내부적으로 Emit으로 위임한다.
void Info(const std::string& tag, const std::string& msg);
void Warn(const std::string& tag, const std::string& msg);
void Error(const std::string& tag, const std::string& msg);

} // namespace Log
```

- [ ] **Step 7: Log.cpp 구현**

`src/Common/Log.cpp` 전체를 다음으로 교체:
```cpp
#include "Log.h"
#include "LogSink.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace Log {
namespace {
std::mutex                              g_mutex;
std::vector<std::shared_ptr<ILogSink>>  g_sinks;
std::atomic<std::uint64_t>              g_sequence{0};

std::int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
} // namespace

void Emit(const LogMessage& in) {
    LogMessage m = in;
    m.sequence    = ++g_sequence;
    if (m.timestampMs == 0) m.timestampMs = NowMs();
    if (m.threadId == std::thread::id{}) m.threadId = std::this_thread::get_id();

    // sink 목록을 lock 하에 복사한 뒤 lock 밖에서 Write(개별 sink가 자기 동기화 책임).
    std::vector<std::shared_ptr<ILogSink>> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        snapshot = g_sinks;
    }
    for (auto& s : snapshot) {
        if (s) s->Write(m);
    }
}

void AddSink(std::shared_ptr<ILogSink> sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sinks.push_back(std::move(sink));
}

void RemoveSink(const std::shared_ptr<ILogSink>& sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sinks.erase(std::remove(g_sinks.begin(), g_sinks.end(), sink), g_sinks.end());
}

void ClearSinks() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sinks.clear();
}

static void EmitLegacy(Severity sev, const std::string& tag, const std::string& msg) {
    LogMessage m;
    m.severity = sev;
    m.category = tag;
    m.message  = msg;
    m.context  = LogContext::Editor;  // 기존 호출처는 대부분 에디터 측; 세분화는 점진적.
    Emit(m);
}

void Info (const std::string& tag, const std::string& msg) { EmitLegacy(Severity::Info,    tag, msg); }
void Warn (const std::string& tag, const std::string& msg) { EmitLegacy(Severity::Warning, tag, msg); }
void Error(const std::string& tag, const std::string& msg) { EmitLegacy(Severity::Error,   tag, msg); }

} // namespace Log
```
`#include <algorithm>`를 `RemoveSink`의 `std::remove`를 위해 상단에 추가한다.

> 주의: 이 시점에는 등록된 sink가 0개이므로 **로그가 어디에도 출력되지 않는다.** 이는 Task B에서 `StdoutSink`를 부트스트랩에 등록하면 복구된다. 단위 테스트는 `CapturingSink`를 직접 등록하므로 영향 없다. (헤드리스/CI stdout 파리티는 Task C에서 보장.)

- [ ] **Step 8: 테스트 통과 + 커밋**

```bash
cmake --build --preset debug --target test_log_message -j4
ctest --preset debug -R test_log_message --output-on-failure
```
Expected: PASS, `3 | 3 passed`.
```bash
git add src/Common/LogMessage.h src/Common/LogSink.h src/Common/Log.h src/Common/Log.cpp \
        tests/test_log_message.cpp tests/CMakeLists.txt
git commit -m "feat(core): structured LogMessage + Log::Emit sink fan-out (UX-2 Task A)"
```

---

## Task B. Sink 구현 + thread 안전성 (StdoutSink / RingBufferSink) (TDD)

> RingBufferSink가 "100k 메시지 메모리 상한" 완료 기준을 닫는다. thread 안전성은 빌드/컴파일 worker가 `Log::Emit`을 호출하는 시나리오를 위해 지금 검증한다.

**Files:**
- Create: `src/Common/StdoutSink.h`/`.cpp`, `src/Common/RingBufferSink.h`/`.cpp`, `tests/test_log_sinks.cpp`
- Modify: `CMakeLists.txt`(ENGINE_SOURCES), `tests/CMakeLists.txt`, `src/Core/Bootstrap.cpp`(StdoutSink 기본 등록)

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_log_sinks.cpp`:
```cpp
#include "Common/Log.h"
#include "Common/RingBufferSink.h"
#include "doctest.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using Log::RingBufferSink;

TEST_CASE("RingBufferSink retains only the most recent N messages (memory cap)") {
    RingBufferSink ring(/*capacity=*/100);
    for (int i = 0; i < 1000; ++i) {
        Log::LogMessage m; m.sequence = static_cast<std::uint64_t>(i);
        m.message = std::to_string(i);
        ring.Write(m);
    }
    auto snapshot = ring.Snapshot();
    CHECK(snapshot.size() == 100);              // 상한을 넘지 않음
    CHECK(snapshot.front().message == "900");   // 가장 오래된 것이 밀려남
    CHECK(snapshot.back().message == "999");
}

TEST_CASE("RingBufferSink survives concurrent writers without data race") {
    RingBufferSink ring(/*capacity=*/4096);
    constexpr int kThreads = 8;
    constexpr int kPer = 5000;
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&ring, t] {
            for (int i = 0; i < kPer; ++i) {
                Log::LogMessage m; m.message = std::to_string(t) + ":" + std::to_string(i);
                ring.Write(m);   // 동시 Write — TSan/ASan 하에서 깨끗해야 함
            }
        });
    }
    for (auto& w : workers) w.join();
    CHECK(ring.Snapshot().size() == 4096);       // 용량 유지, 크래시 없음
}

TEST_CASE("Emit fans out to a ring sink from multiple threads") {
    Log::ClearSinks();
    auto ring = std::make_shared<RingBufferSink>(8192);
    Log::AddSink(ring);

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([] {
            for (int i = 0; i < 1000; ++i) {
                Log::LogMessage m; m.message = "x";
                Log::Emit(m);
            }
        });
    }
    for (auto& w : workers) w.join();
    CHECK(ring->Snapshot().size() == 4000);
    Log::ClearSinks();
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_log_sinks        test_log_sinks.cpp)
```
```bash
cmake --build --preset debug --target test_log_sinks -j4
```
Expected: FAIL — `RingBufferSink` 헤더 없음.

- [ ] **Step 3: RingBufferSink 작성**

Create `src/Common/RingBufferSink.h`:
```cpp
#pragma once

#include "Common/LogSink.h"
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace Log {

// 고정 용량의 thread-safe 링버퍼. 용량 초과 시 가장 오래된 메시지를 버린다.
// 메모리 상한을 보장한다(완료 기준: 100k 입력에도 상한 유지).
class RingBufferSink : public ILogSink {
public:
    explicit RingBufferSink(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    void Write(const LogMessage& m) override {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(m);
        if (buffer_.size() > capacity_) buffer_.pop_front();
    }

    std::vector<LogMessage> Snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<LogMessage>(buffer_.begin(), buffer_.end());
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
    }

private:
    mutable std::mutex     mutex_;
    std::deque<LogMessage> buffer_;
    std::size_t            capacity_;
};

} // namespace Log
```
(헤더 온리로 충분하면 `.cpp`는 비워 둔다. 빌드 단순화를 위해 `RingBufferSink.cpp`는 만들지 않아도 된다 — 이 경우 ENGINE_SOURCES에 추가하지 않는다.)

- [ ] **Step 4: StdoutSink 작성**

Create `src/Common/StdoutSink.h`:
```cpp
#pragma once

#include "Common/LogSink.h"
#include <mutex>

namespace Log {

// 기존 동작 보존: Info/Warning은 stdout, Error/Fatal은 stderr.
// 한 줄 형식: "[category] [SEV] message (path:line)" — CI/헤드리스에서도 동일.
class StdoutSink : public ILogSink {
public:
    void Write(const LogMessage& m) override;
private:
    std::mutex mutex_;  // 인터리브 방지
};

} // namespace Log
```
Create `src/Common/StdoutSink.cpp`:
```cpp
#include "StdoutSink.h"
#include <iostream>

namespace Log {

static const char* SevTag(Severity s) {
    switch (s) {
        case Severity::Trace:   return "TRACE";
        case Severity::Info:    return "INFO";
        case Severity::Warning: return "WARN";
        case Severity::Error:   return "ERROR";
        case Severity::Fatal:   return "FATAL";
    }
    return "INFO";
}

void StdoutSink::Write(const LogMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostream& os = (m.severity >= Severity::Error) ? std::cerr : std::cout;
    os << "[" << m.category << "] [" << SevTag(m.severity) << "] " << m.message;
    if (!m.externalPath.empty()) {
        os << " (" << m.externalPath;
        if (m.externalLine > 0) os << ":" << m.externalLine;
        os << ")";
    }
    os << std::endl;
}

} // namespace Log
```

- [ ] **Step 5: 부트스트랩에서 StdoutSink 등록**

`src/Core/Bootstrap.cpp`의 `EngineInit()`(`:15`) 내부, 다른 서브시스템 `Init` 직전(`Time::Init()` 위, `:48` 근처)에 추가하고 상단에 include 추가:
```cpp
#include "Common/Log.h"
#include "Common/StdoutSink.h"
// EngineInit() 내부, Time::Init() 앞:
Log::AddSink(std::make_shared<Log::StdoutSink>());
```
`#include <memory>`가 없으면 추가한다. `EngineShutdown()`(`:55`)에서 `Log::ClearSinks()`로 정리한다.
> Task A에서 sink 0개로 인해 끊긴 stdout 출력이 여기서 복구된다. headless 런타임(`molga_runtime`)도 `EngineInit`을 거치므로 동일하게 stdout 로그를 얻는다.

- [ ] **Step 6: CMake 등록**

`CMakeLists.txt`의 `ENGINE_SOURCES`에 추가:
```cmake
    src/Common/StdoutSink.cpp
```
(`RingBufferSink`가 헤더 온리이면 추가 불필요.)

- [ ] **Step 7: 테스트 통과 + ASan/TSan + 커밋**

```bash
cmake --build --preset debug --target test_log_sinks -j4
ctest --preset debug -R test_log_sinks --output-on-failure
cmake --preset asan && cmake --build --preset asan --target test_log_sinks -j4
ctest --preset asan -R test_log_sinks --output-on-failure
```
Expected: 모두 PASS, ASan에서 race/leak 없음. (TSan preset이 있으면 동시 writer 테스트를 그 쪽에서도 실행.)
```bash
git add src/Common/RingBufferSink.h src/Common/StdoutSink.h src/Common/StdoutSink.cpp \
        src/Core/Bootstrap.cpp CMakeLists.txt tests/test_log_sinks.cpp tests/CMakeLists.txt
git commit -m "feat(core): StdoutSink + thread-safe RingBufferSink with memory cap (UX-2 Task B)"
```

---

## Task C. FileSink + SmokeReportSink + CI 파리티 (TDD)

> 완료 기준: "CI/smoke 실패가 stdout과 report 파일 양쪽에 동일한 구조화 메시지로 남는다." `SmokeReportSink`는 Error/Fatal 메시지를 모아 stdout(StdoutSink가 이미 처리)과 함께 report 파일에도 기록한다. `FileSink`는 세션 전체 로그를 파일로 남긴다.

**Files:**
- Create: `src/Common/FileSink.h`/`.cpp`, `src/Common/SmokeReportSink.h`/`.cpp`
- Modify: `tests/test_log_sinks.cpp`(케이스 추가), `CMakeLists.txt`(ENGINE_SOURCES)

- [ ] **Step 1: 실패 테스트 추가** (`tests/test_log_sinks.cpp`에 append)
```cpp
#include "Common/FileSink.h"
#include "Common/SmokeReportSink.h"
#include "SmokeTestSupport.h"

TEST_CASE("FileSink writes one structured line per message to disk") {
    test_support::TempDirectory dir("logsink");
    auto path = dir.Path() / "session.log";
    {
        Log::FileSink file(path.string());
        Log::LogMessage m; m.category = "Build"; m.severity = Log::Severity::Error;
        m.message = "link failed";
        file.Write(m);
    } // flush on destruction
    std::ifstream in(path);
    std::string contents((std::istreambuf_iterator<char>(in)), {});
    CHECK(contents.find("link failed") != std::string::npos);
    CHECK(contents.find("ERROR") != std::string::npos);
}

TEST_CASE("SmokeReportSink collects errors and writes them to the report file") {
    test_support::TempDirectory dir("smokesink");
    auto report = dir.Path() / "smoke_log.txt";
    Log::SmokeReportSink sink(report.string());

    Log::LogMessage ok; ok.severity = Log::Severity::Info;  ok.message = "frame ok";
    Log::LogMessage bad; bad.severity = Log::Severity::Error; bad.category = "Runtime";
    bad.message = "missing asset texture.png";
    sink.Write(ok);
    sink.Write(bad);
    sink.Flush();

    std::ifstream in(report);
    std::string contents((std::istreambuf_iterator<char>(in)), {});
    CHECK(contents.find("missing asset texture.png") != std::string::npos);
    CHECK(contents.find("frame ok") == std::string::npos);  // 실패만 기록
}
```
`#include <fstream>`, `#include <iterator>`를 테스트 상단에 추가.

- [ ] **Step 2: 실패 확인 → FileSink 구현**

Create `src/Common/FileSink.h`:
```cpp
#pragma once

#include "Common/LogSink.h"
#include <fstream>
#include <mutex>
#include <string>

namespace Log {

// 세션 전체 로그를 한 줄/메시지로 파일에 기록한다(append). thread-safe.
class FileSink : public ILogSink {
public:
    explicit FileSink(const std::string& path);
    void Write(const LogMessage& m) override;
    void Flush();
private:
    std::mutex    mutex_;
    std::ofstream out_;
};

} // namespace Log
```
Create `src/Common/FileSink.cpp`:
```cpp
#include "FileSink.h"

namespace Log {

static const char* SevTag(Severity s) {
    switch (s) {
        case Severity::Trace:   return "TRACE";
        case Severity::Info:    return "INFO";
        case Severity::Warning: return "WARN";
        case Severity::Error:   return "ERROR";
        case Severity::Fatal:   return "FATAL";
    }
    return "INFO";
}

FileSink::FileSink(const std::string& path) : out_(path, std::ios::app) {}

void FileSink::Write(const LogMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!out_.is_open()) return;
    out_ << m.sequence << "\t[" << SevTag(m.severity) << "]\t[" << m.category << "]\t"
         << m.message;
    if (!m.externalPath.empty()) {
        out_ << "\t(" << m.externalPath;
        if (m.externalLine > 0) out_ << ":" << m.externalLine;
        out_ << ")";
    }
    out_ << "\n";
}

void FileSink::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    out_.flush();
}

} // namespace Log
```

- [ ] **Step 3: SmokeReportSink 구현**

Create `src/Common/SmokeReportSink.h`:
```cpp
#pragma once

#include "Common/LogSink.h"
#include <mutex>
#include <string>
#include <vector>

namespace Log {

// Error/Fatal 메시지만 모아 smoke report 파일에 기록한다.
// StdoutSink가 동일 메시지를 stdout에 내보내므로 stdout↔report 파리티가 성립한다.
class SmokeReportSink : public ILogSink {
public:
    explicit SmokeReportSink(std::string reportPath) : path_(std::move(reportPath)) {}
    void Write(const LogMessage& m) override;
    void Flush();   // 누적된 실패를 report 파일에 기록
    bool HasFailures() const;
private:
    mutable std::mutex     mutex_;
    std::string            path_;
    std::vector<LogMessage> failures_;
};

} // namespace Log
```
Create `src/Common/SmokeReportSink.cpp`:
```cpp
#include "SmokeReportSink.h"
#include <fstream>

namespace Log {

void SmokeReportSink::Write(const LogMessage& m) {
    if (m.severity < Severity::Error) return;     // 실패만 수집
    std::lock_guard<std::mutex> lock(mutex_);
    failures_.push_back(m);
}

void SmokeReportSink::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(path_, std::ios::app);
    if (!out.is_open()) return;
    for (const auto& m : failures_) {
        out << "[" << m.category << "] " << m.message;
        if (!m.externalPath.empty()) {
            out << " (" << m.externalPath;
            if (m.externalLine > 0) out << ":" << m.externalLine;
            out << ")";
        }
        out << "\n";
    }
}

bool SmokeReportSink::HasFailures() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !failures_.empty();
}

} // namespace Log
```

- [ ] **Step 4: CMake 등록 + 테스트 통과 + 커밋**

`CMakeLists.txt` `ENGINE_SOURCES`에:
```cmake
    src/Common/FileSink.cpp
    src/Common/SmokeReportSink.cpp
```
> `test_log_sinks`가 `SmokeTestSupport.h`를 쓰므로 `tests/CMakeLists.txt`에서 해당 타깃에 `target_include_directories(test_log_sinks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")`를 추가한다(다른 smoke 활용 타깃과 동일 패턴).
```bash
cmake --preset debug && cmake --build --preset debug --target test_log_sinks -j4
ctest --preset debug -R test_log_sinks --output-on-failure
```
Expected: PASS(전체 5 케이스). 이후 smoke 테스트(`tests/test_*_smoke.cpp`)에 `SmokeReportSink`를 등록해 실패 시 report 파일이 채워지도록 한 회귀 단계는 Task F 통합 후 추가한다.
```bash
git add src/Common/FileSink.h src/Common/FileSink.cpp \
        src/Common/SmokeReportSink.h src/Common/SmokeReportSink.cpp \
        CMakeLists.txt tests/test_log_sinks.cpp tests/CMakeLists.txt
git commit -m "feat(core): FileSink + SmokeReportSink for CI/stdout parity (UX-2 Task C)"
```

---

## Task D. EditorConsoleSink + ConsoleWindow (필터/가상화/detail) (TDD)

> 핵심 thread-safe 경계: `EditorConsoleSink`는 큐에 **복사 push만** 한다(ImGui 미접근). `ConsoleWindow`가 main thread에서 `Drain()`해 자기 표시 모델로 옮긴 뒤에만 렌더한다. **이것이 "background task 로그가 ImGui를 직접 못 바꾼다" 완료 기준을 닫는다.**

**Files:**
- Create: `src/Editor/EditorConsoleSink.h`/`.cpp`, `src/Editor/Windows/ConsoleWindow.h`/`.cpp`, `tests/test_editor_console_sink.cpp`
- Modify: `src/Editor/EditorConstants.h`, `src/Editor/Editor.cpp`, `CMakeLists.txt`(EDITOR_SOURCES), `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 sink 테스트 작성** (sink는 ImGui 미의존 → `molga_core` 링크로 단위 테스트 가능하게 `src/Editor/EditorConsoleSink.*`를 ImGui-free로 유지)

Create `tests/test_editor_console_sink.cpp`:
```cpp
#include "Editor/EditorConsoleSink.h"
#include "doctest.h"
#include <thread>
#include <vector>

using molga::EditorConsoleSink;

TEST_CASE("Write enqueues and Drain transfers ownership to the caller") {
    EditorConsoleSink sink;
    Log::LogMessage a; a.message = "a";
    Log::LogMessage b; b.message = "b";
    sink.Write(a);
    sink.Write(b);

    auto drained = sink.Drain();
    CHECK(drained.size() == 2);
    CHECK(sink.Drain().empty());          // 두 번째 Drain은 비어 있어야 함
}

TEST_CASE("concurrent producers, single Drain consumer, no loss") {
    EditorConsoleSink sink;
    constexpr int kThreads = 6;
    constexpr int kPer = 2000;
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&sink] {
            for (int i = 0; i < kPer; ++i) {
                Log::LogMessage m; m.message = "x"; sink.Write(m);
            }
        });
    }
    std::size_t total = 0;
    // producer가 도는 동안 main이 주기적으로 Drain(에디터 프레임을 모사)
    while (total < static_cast<std::size_t>(kThreads * kPer)) {
        total += sink.Drain().size();
    }
    for (auto& p : producers) p.join();
    total += sink.Drain().size();
    CHECK(total == static_cast<std::size_t>(kThreads * kPer));  // 유실 없음
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_editor_console_sink test_editor_console_sink.cpp)
target_sources(test_editor_console_sink PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/EditorConsoleSink.cpp)
target_include_directories(test_editor_console_sink PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
```
```bash
cmake --build --preset debug --target test_editor_console_sink -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: EditorConsoleSink 작성 (ImGui-free)**

Create `src/Editor/EditorConsoleSink.h`:
```cpp
#pragma once

#include "Common/LogSink.h"
#include "Common/LogMessage.h"
#include <mutex>
#include <vector>

namespace molga {

// thread-safe 큐 sink. Write는 어느 thread에서든 메시지를 큐에 복사 push만 한다.
// ImGui/Editor 컨테이너에 절대 접근하지 않는다(thread-safe sink 계약).
// 표시 모델은 ConsoleWindow가 소유하며, main thread에서 Drain()으로 가져간다.
class EditorConsoleSink : public Log::ILogSink {
public:
    void Write(const Log::LogMessage& m) override;
    std::vector<Log::LogMessage> Drain();   // main thread 전용. 큐를 비우고 반환.
private:
    std::mutex                    mutex_;
    std::vector<Log::LogMessage>  pending_;
};

} // namespace molga
```
Create `src/Editor/EditorConsoleSink.cpp`:
```cpp
#include "Editor/EditorConsoleSink.h"

namespace molga {

void EditorConsoleSink::Write(const Log::LogMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(m);
}

std::vector<Log::LogMessage> EditorConsoleSink::Drain() {
    std::vector<Log::LogMessage> out;
    std::lock_guard<std::mutex> lock(mutex_);
    out.swap(pending_);
    return out;
}

} // namespace molga
```

- [ ] **Step 4: sink 테스트 통과 확인 + 커밋(부분)**

```bash
cmake --build --preset debug --target test_editor_console_sink -j4
ctest --preset debug -R test_editor_console_sink --output-on-failure
cmake --build --preset asan --target test_editor_console_sink -j4 && ctest --preset asan -R test_editor_console_sink --output-on-failure
```
Expected: PASS, ASan race 없음.
```bash
git add src/Editor/EditorConsoleSink.h src/Editor/EditorConsoleSink.cpp \
        tests/test_editor_console_sink.cpp tests/CMakeLists.txt
git commit -m "feat(editor): thread-safe EditorConsoleSink with main-thread Drain (UX-2 Task D part 1)"
```

- [ ] **Step 5: ConsoleWindow 작성 (ImGui — 단위 테스트 대상 아님; smoke가 커버)**

Create `src/Editor/Windows/ConsoleWindow.h`:
```cpp
#pragma once

#include "Editor/Windows/EditorWindow.h"
#include "Editor/EditorConsoleSink.h"
#include "Common/LogMessage.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ConsoleWindow : public EditorWindow {
public:
    ConsoleWindow();

    // main thread 매 프레임 호출(RenderAll 경유). sink를 Drain해 표시 모델 갱신 후 렌더.
    void OnGUI() override;

    // 등록된 콘솔 sink(부트스트랩이 Log::AddSink로 등록한 동일 인스턴스).
    std::shared_ptr<molga::EditorConsoleSink> Sink() const { return sink_; }

    // source 링크 더블클릭 시 호출. 기본은 VSCodeIntegration; 주입 가능(테스트/대체).
    void SetOpenFileHandler(std::function<void(const std::string&, int)> fn) {
        openFile_ = std::move(fn);
    }

private:
    struct Row {
        Log::LogMessage msg;
        std::uint32_t   repeat = 1;   // collapse 시 동일 메시지 누적 횟수
    };

    void PullPending();               // sink_->Drain() → rows_ (collapse 적용)
    bool PassesFilter(const Log::LogMessage& m) const;

    std::shared_ptr<molga::EditorConsoleSink> sink_;
    std::vector<Row> rows_;           // 표시 모델(이 패널이 단독 소유; main thread)

    // 툴바 상태
    bool clearOnPlay_      = false;
    bool clearOnBuild_     = false;
    bool clearOnRecompile_ = false;
    bool errorPause_       = false;   // Error 발생 시 Play 일시정지(UX-1 PlayMode 연동 지점)
    bool collapse_         = true;

    // 필터 상태
    char searchBuf_[128] = {0};
    bool showInfo_ = true, showWarn_ = true, showError_ = true;
    int  contextMask_ = -1;           // -1 = 전부; LogContext 비트마스크
    int  selectedRow_ = -1;           // detail pane 대상

    std::function<void(const std::string&, int)> openFile_;
};
```
Create `src/Editor/Windows/ConsoleWindow.cpp` (요지 — 실제 구현):
```cpp
#include "Editor/Windows/ConsoleWindow.h"
#include "Editor/EditorConstants.h"
#include "imgui.h"
#include <memory>

ConsoleWindow::ConsoleWindow()
    : EditorWindow(EditorConstants::WIN_CONSOLE),
      sink_(std::make_shared<molga::EditorConsoleSink>()) {}

void ConsoleWindow::PullPending() {
    for (auto& m : sink_->Drain()) {
        if (collapse_ && !rows_.empty()) {
            Row& last = rows_.back();
            if (last.msg.severity == m.severity &&
                last.msg.category == m.category &&
                last.msg.message  == m.message) {
                last.repeat++;                 // 반복 메시지 카운트만 증가
                continue;
            }
        }
        rows_.push_back(Row{m, 1});
    }
}

bool ConsoleWindow::PassesFilter(const Log::LogMessage& m) const {
    using Log::Severity;
    if (m.severity == Severity::Info    && !showInfo_)  return false;
    if (m.severity == Severity::Warning && !showWarn_)  return false;
    if (m.severity >= Severity::Error   && !showError_) return false;
    if (contextMask_ != -1 &&
        !(contextMask_ & (1 << static_cast<int>(m.context)))) return false;
    if (searchBuf_[0] != '\0' &&
        m.message.find(searchBuf_) == std::string::npos &&
        m.category.find(searchBuf_) == std::string::npos) return false;
    return true;
}

void ConsoleWindow::OnGUI() {
    if (!IsOpen()) return;
    PullPending();                    // ← main thread에서만 표시 모델 변경

    ImGui::Begin(title.c_str(), nullptr);

    // ── 툴바 ──
    if (ImGui::Button("Clear")) { rows_.clear(); selectedRow_ = -1; }
    ImGui::SameLine(); ImGui::Checkbox("Collapse", &collapse_);
    ImGui::SameLine(); ImGui::Checkbox("On Play", &clearOnPlay_);
    ImGui::SameLine(); ImGui::Checkbox("On Build", &clearOnBuild_);
    ImGui::SameLine(); ImGui::Checkbox("On Recompile", &clearOnRecompile_);
    ImGui::SameLine(); ImGui::Checkbox("Error Pause", &errorPause_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##search", "Search", searchBuf_, sizeof(searchBuf_));
    ImGui::SameLine(); ImGui::Checkbox("Info", &showInfo_);
    ImGui::SameLine(); ImGui::Checkbox("Warn", &showWarn_);
    ImGui::SameLine(); ImGui::Checkbox("Error", &showError_);
    ImGui::Separator();

    // ── 가상화 row 목록 ──
    ImGui::BeginChild("##rows", ImVec2(0, -120), true);
    // 필터 통과 인덱스를 먼저 모아 ImGuiListClipper로 가상화.
    static std::vector<int> visible; visible.clear();
    for (int i = 0; i < (int)rows_.size(); ++i)
        if (PassesFilter(rows_[i].msg)) visible.push_back(i);

    ImGuiListClipper clipper;
    clipper.Begin((int)visible.size());
    while (clipper.Step()) {
        for (int vi = clipper.DisplayStart; vi < clipper.DisplayEnd; ++vi) {
            int i = visible[vi];
            const Row& r = rows_[i];
            std::string label = "[" + r.msg.category + "] " + r.msg.message;
            if (r.repeat > 1) label += "  (" + std::to_string(r.repeat) + ")";
            if (ImGui::Selectable(label.c_str(), selectedRow_ == i))
                selectedRow_ = i;
            // 더블클릭으로 source 열기(externalPath:externalLine 우선)
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                const auto& m = r.msg;
                if (openFile_ && !m.externalPath.empty())
                    openFile_(m.externalPath, m.externalLine);
                else if (openFile_ && !m.sourceFile.empty())
                    openFile_(m.sourceFile, m.sourceLine);
            }
        }
    }
    ImGui::EndChild();

    // ── detail pane ──
    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    if (selectedRow_ >= 0 && selectedRow_ < (int)rows_.size()) {
        const auto& m = rows_[selectedRow_].msg;
        ImGui::TextWrapped("%s", m.message.c_str());
        if (!m.stack.empty()) ImGui::TextWrapped("%s", m.stack.c_str());
        if (!m.externalPath.empty()) {
            ImGui::Text("%s:%d", m.externalPath.c_str(), m.externalLine);
            ImGui::SameLine();
            if (ImGui::SmallButton("Open") && openFile_)
                openFile_(m.externalPath, m.externalLine);
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy"))
                ImGui::SetClipboardText(m.message.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
```
> `errorPause_`/clear-on-* 토글의 **실제 동작**(Play 일시정지, Play/Build/Recompile 시작 시 `rows_.clear()`)은 UX-1의 PlayMode 서비스와 Task F의 Task 시작 콜백에 연결한다. 이번 슬라이스에서는 토글 상태 + clear 트리거 hook 지점을 만들고, 외부에서 `clearOnBuild_` 등을 읽어 호출할 수 있는 public `RequestClear()` 한 줄을 추가하면 충분하다.

- [ ] **Step 6: EditorConstants + Editor 등록**

`src/Editor/EditorConstants.h`에 추가:
```cpp
    constexpr const char* WIN_CONSOLE = "Console";
```
`src/Editor/Editor.cpp`의 윈도우 등록부(`:36-42` 근처)에 추가하고, 그 sink를 **Log에 등록**한다:
```cpp
auto console = std::make_unique<ConsoleWindow>();
Log::AddSink(console->Sink());                 // 동일 인스턴스를 sink로 등록
console->SetOpenFileHandler([](const std::string& path, int line) {
    VSCodeIntegration::Get().OpenFileInVSCode(path, line);   // Task E에서 오버로드 추가
});
windowManager.Register(EditorConstants::WIN_CONSOLE, std::move(console));
```
> `Editor::RenderGUI()`(`:130-145`) → `windowManager.RenderAll()`가 매 프레임 `ConsoleWindow::OnGUI()`를 호출 → `PullPending()`이 Drain. **에디터 종료 시** `Log::RemoveSink(console->Sink())`를 `ShutdownAll` 경로에서 호출해 dangling sink를 막는다.

- [ ] **Step 7: CMake(EDITOR_SOURCES) + 빌드 + 커밋**

`CMakeLists.txt` `EDITOR_SOURCES`에:
```cmake
    src/Editor/EditorConsoleSink.cpp
    src/Editor/Windows/ConsoleWindow.cpp
```
```bash
cmake --preset debug && cmake --build --preset debug --target molga_engine -j4
```
Expected: 빌드 성공. 수동 검증: 에디터 실행 → Window 메뉴에 Console → 컴파일/빌드/missing texture 시 메시지 표시, severity 필터·검색 동작.
```bash
git add src/Editor/Windows/ConsoleWindow.h src/Editor/Windows/ConsoleWindow.cpp \
        src/Editor/EditorConstants.h src/Editor/Editor.cpp CMakeLists.txt
git commit -m "feat(editor): ConsoleWindow with filters, virtualized list, detail pane (UX-2 Task D part 2)"
```

---

## Task E. Source 링크 open-file (path:line)

> 완료 기준: "컴파일러 오류가 해당 source 파일과 줄을 연다." 현재 `OpenFileInVSCode(path)`에 줄 인자가 없으므로 오버로드를 추가한다(`code -g path:line`).

**Files:**
- Modify: `src/Editor/VSCodeIntegration.h`/`.cpp`

- [ ] **Step 1: 헤더 오버로드 선언**

`src/Editor/VSCodeIntegration.h`의 기존 `bool OpenFileInVSCode(const std::string& filePath);` 아래에 추가:
```cpp
    // 특정 줄로 이동해서 연다(line<=0이면 파일만 연다). "code -g <path>:<line>".
    bool OpenFileInVSCode(const std::string& filePath, int line);
```

- [ ] **Step 2: 구현**

`src/Editor/VSCodeIntegration.cpp`의 기존 `OpenFileInVSCode`(`:237-244`) 옆에 추가:
```cpp
bool VSCodeIntegration::OpenFileInVSCode(const std::string& filePath, int line) {
    if (line <= 0) return OpenFileInVSCode(filePath);   // 줄 정보 없으면 기존 경로
    std::string cmd = GetVSCodeCommand() + " -g \"" + filePath + ":" + std::to_string(line) + "\"";
    return ExecuteCommand(cmd);
}
```
> `ConsoleWindow`의 `SetOpenFileHandler`가 이미 이 2-인자 버전을 호출한다(Task D Step 6). 줄 번호는 `LogMessage.externalLine`(컴파일 진단) 또는 `sourceLine`에서 온다.

- [ ] **Step 3: 빌드 + 커밋**

```bash
cmake --build --preset debug --target molga_engine -j4
```
수동 검증: 콘솔의 컴파일 오류 더블클릭 → VS Code가 해당 파일의 그 줄에서 열림.
```bash
git add src/Editor/VSCodeIntegration.h src/Editor/VSCodeIntegration.cpp
git commit -m "feat(editor): open source file at line from console diagnostics (UX-2 Task E)"
```

---

## Task F. EditorTaskService + build/script 상태 라우팅 (TDD)

> 작업-상태 표면을 세우고 build/script 출력을 구조화 메시지로 Console에 라우팅한다. **비동기 컴파일 수명주기(thread로 컴파일을 옮기고 last-good 라이브러리 유지)는 UX-4의 책임**이다. 여기서는 (1) Task 모델 + 상태 전이, (2) 진행/출력 라인을 `Log::Emit`으로 보내는 라우팅, (3) 컴파일러 출력에서 `파일:줄` 진단을 파싱해 `externalPath/externalLine`을 채우는 것까지 한다.

**Files:**
- Create: `src/Editor/EditorTaskService.h`/`.cpp`, `tests/test_editor_task_service.cpp`
- Modify: `src/Editor/Editor.cpp`(compile 호출 라우팅), `src/Editor/BuildManager.cpp`/`GameBuilder.cpp`(진행 라우팅), `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_editor_task_service.cpp`:
```cpp
#include "Editor/EditorTaskService.h"
#include "Common/Log.h"
#include "Common/LogSink.h"
#include "doctest.h"
#include <memory>
#include <vector>

using molga::EditorTaskService;
using molga::TaskCategory;
using molga::TaskState;

namespace {
struct CapturingSink : Log::ILogSink {
    std::vector<Log::LogMessage> received;
    void Write(const Log::LogMessage& m) override { received.push_back(m); }
};
}

TEST_CASE("Begin/Update/Finish drives a task through its state machine") {
    EditorTaskService svc;
    auto id = svc.Begin("Compile Scripts", TaskCategory::ScriptCompile);
    CHECK(svc.GetState(id) == TaskState::Running);

    svc.Update(id, 0.5f, "compiling Player.cpp");
    CHECK(svc.GetProgress(id) == doctest::Approx(0.5f));

    svc.Finish(id, TaskState::Succeeded);
    CHECK(svc.GetState(id) == TaskState::Succeeded);
}

TEST_CASE("Update routes a line into the log pipeline with the task's context") {
    Log::ClearSinks();
    auto sink = std::make_shared<CapturingSink>();
    Log::AddSink(sink);

    EditorTaskService svc;
    auto id = svc.Begin("Build Game", TaskCategory::Build);
    svc.Update(id, 0.25f, "copying assets");
    svc.Finish(id, TaskState::Succeeded);

    bool routed = false;
    for (auto& m : sink->received)
        if (m.context == Log::LogContext::Build && m.message.find("copying assets") != std::string::npos)
            routed = true;
    CHECK(routed);
    Log::ClearSinks();
}

TEST_CASE("ParseDiagnostic extracts path and line from a compiler error line") {
    Log::LogMessage m;
    // gcc/clang 형식: "Scripts/Player.cpp:42:10: error: expected ';'"
    bool ok = EditorTaskService::ParseDiagnostic(
        "Scripts/Player.cpp:42:10: error: expected ';'", m);
    CHECK(ok);
    CHECK(m.externalPath == "Scripts/Player.cpp");
    CHECK(m.externalLine == 42);
    CHECK(m.severity == Log::Severity::Error);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`:
```cmake
molga_add_test(test_editor_task_service test_editor_task_service.cpp)
target_sources(test_editor_task_service PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Editor/EditorTaskService.cpp)
target_include_directories(test_editor_task_service PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
```
```bash
cmake --build --preset debug --target test_editor_task_service -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: EditorTaskService 작성 (ImGui-free → 단위 테스트 가능)**

Create `src/Editor/EditorTaskService.h`:
```cpp
#pragma once

#include "Common/Log.h"
#include "Common/LogMessage.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace molga {

using TaskId = std::uint64_t;

enum class TaskCategory { Build, ScriptCompile, ScriptReload, Import, Package, ShaderReload };
enum class TaskState    { Queued, Running, Succeeded, Failed, Cancelled };

// 느린 작업의 상태/진행/로그 표면. 출력 라인은 Log::Emit으로 Console에 라우팅한다.
// (전체 비동기 수명주기·취소·last-good 라이브러리는 UX-4가 확장)
class EditorTaskService {
public:
    TaskId Begin(const std::string& name, TaskCategory category);
    void   Update(TaskId id, float progress, const std::string& line);
    void   Finish(TaskId id, TaskState state);

    TaskState GetState(TaskId id) const;
    float     GetProgress(TaskId id) const;

    struct TaskView {
        TaskId       id;
        std::string  name;
        TaskCategory category;
        TaskState    state;
        float        progress;
    };
    std::vector<TaskView> Snapshot() const;   // 작업 상태 UI가 읽음

    // 컴파일러 출력 한 줄에서 path:line 진단을 추출(gcc/clang/MSVC). 실패 시 false.
    static bool ParseDiagnostic(const std::string& line, Log::LogMessage& out);

private:
    static Log::LogContext ContextFor(TaskCategory c);

    struct Task {
        std::string  name;
        TaskCategory category;
        TaskState    state = TaskState::Queued;
        float        progress = 0.0f;
    };
    mutable std::mutex      mutex_;
    std::map<TaskId, Task>  tasks_;
    TaskId                  nextId_ = 1;
};

} // namespace molga
```
Create `src/Editor/EditorTaskService.cpp`:
```cpp
#include "Editor/EditorTaskService.h"
#include <regex>

namespace molga {

Log::LogContext EditorTaskService::ContextFor(TaskCategory c) {
    switch (c) {
        case TaskCategory::Build:
        case TaskCategory::Package:      return Log::LogContext::Build;
        case TaskCategory::ScriptCompile:
        case TaskCategory::ScriptReload: return Log::LogContext::ScriptCompiler;
        case TaskCategory::Import:
        case TaskCategory::ShaderReload: return Log::LogContext::Importer;
    }
    return Log::LogContext::Editor;
}

TaskId EditorTaskService::Begin(const std::string& name, TaskCategory category) {
    std::lock_guard<std::mutex> lock(mutex_);
    TaskId id = nextId_++;
    tasks_[id] = Task{name, category, TaskState::Running, 0.0f};
    return id;
}

void EditorTaskService::Update(TaskId id, float progress, const std::string& line) {
    TaskCategory cat;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return;
        it->second.progress = progress;
        cat = it->second.category;
    }
    if (line.empty()) return;

    // 진단 줄이면 구조화(path:line, severity), 아니면 일반 진행 메시지.
    Log::LogMessage m;
    if (!ParseDiagnostic(line, m)) {
        m.severity = Log::Severity::Info;
        m.message  = line;
    }
    m.context  = ContextFor(cat);
    m.category = (cat == TaskCategory::Build || cat == TaskCategory::Package)
                     ? "Build" : "ScriptCompiler";
    Log::Emit(m);
}

void EditorTaskService::Finish(TaskId id, TaskState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return;
    it->second.state = state;
    if (state == TaskState::Succeeded) it->second.progress = 1.0f;
}

TaskState EditorTaskService::GetState(TaskId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    return it == tasks_.end() ? TaskState::Cancelled : it->second.state;
}

float EditorTaskService::GetProgress(TaskId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    return it == tasks_.end() ? 0.0f : it->second.progress;
}

std::vector<EditorTaskService::TaskView> EditorTaskService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskView> out;
    for (auto& [id, t] : tasks_)
        out.push_back({id, t.name, t.category, t.state, t.progress});
    return out;
}

bool EditorTaskService::ParseDiagnostic(const std::string& line, Log::LogMessage& out) {
    // gcc/clang: path:line:col: error|warning: msg   /   MSVC: path(line): error Cxxxx: msg
    static const std::regex kGcc(R"(^(.*?):(\d+):(?:\d+:)?\s*(error|warning|note):\s*(.*)$)");
    static const std::regex kMsvc(R"(^(.*?)\((\d+)\):\s*(error|warning)\s+\w+:\s*(.*)$)");
    std::smatch mt;
    const std::regex* used = nullptr;
    if (std::regex_match(line, mt, kGcc))  used = &kGcc;
    else if (std::regex_match(line, mt, kMsvc)) used = &kMsvc;
    if (!used) return false;

    out.externalPath = mt[1].str();
    out.externalLine = std::stoi(mt[2].str());
    const std::string kind = mt[3].str();
    out.severity = (kind == "error") ? Log::Severity::Error
                 : (kind == "warning") ? Log::Severity::Warning
                 : Log::Severity::Info;
    out.message = mt[4].str();
    return true;
}

} // namespace molga
```

- [ ] **Step 4: 테스트 통과 + 커밋(부분)**

```bash
cmake --build --preset debug --target test_editor_task_service -j4
ctest --preset debug -R test_editor_task_service --output-on-failure
```
Expected: PASS, `3 | 3 passed`.
```bash
git add src/Editor/EditorTaskService.h src/Editor/EditorTaskService.cpp \
        tests/test_editor_task_service.cpp tests/CMakeLists.txt
git commit -m "feat(editor): EditorTaskService task model + diagnostic parsing (UX-2 Task F part 1)"
```

- [ ] **Step 5: compile 호출을 Task로 라우팅**

`src/Editor/Editor.cpp`의 "Compile Scripts" 메뉴(`:387-396`)를 Task로 감싼다(여전히 **동기 실행** — 비동기는 UX-4):
```cpp
auto& tasks = GetTaskService();   // Editor 멤버로 추가
TaskId tid = tasks.Begin("Compile Scripts", molga::TaskCategory::ScriptCompile);
bool ok = compiler.Compile();
// 컴파일러가 모은 출력을 줄 단위로 라우팅(진단은 ParseDiagnostic이 구조화).
std::istringstream ss(compiler.GetCompileOutput());
for (std::string line; std::getline(ss, line); )
    tasks.Update(tid, ok ? 1.0f : 0.5f, line);
tasks.Finish(tid, ok ? molga::TaskState::Succeeded : molga::TaskState::Failed);
```
`Editor.h`에 `molga::EditorTaskService taskService;` 멤버와 `EditorTaskService& GetTaskService() { return taskService; }`를 추가하고, `Editor.cpp` 상단에 `#include <sstream>`, `#include "Editor/EditorTaskService.h"` 추가.

- [ ] **Step 6: build 진행을 Task로 라우팅**

`src/Editor/BuildManager.cpp::Build()`에서 `GameBuilder`를 호출하는 부분(`:159-180` 근처)을 Task로 감싸고, `GameBuilder::GetCurrentStep()`/`GetProgress()`를 `tasks.Update`로 보낸다. (현재 동기 빌드이므로 시작/종료와, 가능하면 단계 사이에 Update 한두 번을 발행.)
```cpp
TaskId tid = Editor::Get().GetTaskService().Begin("Build Game", molga::TaskCategory::Build);
bool ok = builder.Build(settings);
Editor::Get().GetTaskService().Update(tid, builder.GetProgress(), builder.GetCurrentStep());
if (!ok)
    Editor::Get().GetTaskService().Update(tid, builder.GetProgress(), builder.GetLastError());
Editor::Get().GetTaskService().Finish(tid, ok ? molga::TaskState::Succeeded
                                              : molga::TaskState::Failed);
```
> 빌드 실패 메시지가 `LogContext::Build`로 Console에 들어가 "build 오류가 Console에 나타난다" 완료 기준을 닫는다.

- [ ] **Step 7: smoke 파리티 회귀 단계**

`tests/test_*_smoke.cpp` 중 실패 케이스가 있는 곳에 `SmokeReportSink`를 `Log::AddSink`로 등록하고, smoke 종료 시 `Flush()` 후 report 파일에 실패 메시지가 있는지 + stdout(StdoutSink)에도 동일 메시지가 나갔는지 확인하는 단언을 추가한다. (CI/stdout 파리티 완료 기준.)

- [ ] **Step 8: 빌드/테스트 전체 + 커밋**

```bash
cmake --preset debug && cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 빌드 성공, 모든 테스트 PASS. 수동 검증: 잘못된 스크립트를 컴파일 → Console에 `error`로 표시 → severity를 Error로 필터 → 더블클릭으로 source 줄 열림 → 수정 후 재컴파일(현재는 동기, UX-4에서 background) → 계속 편집.
```bash
git add src/Editor/Editor.h src/Editor/Editor.cpp src/Editor/BuildManager.cpp \
        src/Editor/GameBuilder.cpp tests/CMakeLists.txt
git commit -m "feat(editor): route script/build task output into Console (UX-2 Task F part 2)"
```

---

## 완료 기준

- [ ] **단일 Console:** build·script·runtime·renderer·asset·package 오류가 하나의 `ConsoleWindow`에 구조화 메시지로 나타난다(`StdoutSink`+`EditorConsoleSink` fan-out, `LogContext`별 필터 가능).
- [ ] **메모리 상한:** 10만 개 메시지 입력에도 `RingBufferSink`/Console 표시 모델이 설정한 용량 cap을 넘지 않는다(`test_log_sinks`의 1000→100 케이스를 100k 규모로 확장한 회귀 포함).
- [ ] **source 링크:** 컴파일러 오류 행 더블클릭이 `externalPath:externalLine`으로 source를 연다(`OpenFileInVSCode(path, line)`).
- [ ] **CI/stdout 파리티:** smoke 실패가 `StdoutSink`(stdout)와 `SmokeReportSink`(report 파일) 양쪽에 동일한 구조화 메시지로 남는다.
- [ ] **thread 안전 경계:** background task(빌드/컴파일 라우팅)의 로그가 ImGui 상태를 직접 변경하지 않는다 — `EditorConsoleSink`는 큐 push만, 표시 모델 변경은 main thread `OnGUI()`의 `Drain()` 이후에만 일어난다(`test_editor_console_sink` 동시성 케이스 + ASan).
- [ ] **작업 상태 표면:** `EditorTaskService`가 build/script 작업의 state/progress를 보유하고 출력 라인을 Console로 라우팅한다(`test_editor_task_service`).
- [ ] **collapse/필터:** 반복 메시지 카운트, severity/category/context/검색 필터가 동작한다.
- [ ] **Exit 시나리오:** 스크립트 컴파일 오류 → Console 표시 → Error 필터 → source 줄 열기 → 수정 → 재컴파일 → 편집 지속.
- [ ] Debug 전체 테스트 + 핵심 sink 테스트의 ASan이 깨끗하다.

---

## 의존성 / 순서

```text
UX-1 (에디터 서비스 패턴: main-thread pull, 패널이 서비스를 직접 소유하지 않음)
  │   └─ Console의 매-프레임 Drain·errorPause/clear-on-play hook이 이 패턴을 따른다
  ▼
UX-2 (이 문서 — 진단 토대)
  Task A LogMessage+Emit  →  Task B Stdout/RingBuffer+thread  →  Task C File/SmokeReport+CI
                                                              →  Task D EditorConsoleSink+ConsoleWindow
                                                              →  Task E source open-file(path:line)
                                                              →  Task F EditorTaskService+build/script 라우팅
  ▼
UX-4 (05_ux4…) — EditorTaskService를 비동기 compile 수명주기로 확장:
        compile을 thread로 이동, 취소, last-good 라이브러리, field 보존, play-mode 정책.
        (UX-2가 만든 TaskState/progress/라우팅과 thread-safe sink 큐가 그 토대.)
UX-5 (06_ux5…) — build/asset 타이밍·renderer stats를 같은 Log/Task 표면으로 라우팅.
```

**Task 내부 순서는 엄격하다:** A(구조화 core) → B(sink+thread) → C(파일/CI) → D(Console) → E(open-file) → F(task 라우팅). D는 B의 `ILogSink`/`Emit`에, F는 A·D에, E는 D의 `SetOpenFileHandler`에 의존한다.

**다운스트림이 반드시 알아야 할 thread-safe sink 계약:**
`Log::Emit`은 어느 thread에서도 호출 가능하고 fan-out 자체가 thread-safe하다. 모든 `ILogSink`는 자기 내부에서 동기화하며 **ImGui/Editor/Runtime을 절대 접근하지 않는다.** UX-4가 컴파일을 worker thread로 옮길 때, 그 thread는 `Log::Emit`(또는 `EditorTaskService::Update`)만 호출하고, `EditorConsoleSink`가 큐에 복사 push하면 `ConsoleWindow::OnGUI()`가 main thread에서 `Drain()`해 표시 모델을 갱신한다. **worker thread가 ImGui 위젯·에디터 컨테이너를 직접 만지면 계약 위반이다.**
```
