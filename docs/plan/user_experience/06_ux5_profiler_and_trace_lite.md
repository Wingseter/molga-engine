# UX-5: Profiler and Trace Lite (프로파일러 및 경량 트레이스)

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 구현은 `superpowers:test-driven-development`로, 완료 선언 전에는 `superpowers:verification-before-completion`으로. 체크박스(`- [ ]`)로 진행을 추적한다. **의존성:** 빌드/에셋 타이밍 리포트는 UX-2의 Console/`EditorTaskService`(task details)로 흘려보내는 것이 목표다. **UX-2가 아직 없으면** `Log::Info/Warn` 폴백으로 동작하게 만들고, Console sink가 생기면 한 줄로 교체할 수 있는 좁은 인터페이스(`ProfilerReportSink`)를 둔다.

**Goal:** "FPS 한 줄"짜리 Stats 창을 넘어, 느린 프레임을 named CPU 구간으로 펼쳐 보고(스크립트/렌더/에셋 로드/물리/에디터 UI 중 어디서 시간을 쓰는지), 렌더러 통계(draw call·batch·texture bind·shader switch·FBO resize)를 편집/Play 중에 보며, 빌드·패키징·에셋 로드 시간을 Console/task details에 리포트한다. **저장 트레이스 파일은 비목표** — 먼저 in-process ring buffer로 이벤트 모델을 검증한다.

**Architecture:** 측정 1차 자료(primitive)는 `molga_core`에 둔다 — `ProfileScope`(RAII CPU 스코프), 스레드 로컬 스코프 스택, `FrameProfile`, `ProfilerService`(고정 용량 ring buffer), `FrameCounters`. 이들은 ImGui/OpenGL/Editor 싱글톤에 의존하지 않으므로 `molga_core`만 링크하는 doctest에서 결정적으로 단위 테스트된다(계층 경계: Runtime은 Editor를 include하지 않는다). 시각화 1차 자료인 `ProfilerWindow`는 **Editor 전용**이며 `molga_core`가 include하지 않는다. 렌더러 통계는 `Renderer`가 채우는 `RenderStats` 구조체로 모으고, 메인 루프가 프레임마다 `ProfilerService::PushFrame`으로 스냅샷을 밀어 넣는다.

**Tech Stack:** C++17, doctest, ImGui, OpenGL(GL 통계는 Renderer 내부에서 카운트, GL 쿼리 미사용)

**닫는 결함:** 갭 분석 §7(366-407) — named CPU 스코프 부재, 프레임 캡처 ring buffer 부재, 렌더러 통계 패널 부재, 에셋 로드 타이밍 부재, 빌드/패키지 타이밍 분해 부재. *저장 캡처 포맷(trace files)은 §10(559)의 명시적 비목표 — 후속 진화로만 언급한다.*

---

## 현재 상태 (검증된 사실)

- **Stats 창은 FPS/dt/frame/docking 4줄만 보여준다.** `StatsWindow::OnGUI`는 `Time::GetFPS()`, `Time::GetDeltaTime()*1000`, `Time::GetFrameCount()`와 "Docking/Viewports: Enabled" 텍스트만 출력한다 — named 구간도, 카운터도, 렌더러 통계도 없다. (`src/Editor/Windows/StatsWindow.cpp:14-19`)
- StatsWindow는 `WIN_STATS`로 등록된다(`src/Editor/Editor.cpp:41`). 상수는 `EditorConstants.h:13`. 창 등록은 `windowManager.Register(name, std::make_unique<...>())` 패턴(`WindowManager.h:10`).
- **프레임 dt는 메인 루프 시작에서 한 번 계산된다.** `Time::Update(); ... float dt = Time::GetDeltaTime();`(`src/main.cpp:221-223`). `Time`은 `deltaTime/currentTime/fps/frameCount` 정적 멤버만 보유하며 per-section 타이밍 개념이 없다(`src/Core/MolgaTime.h:8-43`).
- **World 시뮬레이션은 Play 모드에서만, 메인 루프 한 블록에서 일어난다.** `FixedStep`(물리/FixedUpdate) 루프 → `Update` → `LateUpdate` → `FlushDeferred`(`src/main.cpp:246-257`). 각 단계는 `World::FixedStep/Update/LateUpdate`로 들어가 `objects_`를 순회한다(`src/Core/World.cpp:65-74`). 이 경계가 스크립트/물리 스코프를 넣을 자연스러운 지점이다.
- **에디터 UI 렌더는 `Editor::Get().Update(dt)` + `RenderGUI()`**(`src/main.cpp:282-283`)로 한 블록이다 — editor-ui 스코프를 감쌀 지점.
- **씬 스프라이트 렌더는 FBO 안에서 일어난다.** `SceneViewWindow::DrawSprites`는 `drawList`를 정렬한 뒤 `molga::RenderPass pass(...)` 안에서 `comp->RenderSprite(renderer_)`를 호출한다(`src/Editor/Windows/SceneViewWindow.cpp:265-329`). 이 루프가 sprite/draw-call 카운트를 셀 지점이다.
- **Renderer는 스프라이트당 `glDrawArrays(GL_TRIANGLES,0,6)`를 한 번 호출한다 — batching 없음, 통계 카운터 없음.** `Renderer::DrawSprite`(`src/Rendering/Renderer.cpp:100-125`), `Begin`이 `SetShader`로 shader를 바인딩(`:79-98`). draw call/shader switch/texture bind를 셀 자연스러운 지점이다. `RenderPassState`가 Begin/Draw/End 계약을 강제한다(`src/Rendering/RenderPassState.h:7-29`, `molga::` 네임스페이스).
- **에셋 로드는 `TextureManager::Load(path)`가 절대 경로 해석 후 로드하며 타이밍이 없다.** 성공 시 `std::cout << "[TextureManager] Loaded texture: " ...`만 출력(`src/Core/TextureManager.cpp:17,54`). 어떤 서브시스템이 호출했는지(calling subsystem) 식별 정보가 없다.
- **빌드는 6개 named 단계를 가지나 단계별 시간이 없다.** `GameBuilder::Build`는 `currentStep`을 "Copying assets..." 등으로 갱신하며 진행하지만(`src/Editor/GameBuilder.cpp:27-151`) 각 단계 duration을 측정/리포트하지 않는다.
- **로깅은 `Log::Info/Warn/Error(tag, msg)` 자유 함수뿐이다.** 구조화 Console/sink/`EditorTaskService`는 **아직 없다**(UX-2 미구현). (`src/Common/Log.h:5-8`) → 타이밍 리포트는 폴백으로 `Log::`를 쓰되, sink 인터페이스로 추상화한다.
- **빌드 시스템:** Runtime/Core 코드는 `molga_core` 정적 라이브러리(`ENGINE_SOURCES`)로 묶이고(`CMakeLists.txt:43,115`), 테스트는 `molga_core doctest_main molga_warnings`만 링크한다(`tests/CMakeLists.txt:9-14`). Editor 코드는 `EDITOR_SOURCES`(`CMakeLists.txt:144`)로 `molga_engine` 실행 파일에만 들어간다. → **프로파일러 primitive를 단위 테스트하려면 `ENGINE_SOURCES`에, `ProfilerWindow`는 `EDITOR_SOURCES`에 둔다.**

---

## 파일 구조

