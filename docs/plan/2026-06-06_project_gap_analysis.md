# Molga Engine 기능·코드·UI 갭 분석 및 개선 계획

> **For agentic workers:** 구현 시 `superpowers:test-driven-development`와 `superpowers:verification-before-completion`을 사용하고, 아래 단계 순서를 유지한다.

**Goal:** 현재 Molga Engine을 실제 2D 게임을 에디터에서 제작하고 독립 실행 파일로 검증할 수 있는 상태로 만들기 위해, 현재 구현의 부족점과 우선순위를 코드 증거 기반으로 정리한다.

**Architecture:** 새 기능을 계속 추가하기 전에 `편집 중인 씬 = 플레이되는 씬 = 빌드되는 씬`이라는 하나의 수직 워크플로를 먼저 완성한다. 이후 Scene View, 에셋 데이터베이스, 물리, 렌더링 고도화를 같은 World/SceneDocument와 경로 서비스 위에 확장한다.

**Tech Stack:** C++17, CMake/CTest, GLFW, OpenGL 3.3, GLAD, Dear ImGui Docking, nlohmann/json, miniaudio, stb_image

---

## 1. 결론

현재 프로젝트는 **2D 엔진 기능 데모와 ImGui 에디터 셸**로서는 의미 있는 기반이 있다. ECS, 직렬화, 도킹 UI, 프로젝트 선택, 스크립트 컴파일, 빌드 UI, 기본 렌더링·오디오·입력 유틸리티가 각각 존재한다.

하지만 아직 **에디터에서 만든 게임을 플레이하고 빌드하는 게임 엔진 워크플로**는 완성되지 않았다. 가장 큰 문제는 Unity급 고급 기능의 부재가 아니라, 아래 핵심 흐름이 서로 연결되지 않은 점이다.

```text
프로젝트 열기
  -> 씬 작성
  -> Scene View에서 확인·조작
  -> 현재 씬 플레이
  -> Stop 시 편집 상태 복원
  -> 현재 프로젝트 에셋과 씬을 빌드
  -> 독립 런타임에서 동일하게 실행
```

현재 상태에서는 이 흐름 중 프로젝트 열기와 일부 Inspector 편집만 실질적으로 가능하다. 따라서 다음 개발 목표는 기능 수를 늘리는 것이 아니라 **작동하는 단일 수직 슬라이스**를 만드는 것이어야 한다.

### 최우선 P0 요약

| ID | 문제 | 영향 |
|---|---|---|
| P0-1 | `Renderer::Begin/End` 호출 계약 위반 | Debug 실행 중 assertion, Release 렌더 상태 오염 |
| P0-2 | 편집 씬, Play 씬, 샘플 `SceneManager` 씬이 서로 다름 | 에디터에서 만든 내용을 Play로 검증할 수 없음 |
| P0-3 | 경로와 게임 빌드가 현재 작업 디렉터리에 의존 | 빌드 성공 메시지에도 에셋·셰이더·씬이 누락될 수 있음 |
| P0-4 | 부모·자식 관계가 raw pointer이며 삭제 처리 불완전 | dangling pointer, 숨은 오브젝트, 종료 시 크래시 가능 |
| P0-5 | 런타임에서 직렬화된 텍스처를 로드하지 않음 | 빌드 게임에서 텍스처가 사라짐 |
| P0-6 | 테스트 238개 검증식이 모두 `assert` 기반 | Release CI에서는 검증식이 제거되어 거짓 통과 |
| P0-7 | 씬 dirty 추적과 미저장 확인이 불완전 | New/Open/종료 시 사용자 작업 유실 가능 |

---

## 2. 조사 범위와 검증 결과

### 조사 범위

- `src/` 전체 엔진, 런타임, 에디터 구조
- `tests/`와 CI 구성
- 기존 `docs/design`, `docs/ongoing`, `docs/now_going.md`
- 빌드·테스트·직접 실행
- 에디터 UI 구현 코드와 실제 실행 가능 상태

### 실행 검증

| 검증 | 결과 |
|---|---|
| `cmake --build build -j4` | 성공 |
| `ctest --test-dir build --output-on-failure` | 6/6 성공 |
| 별도 Clean Debug 빌드 | 성공 |
| Clean Debug 테스트 | 6/6 성공 |
| `-Wall -Wextra -Wpedantic` 빌드 | 성공, 프로젝트 코드와 서드파티에서 경고 다수 확인 |
| 저장소 루트에서 에디터 직접 실행 | 셰이더 경로 실패 후 렌더러 상태 assertion 확인 |
| `build` 디렉터리에서 에디터 실행 | 프로세스 실행 상태 유지 확인 |
| 픽셀 기반 UI 캡처 | 자동화 세션의 macOS 디스플레이 캡처 제한으로 확보하지 못함 |

직접 실행에서 확인된 대표 오류:

```text
ERROR::SHADER::FILE_NOT_FOUND: Shaders/default.vert
ERROR::SHADER::FILE_NOT_FOUND: Shaders/default.frag
Assertion failed: state == State::Idle && "Renderer::Begin called without matching End()"
```

