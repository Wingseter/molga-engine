# Task 0-1: 렌더러 호출 계약 정상화

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`. 선행: [task-0-5a](task-0-5a_test_framework.md)(doctest)가 끝나 있어야 한다. 체크박스(`- [ ]`)로 추적.

**Goal:** `SpriteRenderer`가 스프라이트마다 자체적으로 호출하는 `Renderer::Begin/End`를 제거하고, 렌더 패스 경계를 프레임 루프(RenderPass)가 단독으로 소유하게 한다. 계약 위반을 Debug/Release **모두**에서 오류로 검증하고, GL 없이 단위 테스트한다.

**Architecture:** 렌더 패스 상태 머신을 GL과 분리한 `RenderPassState`(molga_core, 테스트 가능)로 추출한다. `Renderer`는 이 상태 머신을 사용하고, 위반 시 `assert`(Release에서 사라짐) 대신 `Log::Error` + 안전한 무시로 처리한다. 프레임 루프는 RAII `RenderPass`로 `Begin/End`를 감싼다.

**Tech Stack:** C++17, OpenGL 3.3, doctest

**닫는 결함:** 갭 분석 P0-1 (`docs/plan/2026-06-06_project_gap_analysis.md` §3 P0-1)

---

## 현재 상태 (검증된 사실)

- `Renderer`는 private `enum class State { Idle, Drawing }; State state = State::Idle;` 를 갖고, `Begin()`은 `assert(state == State::Idle ...)`, `End()`은 `assert(state == State::Drawing ...)`, `DrawSprite()`는 `assert(state == State::Drawing ...)`로 계약을 강제한다. (`src/Rendering/Renderer.h:25-27`, `src/Rendering/Renderer.cpp:69-117`) → **assert라서 Release(`-DNDEBUG`)에서 모두 사라진다.**
- `SpriteRenderer::RenderSprite(Renderer*, Shader*, Camera2D*)`는 임시 `Sprite`를 만든 뒤 `renderer->Begin(...); renderer->DrawSprite(&sprite); renderer->End();`를 **스프라이트마다** 호출한다. (`src/ECS/Components/SpriteRenderer.cpp:54-56`)
- `main.cpp` 편집 모드 렌더(`src/main.cpp:195-207`)와 `runtime_main.cpp`(`src/runtime_main.cpp:127-139`)는 **이미** 루프 바깥에서 `Begin`/`End`를 한 번 호출하고, 루프 안에서 `sr->RenderSprite(...)`를 부른다 → **Begin 중첩**. Debug에서 즉시 assert 실패, Release에서 상태 오염.
- `MenuScene`/`GameScene`/`Particle`/`UI`는 올바른 패턴(한 번 Begin → `DrawSprite` N번 → End)을 쓴다. (예: `src/Scenes/GameScene.cpp:220-235`)
- `RenderSystem`/`RenderPass` 추상화는 **없다**. (전체 grep 결과 없음)

---

## 파일 구조

**Files:**
- Create: `src/Rendering/RenderPassState.h` (GL-free 상태 머신, molga_core)
- Create: `src/Rendering/RenderPassState.cpp`
- Create: `src/Rendering/RenderPass.h` (RAII Begin/End 가드)
- Create: `tests/test_renderer_contract.cpp`
- Modify: `src/Rendering/Renderer.h` (state 멤버를 RenderPassState로, `IsDrawing()` 추가)
- Modify: `src/Rendering/Renderer.cpp` (assert → Log::Error + 안전 처리)
- Modify: `src/ECS/Components/SpriteRenderer.h` (RenderSprite 시그니처)
- Modify: `src/ECS/Components/SpriteRenderer.cpp` (Begin/End 제거)
- Modify: `src/main.cpp` (RenderPass 사용, RenderSprite 호출 갱신)
- Modify: `src/runtime_main.cpp` (동일)
- Modify: `CMakeLists.txt` (`src/Rendering/RenderPassState.cpp`를 ENGINE_SOURCES에 추가)
- Modify: `tests/CMakeLists.txt` (test_renderer_contract 등록)

---

## Task A. GL-free 렌더 패스 상태 머신 (TDD)

- [ ] **Step 1: 실패하는 테스트 작성**

Create `tests/test_renderer_contract.cpp`:
```cpp
#include "Rendering/RenderPassState.h"
#include "doctest.h"

using molga::RenderPassState;

TEST_CASE("begin then end is a valid empty pass") {
    RenderPassState s;
    CHECK(s.phase() == RenderPassState::Phase::Idle);
    CHECK(s.TryBegin());
    CHECK(s.CanDraw());
    CHECK(s.TryEnd());
    CHECK(s.phase() == RenderPassState::Phase::Idle);
    CHECK(s.violations() == 0);
}