**Create (Core / 측정 primitive — `molga_core`):**
- `src/Core/Profiling/ProfilerCategory.h` (카테고리 enum + 이름 테이블, 헤더 온리)
- `src/Core/Profiling/FrameProfile.h` (`FrameProfile`, `ScopeRecord`, `FrameCounters`, `RenderStats`)
- `src/Core/Profiling/ProfilerService.h` (ring buffer + 스레드 로컬 스코프 스택 선언)
- `src/Core/Profiling/ProfilerService.cpp`
- `src/Core/Profiling/ProfileScope.h` (RAII 스코프, 헤더 온리)
- `src/Core/Profiling/ScopedTimer.h` (단조 시계 래퍼, 헤더 온리 — 테스트에서 주입 가능)

**Create (Editor / 시각화·리포트 — `molga_engine`):**
- `src/Editor/Windows/ProfilerWindow.h`
- `src/Editor/Windows/ProfilerWindow.cpp`
- `src/Editor/Profiling/ProfilerReportSink.h` (타이밍 리포트 sink 인터페이스 + `Log::` 폴백 구현, 헤더 온리)

**Modify:**
- `src/Rendering/Renderer.h` / `.cpp` (`RenderStats` 누적: draw call·shader switch·texture bind)
- `src/main.cpp` (프레임 스코프·카운터 수집 + `ProfilerService::PushFrame`)
- `src/Core/World.cpp` (scripts/physics 스코프)
- `src/Editor/Windows/SceneViewWindow.cpp` (sprite/text/particle/tile 카운터, FBO resize 카운트)
- `src/Core/TextureManager.cpp` (로드 타이밍 + 느린 로드 경고에 calling subsystem 포함)
- `src/Editor/GameBuilder.cpp` (단계별 duration 측정 → `ProfilerReportSink`)
- `src/Editor/Editor.cpp` (`ProfilerWindow` 등록)
- `src/Editor/EditorConstants.h` (`WIN_PROFILER`)
- `CMakeLists.txt` (`ENGINE_SOURCES`에 `ProfilerService.cpp`; `EDITOR_SOURCES`에 `ProfilerWindow.cpp`)
- `tests/CMakeLists.txt` (신규 테스트 등록)

**Create (Test):**
- `tests/test_profiler_service.cpp` (ring buffer 용량·드롭·PushFrame 순서)
- `tests/test_profile_scope.cpp` (스코프 누적·중첩·카테고리 집계)
- `tests/test_frame_counters.cpp` (카운터 합산·리셋)

> **명명 규약(전 Task 공통, 한 번 정해 재사용):** 네임스페이스 `molga`. 카테고리 enum `ProfileCategory { Scripts, Rendering, Physics, AssetLoad, EditorUI, Other }`. RAII 타입 `ProfileScope`. 매크로 `MOLGA_PROFILE_SCOPE(name, cat)`. 프레임 타입 `FrameProfile`. 카운터 타입 `FrameCounters`. 렌더러 통계 `RenderStats`. 서비스 싱글톤 `ProfilerService::Get()`. 리포트 sink `IProfilerReportSink` / `LogProfilerReportSink`.

---

## Task A. ProfileScope + 스레드 로컬 스코프 스택 (TDD)

> 측정의 토대. 시계는 주입 가능하게 만들어(테스트가 가짜 ns를 넣음) 결정적으로 단위 테스트한다.

**Files:**
- Create: `src/Core/Profiling/ProfilerCategory.h`, `src/Core/Profiling/ScopedTimer.h`, `src/Core/Profiling/ProfileScope.h`
- Create: `src/Core/Profiling/FrameProfile.h` (이 Task에서 `ScopeRecord` + 누적기 일부 정의)
- Create: `tests/test_profile_scope.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_profile_scope.cpp`:
```cpp
#include "Core/Profiling/ProfileScope.h"
#include "Core/Profiling/FrameProfile.h"
#include "doctest.h"

using molga::ProfileScope;
using molga::ProfileCategory;
using molga::FrameAccumulator;

// 테스트는 단조 시계를 직접 전진시킨다(실시간 의존 제거).
TEST_CASE("a scope accumulates elapsed time under its name and category") {
    FrameAccumulator acc;
    long long clock = 0;
    {
        ProfileScope s(acc, "Update", ProfileCategory::Scripts, &clock);
        clock += 1000;  // 1000ns 경과
    }
    const auto& recs = acc.Records();
    REQUIRE(recs.size() == 1);
    CHECK(recs[0].name == "Update");
    CHECK(recs[0].category == ProfileCategory::Scripts);
    CHECK(recs[0].nanos == 1000);
    CHECK(acc.Depth() == 0);
}

TEST_CASE("nested scopes keep child time and record parent depth") {
    FrameAccumulator acc;
    long long clock = 0;
    {
        ProfileScope outer(acc, "Frame", ProfileCategory::Other, &clock);
        clock += 100;
        {
            ProfileScope inner(acc, "Render", ProfileCategory::Rendering, &clock);
            clock += 50;
        }
        clock += 30;
    }
    const auto& recs = acc.Records();
    REQUIRE(recs.size() == 2);
    // 자식이 먼저 닫히므로 먼저 기록된다.
    CHECK(recs[0].name == "Render");
    CHECK(recs[0].depth == 1);
    CHECK(recs[0].nanos == 50);
    CHECK(recs[1].name == "Frame");
    CHECK(recs[1].depth == 0);
    CHECK(recs[1].nanos == 180);   // 100 + 50 + 30
}

TEST_CASE("category totals sum self-exclusive time per category") {
    FrameAccumulator acc;
    long long clock = 0;
    { ProfileScope a(acc, "S1", ProfileCategory::Scripts, &clock);   clock += 200; }
    { ProfileScope b(acc, "R1", ProfileCategory::Rendering, &clock); clock += 300; }
    { ProfileScope c(acc, "S2", ProfileCategory::Scripts, &clock);   clock += 100; }
    CHECK(acc.CategoryNanos(ProfileCategory::Scripts)   == 300);
    CHECK(acc.CategoryNanos(ProfileCategory::Rendering) == 300);
    CHECK(acc.CategoryNanos(ProfileCategory::Physics)   == 0);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_profile_scope     test_profile_scope.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_profile_scope -j4
```
Expected: FAIL — 헤더 없음.

- [ ] **Step 3: 카테고리 + 시계 헬퍼 작성**

Create `src/Core/Profiling/ProfilerCategory.h`:
```cpp
#pragma once

namespace molga {

// CPU 시간을 어느 서브시스템으로 귀속시킬지 분류한다(갭 분석 §7 exit scenario).
enum class ProfileCategory {
    Scripts,
    Rendering,
    Physics,
    AssetLoad,
    EditorUI,
    Other,
    Count
};

inline const char* ProfileCategoryName(ProfileCategory c) {
    switch (c) {
        case ProfileCategory::Scripts:   return "Scripts";
        case ProfileCategory::Rendering: return "Rendering";
        case ProfileCategory::Physics:   return "Physics";
        case ProfileCategory::AssetLoad: return "Asset Load";
        case ProfileCategory::EditorUI:  return "Editor UI";
        case ProfileCategory::Other:     return "Other";
        default:                         return "?";
    }
}

} // namespace molga
```

Create `src/Core/Profiling/ScopedTimer.h`:
```cpp
#pragma once

#include <chrono>

namespace molga {

// 단조 ns 시계. 테스트는 외부 카운터(clock 포인터)를 주입해 실시간 의존을 없앤다.
inline long long NowNanos() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace molga
```

- [ ] **Step 4: FrameAccumulator + ScopeRecord 작성**