UI의 색상·간격 같은 픽셀 수준 평가는 제한되었지만, Scene View와 각 에디터 창의 기능 상태는 구현 코드에서 명확히 확인할 수 있다.

### 현재 잘 되어 있는 기반

- `molga_core`, `molga_engine`, `molga_runtime` 타깃 분리
- ImGui Docking과 기본 레이아웃
- 현대적 다크 테마와 Font Awesome 아이콘 폰트
- ECS 컴포넌트 팩토리와 기본 생명주기
- 부모·자식 Transform 월드 좌표 계산
- JSON 씬 직렬화와 기본 라운드트립 테스트
- 고정 시간 accumulator와 타입 기반 EventBus 구현
- 프로젝트 생성·열기, 최근 프로젝트 목록
- 스크립트 템플릿 생성과 CMake 컴파일 기반
- macOS Debug/Release CI 구성

이 기반은 유지할 가치가 있다. 문제는 다수 기능이 **독립 유틸리티 또는 샘플 코드 수준**에 머물고 실제 에디터 제작 흐름에 연결되지 않았다는 점이다.

---

## 3. P0: 먼저 해결해야 하는 결함

### P0-1. 렌더러 호출 계약이 현재 엔트리 포인트와 충돌한다

**증거**

- `src/main.cpp:198-207`과 `src/runtime_main.cpp:130-139`가 전체 오브젝트 렌더 전에 `Renderer::Begin()`을 호출한다.
- `src/ECS/Components/SpriteRenderer.cpp:54-56`도 각 스프라이트마다 다시 `Begin/End`를 호출한다.
- `src/Rendering/Renderer.cpp:69-71`은 중첩 `Begin`을 assertion으로 금지한다.

**영향**

- Debug에서는 첫 SpriteRenderer 렌더 시 즉시 종료될 수 있다.
- Release에서는 assertion이 제거되어 호출은 계속되지만, 외부 렌더 패스가 내부 `End()`로 종료되어 상태 계약이 깨진다.
- 스프라이트마다 셰이더 바인딩과 `Begin/End`가 반복되어 배칭 이전에도 불필요한 오버헤드가 크다.

**개선 방향**

- `SpriteRenderer`는 렌더 데이터만 제출하고 `Begin/End` 소유권은 RenderSystem 또는 RenderPass 하나가 가진다.
- `SpriteRenderer::RenderSprite()`에서 `Begin/End`를 제거한다.
- 편집, 런타임, 파티클, 텍스트, UI의 렌더 패스 경계를 명시한다.

**완료 기준**

- Debug 빌드에서 에디터와 런타임이 assertion 없이 실행된다.
- 100개 이상의 SpriteRenderer를 한 패스에서 렌더한다.
- 렌더 패스 중첩을 검증하는 테스트가 추가된다.

### P0-2. 편집 중인 씬이 Play 모드에서 실행되지 않는다

**증거**

- `src/main.cpp:108-136`은 프로젝트를 연 뒤에도 샘플 Player, Enemy, Ground를 강제로 생성한다.
- `src/main.cpp:138-141`은 별도의 하드코딩된 `MenuScene`, `GameScene`을 등록한다.
- Play 모드에서는 `editorObjects`를 업데이트하지만 렌더링은 `SceneManager::Render()`로 샘플 씬을 표시한다 (`src/main.cpp:169-210`).
- `EditorState::Play/Stop()`은 씬 저장·복원을 TODO로 남긴다 (`src/Editor/EditorState.cpp:28-47`).

**영향**

- Inspector나 Hierarchy에서 만든 결과가 Play 화면과 일치하지 않는다.
- Stop 후 런타임 변경사항이 복원되지 않는다.
- SceneManager 씬과 JSON 씬 모델이 서로 다른 두 개의 시스템으로 존재한다.

**개선 방향**

- `World` 또는 `SceneDocument`를 편집, 플레이, 런타임의 단일 데이터 모델로 만든다.
- Play 진입 시 현재 SceneDocument를 메모리 직렬화하여 PlayWorld를 생성한다.
- Stop 시 PlayWorld를 폐기하고 EditWorld를 그대로 복원한다.
- 샘플 `MenuScene/GameScene`은 엔진 실행 흐름에서 제거하고 예제 프로젝트로 이동한다.

**완료 기준**

- 편집 씬에 추가한 오브젝트와 스크립트가 Play에서 그대로 실행된다.
- Play 중 Transform을 변경해도 Stop 후 편집 값이 복원된다.
- 에디터와 런타임이 동일한 씬 로딩 코드를 사용한다.

### P0-3. 경로 처리와 게임 빌드가 신뢰할 수 없다

**증거**

