# Task 0-5a: 실제 테스트 체계 구축 (doctest + CMakePresets + Sanitizer + CI)

> **For agentic workers:** REQUIRED SUB-SKILL: `superpowers:subagent-driven-development` 또는 `superpowers:executing-plans`로 단계별 구현. 체크박스(`- [ ]`)로 추적한다. 이 작업은 슬라이스의 **1순위**다 — 이후 모든 작업의 TDD가 이 위에서 동작한다.

**Goal:** `assert()` 기반 테스트(Release `-DNDEBUG`에서 전부 사라짐)를 doctest로 옮겨, Debug/Release 양쪽에서 진짜로 검증되게 한다. CMakePresets로 debug/release/asan/ubsan 빌드를 표준화하고, CI가 Release에서도 실제 검증하도록 고친다.

**Architecture:** doctest 단일 헤더를 `external/`에 vendoring(stb/miniaudio와 동일 관례). 공용 `doctest_main` 정적 라이브러리가 `main()`을 제공하고, 각 테스트 실행 파일은 `test_*.cpp + doctest_main`으로 링크된다. 경고 플래그와 선택적 sanitizer는 `molga_warnings` INTERFACE 라이브러리로 우리 타깃에만 적용한다.

**Tech Stack:** CMake 3.27, CTest, doctest 2.4.11(vendored), Clang/AppleClang sanitizers

**닫는 결함:** 갭 분석 P0-6 (`docs/plan/2026-06-06_project_gap_analysis.md` §3 P0-6, §5.6)

---

## 현재 상태 (검증된 사실)

- `CMakeLists.txt`는 `set(CMAKE_CXX_STANDARD 17)`만 설정. `CMAKE_CXX_STANDARD_REQUIRED`/`CMAKE_CXX_EXTENSIONS`/경고 플래그/NDEBUG 처리 **전부 없음**. (`CMakeLists.txt:1-4`)
- 테스트 6개(`test_types/collision/ecs/scene_serializer/time/event`)는 모두 `static void test_*()` + 파일별 `main()` + `assert()` 패턴. 총 `assert()` 238회. (`tests/test_time.cpp` 전체가 대표 예시)
- `tests/CMakeLists.txt`의 `molga_add_test`는 각 테스트를 `molga_core`에만 링크. (`tests/CMakeLists.txt:1-12`)
- `.github/workflows/ci.yml`: `macos-latest`, matrix `[Debug, Release]`, `cmake -B build -DCMAKE_BUILD_TYPE=...` → build → `ctest --output-on-failure --timeout 30`. **Release leg는 NDEBUG로 assert가 제거되어 사실상 검증 0.**
- `CMakePresets.json` 없음. `.clang-format`/`.clang-tidy`/sanitizer 설정 없음. doctest/Catch2/gtest 없음.
- 의존성: `external/`에 glfw(submodule), glad/imgui(vendored source), nlohmann_json/stb/miniaudio(vendored header).

---

## 파일 구조

**Files:**
- Create: `external/doctest/doctest.h` (vendored 단일 헤더, 다운로드)
- Create: `external/doctest/LICENSE.txt` (doctest MIT 라이선스 — 다운로드)
- Create: `tests/doctest_main.cpp` (공용 main 제공)
- Create: `CMakePresets.json`
- Modify: `CMakeLists.txt` (표준 강제, 경고/sanitizer interface 라이브러리, doctest infra)
- Modify: `tests/CMakeLists.txt` (`molga_add_test`가 doctest_main 링크)
- Modify: `tests/test_time.cpp` … `tests/test_event.cpp` (6개 파일: assert → doctest)
- Modify: `.github/workflows/ci.yml` (프리셋 사용 + sanitizer 잡)

---

## Task A. doctest vendoring & 빌드 인프라

- [ ] **Step 1: doctest 단일 헤더 vendoring**

Run:
```bash
mkdir -p external/doctest
curl -L -o external/doctest/doctest.h \
  https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
curl -L -o external/doctest/LICENSE.txt \
  https://raw.githubusercontent.com/doctest/doctest/v2.4.11/LICENSE.txt
```
Expected: `external/doctest/doctest.h`가 생성되고 크기가 200KB 이상이다. 확인:
```bash
head -5 external/doctest/doctest.h    # "doctest.h - the lightest feature-rich C++ ..." 주석이 보여야 함
wc -l external/doctest/doctest.h      # 약 7000줄 내외
```

- [ ] **Step 2: 공용 doctest main TU 작성**

Create `tests/doctest_main.cpp`:
```cpp
// Provides main() and the doctest implementation for every test executable.
// Exactly one TU per executable may define this; we link it as a shared lib.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
```