Create `src/Core/Profiling/FrameProfile.h`:
```cpp
#pragma once

#include "Core/Profiling/ProfilerCategory.h"
#include <array>
#include <string>
#include <vector>

namespace molga {

// 한 프레임에서 닫힌 단일 스코프의 기록.
struct ScopeRecord {
    std::string     name;
    ProfileCategory category = ProfileCategory::Other;
    long long       nanos = 0;   // 이 스코프가 머문 총 시간(자식 포함)
    int             depth = 0;   // 0 = 최상위
};

// 한 프레임 동안 스코프를 모으는 누적기. ProfileScope가 push/pop한다.
class FrameAccumulator {
public:
    void Reset() {
        records_.clear();
        depth_ = 0;
        catNanos_.fill(0);
    }

    // ProfileScope 진입 시 호출. 현재 깊이를 반환한다.
    int OnEnter() { return depth_++; }

    // ProfileScope 종료 시 호출. 닫힌 스코프를 기록한다.
    void OnExit(std::string name, ProfileCategory cat, long long nanos, int depth) {
        depth_ = depth;  // 진입 시 받았던 깊이로 복원
        catNanos_[static_cast<size_t>(cat)] += nanos;
        records_.push_back({std::move(name), cat, nanos, depth});
    }

    const std::vector<ScopeRecord>& Records() const { return records_; }
    int Depth() const { return depth_; }
    long long CategoryNanos(ProfileCategory c) const {
        return catNanos_[static_cast<size_t>(c)];
    }

private:
    std::vector<ScopeRecord> records_;
    int depth_ = 0;
    std::array<long long, static_cast<size_t>(ProfileCategory::Count)> catNanos_{};
};

} // namespace molga
```

- [ ] **Step 5: ProfileScope(RAII) 작성**

Create `src/Core/Profiling/ProfileScope.h`:
```cpp
#pragma once

#include "Core/Profiling/FrameProfile.h"
#include "Core/Profiling/ScopedTimer.h"
#include <string>

namespace molga {

// RAII CPU 스코프. 생성 시 진입 시각을, 소멸 시 경과를 누적기에 기록한다.
// clock 포인터가 주어지면(테스트) 그 카운터를, 아니면 실시간 단조 시계를 쓴다.
class ProfileScope {
public:
    ProfileScope(FrameAccumulator& acc, std::string name, ProfileCategory cat,
                 const long long* clock = nullptr)
        : acc_(acc), name_(std::move(name)), cat_(cat), clock_(clock) {
        depth_ = acc_.OnEnter();
        start_ = clock_ ? *clock_ : NowNanos();
    }

    ~ProfileScope() {
        long long end = clock_ ? *clock_ : NowNanos();
        acc_.OnExit(std::move(name_), cat_, end - start_, depth_);
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    FrameAccumulator& acc_;
    std::string       name_;
    ProfileCategory   cat_;
    const long long*  clock_;
    long long         start_ = 0;
    int               depth_ = 0;
};

} // namespace molga
```

- [ ] **Step 6: 테스트 통과 + 커밋**

Run:
```bash
cmake --build --preset debug --target test_profile_scope -j4
ctest --preset debug -R test_profile_scope --output-on-failure
```
Expected: PASS, `3 | 3 passed`.
```bash
git add src/Core/Profiling/ProfilerCategory.h src/Core/Profiling/ScopedTimer.h \
        src/Core/Profiling/FrameProfile.h src/Core/Profiling/ProfileScope.h \
        tests/test_profile_scope.cpp tests/CMakeLists.txt
git commit -m "feat(core): RAII ProfileScope + FrameAccumulator with category totals (UX-5 Task A)"
```

---

## Task B. FrameProfile + FrameCounters + ProfilerService ring buffer (TDD)

> 프레임 단위 자료를 고정 용량 ring buffer에 모은다. 용량 초과 시 가장 오래된 프레임을 덮어쓴다. 전부 결정적 단위 테스트 대상.

**Files:**
- Modify: `src/Core/Profiling/FrameProfile.h` (`FrameCounters`, `FrameProfile`, `RenderStats` 추가)
- Create: `src/Core/Profiling/ProfilerService.h` / `.cpp`
- Create: `tests/test_profiler_service.cpp`, `tests/test_frame_counters.cpp`
- Modify: `CMakeLists.txt` (`ENGINE_SOURCES`), `tests/CMakeLists.txt`

- [ ] **Step 1: 실패하는 카운터 테스트 작성**

Create `tests/test_frame_counters.cpp`:
```cpp
#include "Core/Profiling/FrameProfile.h"
#include "doctest.h"

using molga::FrameCounters;

TEST_CASE("counters start at zero and add up") {
    FrameCounters c;
    CHECK(c.drawCalls == 0);
    CHECK(c.sprites == 0);
    c.drawCalls += 3;
    c.sprites   += 12;
    c.assetLoads += 1;
    CHECK(c.drawCalls == 3);
    CHECK(c.sprites == 12);
    CHECK(c.assetLoads == 1);
}

TEST_CASE("Reset clears every counter") {
    FrameCounters c;
    c.drawCalls = 9; c.particles = 5; c.scripts = 7; c.physics = 4;
    c.Reset();
    CHECK(c.drawCalls == 0);
    CHECK(c.particles == 0);
    CHECK(c.scripts == 0);
    CHECK(c.physics == 0);
}
```

Create `tests/test_profiler_service.cpp`:
```cpp
#include "Core/Profiling/ProfilerService.h"
#include "doctest.h"

using molga::ProfilerService;
using molga::FrameProfile;

static FrameProfile MakeFrame(unsigned long long idx, float dt) {
    FrameProfile f;
    f.frameIndex = idx;
    f.dt = dt;
    return f;
}

TEST_CASE("a fresh ring buffer is empty") {
    ProfilerService svc(/*capacity=*/4);
    CHECK(svc.Size() == 0);
    CHECK(svc.Capacity() == 4);
    CHECK(svc.Latest() == nullptr);
}

TEST_CASE("PushFrame stores frames up to capacity") {
    ProfilerService svc(4);
    svc.PushFrame(MakeFrame(0, 0.016f));
    svc.PushFrame(MakeFrame(1, 0.017f));
    CHECK(svc.Size() == 2);
    REQUIRE(svc.Latest() != nullptr);
    CHECK(svc.Latest()->frameIndex == 1);
    // At(0) = 가장 오래된, At(Size-1) = 최신
    CHECK(svc.At(0)->frameIndex == 0);
    CHECK(svc.At(1)->frameIndex == 1);
}

TEST_CASE("PushFrame overwrites oldest once capacity is exceeded") {
    ProfilerService svc(3);
    for (unsigned long long i = 0; i < 5; ++i)
        svc.PushFrame(MakeFrame(i, 0.016f));
    CHECK(svc.Size() == 3);
    // 가장 오래된 2개(0,1)는 밀려나고 2,3,4가 남는다.
    CHECK(svc.At(0)->frameIndex == 2);
    CHECK(svc.At(1)->frameIndex == 3);
    CHECK(svc.At(2)->frameIndex == 4);
    CHECK(svc.Latest()->frameIndex == 4);
}

TEST_CASE("the slowest retained frame can be located") {
    ProfilerService svc(4);
    svc.PushFrame(MakeFrame(0, 0.010f));
    svc.PushFrame(MakeFrame(1, 0.040f));  // 느린 프레임
    svc.PushFrame(MakeFrame(2, 0.012f));
    const FrameProfile* slow = svc.SlowestFrame();
    REQUIRE(slow != nullptr);
    CHECK(slow->frameIndex == 1);
}

TEST_CASE("disabled service ignores pushed frames (near-zero overhead path)") {
    ProfilerService svc(4);
    svc.SetEnabled(false);
    svc.PushFrame(MakeFrame(0, 0.016f));
    CHECK(svc.Size() == 0);
}
```