- 셰이더는 `"Shaders/default.vert"` 같은 현재 작업 디렉터리 상대 경로로 로드된다 (`src/main.cpp:58`, `src/runtime_main.cpp:81`).
- 에디터 엔진 경로는 `current_path().parent_path()`로 추정한다 (`src/Editor/Editor.cpp:50-54`).
- Save/Open은 프로젝트의 `Scenes`가 아닌 고정 `"scene.json"`을 사용한다 (`src/Editor/SceneOperations.cpp:26-45`).
- GameBuilder는 프로젝트 Assets가 아니라 현재 작업 디렉터리의 `"assets"`를 복사한다 (`src/Editor/GameBuilder.cpp:82-90`).
- 셰이더 원본도 현재 작업 디렉터리의 `"src/Shaders"`로 찾는다 (`src/Editor/GameBuilder.cpp:98-106`).
- 필수 파일이 없어도 `CopyAssets`, `CopyShaders`, `CopyScenes`는 성공을 반환한다.
- 런타임 실행 파일 경로가 `"build/molga_runtime"`으로 고정되어 있다 (`src/Editor/GameBuilder.cpp:174-186`).
- 출력 폴더가 존재하면 사용자 입력 경로를 `remove_all()`로 전부 삭제한다 (`src/Editor/GameBuilder.cpp:68-74`).

**영향**

- 실행 위치에 따라 에디터가 작동하거나 실패한다.
- Build 성공 로그가 출력되어도 게임에 프로젝트 에셋, 셰이더, 메인 씬이 없을 수 있다.
- 잘못 입력한 출력 경로로 중요한 폴더를 삭제할 위험이 있다.
- macOS 앱 번들, Windows, Linux 패키징으로 확장하기 어렵다.

**개선 방향**

- 실행 파일 위치, 엔진 루트, 프로젝트 루트, 빌드 출력 루트를 관리하는 `PathService`를 만든다.
- 모든 프로젝트 에셋과 씬 경로는 `Project`를 통해 해석한다.
- 빌드 전 필수 파일 manifest를 만들고 누락 시 실패한다.
- 출력 경로를 canonicalize하고 프로젝트 루트, 엔진 루트, 홈 루트 삭제를 금지한다.
- 런타임은 자신의 실행 파일 위치 기준으로 `game.json`과 리소스를 찾는다.

**완료 기준**

- 저장소 루트, `build`, 임의 디렉터리에서 실행해도 동일하게 동작한다.
- Build 결과를 임의 디렉터리에서 실행할 수 있다.
- 프로젝트 텍스처, 씬, 셰이더 누락 시 Build가 실패하고 원인을 표시한다.

### P0-4. Hierarchy 소유권 모델에 dangling pointer 위험이 있다

**증거**

- GameObject는 부모와 자식을 raw pointer로 보관한다 (`src/ECS/GameObject.h:122-123`).
- 부모 소멸자는 자식의 parent를 해제하지 않고 `children.clear()`만 수행한다 (`src/ECS/GameObject.cpp:13-23`).
- Hierarchy 삭제는 벡터에서 선택 오브젝트 하나만 제거한다 (`src/Editor/Windows/HierarchyWindow.cpp:163-179`).
- `SetParent`는 자기 자신 외 조상으로의 재부모화를 막지 않아 순환 계층을 만들 수 있다 (`src/ECS/GameObject.cpp:25-40`).

**영향**

- 부모 삭제 후 살아 있는 자식이 해제된 부모를 가리킬 수 있다.
- Hierarchy에서 자식이 사라지거나 Transform 재귀 계산·종료 시 크래시할 수 있다.
- 순환 계층은 Transform과 Hierarchy 재귀를 무한 루프로 만든다.

**개선 방향**

- Scene/World가 모든 오브젝트를 ID 기반으로 소유하고, 부모·자식 관계도 ID 또는 검증 가능한 handle로 관리한다.
- 삭제 정책을 `DeleteSubtree` 또는 `UnparentChildren` 중 하나로 명확히 한다.
- 재부모화 시 자기 자신과 모든 자손을 대상으로 cycle 검사를 수행한다.
- 삭제는 지연 명령 큐를 통해 안전한 시점에 처리한다.

**완료 기준**

- 부모 삭제, 자식 삭제, 계층 전체 삭제, 재부모화, cycle 거부 테스트가 모두 통과한다.
- Hierarchy 조작 후 AddressSanitizer 실행에서 오류가 없다.

### P0-5. 런타임 에셋 로딩이 편집기 상태를 재현하지 못한다

**증거**

- `SpriteRenderer::Deserialize()`는 `texturePath`만 저장하고 텍스처를 로드하지 않는다.
- `TextureManager.cpp`는 `molga_core`가 아니라 Editor 소스에만 포함되어 있다 (`CMakeLists.txt:80-104`).
- 런타임은 SceneSerializer로 오브젝트를 로드한 뒤 SpriteRenderer를 그리지만 텍스처 로드 단계가 없다.
- `sortingOrder`는 직렬화·Inspector 편집은 되지만 렌더 순서에서 사용되지 않는다.
- Audio, Animation, Tilemap, Particle은 대부분 ECS 컴포넌트 또는 씬 직렬화 대상이 아니다.

**영향**

- 에디터에서 지정한 텍스처가 빌드 게임에서 표시되지 않는다.
- 문서상 구현된 시스템 다수가 실제 게임 제작 데이터로 저장·실행되지 않는다.

**개선 방향**

- AssetResolver와 runtime용 TextureManager를 `molga_core`로 이동한다.
- SpriteRenderer 역직렬화 이후 asset handle을 resolve하는 단계가 필요하다.
- sortingOrder를 RenderQueue에서 실제로 적용한다.
- AudioSource, Animator, TilemapRenderer 등 제작 가능한 컴포넌트 형태로 시스템을 통합한다.