- [ ] **Step 3: 루트 CMake에 표준 강제 + 경고/sanitizer interface 추가**

`CMakeLists.txt`의 상단(`set(CMAKE_CXX_STANDARD 17)` 직후)을 다음으로 교체한다. 현재:
```cmake
set(CMAKE_CXX_STANDARD 17)
```
교체:
```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 우리 코드에만 적용할 경고/sanitizer 모음 (external 라이브러리는 제외)
set(MOLGA_SANITIZER "" CACHE STRING "Enable a sanitizer: address | undefined | thread (empty = off)")

add_library(molga_warnings INTERFACE)
target_compile_options(molga_warnings INTERFACE -Wall -Wextra -Wpedantic)
if(NOT MOLGA_SANITIZER STREQUAL "")
    target_compile_options(molga_warnings INTERFACE
        -fsanitize=${MOLGA_SANITIZER} -fno-omit-frame-pointer -g)
    target_link_options(molga_warnings INTERFACE -fsanitize=${MOLGA_SANITIZER})
endif()
```
> 주의: 지금은 `-Werror`를 넣지 않는다. 갭 분석이 기존 코드/서드파티에 경고가 많다고 했으므로, warnings-as-errors는 별도 정리 단계(Phase 1)에서 범위를 좁혀 도입한다.

- [ ] **Step 4: 우리 타깃에 molga_warnings 링크**

`CMakeLists.txt`에서 세 타깃 정의 직후에 링크를 추가한다.

`add_library(molga_core STATIC ${ENGINE_SOURCES})` 블록의 `target_link_libraries(molga_core PUBLIC glad glfw)` 줄을 다음으로 교체:
```cmake
target_link_libraries(molga_core PUBLIC glad glfw)
target_link_libraries(molga_core PRIVATE molga_warnings)
```
`target_link_libraries(molga_engine PRIVATE molga_core imgui)` 줄 다음에 추가:
```cmake
target_link_libraries(molga_engine PRIVATE molga_warnings)
```
`target_link_libraries(molga_runtime PRIVATE molga_core)` 줄 다음에 추가:
```cmake
target_link_libraries(molga_runtime PRIVATE molga_warnings)
```

- [ ] **Step 5: tests CMake에 doctest 인프라 추가**

`tests/CMakeLists.txt` 전체를 다음으로 교체:
```cmake
# ── doctest (vendored single header) ──────────────────────────────────────────
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_SOURCE_DIR}/external/doctest)

# 공용 main: 각 테스트 실행 파일에 링크되어 main()을 제공
add_library(doctest_main STATIC doctest_main.cpp)
target_link_libraries(doctest_main PUBLIC doctest)

function(molga_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE molga_core doctest_main molga_warnings)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

molga_add_test(test_types             test_types.cpp)
molga_add_test(test_collision         test_collision.cpp)
molga_add_test(test_ecs               test_ecs.cpp)
molga_add_test(test_scene_serializer  test_scene_serializer.cpp)
molga_add_test(test_time              test_time.cpp)
molga_add_test(test_event             test_event.cpp)
```

- [ ] **Step 6: CMakePresets.json 작성**

Create `CMakePresets.json`:
```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 27, "patch": 0 },
  "configurePresets": [
    {
      "name": "debug",
      "displayName": "Debug",
      "binaryDir": "${sourceDir}/build/debug",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "release",
      "displayName": "Release",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    },
    {
      "name": "asan",
      "displayName": "Debug + AddressSanitizer",
      "binaryDir": "${sourceDir}/build/asan",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "MOLGA_SANITIZER": "address" }
    },
    {
      "name": "ubsan",
      "displayName": "Debug + UndefinedBehaviorSanitizer",
      "binaryDir": "${sourceDir}/build/ubsan",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "MOLGA_SANITIZER": "undefined" }
    }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" },
    { "name": "asan",    "configurePreset": "asan" },
    { "name": "ubsan",   "configurePreset": "ubsan" }
  ],
  "testPresets": [
    { "name": "debug",   "configurePreset": "debug",   "output": { "outputOnFailure": true }, "execution": { "timeout": 60 } },
    { "name": "release", "configurePreset": "release", "output": { "outputOnFailure": true }, "execution": { "timeout": 60 } },
    { "name": "asan",    "configurePreset": "asan",    "output": { "outputOnFailure": true }, "execution": { "timeout": 120 } },
    { "name": "ubsan",   "configurePreset": "ubsan",   "output": { "outputOnFailure": true }, "execution": { "timeout": 120 } }
  ]
}
```

- [ ] **Step 7: 프리셋으로 설정만 검증 (테스트는 아직 옛 형태)**

