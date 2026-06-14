# Playable Editor Vertical Slice — 구현 계획 개요

> **For agentic workers:** 이 디렉터리는 `docs/plan/2026-06-06_project_gap_analysis.md`의 P0 결함과 Phase 0(권장 실행 계획)을 **실제로 따라 구현할 수 있는 단위 계획서**로 분해한 것이다. 각 파일은 독립적으로 빌드/테스트 가능한 산출물을 만든다. 구현 시 `superpowers:test-driven-development`와 `superpowers:verification-before-completion`을 사용한다.

**Goal:** `프로젝트 생성 → 스프라이트 배치 → 저장 → Play → Stop → Build → 독립 런타임 실행`을 하나의 씬과 에셋으로 끝까지 성공시키는 **단일 수직 슬라이스**를 완성한다. 기능 개수를 늘리는 것이 아니라, 위 흐름이 끊김 없이 작동하게 만드는 것이 목표다.

**Architecture:** `편집 중인 씬 = 플레이되는 씬 = 빌드되는 씬`이라는 하나의 데이터 모델(`World` / `SceneDocument`)을 만들고, 모든 경로를 `PathService`로 절대화하며, 모든 편집을 `Command`로 처리한다. 새 고급 기능(라이팅, 프리팹, 배칭 등)은 이 슬라이스가 통과한 뒤에 같은 토대 위에 확장한다.

**Tech Stack:** C++17, CMake 3.27 / CTest, GLFW(submodule), OpenGL 3.3, GLAD, Dear ImGui(Docking), nlohmann/json(vendored), miniaudio, stb_image, **doctest(신규 vendored)**

---

## 1. 이 슬라이스가 닫는 P0 결함

| 작업 파일 | 닫는 P0 | 한 줄 요약 |
|---|---|---|
| [task-0-5a_test_framework.md](task-0-5a_test_framework.md) | P0-6 | doctest 도입 + CMakePresets + ASan/UBSan + Release에서도 검증되는 CI |
| [task-0-1_renderer_contract.md](task-0-1_renderer_contract.md) | P0-1 | `Begin/End` 중첩 제거, GL 없는 상태머신으로 계약을 테스트·검증 |
| [task-0-4_hierarchy_and_commands.md](task-0-4_hierarchy_and_commands.md) | P0-4, P0-7 | dangling parent/cycle 제거, 삭제 정책, Command/Undo + dirty 추적 |
| [task-0-2_scene_document_and_play_world.md](task-0-2_scene_document_and_play_world.md) | P0-2 | `World`/`SceneDocument` 단일 모델, Play 스냅샷 → Stop 복원, 샘플 씬 제거 |
| [task-0-3_path_service_and_build.md](task-0-3_path_service_and_build.md) | P0-3, P0-5 | `PathService` 절대 경로, 프로젝트 에셋·셰이더 빌드, 누락 시 실패, 런타임 텍스처 resolve |
| [task-0-5b_smoke_tests.md](task-0-5b_smoke_tests.md) | P0-6 | editor/runtime/build smoke test (앞 작업들이 끝난 뒤 추가) |

> P0-5(런타임 텍스처 로딩)는 task-0-3에 통합되어 있다. 빌드된 게임이 텍스처를 그대로 보여주는 것은 경로/에셋 파이프라인 문제와 분리할 수 없기 때문이다.

---

## 2. 권장 실행 순서와 의존성

갭 분석은 작업을 0-1 … 0-5로 번호 매겼지만, **TDD를 실제로 성립시키려면 테스트 토대를 먼저 깔아야 한다.** 아래는 의존성 기반 권장 순서다. (파일명은 추적성을 위해 갭 분석의 Task ID를 유지한다.)

```text
①  task-0-5a  테스트 프레임워크(doctest) + 프리셋 + CI
        │   (이후 모든 작업이 doctest로 TDD)
        ▼
②  task-0-1  렌더러 계약 정상화        ← 가장 작고 독립적, 빠른 첫 성공
        │
        ▼
③  task-0-4  안전한 Hierarchy + Command  ← GameObject 수명/계층을 먼저 고쳐야
        │                                  task-0-2의 World 복제가 안전하다
        ▼
④  task-0-2  World / SceneDocument / PlayWorld  ← 핵심. 단일 데이터 모델
        │
        ▼
⑤  task-0-3  PathService + 빌드 + 런타임 텍스처  ← World 저장 위치/빌드가 필요
        │
        ▼
⑥  task-0-5b  editor/runtime/build smoke test    ← 위가 모두 끝나야 의미 있음
```