**완료 기준**

- 프로젝트 텍스처를 지정한 스프라이트가 에디터, Play, 빌드 런타임에서 동일하게 보인다.
- 누락 에셋은 명확한 오류와 fallback 표시를 제공한다.

### P0-6. Release 테스트는 실질적으로 검증하지 않는다

**증거**

- 테스트에는 `assert()`가 238회 사용된다.
- CMake Release 플래그에는 `-DNDEBUG`가 포함된다.
- CI는 Debug와 Release 모두 같은 assert 기반 테스트를 실행한다 (`.github/workflows/ci.yml:13-29`).

**영향**

- Release에서는 검증식이 컴파일에서 제거되어 테스트가 거짓 통과한다.
- Renderer assertion 같은 계약 위반도 Release에서 숨겨진다.

**개선 방향**

- Catch2, doctest 또는 GoogleTest 중 하나로 전환한다.
- Debug/Release 모두 실제 assertion을 수행한다.
- Editor/Runtime smoke test, 빌드 결과 실행 테스트, 직렬화 오류 테스트를 추가한다.
- ASan/UBSan CI 구성을 추가한다.

**완료 기준**

- 의도적으로 실패시킨 검증이 Debug와 Release CI에서 모두 실패한다.
- 최소 테스트 범위가 core unit, scene integration, editor smoke, runtime smoke, build smoke로 확장된다.

### P0-7. 미저장 변경 추적과 데이터 유실 방지가 없다

**증거**

- `SceneOperations::NewScene()`은 확인 없이 오브젝트를 즉시 제거한다.
- dirty 상태는 `Editor::CreateGameObject()` 일부 경로에서만 표시한다.
- Hierarchy 생성·삭제·복제·이름 변경, Inspector 프로퍼티 변경은 dirty 상태를 갱신하지 않는다.
- Exit 메뉴는 실제 종료되지 않으며, 미저장 확인도 없다.
- 메뉴에 표시된 `Ctrl+S`, `Ctrl+N` 등의 단축키는 실제 shortcut 처리 코드가 없다.

**영향**

- 사용자는 변경 여부를 알기 어렵고 New/Open/종료에서 작업을 잃을 수 있다.

**개선 방향**

- 모든 편집 작업을 Command/Undo 시스템을 통해 수행하고 자동으로 dirty 표시한다.
- New/Open/Close/Exit 전에 Save/Discard/Cancel 모달을 제공한다.
- 씬 탭이나 윈도우 제목에 `*` dirty 표시를 제공한다.

---

## 4. 기능적 부족점

### 4.1 코어·씬·ECS

| 부족점 | 현재 상태 | 필요한 결과 |
|---|---|---|
| 단일 World/Scene 모델 | JSON 오브젝트 벡터와 하드코딩 SceneManager가 분리됨 | 편집·Play·런타임이 같은 World 사용 |
| 오브젝트 생성·삭제 수명주기 | 직접 vector push/erase | 지연 생성·삭제와 안전한 handle |
| 업데이트 단계 | FixedUpdate/LateUpdate 함수는 있으나 전체 루프 통합 불완전 | Fixed, Update, Late, EventQueue의 명시적 순서 |
| 컴포넌트 실행 순서 | `unordered_map` 순회로 비결정적 | 명시적 lifecycle/render 순서 |
| EventBus 통합 | EventBus 자체 테스트만 있고 엔진 사용처 없음 | 씬, 물리, 에셋, 에디터 이벤트 연결 |
| 태그·레이어 | 없음 | 검색, 렌더링, 충돌 필터 기반 |
| Prefab | 없음 | 재사용 가능한 오브젝트 그래프와 override |
| 씬 관리 | 하드코딩 씬 전환과 JSON 씬이 별개 | 저장 씬 로드, additive, active scene |

### 4.2 렌더링

| 부족점 | 현재 상태 | 필요한 결과 |
|---|---|---|
| RenderQueue | 직접 순회, sortingOrder 미사용 | 정렬 키 기반 제출·렌더 |
| 배칭 | 스프라이트당 Draw Call | 텍스처/머티리얼 기준 배칭 |
| Material/Shader asset | 기본 셰이더 1개 | Material과 직렬화 가능한 셰이더 프로퍼티 |
| Framebuffer/RenderTexture | 없음 | Scene/Game View와 후처리 기반 |
| 디버그 렌더링 | 없음 | 콜라이더·기즈모·선·그리드 |
| 카메라 시스템 | 단일 기본 카메라 | Camera 컴포넌트, viewport, pixel perfect |
| 리소스 수명 | GL 소유 클래스 복사 방지 미정의 | move-only RAII 리소스 |
| 성능 측정 | FPS만 표시 | Draw Call, vertex, texture bind, GPU time |

### 4.3 물리

현재 물리는 충돌 계산 함수와 BoxCollider2D 데이터만 있다. 자동 충돌 탐색, 응답, Rigidbody2D, trigger enter/stay/exit, layer mask, broad phase, 디버그 표시가 없다. Collider를 추가해도 엔진 루프가 자동으로 충돌을 처리하지 않는다.