Run:
```bash
cmake --preset debug
```
Expected: 설정 성공. `build/debug/`가 생성된다. (이 시점엔 테스트가 아직 assert 기반이라 빌드는 되지만 다음 Task에서 변환한다.)

- [ ] **Step 8: 커밋**

```bash
git add external/doctest tests/doctest_main.cpp CMakePresets.json CMakeLists.txt tests/CMakeLists.txt
git commit -m "build: vendor doctest, add CMakePresets and sanitizer/warning infra"
```

---

## Task B. 기존 테스트를 doctest로 변환

각 파일을 **한 번에 하나씩** 변환하고 커밋한다. 아래 변환 규칙은 6개 파일 모두에 동일하게 적용된다.

### 변환 규칙 (R1–R5)

- **R1.** 파일 맨 위 `#include <cassert>`를 제거하고 `#include "doctest.h"`를 추가한다. (`<cmath>`/`<cstdio>`가 `approx`/`printf`에만 쓰였다면 함께 제거 가능.)
- **R2.** 각 `static void test_xxx() { ... }`를 `TEST_CASE("사람이 읽는 설명") { ... }`로 바꾼다. 본문은 그대로 둔다.
- **R3.** 본문의 모든 `assert(expr);`를 `CHECK(expr);`로 바꾼다. **단,** 그 뒤 코드가 해당 조건에 의존해 크래시할 수 있으면(널 포인터 역참조 등) `REQUIRE(expr);`를 쓴다(실패 시 그 케이스 즉시 중단).
- **R4.** 파일별 `approx(a, b)` 부동소수 비교는 `CHECK(a == doctest::Approx(b));`로 바꾸고, 파일 상단의 `static bool approx(...)` 헬퍼 정의를 삭제한다.
- **R5.** 파일 맨 아래 `int main() { ...; return 0; }`를 **통째로 삭제**한다. (main은 `doctest_main`이 제공.)

### Task B-1. test_time.cpp (대표 예시 — 완전한 변환 결과)

- [ ] **Step 1: 변환 후 전체 내용으로 교체**

`tests/test_time.cpp` 전체를 다음으로 교체:
```cpp
#include "Core/MolgaTime.h"
#include "doctest.h"

// ── Fixed accumulator ────────────────────────────────────────────────────────

TEST_CASE("fixed accumulator yields 2 steps for 0.05s at 50Hz") {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();

    Time::AccumulateFixedTime(0.05f);  // 0.05 / 0.02 = 2.5 → 2 steps
    int steps = 0;
    while (Time::HasPendingFixedStep()) {
        steps++;
        Time::ConsumeFixedStep();
    }
    CHECK(steps == 2);
}

TEST_CASE("ResetFixedAccumulator clears backlog") {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(1.0f);   // large backlog
    Time::ResetFixedAccumulator();
    CHECK_FALSE(Time::HasPendingFixedStep());
}

TEST_CASE("fixed alpha is fraction of a step") {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(0.01f);
    CHECK(Time::GetFixedAlpha() == doctest::Approx(0.5f));  // 0.01 / 0.02
}

TEST_CASE("0.1s produces 5 fixed steps") {
    Time::SetFixedDeltaTime(0.02f);
    Time::ResetFixedAccumulator();
    Time::AccumulateFixedTime(0.1f);
    int steps = 0;
    while (Time::HasPendingFixedStep()) {
        steps++;
        Time::ConsumeFixedStep();
    }
    CHECK(steps == 5);  // 0.1 / 0.02
}
```

- [ ] **Step 2: 빌드 후 실행하여 통과 확인**

Run:
```bash
cmake --build --preset debug --target test_time -j4
ctest --preset debug -R test_time
```
Expected: `test_time` PASS. 출력에 doctest 요약(`test cases: 4 | 4 passed`)이 보인다.

- [ ] **Step 3: 커밋**

```bash
git add tests/test_time.cpp
git commit -m "test: migrate test_time to doctest"
```

### Task B-2 … B-6. 나머지 5개 파일

각 파일에 대해 동일 절차를 반복한다. 파일을 **먼저 읽고** 규칙 R1–R5를 적용한 뒤, TEST_CASE 이름은 원래 `test_xxx` 함수명을 사람이 읽기 좋게 풀어 쓴다.

- [ ] **B-2 `tests/test_types.cpp`** (18개 함수 → 18개 TEST_CASE, assert 70개 → CHECK)
- [ ] **B-3 `tests/test_collision.cpp`** (13개 → 13개, assert 20개)
- [ ] **B-4 `tests/test_event.cpp`** (7개 → 7개, assert 18개)
- [ ] **B-5 `tests/test_scene_serializer.cpp`** (8개 → 8개, assert 48개)
- [ ] **B-6 `tests/test_ecs.cpp`** (26개 → 26개, assert 78개. 파일 내부에 `COMPONENT_TYPE` 매크로로 정의한 테스트용 Component 클래스들은 그대로 둔다 — TEST_CASE 밖, 파일 스코프에 유지.)