TEST_CASE("nested begin is rejected and counted") {
    RenderPassState s;
    REQUIRE(s.TryBegin());
    CHECK_FALSE(s.TryBegin());     // nested begin → violation, state unchanged
    CHECK(s.CanDraw());            // still inside the first pass
    CHECK(s.violations() == 1);
}

TEST_CASE("draw is not allowed outside a pass") {
    RenderPassState s;
    CHECK_FALSE(s.CanDraw());
}

TEST_CASE("end without begin is rejected and counted") {
    RenderPassState s;
    CHECK_FALSE(s.TryEnd());
    CHECK(s.violations() == 1);
    CHECK(s.phase() == RenderPassState::Phase::Idle);
}

TEST_CASE("one pass can host 100+ draws") {
    RenderPassState s;
    REQUIRE(s.TryBegin());
    for (int i = 0; i < 128; ++i) {
        CHECK(s.CanDraw());        // 100개 이상 스프라이트가 한 패스 안에서 그려질 수 있어야 한다
    }
    REQUIRE(s.TryEnd());
    CHECK(s.violations() == 0);
}
```

- [ ] **Step 2: tests/CMakeLists.txt에 등록**

`tests/CMakeLists.txt`의 마지막 `molga_add_test(...)` 줄들 아래에 추가:
```cmake
molga_add_test(test_renderer_contract test_renderer_contract.cpp)
```

- [ ] **Step 3: 컴파일 실패 확인**

Run:
```bash
cmake --preset debug
cmake --build --preset debug --target test_renderer_contract -j4
```
Expected: FAIL — `RenderPassState.h` 파일 없음(컴파일 에러).

- [ ] **Step 4: RenderPassState 헤더 작성**

Create `src/Rendering/RenderPassState.h`:
```cpp
#pragma once

namespace molga {

// GL과 분리된, Renderer Begin/Draw/End 계약을 강제하는 상태 머신.
// OpenGL 컨텍스트 없이 단위 테스트할 수 있도록 molga_core에 둔다.
class RenderPassState {
public:
    enum class Phase { Idle, Drawing };

    // 패스를 연다. 이미 Drawing이면 계약 위반(중첩 Begin)으로 false를 반환하고
    // 상태를 바꾸지 않는다.
    bool TryBegin();

    // 패스가 열려 있는 동안에만 true.
    bool CanDraw() const { return phase_ == Phase::Drawing; }

    // 패스를 닫는다. Drawing이 아니면 계약 위반으로 false를 반환한다.
    bool TryEnd();

    Phase phase() const { return phase_; }
    int violations() const { return violations_; }

private:
    Phase phase_ = Phase::Idle;
    int violations_ = 0;
};

} // namespace molga
```

- [ ] **Step 5: RenderPassState 구현 작성**

Create `src/Rendering/RenderPassState.cpp`:
```cpp
#include "Rendering/RenderPassState.h"

namespace molga {

bool RenderPassState::TryBegin() {
    if (phase_ != Phase::Idle) {
        ++violations_;
        return false;
    }
    phase_ = Phase::Drawing;
    return true;
}

bool RenderPassState::TryEnd() {
    if (phase_ != Phase::Drawing) {
        ++violations_;
        return false;
    }
    phase_ = Phase::Idle;
    return true;
}

} // namespace molga
```

- [ ] **Step 6: ENGINE_SOURCES에 추가**

`CMakeLists.txt`의 `set(ENGINE_SOURCES ...)` 목록에서 `src/Rendering/Shader.cpp` 같은 렌더링 항목 근처에 추가:
```cmake
    src/Rendering/RenderPassState.cpp
```

- [ ] **Step 7: 테스트 통과 확인**

Run:
```bash
cmake --build --preset debug --target test_renderer_contract -j4
ctest --preset debug -R test_renderer_contract --output-on-failure
```
Expected: PASS, `test cases: 5 | 5 passed`.

- [ ] **Step 8: 커밋**

```bash
git add src/Rendering/RenderPassState.h src/Rendering/RenderPassState.cpp \
        tests/test_renderer_contract.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(render): add GL-free RenderPassState with contract tests"
```

---

## Task B. Renderer가 RenderPassState를 사용 (assert → 런타임 검증)

- [ ] **Step 1: Renderer.h 수정 — state 멤버 교체 + IsDrawing 추가**

`src/Rendering/Renderer.h` 상단 include 부분에 추가:
```cpp
#include "Rendering/RenderPassState.h"
```
public 메서드(예: `void End();` 아래)에 추가:
```cpp
    bool IsDrawing() const;
```
private 멤버에서 다음을 찾아:
```cpp
    enum class State { Idle, Drawing };
    State state = State::Idle;
```
다음으로 교체:
```cpp
    molga::RenderPassState pass;