권장 방향은 자체 물리 응답을 확장하기보다 Box2D를 통합하고 Molga 컴포넌트와 이벤트로 감싸는 것이다.

### 4.4 에셋

- GUID/meta 파일 없음
- 에셋 이동·이름 변경 시 참조 보존 불가
- importer와 import settings 없음
- 파일 watcher 없음
- 의존성 그래프와 중복 에셋 검사 없음
- 썸네일/프리뷰 생성 없음
- project asset과 engine asset 경계가 불명확
- 빌드에 포함할 에셋을 결정하는 의존성 수집 없음

### 4.5 입력·오디오·스크립팅

**입력**

- GLFW 키 코드 직접 사용
- action map, rebinding, gamepad, dead zone 없음
- ImGui 캡처 여부를 게임 입력 차단에 사용하지 않음
- `glfwPollEvents()`가 프레임 끝에 있고 `Input::Update()`가 scroll 값을 먼저 초기화하므로 scroll 이벤트가 소비 전에 사라질 수 있음

**오디오**

- 전역 Audio API만 존재
- AudioSource/AudioListener 컴포넌트, mixer group, spatial audio, streaming 설정 없음

**스크립팅**

- Compile이 UI 스레드에서 동기 실행되어 에디터가 멈춤
- `isCompiling` 상태는 컴파일 완료 전 화면에 표시될 수 없음
- Hot Reload 버튼은 이미 로드된 라이브러리에 `LoadScriptLibrary()`를 다시 호출하므로 실제 reload가 되지 않으면서 성공으로 보일 수 있음
- 실제 unload/reload를 수행하면 기존 동적 Script 인스턴스의 vtable 수명 문제가 발생할 수 있음
- ABI 버전, 상태 보존, 실패 rollback, 진단 위치 연동이 없음

### 4.6 빌드·플랫폼

- 현재 빌드는 패키저가 아니라 파일 복사기 수준
- 프로젝트 Assets가 아닌 엔진 `assets`를 복사함
- 누락 파일을 오류로 처리하지 않음
- 게임 실행 검증 단계 없음
- macOS `.app`, Windows 배포 폴더, Linux 실행 권한·RPATH 등의 타깃별 패키징 없음
- Linux에서도 실행 파일명에 `.exe`를 붙이는 분기가 존재함
- CI가 macOS만 검증함
- install/package target, 버전 정보, crash report, release symbols 없음

---

## 5. 코드 구조와 품질 부족점

### 5.1 엔트리 포인트에 책임이 과도하게 집중됨

`src/main.cpp`와 `src/runtime_main.cpp`가 초기화, 입력, 시간, fixed update, 씬 업데이트, 렌더링, UI, 종료를 직접 조정한다. 두 루프가 이미 서로 다르게 발전하고 있어 기능 추가 시 동작 차이가 커질 가능성이 높다.

권장 구조:

```text
Application
  -> EngineContext
  -> World
  -> SystemScheduler
  -> RenderPipeline
  -> EditorHost 또는 RuntimeHost
```

### 5.2 모듈 경계가 불명확함

- `Core/TextureManager.cpp`가 `Editor/Project.h`에 의존한다.
- `SpriteRenderer.cpp`도 Editor Project를 include한다.
- TextureManager 구현은 Editor 타깃 소스에만 포함된다.
- 샘플 Scene 코드가 Editor 실행 파일에 포함되어 실제 편집 씬과 경쟁한다.

Core는 프로젝트 파일 시스템을 직접 알기보다 `AssetResolver` 인터페이스를 받아야 한다.

### 5.3 직렬화 안정성이 부족함

- 로드 시 전체 문서를 검증하기 전에 기존 objects를 먼저 `clear()`한다.
- parse error 외 type error, schema 오류의 복구 전략이 부족하다.
- 알 수 없는 컴포넌트는 로그 후 데이터가 유실된다.
- version 필드는 기록하지만 migration이 없다.
- 저장이 임시 파일 + atomic rename 방식이 아니어서 쓰기 실패 시 손상될 수 있다.
- 씬 이름이 항상 `"Untitled Scene"`으로 저장된다.

### 5.4 리소스와 소유권 규칙이 부족함

- Shader, Texture, Renderer는 GL 리소스 destructor를 가지지만 복사 생성/대입을 명시적으로 금지하지 않았다.
- SpriteRenderer는 Texture raw pointer를 보관하여 TextureManager unload 시 dangling pointer가 될 수 있다.
- GameObject 계층도 raw pointer에 의존한다.

### 5.5 오류·진단 체계가 부족함

- 로그가 stdout/stderr에만 기록되어 Editor Console과 연결되지 않는다.
- 빌드, 에셋 로드, 스크립트 컴파일 오류에 구조화된 진단 정보가 없다.
- 사용자에게 toast, status bar, 문제 목록 형태로 전달되지 않는다.
- assertion에 지나치게 의존하여 Release에서 오류가 숨겨진다.

### 5.6 테스트와 개발 도구가 부족함

