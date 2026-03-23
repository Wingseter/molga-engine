# Phase 0: 기준선 확보 ✅ 완료

> 출처: Codex 리팩토링 계획의 "Phase 0" 개념 반영
> 완료일: 2026-03-08 | 커밋: `d4d4988`

## 목표
리팩토링을 안전하게 진행하기 위한 최소 안전장치를 구축한다. 빌드 구조 정리와 자동화 테스트 기반을 마련한다.

---

## 0.1 ENGINE_SOURCES 중복 컴파일 제거 - molga_core 라이브러리 타깃 ✅

### 이전 문제

```cmake
# CMakeLists.txt - 이전 구조
set(ENGINE_SOURCES
    src/Shader.cpp
    src/Texture.cpp
    # ... 20+ 파일
)

# 동일 소스가 두 번 컴파일됨
add_executable(molga_engine src/main.cpp ${ENGINE_SOURCES} ${EDITOR_SOURCES})
add_executable(molga_runtime src/runtime_main.cpp ${ENGINE_SOURCES})
```

- `ENGINE_SOURCES`가 `molga_engine`과 `molga_runtime` 두 타깃에 각각 컴파일
- 빌드 시간 2배, 오브젝트 파일 중복
- 빌드 시 `ld: warning: ignoring duplicate libraries: 'external/glfw/src/libglfw3.a'` 경고 발생

### 적용 결과

```cmake
# 공용 엔진을 정적 라이브러리로 분리
add_library(molga_core STATIC ${ENGINE_SOURCES})
target_include_directories(molga_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external/stb
    ${CMAKE_SOURCE_DIR}/external/miniaudio
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include
)
target_link_libraries(molga_core PUBLIC glad glfw)

if(APPLE)
    target_link_libraries(molga_core PUBLIC "-framework CoreAudio" "-framework AudioToolbox")
endif()

# 에디터: molga_core + 에디터 소스 링크
add_executable(molga_engine src/main.cpp ${EDITOR_SOURCES})
target_link_libraries(molga_engine PRIVATE molga_core imgui)
target_compile_definitions(molga_engine PRIVATE MOLGA_EDITOR)

# 런타임: molga_core만 링크
add_executable(molga_runtime src/runtime_main.cpp)
target_link_libraries(molga_runtime PRIVATE molga_core)
```

### 계획 대비 차이점
- `target_include_directories`에 `stb`, `miniaudio`, `nlohmann_json` 경로 추가 필요 (기존 글로벌 `include_directories()` 4개를 스코프 지정으로 전환)
- `${CMAKE_SOURCE_DIR}/src` 형태로 절대 경로 사용 (상대 경로 `src/` 대신)

### 효과
- 빌드 시간 감소 (엔진 소스 1회만 컴파일)
- 링크 경고 해소
- 엔진 코어와 에디터의 의존성 경계 명확화

### 변경 파일
- `CMakeLists.txt`

---

## 0.2 CTest 도입 및 Smoke Test 추가 ✅

### 이전 문제

- 저장소에 자동화 테스트가 전혀 없음
- `add_test`, `CTest`, 별도 테스트 타깃 없음
- 리팩토링 시 회귀 버그를 감지할 수단이 없음

### 적용 결과

```cmake
# CMakeLists.txt
enable_testing()
add_subdirectory(tests)

# tests/CMakeLists.txt - 헬퍼 함수로 테스트 추가 간소화
function(molga_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE molga_core)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

molga_add_test(test_types             test_types.cpp)
molga_add_test(test_collision         test_collision.cpp)
molga_add_test(test_ecs               test_ecs.cpp)
molga_add_test(test_scene_serializer  test_scene_serializer.cpp)
```

### 구현된 테스트 (4개, 전체 통과)

| 테스트 | 내용 | 검증 항목 수 |
|--------|------|-------------|
| test_types | Vector2, Color, AABB, Circle, CollisionResult, Frame 구조체 | ~30 |
| test_collision | CheckAABB, CheckCircle, CheckAABBCircle, PointIn* 전체 | ~15 |
| test_ecs | GameObject 생성/ID/활성화, Component CRUD, Transform 좌표 변환, 부모-자식 계층, BoxCollider2D 월드 AABB | ~20 |
| test_scene_serializer | SerializeGameObject/Deserialize round-trip, SaveScene/LoadScene round-trip, 잘못된 JSON 처리, null 처리, 없는 파일 처리 | ~10 |

### 테스트 디렉토리 구조 (실제)

```
tests/
├── CMakeLists.txt
├── test_types.cpp
├── test_collision.cpp
├── test_ecs.cpp
└── test_scene_serializer.cpp
```