각 파일마다:
```bash
# 변환 후
cmake --build --preset debug --target <test_name> -j4
ctest --preset debug -R <test_name>
git add tests/<test_name>.cpp
git commit -m "test: migrate <test_name> to doctest"
```

- [ ] **B-7: 전체 테스트 일괄 통과 확인**

Run:
```bash
cmake --build --preset debug -j4
ctest --preset debug --output-on-failure
```
Expected: 6/6 PASS.

---

## Task C. Release/Sanitizer에서 검증이 실제로 작동함을 증명

- [ ] **Step 1: Release에서도 통과 확인**

Run:
```bash
cmake --preset release
cmake --build --preset release -j4
ctest --preset release --output-on-failure
```
Expected: 6/6 PASS. (예전과 달리 doctest의 `CHECK`는 NDEBUG에서도 살아있다.)

- [ ] **Step 2: 의도적 실패가 Debug/Release 모두에서 실패하는지 확인 (커밋하지 않음)**

`tests/test_time.cpp`의 첫 `CHECK(steps == 2);`를 임시로 `CHECK(steps == 999);`로 바꾼 뒤:
```bash
cmake --build --preset debug -j4 && ctest --preset debug -R test_time   # FAIL 이어야 함
cmake --build --preset release -j4 && ctest --preset release -R test_time # FAIL 이어야 함
```
Expected: **둘 다 FAIL.** (이것이 P0-6 완료 기준이다 — 예전 assert 방식이라면 Release는 거짓 통과했다.) 확인 후 변경을 되돌린다:
```bash
git checkout tests/test_time.cpp
```

- [ ] **Step 3: ASan/UBSan 빌드가 깨끗한지 확인**

Run:
```bash
cmake --preset asan  && cmake --build --preset asan  -j4 && ctest --preset asan  --output-on-failure
cmake --preset ubsan && cmake --build --preset ubsan -j4 && ctest --preset ubsan --output-on-failure
```
Expected: 6/6 PASS, sanitizer 오류 출력 없음. (현재 단위 테스트 범위에서는 깨끗해야 한다. 계층 관련 메모리 버그는 task-0-4에서 ASan으로 잡는다.)

---

## Task D. CI 갱신

- [ ] **Step 1: ci.yml 교체**

`.github/workflows/ci.yml` 전체를 다음으로 교체:
```yaml
name: CI

on:
  push:
    branches: [main, phase4, phase6]
  pull_request:
    branches: [main]

jobs:
  build-test:
    runs-on: macos-latest
    strategy:
      fail-fast: false
      matrix:
        preset: [debug, release]
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure
        run: cmake --preset ${{ matrix.preset }}
      - name: Build
        run: cmake --build --preset ${{ matrix.preset }} -j$(sysctl -n hw.ncpu)
      - name: Test
        run: ctest --preset ${{ matrix.preset }} --output-on-failure --timeout 60

  sanitizers:
    runs-on: macos-latest
    strategy:
      fail-fast: false
      matrix:
        preset: [asan, ubsan]
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure
        run: cmake --preset ${{ matrix.preset }}
      - name: Build
        run: cmake --build --preset ${{ matrix.preset }} -j$(sysctl -n hw.ncpu)
      - name: Test
        run: ctest --preset ${{ matrix.preset }} --output-on-failure --timeout 120
```
> 아티팩트 업로드 잡은 빌드 디렉터리가 `build/release/`로 바뀌었으므로 0-5b(또는 배포 단계)에서 경로를 갱신한다. 지금은 제거해 둔다.

- [ ] **Step 2: 커밋**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: run debug/release via presets and add sanitizer jobs"
```

---

## 작업 완료 기준

- [ ] doctest가 `external/doctest/`에 vendoring되고, `doctest_main`이 모든 테스트에 링크된다.
- [ ] 6개 테스트 모두 doctest로 변환되어 Debug/Release/asan/ubsan에서 PASS한다.
- [ ] 임의의 `CHECK`를 깨뜨리면 Debug와 Release **모두** ctest가 FAIL한다. (Task C-2로 증명)
- [ ] `CMakePresets.json`으로 debug/release/asan/ubsan 빌드가 표준화된다.
- [ ] CI가 debug/release + asan/ubsan 잡을 실행한다.

## 다음 작업

[task-0-1_renderer_contract.md](task-0-1_renderer_contract.md) — 렌더러 계약 정상화. 첫 doctest 신규 테스트(`test_renderer_contract`)를 이 토대 위에 작성한다.