- Editor 창, 프로젝트 흐름, 빌드 결과, 런타임 실행 테스트가 없음
- 렌더러 상태 계약 테스트가 없음
- Hierarchy 삭제·cycle·재부모화 테스트가 없음
- 누락/손상 에셋, 손상 씬, schema migration 테스트가 없음
- 성능 benchmark와 회귀 기준이 없음
- 프로젝트 CMake에 기본 warning policy, sanitizer preset, formatter/linter 설정이 없음

---

## 6. UI·UX 부족점

### 현재 UI의 장점

- Docking과 multi-viewport가 활성화되어 있다.
- 기본 레이아웃이 Hierarchy / Scene / Inspector / 하단 창으로 구성되어 익숙하다.
- 다크 테마, 색상 팔레트, Font Awesome 아이콘을 적용했다.
- 프로젝트 선택 창과 Project Browser의 기본 뼈대가 있다.

### 핵심 UI 문제

#### 6.1 Scene View가 실제 편집 도구가 아니다

`src/Editor/Windows/SceneViewWindow.cpp`는 현재 크기 텍스트와 빈 사각형만 그린다. 다음 기능이 전부 없다.

- 씬 렌더링
- 편집 카메라 pan/zoom/focus
- 클릭 선택
- 이동·회전·스케일 기즈모
- grid와 snap
- collider/debug overlay
- drag-and-drop 생성

Unity형 GUI를 목표로 한다면 다른 창보다 Scene View가 먼저 완성되어야 한다.

#### 6.2 Game View와 Scene View가 분리되어 있지 않다

- 중앙에는 placeholder Scene 창만 있다.
- 플레이 결과를 보여주는 별도 Game View가 없다.
- 해상도·aspect ratio·scale 선택이 없다.
- Play 상태와 편집 상태의 시각적 구분이 약하다.

#### 6.3 툴바와 아이콘 자산이 실제 UI에 연결되지 않았다

- Play/Pause/Stop은 `" > Play "`, `" || Pause "`, `" [] Stop "` 텍스트 버튼이다.
- `ui_images/`의 36개 이미지 에셋은 코드에서 사용되지 않는다.
- Move/Rotate/Scale, local/global, pivot/center 도구가 없다.
- 메뉴에 단축키 문자열은 표시되지만 실제 단축키 처리가 없다.

#### 6.4 Hierarchy가 기본 트리 표시 이상으로 확장되지 않았다

- drag-and-drop 재부모화 없음
- multi-select 없음
- visibility/lock/prefab 상태 없음
- 검색은 root 이름에만 적용되어 일치하는 child를 찾기 어렵다
- Sprite/Tilemap 생성 context menu는 클릭해도 동작하지 않는다
- 삭제와 복제가 계층과 전체 컴포넌트를 안전하게 처리하지 않는다

#### 6.5 Inspector가 편집 안정성을 제공하지 않는다

- 컴포넌트 remove/reset/copy/paste/reorder 없음
- multi-object editing 없음
- validation, tooltip, range, unit 표현이 부족함
- 컴포넌트 순서가 `unordered_map` 순서에 의존하여 안정적이지 않음
- 변경이 dirty/Undo 시스템과 연결되지 않음
- Add Component 검색과 카테고리화가 부족함

#### 6.6 Project Browser는 파일 보기 수준이다

- 아이콘은 이미지 썸네일이 아닌 색상 버튼 placeholder
- create/import/rename/delete/move/duplicate 없음
- 파일 watcher가 없어 수동 Refresh 필요
- scene, script, audio, prefab의 실제 에디터 동작이 부족함
- context menu는 Refresh와 hidden file 표시만 제공
- 폴더 트리 전체를 재귀 스캔하여 대형 프로젝트에서 느려질 수 있음

#### 6.7 Console과 Profiler가 없다

- Stats는 FPS, delta time, frame number만 표시한다.
- Editor Console이 없어 오류를 터미널에서 찾아야 한다.
- 로그 필터, stack trace, double-click navigation, clear/collapse 없음
- CPU/GPU frame breakdown, draw call, 메모리, asset load 통계 없음

#### 6.8 상태 피드백과 데이터 보호가 부족하다

- 저장 성공/실패, 빌드 진행, 스크립트 컴파일 진행이 일관된 status bar나 toast로 표시되지 않는다.
- 동기 컴파일·빌드 중 UI가 응답하지 않을 수 있다.
- 씬 dirty 표시와 미저장 확인 모달이 없다.
- 사용자 설정, UI scale, 키맵, 레이아웃 저장 정책이 없다.

### UI 사용 흐름별 현재 막힘

| 사용자 목표 | 현재 막히는 지점 |
|---|---|
| 새 프로젝트를 만들어 게임 시작 | 프로젝트를 열어도 샘플 오브젝트가 주입됨 |
| 오브젝트를 화면에서 배치 | Scene View가 placeholder라 불가능 |
| Play로 현재 씬 확인 | 하드코딩 샘플 SceneManager가 렌더됨 |
| Stop 후 편집 계속 | Play 상태 복원 없음 |
| 씬 저장 | 파일 선택 없이 작업 디렉터리의 `scene.json` 사용 |
| 텍스처 포함 게임 빌드 | project asset과 runtime texture resolve가 연결되지 않음 |
| 오류 해결 | Console과 문제 위치 이동 없음 |