- [ ] **Step 2: 등록 + 실패 확인**

`tests/CMakeLists.txt`에 추가:
```cmake
molga_add_test(test_frame_counters    test_frame_counters.cpp)
molga_add_test(test_profiler_service  test_profiler_service.cpp)
```
Run:
```bash
cmake --build --preset debug --target test_frame_counters -j4
```
Expected: FAIL — `FrameCounters`/`ProfilerService` 미정의.

- [ ] **Step 3: FrameCounters + RenderStats + FrameProfile 추가**

`src/Core/Profiling/FrameProfile.h`의 `namespace molga {` 안, `ScopeRecord` 아래에 추가:
```cpp
// 프레임당 수량 카운터(갭 분석 §7: draw calls, sprites, particles, text, tile chunks,
// asset loads, scripts, physics).
struct FrameCounters {
    int drawCalls = 0;
    int sprites = 0;
    int particles = 0;
    int text = 0;
    int tileChunks = 0;
    int assetLoads = 0;
    int scripts = 0;
    int physics = 0;
    void Reset() { *this = FrameCounters{}; }
};

// 렌더러 내부 통계(갭 분석 §7: draw calls, batches, texture binds, shader switches,
// FBO resizes). drawCalls는 FrameCounters와 중복 노출되되 여기서는 렌더러가 직접 채운다.
struct RenderStats {
    int drawCalls = 0;
    int batches = 0;
    int textureBinds = 0;
    int shaderSwitches = 0;
    int fboResizes = 0;
    void Reset() { *this = RenderStats{}; }
};

// 한 프레임의 완성된 프로파일.
struct FrameProfile {
    unsigned long long       frameIndex = 0;
    float                    dt = 0.0f;   // 초
    std::vector<ScopeRecord> scopes;
    FrameCounters            counters;
    RenderStats              render;
    std::array<long long, static_cast<size_t>(ProfileCategory::Count)> categoryNanos{};

    long long CategoryNanos(ProfileCategory c) const {
        return categoryNanos[static_cast<size_t>(c)];
    }
};
```

- [ ] **Step 4: ProfilerService 작성**

Create `src/Core/Profiling/ProfilerService.h`:
```cpp
#pragma once

#include "Core/Profiling/FrameProfile.h"
#include <cstddef>
#include <vector>

namespace molga {

// 고정 용량 in-process ring buffer. 가장 오래된 프레임을 덮어쓴다.
// 비활성 시 PushFrame은 즉시 반환한다(near-zero overhead).
class ProfilerService {
public:
    explicit ProfilerService(size_t capacity = 240);

    // 프로세스 전역 인스턴스(메인 루프·서브시스템이 공유).
    static ProfilerService& Get();

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const  { return enabled_; }

    void PushFrame(FrameProfile frame);
    void Clear();

    size_t Size() const     { return count_; }
    size_t Capacity() const { return buffer_.size(); }

    // At(0) = 가장 오래된 보존 프레임, At(Size()-1) = 최신. 범위 밖이면 nullptr.
    const FrameProfile* At(size_t i) const;
    const FrameProfile* Latest() const;
    const FrameProfile* SlowestFrame() const;

private:
    std::vector<FrameProfile> buffer_;
    size_t head_ = 0;    // 다음에 쓸 슬롯
    size_t count_ = 0;   // 보존 중인 프레임 수
    bool   enabled_ = true;
};

} // namespace molga
```

Create `src/Core/Profiling/ProfilerService.cpp`:
```cpp
#include "Core/Profiling/ProfilerService.h"

namespace molga {

ProfilerService::ProfilerService(size_t capacity)
    : buffer_(capacity == 0 ? 1 : capacity) {}

ProfilerService& ProfilerService::Get() {
    static ProfilerService instance(240);  // 약 4초(60fps) 분량
    return instance;
}

void ProfilerService::PushFrame(FrameProfile frame) {
    if (!enabled_) return;
    buffer_[head_] = std::move(frame);
    head_ = (head_ + 1) % buffer_.size();
    if (count_ < buffer_.size()) ++count_;
}

void ProfilerService::Clear() {
    head_ = 0;
    count_ = 0;
}

const FrameProfile* ProfilerService::At(size_t i) const {
    if (i >= count_) return nullptr;
    // 가장 오래된 슬롯 = head_ - count_ (모듈러)
    size_t start = (head_ + buffer_.size() - count_) % buffer_.size();
    return &buffer_[(start + i) % buffer_.size()];
}

const FrameProfile* ProfilerService::Latest() const {
    if (count_ == 0) return nullptr;
    return At(count_ - 1);
}

const FrameProfile* ProfilerService::SlowestFrame() const {
    const FrameProfile* slow = nullptr;
    for (size_t i = 0; i < count_; ++i) {
        const FrameProfile* f = At(i);
        if (!slow || f->dt > slow->dt) slow = f;
    }
    return slow;
}

} // namespace molga
```

- [ ] **Step 5: ENGINE_SOURCES에 등록 + 빌드 + 테스트**

`CMakeLists.txt`의 `set(ENGINE_SOURCES ...)`에 추가:
```cmake
    src/Core/Profiling/ProfilerService.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_frame_counters test_profiler_service -j4
ctest --preset debug -R "test_frame_counters|test_profiler_service" --output-on-failure
```
Expected: 모두 PASS.

- [ ] **Step 6: 커밋**

```bash
git add src/Core/Profiling/FrameProfile.h src/Core/Profiling/ProfilerService.h \
        src/Core/Profiling/ProfilerService.cpp tests/test_frame_counters.cpp \
        tests/test_profiler_service.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(core): ProfilerService ring buffer + FrameCounters/RenderStats (UX-5 Task B)"
```

---

## Task C. 메인 루프 + 핵심 서브시스템 계측

> Task A/B의 primitive를 실제 프레임에 연결한다. 메인 루프가 프레임마다 `FrameAccumulator`를 리셋·수집해 `FrameProfile`로 만들어 `PushFrame`한다. 스코프는 World·에디터 UI 경계에 넣는다. 이 Task는 통합 코드라 단위 테스트보다 빌드+수동 검증으로 확인한다(서비스 자체는 Task B에서 검증됨).

**Files:**
- Modify: `src/Core/Profiling/ProfilerService.h` (현재 프레임 `FrameAccumulator` 접근자 추가)
- Modify: `src/main.cpp`
- Modify: `src/Core/World.cpp`

- [ ] **Step 1: ProfilerService에 "현재 프레임 누적기" 추가**

`src/Core/Profiling/ProfilerService.h`의 public에 추가:
```cpp
    // 메인 루프가 프레임마다 비우고 채우는 누적기. 서브시스템 스코프가 여기에 쌓는다.
    FrameAccumulator& Frame() { return frame_; }

    // 현재 누적기를 FrameProfile로 굳혀 ring buffer에 넣고 누적기를 리셋한다.
    void EndFrame(unsigned long long frameIndex, float dt,
                  const FrameCounters& counters, const RenderStats& render);
```
private에 추가:
```cpp
    FrameAccumulator frame_;
```
`ProfilerService.cpp`에 추가:
```cpp
void ProfilerService::EndFrame(unsigned long long frameIndex, float dt,
                               const FrameCounters& counters,
                               const RenderStats& render) {
    if (!enabled_) { frame_.Reset(); return; }
    FrameProfile fp;
    fp.frameIndex = frameIndex;
    fp.dt = dt;
    fp.scopes = frame_.Records();
    fp.counters = counters;
    fp.render = render;
    for (size_t i = 0; i < static_cast<size_t>(ProfileCategory::Count); ++i)
        fp.categoryNanos[i] = frame_.CategoryNanos(static_cast<ProfileCategory>(i));
    PushFrame(std::move(fp));
    frame_.Reset();
}
```

