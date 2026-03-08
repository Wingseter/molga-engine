# Phase 0: 기준선 확보

> 출처: Codex 리팩토링 계획의 "Phase 0" 개념 반영

## 목표
리팩토링을 안전하게 진행하기 위한 최소 안전장치를 구축한다. 빌드 구조 정리와 자동화 테스트 기반을 마련한다.

---

## 0.1 ENGINE_SOURCES 중복 컴파일 제거 - molga_core 라이브러리 타깃

### 현재 문제

```cmake
# CMakeLists.txt - 현재 구조
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

### 변경 계획

```cmake
# 공용 엔진을 정적 라이브러리로 분리
add_library(molga_core STATIC ${ENGINE_SOURCES})
target_include_directories(molga_core PUBLIC src/)
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

### 기대 효과
- 빌드 시간 약 40% 감소
- 링크 경고 해소
- 엔진 코어와 에디터의 의존성 경계 명확화

### 대상 파일
- `CMakeLists.txt`

---

## 0.2 CTest 도입 및 Smoke Test 추가

### 현재 문제

- 저장소에 자동화 테스트가 전혀 없음
- `add_test`, `CTest`, 별도 테스트 타깃 없음
- 리팩토링 시 회귀 버그를 감지할 수단이 없음

### 변경 계획

```cmake
# CMakeLists.txt에 추가
enable_testing()

# GUI 의존 없는 모듈부터 테스트
add_executable(test_scene_serializer tests/test_scene_serializer.cpp)
target_link_libraries(test_scene_serializer PRIVATE molga_core)
add_test(NAME SceneSerializer COMMAND test_scene_serializer)

add_executable(test_project tests/test_project.cpp)
target_link_libraries(test_project PRIVATE molga_core)
add_test(NAME Project COMMAND test_project)
```

### 첫 테스트 후보 (GUI 의존 없는 모듈)

| 모듈 | 테스트 내용 | 난이도 |
|------|-----------|--------|
| SceneSerializer | 저장 → 로드 → 비교 (round-trip) | 낮음 |
| Project | 생성 → 디렉토리 구조 검증 → 닫기 | 낮음 |
| GameBuilder | 설정 검증, 경로 생성 | 낮음 |
| Collision | AABB/Circle 충돌 결과 검증 | 매우 낮음 |
| ScriptManager | 팩토리 등록 → 생성 → 타입 확인 | 낮음 |
| Transform | 로컬→월드 좌표 변환 검증 | 낮음 |

### 테스트 디렉토리 구조

```
tests/
├── test_scene_serializer.cpp
├── test_project.cpp
├── test_collision.cpp
├── test_transform.cpp
└── test_data/
    └── test_scene.json
```

### 대상 파일
- `CMakeLists.txt`
- 새 디렉토리: `tests/`

---

## 0.3 빌드 경고 해결

### 현재 문제

```
ld: warning: ignoring duplicate libraries: 'external/glfw/src/libglfw3.a'
```

- imgui가 glfw에 링크되고, molga_engine도 glfw에 링크 → 중복
- `molga_core` 라이브러리 타깃 도입으로 자동 해소 예정

### 추가 확인 사항
- `-Wall -Wextra` 활성화 시 발생하는 경고 확인 및 기록
- 당장 수정하지 않더라도, 현재 경고 수준을 문서화하여 이후 개선 기준선으로 활용

### 대상 파일
- `CMakeLists.txt`

---

## 체크리스트

- [ ] `molga_core` 정적 라이브러리 타깃 생성
- [ ] `molga_engine`과 `molga_runtime`이 `molga_core`를 링크하도록 변경
- [ ] 중복 링크 경고 해소 확인
- [ ] `enable_testing()` 추가
- [ ] `tests/` 디렉토리 생성
- [ ] SceneSerializer round-trip 테스트 작성
- [ ] Collision 유닛 테스트 작성
- [ ] `cmake --build && ctest` 통과 확인