```

- [ ] **Step 2: Renderer.cpp 수정 — Log include + Begin/DrawSprite/End 검증 교체**

`src/Rendering/Renderer.cpp` 상단 include에 추가(없다면):
```cpp
#include "Common/Log.h"
```
`Begin()`(현재 `src/Rendering/Renderer.cpp:69-87`)의 앞부분 두 assert
```cpp
    assert(state == State::Idle && "Renderer::Begin called without matching End()");
    assert(shader != nullptr && "Renderer::Begin called with null shader");
    state = State::Drawing;
```
을 다음으로 교체:
```cpp
    if (!pass.TryBegin()) {
        Log::Error("Renderer", "Begin() called while a pass is already active; ignoring nested Begin");
        return;
    }
    if (shader == nullptr) {
        Log::Error("Renderer", "Begin() called with null shader");
        pass.TryEnd();
        return;
    }
```
`DrawSprite()`(현재 `:89-111`)의 첫 줄 assert
```cpp
    assert(state == State::Drawing && "Renderer::DrawSprite called without Begin()");
```
을 다음으로 교체:
```cpp
    if (!pass.CanDraw()) {
        Log::Error("Renderer", "DrawSprite() called outside an active Begin()/End() pass");
        return;
    }
```
`End()`(현재 `:113-117`) 전체
```cpp
void Renderer::End() {
    assert(state == State::Drawing && "Renderer::End called without Begin()");
    state = State::Idle;
    currentShader = nullptr;
}
```
를 다음으로 교체:
```cpp
void Renderer::End() {
    if (!pass.TryEnd()) {
        Log::Error("Renderer", "End() called without a matching Begin()");
        return;
    }
    currentShader = nullptr;
}

bool Renderer::IsDrawing() const {
    return pass.CanDraw();
}
```
> `<cassert>` include가 다른 곳에서 안 쓰이면 제거해도 된다(필수는 아님).

- [ ] **Step 3: 빌드 확인 (아직 호출부는 중첩 상태)**

Run:
```bash
cmake --build --preset debug --target molga_core -j4
```
Expected: 성공. (계약은 이제 런타임 검증이라 빌드는 통과한다. 호출부 중첩은 다음 Task에서 제거.)

- [ ] **Step 4: 커밋**

```bash
git add src/Rendering/Renderer.h src/Rendering/Renderer.cpp
git commit -m "refactor(render): enforce pass contract at runtime in Debug and Release"
```

---

## Task C. SpriteRenderer에서 Begin/End 제거

- [ ] **Step 1: SpriteRenderer.h 시그니처 변경**

`src/ECS/Components/SpriteRenderer.h`에서 다음 선언을 찾아:
```cpp
    void RenderSprite(Renderer* renderer, Shader* shader, Camera2D* camera);
```
다음으로 교체:
```cpp
    // 패스(Begin/End)는 호출자(프레임 루프/RenderPass)가 소유한다.
    // 이 함수는 활성 패스 안에 스프라이트 1개를 제출만 한다.
    void RenderSprite(Renderer* renderer);