### 의존성 근거

- **0-5a가 1순위인 이유:** 갭 분석 P0-6대로, 현재 238개 검증식이 `assert` 기반이라 Release(`-DNDEBUG`)에서 전부 제거된다. doctest의 `CHECK/REQUIRE`는 NDEBUG와 무관하므로, 이걸 먼저 깔아야 이후 모든 작업의 테스트가 Debug/Release 양쪽에서 진짜로 검증된다.
- **0-4가 0-2보다 먼저인 이유:** task-0-2는 Play 진입 시 EditWorld를 직렬화로 복제한다. 현재 `~GameObject`가 자식의 `parent`를 정리하지 않아(`GameObject.cpp:13-23`) 복제·폐기 과정에서 dangling pointer가 발생할 수 있다. 계층 수명을 먼저 안전하게 만들어야 World 복제가 ASan-clean 해진다.
- **0-3이 0-2 뒤인 이유:** 저장/빌드 경로는 "무엇을 저장·빌드하는가"(World/SceneDocument)가 정해진 뒤라야 프로젝트 `Scenes/` 아래로 정렬할 수 있다.
- **0-5b가 마지막인 이유:** runtime/build smoke test는 0-1(렌더 계약), 0-2(런타임이 World 사용), 0-3(빌드 신뢰성)이 모두 끝나야 실행 가능하다.

---

## 3. 각 작업의 산출물(파일) 한눈에

| 작업 | 신규 파일 | 주요 수정 파일 |
|---|---|---|
| 0-5a | `external/doctest/doctest.h`, `CMakePresets.json`, `tests/doctest_main.cpp` | `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_*.cpp`(6개), `.github/workflows/ci.yml` |
| 0-1 | `src/Rendering/RenderPassState.{h,cpp}`, `src/Rendering/RenderPass.h`, `tests/test_renderer_contract.cpp` | `src/Rendering/Renderer.{h,cpp}`, `src/ECS/Components/SpriteRenderer.cpp`, `src/main.cpp`, `src/runtime_main.cpp` |
| 0-4 | `src/Editor/Commands/EditorCommand.h`, `src/Editor/Commands/CommandHistory.{h,cpp}`, `src/Editor/Commands/ObjectCommands.{h,cpp}`, `tests/test_hierarchy.cpp`, `tests/test_command_history.cpp` | `src/ECS/GameObject.{h,cpp}`, `src/Editor/Windows/HierarchyWindow.{h,cpp}`, `src/Editor/Editor.{h,cpp}`, `src/Editor/EditorState.{h,cpp}` |
| 0-2 | `src/Core/World.{h,cpp}`, `src/Editor/SceneDocument.h`, `tests/test_world.cpp` | `src/Core/SceneSerializer.{h,cpp}`, `src/main.cpp`, `src/runtime_main.cpp`, `src/Editor/EditorState.{h,cpp}`, `CMakeLists.txt`(샘플 씬 제거) |
| 0-3 | `src/Core/PathService.{h,cpp}`, `src/Core/BuildManifest.{h,cpp}`, `tests/test_path_service.cpp`, `tests/test_game_builder.cpp` | `src/Editor/Project.cpp`, `src/Editor/SceneOperations.cpp`, `src/Editor/GameBuilder.cpp`, `src/runtime_main.cpp`, `src/main.cpp`, `src/Editor/Editor.cpp`, `src/ECS/Components/SpriteRenderer.cpp`, `CMakeLists.txt` |
| 0-5b | `src/Core/SmokeReport.{h,cpp}`, `src/Core/PackageLayout.{h,cpp}`, `tests/SmokeTestSupport.h`, `tests/test_editor_smoke.cpp`, `tests/test_runtime_smoke.cpp`, `tests/test_build_smoke.cpp`, `tests/smoke/*.cmake` | `src/Core/Bootstrap.{h,cpp}`, `src/Editor/GameBuilder.cpp`, `src/main.cpp`, `src/runtime_main.cpp`(`--smoke` 플래그), `tests/CMakeLists.txt`, `.github/workflows/ci.yml` |

---

## 4. 슬라이스 전체 완료 기준 (Definition of Done)