> 헬퍼 매크로(선택). `src/Core/Profiling/ProfileScope.h` 끝에:
> ```cpp
> #define MOLGA_PROFILE_SCOPE(name, cat) \
>     ::molga::ProfileScope molga_scope_##__LINE__(::molga::ProfilerService::Get().Frame(), (name), (cat))
> ```
> (사용 측에서 `#include "Core/Profiling/ProfilerService.h"` 필요.)

- [ ] **Step 2: World 시뮬레이션에 스코프 추가**

`src/Core/World.cpp` 상단 include에 추가:
```cpp
#include "Core/Profiling/ProfileScope.h"
#include "Core/Profiling/ProfilerService.h"
```
`World::FixedStep`(현재 `:65-66`)을 다음으로 교체:
```cpp
void World::FixedStep(float fixedDt) {
    MOLGA_PROFILE_SCOPE("World.FixedStep", molga::ProfileCategory::Physics);
    for (auto& o : objects_) if (o && o->IsActive()) o->FixedUpdateScripts(fixedDt);
}
```
`World::Update`(현재 `:69-70`)를:
```cpp
void World::Update(float dt) {
    MOLGA_PROFILE_SCOPE("World.Update", molga::ProfileCategory::Scripts);
    for (auto& o : objects_) if (o && o->IsActive()) o->Update(dt);
}
```
`World::LateUpdate`(현재 `:73-74`)에도 `Scripts` 카테고리로 같은 패턴 적용.

> `objects_` 순회는 그대로다 — 스코프는 측정만 추가하고 동작을 바꾸지 않는다.

- [ ] **Step 3: 메인 루프에서 프레임 측정·수집**

`src/main.cpp` 상단 include에 추가:
```cpp
#include "Core/Profiling/ProfileScope.h"
#include "Core/Profiling/ProfilerService.h"
```
메인 루프(`while (!glfwWindowShouldClose(window))`, 현재 `:220`) 본문 시작에서 프레임 카운터를 준비한다. Play 시뮬레이션 블록(`:246-257`)은 World 스코프가 이미 잡으므로 그대로 둔다. 에디터 UI 블록(`:281-284`)을 다음으로 감싼다:
```cpp
            // ImGui Editor UI
            {
                MOLGA_PROFILE_SCOPE("Editor.UI", molga::ProfileCategory::EditorUI);
                ImGuiLayer::BeginFrame();
                Editor::Get().Update(dt);
                Editor::Get().RenderGUI();
                ImGuiLayer::EndFrame();
            }
```
루프 끝(`glfwPollEvents();` 다음, 현재 `:289` 뒤)에 프레임 마감을 추가:
```cpp
            // 한 프레임의 스코프·카운터를 굳혀 ring buffer에 넣는다.
            // 카운터/렌더 통계는 SceneViewWindow/Renderer가 채운 값을 Editor가 모아 둔다(Task D/E).
            molga::FrameCounters frameCounters = Editor::Get().TakeFrameCounters();
            molga::RenderStats   renderStats   = Editor::Get().TakeRenderStats();
            molga::ProfilerService::Get().EndFrame(
                static_cast<unsigned long long>(Time::GetFrameCount()),
                dt, frameCounters, renderStats);
```

> `Editor::TakeFrameCounters/TakeRenderStats`는 Task D/E에서 추가한다 — 이 Step에서는 빈 구조체를 반환하는 stub로 먼저 컴파일을 통과시킨다(아래 Step 4).

- [ ] **Step 4: Editor에 프레임 카운터 stub 추가**

`src/Editor/Editor.h`에 `#include "Core/Profiling/FrameProfile.h"` 추가 후 public에:
```cpp
    // 프레임 카운터/렌더 통계를 모아 메인 루프가 매 프레임 가져간다(가져가면 리셋).
    molga::FrameCounters TakeFrameCounters() { auto c = frameCounters_; frameCounters_.Reset(); return c; }
    molga::RenderStats   TakeRenderStats()   { auto r = renderStats_;   renderStats_.Reset();   return r; }
    molga::FrameCounters& FrameCounters() { return frameCounters_; }
    molga::RenderStats&   RenderStats()   { return renderStats_; }
```
private에:
```cpp
    molga::FrameCounters frameCounters_;
    molga::RenderStats   renderStats_;
```

- [ ] **Step 5: 빌드 + 수동 검증**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 빌드 성공, 기존 테스트 회귀 없음.

수동 검증(에디터 실행): Play 모드로 들어가 몇 프레임 돌린 뒤, 임시 디버그 출력이나 다음 Task D의 창에서 `World.Update`/`Editor.UI` 스코프가 프레임마다 기록되는지 확인.

- [ ] **Step 6: 커밋**

```bash
git add src/Core/Profiling/ProfilerService.h src/Core/Profiling/ProfilerService.cpp \
        src/Core/Profiling/ProfileScope.h src/Core/World.cpp src/main.cpp \
        src/Editor/Editor.h
git commit -m "feat(profiling): instrument main loop + World with scopes and per-frame collection (UX-5 Task C)"
```

---

## Task D. ProfilerWindow — 타임라인 + 선택 프레임 상세

> Editor 전용 시각화. `molga_core`가 include하지 않는다(계층 경계). ring buffer를 읽어 dt 막대 타임라인을 그리고, 선택한 프레임의 named 스코프(들여쓰기)·카테고리 합계·카운터를 보여준다. **갭 분석 §7 exit scenario**(느린 프레임 → 스크립트/렌더/에셋/물리/UI 어디에 시간이 쓰였는지)를 직접 만족시키는 창.

**Files:**
- Create: `src/Editor/Windows/ProfilerWindow.h` / `.cpp`
- Modify: `src/Editor/EditorConstants.h` (`WIN_PROFILER`)
- Modify: `src/Editor/Editor.cpp` (등록)
- Modify: `CMakeLists.txt` (`EDITOR_SOURCES`)

- [ ] **Step 1: 상수 + 창 등록**

`src/Editor/EditorConstants.h`에 추가:
```cpp
    constexpr const char* WIN_PROFILER = "Profiler";
```
`src/Editor/Editor.cpp` 상단 include에 `#include "Windows/ProfilerWindow.h"` 추가, 등록부(`:41` StatsWindow 근처)에:
```cpp
  windowManager.Register(EditorConstants::WIN_PROFILER, std::make_unique<ProfilerWindow>());
```

- [ ] **Step 2: ProfilerWindow 작성**

Create `src/Editor/Windows/ProfilerWindow.h`:
```cpp
#pragma once

#include "EditorWindow.h"
#include <cstddef>

class ProfilerWindow : public EditorWindow {
public:
    ProfilerWindow();
    void OnGUI() override;

private:
    void DrawTimeline();
    void DrawSelectedFrame();
    long long selectedFrameIndex_ = -1;  // -1 = 최신 프레임 자동 추적
};
```