```

- [ ] **Step 2: SpriteRenderer.cpp 본문 변경**

`src/ECS/Components/SpriteRenderer.cpp`를 읽고, 메서드 시그니처를
```cpp
void SpriteRenderer::RenderSprite(Renderer* renderer, Shader* shader, Camera2D* camera) {
```
다음으로 바꾼다:
```cpp
void SpriteRenderer::RenderSprite(Renderer* renderer) {
```
그리고 끝부분 3줄(현재 `:54-56`)
```cpp
    renderer->Begin(shader, camera);
    renderer->DrawSprite(&sprite);
    renderer->End();
```
을 다음 1줄로 교체:
```cpp
    renderer->DrawSprite(&sprite);
```
> 본문에서 `shader`/`camera`를 다른 곳에 쓰지 않으면(임시 `Sprite` 생성은 Transform만 사용) 그대로 컴파일된다. 만약 `Shader`/`Camera2D` include나 forward decl이 더 이상 안 쓰여 경고가 나면 남겨도 무방하다.

- [ ] **Step 3: 빌드 (호출부 미수정이라 컴파일 에러 예상)**

Run:
```bash
cmake --build --preset debug -j4
```
Expected: FAIL — `main.cpp`/`runtime_main.cpp`에서 `RenderSprite(...)` 인자 수 불일치. 다음 Task에서 고친다.

---

## Task D. 프레임 루프가 RenderPass로 패스를 소유

- [ ] **Step 1: RenderPass RAII 가드 작성**

Create `src/Rendering/RenderPass.h`:
```cpp
#pragma once

#include "Rendering/Renderer.h"

namespace molga {

// 프레임 단위 렌더 패스 경계를 소유하는 RAII 가드.
// 스코프 진입 시 Begin, 탈출 시 End를 호출한다.
class RenderPass {
public:
    RenderPass(Renderer& renderer, Shader* shader, Camera2D* camera = nullptr)
        : renderer_(renderer) {
        renderer_.Begin(shader, camera);
    }
    ~RenderPass() { renderer_.End(); }

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

private:
    Renderer& renderer_;
};

} // namespace molga
```

- [ ] **Step 2: main.cpp 편집 모드 렌더를 RenderPass로 교체**

`src/main.cpp` 상단 include에 추가:
```cpp
#include "Rendering/RenderPass.h"
```
편집 모드 렌더 블록(현재 `src/main.cpp:195-207`)
```cpp
            if (editorState.IsEditMode()) {
                // Edit mode: Render editor scene
                renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);
                renderer->Begin(shader.get(), camera.get());
                for (auto& obj : editorObjects) {
                    if (obj && obj->IsActive()) {
                        auto sr = obj->GetComponent<SpriteRenderer>();
                        if (sr) {
                            sr->RenderSprite(renderer.get(), shader.get(), camera.get());
                        }
                    }
                }
                renderer->End();
            } else {
```
를 다음으로 교체:
```cpp
            if (editorState.IsEditMode()) {
                // Edit mode: Render editor scene
                renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);
                {
                    molga::RenderPass pass(*renderer, shader.get(), camera.get());
                    for (auto& obj : editorObjects) {
                        if (obj && obj->IsActive()) {
                            if (auto sr = obj->GetComponent<SpriteRenderer>()) {
                                sr->RenderSprite(renderer.get());
                            }
                        }
                    }
                }
            } else {
```
> 참고: task-0-2가 이 루프를 `World` 기반으로 다시 구성한다. 지금은 계약만 바로잡는다.

- [ ] **Step 3: runtime_main.cpp 렌더를 RenderPass로 교체**

`src/runtime_main.cpp` 상단 include에 추가:
```cpp
#include "Rendering/RenderPass.h"
```
렌더 블록(현재 `src/runtime_main.cpp:130-139`)
```cpp
        renderer->Begin(shader.get(), camera.get());
        for (auto& obj : gameObjects) {
            if (obj && obj->IsActive()) {
                auto sr = obj->GetComponent<SpriteRenderer>();
                if (sr) {
                    sr->RenderSprite(renderer.get(), shader.get(), camera.get());
                }
            }
        }
        renderer->End();
```
를 다음으로 교체:
```cpp
        {
            molga::RenderPass pass(*renderer, shader.get(), camera.get());
            for (auto& obj : gameObjects) {
                if (obj && obj->IsActive()) {
                    if (auto sr = obj->GetComponent<SpriteRenderer>()) {
                        sr->RenderSprite(renderer.get());
                    }
                }
            }
        }
```

- [ ] **Step 4: 전체 빌드 + 테스트**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 빌드 성공, 모든 테스트 PASS(`test_renderer_contract` 포함).

- [ ] **Step 5: 에디터/런타임 수동 실행으로 assertion 없음 확인**

Run (저장소 루트에서; 경로 문제로 셰이더 로드는 실패할 수 있으나 **렌더 계약 assertion은 없어야** 한다 — 경로 문제는 task-0-3에서 해결):
```bash
./build/debug/molga_engine &
sleep 2 && kill %1 2>/dev/null
```
Expected: `Assertion failed: state == State::Idle ...` 류 메시지가 **나오지 않는다**. (이전엔 첫 스프라이트에서 즉시 죽었다.)

- [ ] **Step 6: 커밋**

```bash
git add src/Rendering/RenderPass.h src/ECS/Components/SpriteRenderer.h \
        src/ECS/Components/SpriteRenderer.cpp src/main.cpp src/runtime_main.cpp
git commit -m "fix(render): caller owns the render pass; remove nested Begin/End (P0-1)"
```

---

## 작업 완료 기준

- [ ] `test_renderer_contract`가 Debug/Release/asan에서 통과한다(중첩 Begin·패스 밖 Draw·짝 없는 End를 모두 검증).
- [ ] `SpriteRenderer::RenderSprite`가 더 이상 `Begin/End`를 호출하지 않는다.
- [ ] 편집/런타임 렌더 루프가 `RenderPass`(=단일 Begin/End)로 100개 이상 스프라이트를 한 패스에서 그린다.
- [ ] 계약 위반이 Debug와 Release 모두에서 `Log::Error`로 드러난다(assert에 의존하지 않음).
- [ ] 에디터 실행 시 렌더 계약 assertion이 발생하지 않는다.

## 다음 작업

[task-0-4_hierarchy_and_commands.md](task-0-4_hierarchy_and_commands.md) — GameObject 수명/계층을 안전하게 만들고 Command/Undo를 도입한다(task-0-2의 World 복제가 안전해지도록 먼저 처리).