아래가 모두 참이면 이 마일스톤은 끝난다. 각 항목은 담당 작업 파일에서 테스트로 검증된다.

- [ ] **(0-1)** Debug/Release 모두에서 에디터·런타임이 렌더 계약 assertion 없이 실행되고, 100개 이상의 SpriteRenderer가 한 패스에서 렌더된다.
- [ ] **(0-2)** 편집 씬에 추가한 오브젝트·스크립트가 Play에서 그대로 실행되고, Play 중 Transform을 바꿔도 Stop 후 편집 값이 복원된다.
- [ ] **(0-2)** 에디터와 런타임이 동일한 씬 로딩 코드(`World` + `SceneSerializer`)를 사용한다. 하드코딩 `MenuScene/GameScene`는 엔진 실행 흐름에서 제거된다.
- [ ] **(0-3)** 저장소 루트, `build/`, 임의 디렉터리 어디에서 실행해도 에디터·런타임이 동일하게 동작한다.
- [ ] **(0-3)** Build 결과를 임의 디렉터리에서 실행할 수 있고, 프로젝트 텍스처가 에디터/Play/빌드 런타임에서 동일하게 보인다. 필수 파일 누락 시 Build가 실패하고 원인을 표시한다.
- [ ] **(0-4)** 부모 삭제·자식 삭제·계층 전체 삭제·재부모화·cycle 거부 테스트가 모두 통과하고, Hierarchy 조작 후 ASan 실행에 오류가 없다.
- [ ] **(0-4)** 생성/삭제/이름 변경/재부모화가 Undo/Redo되며, 모든 편집이 dirty 상태를 갱신한다.
- [ ] **(0-5)** 의도적으로 실패시킨 검증이 Debug와 Release CI 모두에서 실패한다. editor/runtime/build smoke test가 CI에서 통과한다.

---

## 5. 공통 규칙 (모든 작업에 적용)

1. **TDD 순서 고정:** 실패하는 테스트 작성 → 실패 확인 → 최소 구현 → 통과 확인 → 커밋. 각 단계는 한 동작(2–5분)이다.
2. **빌드/테스트 명령은 프리셋 사용**(0-5a 이후):
   ```bash
   cmake --preset debug
   cmake --build --preset debug -j4
   ctest --preset debug --output-on-failure
   ```
   0-5a 이전에는 기존 방식:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build -j4
   ctest --test-dir build --output-on-failure
   ```
3. **커밋 단위:** 작업(Task) 내부의 각 테스트-통과 지점마다 커밋한다. 커밋 메시지는 `feat:`/`fix:`/`refactor:`/`test:` 접두사를 쓴다.
4. **수정 전 반드시 읽기:** 계획서의 라인 번호는 작성 시점(2026-06-06) 기준이다. 앞 작업이 파일을 바꾸면 라인이 밀린다. 각 수정 전에 대상 파일을 다시 읽어 현재 내용으로 매칭한다.
5. **모듈 경계:** 런타임에서도 쓰는 코드(`World`, `PathService`, `RenderPassState`)는 `molga_core`에, 에디터 전용 코드(`SceneDocument`, `Commands`, `BuildManifest`)는 `EDITOR_SOURCES`에 둔다.

---

## 6. 실행 방식

이 계획서들은 `superpowers:subagent-driven-development`(작업마다 새 서브에이전트 + 단계별 리뷰) 또는 `superpowers:executing-plans`(현재 세션에서 체크포인트 배치 실행)로 실행한다. 권장 순서(§2)를 따른다. 한 작업이 끝날 때마다 해당 파일의 "작업 완료 기준" 체크박스를 검증하고 다음으로 넘어간다.

---

## 7. 이 슬라이스 이후 (Phase 1–3)

이 슬라이스를 통과한 뒤 추가할 항목은 [phase-1-3_roadmap.md](phase-1-3_roadmap.md)에 단계별로 정리되어 있다. 순서는 Undo/Redo UX 완성 → AssetDatabase → Physics(Box2D) → 배칭 순이며, 이 슬라이스가 만든 `World`/`PathService`/`Command` 토대 위에 확장한다. **이 슬라이스 전에 라이팅/포스트프로세싱/프리팹 같은 상위 기능을 먼저 손대지 않는다.** 서로 다른 씬·에셋·렌더 경로 위에 다시 구현하게 되어 재작업이 커진다.