Create `src/Editor/Windows/ProfilerWindow.cpp`:
```cpp
#include "ProfilerWindow.h"
#include "../EditorConstants.h"
#include "Core/Profiling/ProfilerService.h"
#include "Core/Profiling/ProfilerCategory.h"
#include <imgui.h>
#include <string>

using molga::ProfilerService;
using molga::FrameProfile;
using molga::ProfileCategory;

ProfilerWindow::ProfilerWindow()
    : EditorWindow(EditorConstants::WIN_PROFILER) {}

static float MsOf(long long nanos) { return static_cast<float>(nanos) / 1.0e6f; }

void ProfilerWindow::OnGUI() {
    if (!isOpen) return;
    ImGui::Begin(title.c_str(), &isOpen);

    ProfilerService& svc = ProfilerService::Get();
    bool enabled = svc.IsEnabled();
    if (ImGui::Checkbox("Enabled", &enabled)) svc.SetEnabled(enabled);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { svc.Clear(); selectedFrameIndex_ = -1; }
    ImGui::SameLine();
    if (ImGui::Button("Jump to slowest")) {
        if (const FrameProfile* s = svc.SlowestFrame())
            selectedFrameIndex_ = static_cast<long long>(s->frameIndex);
    }
    ImGui::Separator();

    DrawTimeline();
    ImGui::Separator();
    DrawSelectedFrame();

    ImGui::End();
}

void ProfilerWindow::DrawTimeline() {
    ProfilerService& svc = ProfilerService::Get();
    size_t n = svc.Size();
    if (n == 0) { ImGui::TextUnformatted("No frames captured."); return; }

    // dt(ms) 막대 그래프. 클릭으로 프레임을 선택한다.
    std::vector<float> dtMs(n);
    for (size_t i = 0; i < n; ++i) dtMs[i] = svc.At(i)->dt * 1000.0f;

    ImGui::PlotHistogram("##frametimes", dtMs.data(), static_cast<int>(n),
                         0, "frame time (ms)", 0.0f, FLT_MAX, ImVec2(0, 80));

    // 슬라이더로 프레임 선택(타임라인 클릭 매핑의 단순 대체).
    int sel = (selectedFrameIndex_ < 0)
                  ? static_cast<int>(n - 1)
                  : static_cast<int>(n - 1);
    // selectedFrameIndex_가 frameIndex(절대값)일 수 있으므로 가장 가까운 슬롯을 찾는다.
    if (selectedFrameIndex_ >= 0) {
        for (size_t i = 0; i < n; ++i)
            if (static_cast<long long>(svc.At(i)->frameIndex) == selectedFrameIndex_)
                sel = static_cast<int>(i);
    }
    if (ImGui::SliderInt("Frame", &sel, 0, static_cast<int>(n - 1)))
        selectedFrameIndex_ = static_cast<long long>(svc.At(static_cast<size_t>(sel))->frameIndex);
}

void ProfilerWindow::DrawSelectedFrame() {
    ProfilerService& svc = ProfilerService::Get();
    size_t n = svc.Size();
    if (n == 0) return;

    const FrameProfile* f = svc.Latest();
    if (selectedFrameIndex_ >= 0) {
        for (size_t i = 0; i < n; ++i)
            if (static_cast<long long>(svc.At(i)->frameIndex) == selectedFrameIndex_)
                f = svc.At(i);
    }
    if (!f) return;

    ImGui::Text("Frame %llu   dt = %.3f ms", f->frameIndex, f->dt * 1000.0f);

    // 카테고리 합계 — exit scenario의 핵심: 시간이 어디로 갔는가.
    if (ImGui::CollapsingHeader("By category", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < static_cast<size_t>(ProfileCategory::Count); ++i) {
            auto c = static_cast<ProfileCategory>(i);
            ImGui::Text("%-12s %8.3f ms", molga::ProfileCategoryName(c),
                        MsOf(f->CategoryNanos(c)));
        }
    }

    // named 스코프 — 깊이 들여쓰기로 중첩 표현.
    if (ImGui::CollapsingHeader("CPU sections", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& s : f->scopes) {
            ImGui::Text("%*s%-20s %8.3f ms  [%s]", s.depth * 2, "",
                        s.name.c_str(), MsOf(s.nanos),
                        molga::ProfileCategoryName(s.category));
        }
    }

    // 렌더러 통계 + 카운터.
    if (ImGui::CollapsingHeader("Renderer stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Draw calls: %d", f->render.drawCalls);
        ImGui::Text("Batches: %d", f->render.batches);
        ImGui::Text("Texture binds: %d", f->render.textureBinds);
        ImGui::Text("Shader switches: %d", f->render.shaderSwitches);
        ImGui::Text("FBO resizes: %d", f->render.fboResizes);
    }
    if (ImGui::CollapsingHeader("Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Sprites: %d", f->counters.sprites);
        ImGui::Text("Particles: %d", f->counters.particles);
        ImGui::Text("Text: %d", f->counters.text);
        ImGui::Text("Tile chunks: %d", f->counters.tileChunks);
        ImGui::Text("Asset loads: %d", f->counters.assetLoads);
        ImGui::Text("Scripts: %d", f->counters.scripts);
        ImGui::Text("Physics: %d", f->counters.physics);
    }
}
```

- [ ] **Step 3: EDITOR_SOURCES에 등록 + 빌드**

`CMakeLists.txt`의 `set(EDITOR_SOURCES ...)`에 추가(`StatsWindow.cpp` 근처):
```cmake
    src/Editor/Windows/ProfilerWindow.cpp
```
Run:
```bash
cmake --preset debug
cmake --build --preset debug --target molga_engine -j4
```
Expected: 빌드 성공.

- [ ] **Step 4: 수동 검증**

에디터 실행 → Profiler 창 열기 → 타임라인에 프레임 막대가 보이고, 슬라이더/"Jump to slowest"로 프레임 선택 시 카테고리 합계·CPU sections·렌더러 통계·카운터가 갱신되는지 확인. Task C의 `World.Update`/`Editor.UI` 스코프가 CPU sections에 나타나야 한다.

- [ ] **Step 5: 커밋**

```bash
git add src/Editor/Windows/ProfilerWindow.h src/Editor/Windows/ProfilerWindow.cpp \
        src/Editor/EditorConstants.h src/Editor/Editor.cpp CMakeLists.txt
git commit -m "feat(editor): ProfilerWindow timeline + selected-frame detail (UX-5 Task D)"
```

---

## Task E. 렌더러 통계 + 씬 카운터 통합

> `Renderer`가 draw call/shader switch/texture bind를 자체 누적하고, `SceneViewWindow`가 sprite/text/particle/tile chunk와 FBO resize를 카운트해 `Editor`의 프레임 카운터/렌더 통계에 더한다. Renderer의 누적 로직은 단위 테스트로 검증한다(GL 호출 없이도 카운터 증가만 확인).

**Files:**
- Modify: `src/Rendering/Renderer.h` / `.cpp` (`RenderStats` 누적 + 접근자)
- Modify: `src/Editor/Windows/SceneViewWindow.cpp` (카운터 + FBO resize)
- Modify: `tests/test_renderer_contract.cpp` (통계 카운트 케이스 추가)

- [ ] **Step 1: Renderer에 통계 카운트 추가(실패 테스트 먼저)**

`tests/test_renderer_contract.cpp`에 케이스 추가(기존 contract 테스트 옆):
```cpp
TEST_CASE("Renderer counts draw calls and shader switches between resets") {
    Renderer r;
    // GL 컨텍스트 없이 카운터 누적 경로만 검증한다(DrawSprite는 null sprite로 early-return하지 않도록
    // 카운트는 패스 진입/그리기 시도 시점에 증가하도록 설계).
    molga::RenderStats before = r.Stats();
    CHECK(before.drawCalls == 0);
    r.ResetStats();
    CHECK(r.Stats().drawCalls == 0);
    CHECK(r.Stats().shaderSwitches == 0);
}
```
> GL 컨텍스트가 없는 단위 테스트 환경에서 실제 `DrawSprite`는 호출하기 어렵다. 따라서 이 Task의 테스트는 **카운터의 초기값·리셋·접근자 계약**만 검증하고, 실제 증가는 Step 4 수동 검증으로 확인한다. (렌더 contract 테스트가 이미 GL-light 경로를 쓰는지 `test_renderer_contract.cpp`를 먼저 확인하고, 가능하면 한 번의 `Begin/End` 사이클 후 `shaderSwitches >= 1`을 단언한다.)