### 계획 대비 차이점
- `test_project.cpp`, `test_transform.cpp` 대신 → `test_types.cpp`, `test_ecs.cpp`로 변경
  - Types: Common/Types.h의 모든 구조체를 포괄 테스트
  - ECS: Transform을 포함한 전체 ECS 계층 테스트
- `tests/CMakeLists.txt` 분리 + `molga_add_test()` 헬퍼 함수 도입 (향후 테스트 추가 간소화)
- `test_data/` 디렉토리 불필요 (SceneSerializer 테스트가 `/tmp/` 사용)

### 변경 파일
- `CMakeLists.txt`
- 새 파일: `tests/CMakeLists.txt`, `tests/test_types.cpp`, `tests/test_collision.cpp`, `tests/test_ecs.cpp`, `tests/test_scene_serializer.cpp`

---

## 0.3 빌드 경고 해결 ✅

### 이전 문제

```
ld: warning: ignoring duplicate libraries: 'external/glfw/src/libglfw3.a'
```

- imgui가 glfw에 링크되고, molga_engine도 glfw에 링크 → 중복
- `molga_core` 라이브러리 타깃 도입으로 자동 해소

### 결과
- `molga_core`가 glfw를 `PUBLIC`으로 링크하므로, `molga_engine`과 `molga_runtime`이 별도로 glfw 링크 불필요 → 중복 경고 해소

### 변경 파일
- `CMakeLists.txt` (0.1에서 함께 해결)

---

## 0.4 macOS 대소문자 비구분 파일시스템 버그 수정 ✅

> 구현 중 발견된 pre-existing 버그

### 문제

Phase 0에서 `target_include_directories(molga_core PUBLIC ${CMAKE_SOURCE_DIR}/src)` 추가 후, C++ 표준 라이브러리 컴파일 실패:

```
error: reference to unresolved using declaration (time_t)
error: member access into incomplete type 'tm'
```

### 원인 분석

macOS APFS는 **대소문자 비구분** 파일시스템. C++ 표준 헤더 `<ctime>`이 내부적으로 `<time.h>`를 포함할 때:

1. 컴파일러가 `-I src/` 경로에서 먼저 `src/Time.h`를 발견
2. macOS에서 `Time.h` = `time.h` (대소문자 비구분)
3. 프로젝트의 `Time.h`는 `class Time`을 정의하지만 `time_t`, `struct tm`은 없음
4. 시스템 `<time.h>` 대신 프로젝트 `Time.h`가 포함되어 모든 C++ 표준 라이브러리 빌드 실패

### 수정

```
src/Time.h   → src/MolgaTime.h  (파일명 변경)
src/Time.cpp → src/MolgaTime.cpp (파일명 변경)
```

참조하는 5개 파일의 `#include` 경로 업데이트:
- `src/MolgaTime.cpp`: `#include "MolgaTime.h"`
- `src/main.cpp`: `#include "MolgaTime.h"`
- `src/runtime_main.cpp`: `#include "MolgaTime.h"`
- `src/Editor/Editor.cpp`: `#include "../MolgaTime.h"`
- `src/Core/Application.cpp`: `#include "../MolgaTime.h"`

### 교훈
- macOS에서 소스 파일명은 시스템 헤더명과 충돌하지 않도록 주의
- `target_include_directories`로 `src/`를 포함 경로에 추가하면 이런 충돌이 표면화될 수 있음
- 향후 Phase 5 (코드 품질)에서 디렉토리 재구성 시 이 점 참고

### 변경 파일
- `src/Time.h` → `src/MolgaTime.h` (이름 변경)
- `src/Time.cpp` → `src/MolgaTime.cpp` (이름 변경)
- `src/main.cpp`, `src/runtime_main.cpp`, `src/Editor/Editor.cpp`, `src/Core/Application.cpp` (include 경로 수정)
- `CMakeLists.txt` (ENGINE_SOURCES에서 `src/MolgaTime.cpp`로 변경)

---

## 체크리스트

- [x] `molga_core` 정적 라이브러리 타깃 생성
- [x] `molga_engine`과 `molga_runtime`이 `molga_core`를 링크하도록 변경
- [x] 중복 링크 경고 해소 확인
- [x] `enable_testing()` 추가
- [x] `tests/` 디렉토리 생성
- [x] SceneSerializer round-trip 테스트 작성
- [x] Collision 유닛 테스트 작성
- [x] Types (Vector2, Color, AABB, Circle) 유닛 테스트 작성
- [x] ECS (GameObject, Transform, BoxCollider2D) 유닛 테스트 작성
- [x] `cmake --build && ctest` 통과 확인 (4/4 tests, 1.72s)
- [x] macOS Time.h → MolgaTime.h 이름 충돌 해결