---

## 7. 기존 문서와 실제 구현의 차이

기존 설계 문서는 방향성 자료로는 유용하지만 현재 완료 상태를 판단하는 근거로 사용하기에는 일부 과장이 있다.

| 기존 표현 | 실제 확인 결과 |
|---|---|
| 에디터가 씬 편집 지원 | Scene View는 placeholder이며 시각적 편집 불가 |
| 독립 실행 게임 빌드 | 프로젝트 에셋·셰이더 누락을 성공으로 처리할 수 있음 |
| 모든 경로에 `fs::exists()` 검증 | 존재하지 않아도 복사 단계가 성공을 반환하는 곳이 있음 |
| C++ 핫리로드 | UI의 Hot Reload는 이미 로드된 라이브러리를 실제 reload하지 않음 |
| SpriteRenderer sorting order | 값은 존재하지만 렌더 순서에 적용되지 않음 |
| 이벤트 시스템 완료 | 구현·단위 테스트는 있으나 엔진 루프와 시스템에서 사용되지 않음 |
| 구현된 Audio/Animation/Tilemap/Particle | API와 샘플은 있으나 에디터 제작·직렬화·런타임 흐름 통합이 부족 |
| 테스트 통과 | Debug 단위 테스트는 통과하지만 Release에서는 assert 검증이 제거됨 |

향후 문서는 각 기능을 다음 상태로 구분하는 것이 적절하다.

```text
Designed -> Utility Implemented -> Engine Integrated -> Editor Authorable
         -> Serialized -> Runtime Verified -> Build Verified
```

---

## 8. 권장 실행 계획

### Phase 0. 작동하는 수직 슬라이스 만들기

**목표:** `프로젝트 생성 -> 스프라이트 배치 -> 저장 -> Play -> Stop -> Build -> Runtime 실행`을 하나의 씬과 에셋으로 끝까지 성공시킨다.

#### Task 0-1. 렌더러 계약 정상화

**주요 파일**

- Modify: `src/Rendering/Renderer.h`
- Modify: `src/Rendering/Renderer.cpp`
- Modify: `src/ECS/Components/SpriteRenderer.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Test: `tests/test_renderer_contract.cpp`

**작업**

- [ ] SpriteRenderer 내부 `Begin/End` 제거
- [ ] RenderPass가 프레임 단위 호출 경계를 소유
- [ ] Debug/Release 모두에서 계약 위반을 오류로 검증
- [ ] 렌더 smoke test 추가

**검증**

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/molga_engine <test-project>
```

#### Task 0-2. SceneDocument와 PlayWorld 통합

**주요 파일**

- Create: `src/Core/World.h`
- Create: `src/Core/World.cpp`
- Create: `src/Editor/SceneDocument.h`
- Create: `src/Editor/SceneDocument.cpp`
- Modify: `src/main.cpp`
- Modify: `src/runtime_main.cpp`
- Modify: `src/Editor/EditorState.cpp`
- Modify: `src/Core/SceneSerializer.cpp`

**작업**

- [ ] editorObjects와 하드코딩 SceneManager 이중 구조 제거
- [ ] Play 진입 시 EditWorld 복제
- [ ] Stop 시 PlayWorld 폐기
- [ ] 샘플 GameScene/MenuScene를 예제 프로젝트로 이동
- [ ] Fixed/Update/Late/EventQueue 순서를 하나의 scheduler로 통합

**검증 시나리오**

1. 편집 씬 Transform 값을 기록한다.
2. Play 중 Script로 값을 변경한다.
3. Stop 후 원래 값이 복원되는지 확인한다.
4. 같은 씬을 runtime에서 실행해 동일한 초기 상태를 확인한다.

#### Task 0-3. 경로 서비스와 빌드 검증

**주요 파일**

- Create: `src/Core/PathService.h`
- Create: `src/Core/PathService.cpp`
- Modify: `src/Editor/Project.cpp`
- Modify: `src/Editor/SceneOperations.cpp`
- Modify: `src/Editor/GameBuilder.cpp`
- Modify: `src/runtime_main.cpp`
- Test: `tests/test_game_builder.cpp`

**작업**

- [ ] 엔진·실행 파일·프로젝트·출력 경로를 절대 경로로 관리
- [ ] Save/Open을 프로젝트 `Scenes` 아래로 제한
- [ ] project Assets와 dependency manifest를 빌드에 포함
- [ ] 필수 파일 누락 시 Build 실패
- [ ] 위험한 output path 삭제 방지
- [ ] runtime이 실행 파일 위치 기준으로 리소스 탐색

#### Task 0-4. 안전한 Hierarchy와 Command 기반 편집

**주요 파일**

- Modify: `src/ECS/GameObject.h`
- Modify: `src/ECS/GameObject.cpp`
- Modify: `src/Editor/Windows/HierarchyWindow.cpp`
- Create: `src/Editor/Commands/EditorCommand.h`
- Create: `src/Editor/Commands/CommandHistory.h`
- Create: `src/Editor/Commands/CommandHistory.cpp`
- Test: `tests/test_hierarchy.cpp`