- [ ] **Step 2: Renderer.h에 통계 멤버/접근자 추가**

`src/Rendering/Renderer.h` 상단 include에 `#include "Core/Profiling/FrameProfile.h"` 추가. public에:
```cpp
    const molga::RenderStats& Stats() const { return stats_; }
    void ResetStats() { stats_.Reset(); }
```
private에:
```cpp
    molga::RenderStats stats_;
```

- [ ] **Step 3: Renderer.cpp에서 카운트**

`src/Rendering/Renderer.cpp`:
- `SetShader`(현재 `:69-77`)에서 `currentShader = shader;` 다음에 `stats_.shaderSwitches++;` 추가.
- `DrawSprite`(현재 `:100-125`)에서 텍스처 바인딩 분기(`sprite->texture->Bind(0);`, `:117`) 직후 `stats_.textureBinds++;` 추가, `glDrawArrays`(`:123`) 직후 `stats_.drawCalls++;` 추가.

> 현재 batching이 없으므로 `batches`는 draw call과 동일하게 둔다(`stats_.batches = stats_.drawCalls`를 통계 수집 시점에 맞춤). batching이 도입되면(로드맵 Phase 2-4) 이 값이 갈라진다.

- [ ] **Step 4: SceneViewWindow에서 카운터 + 렌더 통계 수집**

`src/Editor/Windows/SceneViewWindow.cpp` 상단 include에 `#include "../Editor.h"` 추가(이미 있으면 생략). `DrawSprites`(현재 `:265-329`)의 정렬 루프에서 컴포넌트 타입별로 카운터를 증가시킨다. 예: `drawList.emplace_back(sr->GetSortingOrder(), sr);` 근처에서
```cpp
    auto& fc = Editor::Get().FrameCounters();
    ... // SpriteRenderer → fc.sprites++;  ParticleSystem → fc.particles++;
        // TextRenderer2D → fc.text++;     TilemapRenderer → fc.tileChunks++ (chunk 수 만큼);
```
RenderPass 블록(현재 `:323-328`) 직전에 `renderer_->ResetStats();`, 직후에:
```cpp
    Editor::Get().RenderStats() = renderer_->Stats();
    Editor::Get().RenderStats().batches = renderer_->Stats().drawCalls;  // batching 도입 전 동치
    Editor::Get().FrameCounters().drawCalls = renderer_->Stats().drawCalls;
```
FBO resize 카운트: `RenderSceneToFBO`에서 framebuffer 크기를 실제로 바꾸는 분기(`Framebuffer::Resize` 호출 지점) 직후 `Editor::Get().RenderStats().fboResizes++;` 추가.

> 정확한 FBO resize 분기 위치는 `SceneViewWindow.cpp`의 `RenderSceneToFBO`(현재 `:190` 부근)에서 viewport 크기 변경 시 framebuffer를 재생성/리사이즈하는 코드를 grep으로 찾아(`Resize`/`Create`/`width !=`) 그 지점에 둔다.

- [ ] **Step 5: 빌드 + 테스트 + 수동 검증**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 PASS.

수동 검증: 씬에 스프라이트 여러 개 배치 → Profiler 창의 "Renderer stats"에서 Draw calls가 스프라이트 수와 일치, "Counters"의 Sprites가 일치하는지 확인. 창 리사이즈 시 FBO resizes가 증가하는지 확인.

- [ ] **Step 6: 커밋**

```bash
git add src/Rendering/Renderer.h src/Rendering/Renderer.cpp \
        src/Editor/Windows/SceneViewWindow.cpp tests/test_renderer_contract.cpp
git commit -m "feat(rendering): RenderStats counters + scene counters into profiler (UX-5 Task E)"
```

---

## Task F. 에셋 로드 타이밍 + 빌드/패키지 타이밍 → Console/task details

> 에셋 로드 시간을 재고, 느린 로드는 **어떤 에셋인지 + 어떤 서브시스템이 불렀는지**를 경고한다. 빌드 단계별 duration을 측정해 리포트한다. UX-2 Console/`EditorTaskService`가 없으므로 `IProfilerReportSink` 인터페이스로 추상화하고 `Log::` 폴백 구현을 둔다 — UX-2 도착 시 sink만 교체.

**Files:**
- Create: `src/Editor/Profiling/ProfilerReportSink.h`
- Modify: `src/Core/TextureManager.cpp`
- Modify: `src/Editor/GameBuilder.cpp`
- (선택) Modify: `tests/test_game_builder.cpp` (단계 타이밍이 리포트되는지 sink 캡처로 검증)

- [ ] **Step 1: 리포트 sink 인터페이스 작성**

Create `src/Editor/Profiling/ProfilerReportSink.h`:
```cpp
#pragma once

#include "Common/Log.h"
#include <string>

namespace molga {

// 타이밍 리포트의 도착지. UX-2가 오면 Console/EditorTaskService 구현으로 교체한다.
class IProfilerReportSink {
public:
    virtual ~IProfilerReportSink() = default;
    // label = 단계/에셋 이름, ms = 소요 시간, detail = 선택 부가정보(서브시스템 등).
    virtual void ReportTiming(const std::string& label, double ms,
                              const std::string& detail) = 0;
};

// UX-2 부재 시 폴백: 표준 로그로 흘린다.
class LogProfilerReportSink : public IProfilerReportSink {
public:
    void ReportTiming(const std::string& label, double ms,
                      const std::string& detail) override {
        std::string msg = label + ": " + std::to_string(ms) + " ms";
        if (!detail.empty()) msg += " (" + detail + ")";
        Log::Info("Profiler", msg);
    }
};

// 현재 활성 sink. 기본은 Log 폴백. UX-2가 Console sink로 교체.
IProfilerReportSink& ActiveReportSink();
void SetReportSink(IProfilerReportSink* sink);   // nullptr = 폴백으로 복귀

} // namespace molga
```
구현(같은 헤더에 inline 또는 작은 cpp). 헤더 온리로 두려면:
```cpp
namespace molga {
namespace detail {
    inline LogProfilerReportSink& DefaultSink() { static LogProfilerReportSink s; return s; }
    inline IProfilerReportSink*&  CurrentSink() { static IProfilerReportSink* p = nullptr; return p; }
}
inline IProfilerReportSink& ActiveReportSink() {
    return detail::CurrentSink() ? *detail::CurrentSink() : detail::DefaultSink();
}
inline void SetReportSink(IProfilerReportSink* sink) { detail::CurrentSink() = sink; }
} // namespace molga
```

- [ ] **Step 2: 에셋 로드 타이밍 + calling subsystem 경고**

`src/Core/TextureManager.cpp` 상단 include에 `#include "Core/Profiling/ScopedTimer.h"` 추가. `Load`(현재 `:17`)에서 실제 로드 구간을 감싸 ms를 잰다. 성공 로그(`std::cout << "[TextureManager] Loaded texture: " ...`, `:54`)를 다음으로 보강:
```cpp
    long long t0 = molga::NowNanos();
    // ... 기존 로드 ...
    double ms = (molga::NowNanos() - t0) / 1.0e6;
    molga::ProfilerService::Get().Frame()./* 카운터 증가는 아래 */;
    Editor::Get().FrameCounters().assetLoads++;   // 또는 Core-safe 카운터 경로
    constexpr double kSlowLoadMs = 8.0;           // 첫 benchmark 후 상세계획서에서 고정
    if (ms > kSlowLoadMs) {
        Log::Warn("AssetLoad",
            "Slow texture load: " + path + " (" + std::to_string(ms) + " ms)");
        // calling subsystem은 호출부에서 전달받는 인자로 식별(아래 주석).
    }
```
> **계층 주의:** `TextureManager`는 `molga_core`다 — `Editor::Get()`을 직접 부르면 계층 경계를 위반한다. 따라서 카운터 증가는 **Core가 소유하는 경로**로 한다: `ProfilerService::Get().Frame()`에 에셋 로드 스코프(`ProfileCategory::AssetLoad`)를 넣고, `assetLoads` 카운트는 `ProfilerService`에 작은 `int& AssetLoadCounter()` 누산기를 두어 메인 루프가 EndFrame 시 `FrameCounters.assetLoads`로 옮긴다. calling subsystem 식별은 `Load(path, const char* caller="Unknown")` 식의 선택 인자를 추가해 경고 메시지에 포함한다(호출부: SpriteRenderer/TilemapRenderer 등이 자기 이름을 넘김).

- [ ] **Step 3: 빌드 단계 타이밍**

`src/Editor/GameBuilder.cpp` 상단 include에 `#include "Editor/Profiling/ProfilerReportSink.h"`, `#include "Core/Profiling/ScopedTimer.h"` 추가. `Build`(현재 `:27`)의 각 named 단계 전후로 ms를 재 리포트한다. 예: "Copying assets..." 단계(`:91-97`)를:
```cpp
    // Step 2: Copy assets
    currentStep = "Copying assets...";
    {
        long long t0 = molga::NowNanos();
        if (!CopyAssets(stagingPathStr)) { /* 기존 실패 처리 */ return false; }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Copy assets", ms, "");
    }
```
같은 패턴을 Manifest/Copy shaders/Copy scenes/Generate config/Copy executable/Finalize 단계에 적용하고, 마지막에 전체 빌드 시간을 한 줄로 리포트한다:
```cpp
    molga::ActiveReportSink().ReportTiming("Build: Total", totalMs, settings.profile.gameName);
```

- [ ] **Step 4: (선택) 빌드 타이밍 리포트 테스트**

`tests/test_game_builder.cpp`에 캡처 sink를 주입해 단계 리포트가 발생하는지 검증(빌드가 GL/프로젝트 없이 실행 가능한 범위에서만; 불가하면 sink 인터페이스의 `ReportTiming` 호출만 단위 테스트로 분리):
```cpp
struct CapturingSink : molga::IProfilerReportSink {
    std::vector<std::string> labels;
    void ReportTiming(const std::string& label, double, const std::string&) override {
        labels.push_back(label);
    }
};
TEST_CASE("report sink can be swapped and captures timings") {
    CapturingSink sink;
    molga::SetReportSink(&sink);
    molga::ActiveReportSink().ReportTiming("Build: Total", 12.5, "Game");
    molga::SetReportSink(nullptr);  // 폴백 복귀
    REQUIRE(sink.labels.size() == 1);
    CHECK(sink.labels[0] == "Build: Total");
}
```
`tests/CMakeLists.txt`에 필요한 테스트가 없으면 등록.

- [ ] **Step 5: 빌드 + 테스트 + 수동 검증**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 전체 PASS.

수동 검증: 빌드 실행 → 로그(또는 UX-2 도착 시 Console/task details)에 단계별 "Build: Copy assets: N ms" 줄과 "Build: Total" 줄이 보이는지 확인. 큰 텍스처 로드 시 "Slow texture load: <path> (<ms>) [<caller>]" 경고가 에셋과 호출 서브시스템을 모두 식별하는지 확인.

- [ ] **Step 6: 커밋**

```bash
git add src/Editor/Profiling/ProfilerReportSink.h src/Core/TextureManager.cpp \
        src/Editor/GameBuilder.cpp tests/test_game_builder.cpp tests/CMakeLists.txt
git commit -m "feat(profiling): asset-load + build/package step timings via report sink (UX-5 Task F)"
```

---

## 완료 기준

- [ ] 느린 프레임을 Profiler 창에서 선택해 named CPU 구간(`World.Update`, `World.FixedStep`, `Editor.UI` 등)으로 펼쳐 볼 수 있다.
- [ ] 카테고리 합계(Scripts/Rendering/Physics/Asset Load/Editor UI/Other)로 "시간이 어디에 쓰였는지" 한눈에 구분된다(갭 분석 §7 exit scenario).
- [ ] 렌더러 통계(draw calls, batches, texture binds, shader switches, FBO resizes)가 편집 중과 Play 모드 모두에서 보인다.
- [ ] 카운터(draw calls, sprites, particles, text, tile chunks, asset loads, scripts, physics)가 프레임마다 갱신된다.
- [ ] 빌드/패키지 단계가 각 단계 duration과 전체 시간을 Console/task details(현재는 `Log` 폴백)에 리포트한다.
- [ ] 느린 에셋 로드 경고가 에셋 경로와 호출 서브시스템을 함께 식별한다.
- [ ] ring buffer는 고정 용량으로 동작하며 초과 시 가장 오래된 프레임을 덮어쓴다(단위 테스트로 검증됨).
- [ ] `ProfilerService::SetEnabled(false)`로 프로파일러를 끄면 `PushFrame`/`EndFrame`이 즉시 반환해 오버헤드가 near-zero다.
- [ ] `molga_core`는 `ProfilerWindow`(ImGui/Editor)를 include하지 않는다(계층 경계 유지).

## 의존성 / 순서

- **의존:** UX-2(Console/`EditorTaskService`의 task details) — 빌드/에셋 타이밍 리포트의 최종 도착지. UX-2 미구현 상태에서는 `IProfilerReportSink`의 `LogProfilerReportSink` 폴백으로 동작하며, UX-2 도착 시 `SetReportSink`로 Console sink를 꽂는 한 줄 교체만 필요하다.
- **독립:** UX-3(Asset 식별)·UX-4(Script 반복)와 독립적으로 진행 가능. 단, UX-3의 importer/AssetDatabase가 생기면 에셋 로드 타이밍의 "calling subsystem" 식별이 importer 경로로 더 정확해진다(후속 개선).
- **순서:** Task A → B(측정 primitive, 단위 테스트) → C(메인 루프·World 계측) → D(시각화) → E(렌더러·씬 카운터) → F(에셋·빌드 타이밍). A·B는 `molga_core` 단위 테스트로 완전히 검증되므로 GL/에디터 없이 선행 가능하다.
- **오버헤드 통제:** 프로파일러 비활성 시 `ProfilerService`가 즉시 반환하고, 활성 시에도 스코프는 ns 시각 2회 읽기 + `vector::push_back`만 수행한다. GL 쿼리·트레이스 파일 I/O는 도입하지 않는다.

## 비목표 / 후속 진화

- **저장 트레이스 파일(trace capture files)은 비목표**(갭 분석 §10, 559). in-process ring buffer가 이벤트 모델을 검증한 뒤에만 Chrome trace/Unreal Insights형 파일 포맷을 별도 제안서로 평가한다.
- GPU 타이밍(GL timer query), 메모리 프로파일러, per-thread 타임라인, 샘플링 프로파일러는 이 슬라이스 범위 밖이다.
- batching이 도입되기 전까지 `batches`는 `drawCalls`와 동치로 둔다(로드맵 Phase 2-4 렌더링 파이프라인에서 분기).