**작업**

- [ ] ID/handle 기반 계층 관계로 변경
- [ ] cycle 방지
- [ ] subtree 삭제 정책 구현
- [ ] 생성·삭제·이름 변경·재부모화를 Undo 가능한 Command로 처리
- [ ] 모든 편집에서 dirty 상태 갱신

#### Task 0-5. 실제 테스트 체계 구축

**주요 파일**

- Modify: `tests/CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`
- Create: `CMakePresets.json`
- Create: `tests/test_editor_smoke.cpp`
- Create: `tests/test_runtime_smoke.cpp`

**작업**

- [ ] assert 기반 테스트를 테스트 프레임워크로 이전
- [ ] Release에서도 검증이 유지되는지 확인
- [ ] ASan/UBSan preset 추가
- [ ] editor/runtime/build smoke test 추가
- [ ] warning policy와 warnings-as-errors 범위 정의

### Phase 1. 에디터 제작 경험 MVP

**목표:** 마우스로 씬을 편집하고, 실수를 복구하며, 오류를 에디터 안에서 해결할 수 있게 한다.

- [ ] FBO 기반 Scene View
- [ ] 별도 Game View와 해상도/aspect 선택
- [ ] pan/zoom/focus, picking, Move/Rotate/Scale gizmo
- [ ] grid/snap과 collider/debug overlay
- [ ] Undo/Redo 전 편집 작업 적용
- [ ] 실제 단축키 처리
- [ ] 파일 dialog와 미저장 확인
- [ ] Console 창과 구조화 로그 sink
- [ ] Project Browser create/import/rename/delete/move
- [ ] Inspector component remove/reset/copy/paste/search
- [ ] 비동기 스크립트 컴파일과 진단 목록
- [ ] `ui_images`를 실제 툴바·파일·상태 아이콘에 연결하거나 제거

### Phase 2. 실제 게임 제작 기능 MVP

**목표:** 간단한 2D 액션 게임을 하드코딩 샘플 코드 없이 에디터에서 제작한다.

- [ ] GUID/meta 기반 AssetDatabase와 importer
- [ ] RenderQueue, sorting layer, sprite batching
- [ ] Material/Shader asset
- [ ] Box2D 기반 PhysicsWorld, Rigidbody2D, Collider 이벤트
- [ ] Input Action Map과 gamepad
- [ ] AudioSource/AudioListener 컴포넌트
- [ ] Animator/AnimationClip asset과 컴포넌트
- [ ] Tilemap component와 Tile Palette 편집기
- [ ] Prefab과 override
- [ ] 태그·레이어·collision matrix

### Phase 3. 배포와 생산성

- [ ] Windows/Linux CI와 패키징
- [ ] macOS `.app` 번들
- [ ] Profiler와 frame capture 지표
- [ ] 다중 씬/additive loading
- [ ] 빌드 프로파일과 프로젝트 설정
- [ ] crash report와 structured diagnostics
- [ ] 문서·예제 프로젝트·튜토리얼

---

## 9. Unity형 2D 에디터 MVP 완료 정의

다음 조건을 만족하기 전에는 Unity급 또는 완성된 에디터라고 표현하지 않는 것이 적절하다.

- [ ] 새 프로젝트가 빈 씬 또는 선택한 템플릿으로 열린다.
- [ ] Scene View에서 오브젝트를 보고 선택·이동·회전·스케일할 수 있다.
- [ ] Hierarchy와 Inspector의 모든 변경을 Undo/Redo할 수 있다.
- [ ] 텍스처를 import하고 SpriteRenderer에 지정할 수 있다.
- [ ] 현재 편집 씬을 Play하면 같은 내용이 실행된다.
- [ ] Stop하면 편집 상태가 정확히 복원된다.
- [ ] 저장 후 다시 열어도 계층·컴포넌트·에셋 참조가 유지된다.
- [ ] Build 결과가 다른 작업 디렉터리에서 독립 실행된다.
- [ ] Console에서 엔진·스크립트·에셋 오류를 확인하고 위치로 이동할 수 있다.
- [ ] Debug/Release와 최소 2개 플랫폼에서 자동 테스트를 통과한다.

---

## 10. 바로 다음 마일스톤 권장안

다음 마일스톤 이름은 **“Playable Editor Vertical Slice”**가 적절하다.

범위는 아래로 제한한다.

1. Renderer 중첩 호출 제거
2. 하나의 JSON 씬을 Edit/Play/Runtime이 공동 사용
3. Runtime texture resolve
4. 프로젝트 기준 Save/Open/Build 경로
5. Scene View에 최소 FBO 출력과 선택
6. Stop 복원
7. Build 결과 자동 실행 smoke test

이 마일스톤을 통과한 뒤에 Undo/Redo, AssetDatabase, Physics, 배칭을 순서대로 추가해야 한다. 현재 상태에서 2D lighting, post-processing, prefab 같은 상위 기능을 먼저 추가하면 서로 다른 씬·에셋·렌더 경로 위에 다시 구현하게 되어 재작업 가능성이 높다.
