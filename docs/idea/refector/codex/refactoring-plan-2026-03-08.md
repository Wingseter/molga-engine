# Molga Engine Refactoring Plan

작성일: 2026-03-08

이 문서는 현재 저장소 상태를 다시 분석해서 정리한 실행 계획이다. `docs/refector/*`는 참고할 가치가 있지만 일부 가정은 현재 코드와 다르다. 예를 들어 `SceneSerializer`에는 이미 컴포넌트 팩토리 테이블이 있고, 전체 CMake 빌드는 현재 기준으로 통과한다. 따라서 이번 계획은 "옛 진단 복사"가 아니라 "현재 코드 기준 재정렬"에 초점을 둔다.

## 분석 기준

- 주요 진입점, 에디터, ECS, 씬 직렬화, 프로젝트/빌드, 스크립팅 모듈을 직접 확인했다.
- 2026-03-08 기준 `cmake -S . -B /tmp/molga-build && cmake --build /tmp/molga-build -j4` 빌드는 성공했다.
- 자동화 테스트는 저장소에 없다. `add_test`, `CTest`, 별도 테스트 타깃을 찾지 못했다.

## 현재 구조 요약

- 엔트리 포인트는 에디터용 [`src/main.cpp`](../../src/main.cpp) 와 런타임용 [`src/runtime_main.cpp`](../../src/runtime_main.cpp) 로 분리되어 있다.
- 공용 엔진 코드는 [`CMakeLists.txt`](../../CMakeLists.txt) 의 `ENGINE_SOURCES` 로 묶여 있지만 실제로는 라이브러리 타깃이 아니라 두 실행 파일에 중복 컴파일된다.
- 에디터는 [`src/Editor/Editor.cpp`](../../src/Editor/Editor.cpp) 가 도킹, 메뉴, 씬 작업, 빌드, 스크립팅을 한 번에 조율한다.
- ECS는 [`src/ECS/GameObject.h`](../../src/ECS/GameObject.h) 의 `std::vector<std::unique_ptr<Component>>` 기반이며 조회는 `dynamic_cast` 선형 탐색이다.
- 프로젝트/패키징/스크립트 컴파일은 [`src/Core/Project.cpp`](../../src/Core/Project.cpp), [`src/Core/GameBuilder.cpp`](../../src/Core/GameBuilder.cpp), [`src/Scripting/ScriptCompiler.cpp`](../../src/Scripting/ScriptCompiler.cpp) 에 흩어져 있다.

## 확인된 핵심 문제

### 1. 부트스트랩이 세 갈래로 나뉘어 있다

- 에디터 부트스트랩은 [`src/main.cpp:37`](../../src/main.cpp#L37) 부근의 글로벌 리소스와 [`src/main.cpp:53`](../../src/main.cpp#L53) 이후의 수동 초기화에 들어 있다.
- 런타임도 거의 같은 초기화/루프/정리 코드를 [`src/runtime_main.cpp:37`](../../src/runtime_main.cpp#L37) 부근에서 별도로 갖고 있다.
- 그런데 `Application` 클래스는 이미 또 다른 윈도우 초기화와 루프를 [`src/Core/Application.cpp:16`](../../src/Core/Application.cpp#L16) 부터 구현하고 있지만, 실제 엔트리 포인트에서는 사용되지 않는다.
- 결과적으로 창 생성, GL 초기화, 입력 초기화, 메인 루프, 종료 순서가 세 곳에서 따로 관리된다.

영향:
- 버그 수정이 진입점마다 중복된다.
- 경로 처리, 종료 처리, 입력 처리 정책이 쉽게 어긋난다.
- `Application` 은 유지 비용만 있고 현재는 사실상 데드 코드에 가깝다.

### 2. 리소스 수명과 상태 관리가 여전히 수동적이다

- 에디터와 런타임 모두 `Renderer`, `Shader`, `Camera2D` 를 raw pointer 로 생성/삭제한다. 관련 코드는 [`src/main.cpp:87`](../../src/main.cpp#L87), [`src/main.cpp:252`](../../src/main.cpp#L252), [`src/runtime_main.cpp:113`](../../src/runtime_main.cpp#L113), [`src/runtime_main.cpp:174`](../../src/runtime_main.cpp#L174) 에 있다.
- 오디오는 `ma_engine*`, `ma_sound*` 를 정적으로 들고 수동 해제한다. [`src/Audio.cpp:6`](../../src/Audio.cpp#L6) 부터 [`src/Audio.cpp:159`](../../src/Audio.cpp#L159) 까지가 전부 수동 수명 관리다.
- UI는 [`src/UI.h:99`](../../src/UI.h#L99) 와 [`src/UI.cpp:231`](../../src/UI.cpp#L231) 에서 비소유 raw pointer 컬렉션을 유지한다.

영향:
- 예외 경로나 early return 에서 정리 순서가 깨지기 쉽다.
- 소유권 모델이 코드에 드러나지 않아 유지보수 비용이 높다.
- 나중에 `Application` 기반으로 통합할 때도 수명 모델이 먼저 정리되어야 한다.

### 3. 경로 규칙과 패키징 규칙이 일관되지 않다

- 프로젝트 시스템은 `Assets`, `Scenes`, `ProjectSettings`, `Scripts` 를 사용한다. [`src/Core/Project.cpp:84`](../../src/Core/Project.cpp#L84) 부터 [`src/Core/Project.cpp:162`](../../src/Core/Project.cpp#L162)
- 패키저는 소문자 `assets`, `scenes` 와 `src/Shaders` 를 사용한다. [`src/Core/GameBuilder.cpp:81`](../../src/Core/GameBuilder.cpp#L81) 부터 [`src/Core/GameBuilder.cpp:176`](../../src/Core/GameBuilder.cpp#L176)
- 에디터는 셰이더를 `src/Shaders/default.vert` 에서 읽고, 런타임은 `Shaders/default.vert` 에서 읽는다. [`src/main.cpp:89`](../../src/main.cpp#L89), [`src/runtime_main.cpp:115`](../../src/runtime_main.cpp#L115)
- 프로젝트 브라우저는 프로젝트 루트를 "Assets" 로 가정한다. [`src/Editor/Windows/ProjectBrowserWindow.cpp:23`](../../src/Editor/Windows/ProjectBrowserWindow.cpp#L23)
- `ScriptCompiler` 는 `enginePath` 아래 `external/glm` 를 include 하도록 생성하지만 현재 저장소에는 `external/glm` 디렉터리가 없다. [`src/Scripting/ScriptCompiler.cpp:143`](../../src/Scripting/ScriptCompiler.cpp#L143)

영향:
- macOS 같은 대소문자 비엄격 파일시스템에서는 숨어 있지만, Linux CI/배포에서는 바로 터질 수 있다.
- 빌드 산출물과 프로젝트 디렉터리 규칙이 서로 다르다.
- 스크립트 빌드 경로와 에셋 로딩 경로의 실패 원인을 추적하기 어렵다.

### 4. 에디터는 큰 조율자 하나에 기능이 과집중되어 있다

- [`src/Editor/Editor.cpp`](../../src/Editor/Editor.cpp) 는 500라인이 넘고, 도킹 레이아웃, 메뉴바, 씬 작업, 플레이 컨트롤, 빌드, 스크립팅, 통계창, 윈도우 visibility 를 모두 담당한다.
- `Exit`, `Undo`, `Redo` 는 메뉴만 있고 동작은 비어 있다. [`src/Editor/Editor.cpp:205`](../../src/Editor/Editor.cpp#L205), [`src/Editor/Editor.cpp:211`](../../src/Editor/Editor.cpp#L211)
- Scene 뷰는 실제 렌더 타깃이 아니라 placeholder 사각형이다. [`src/Editor/Editor.cpp:157`](../../src/Editor/Editor.cpp#L157)
- Hierarchy 는 생성/이름변경/복제/삭제가 TODO 로 남아 있다. [`src/Editor/Windows/HierarchyWindow.cpp:22`](../../src/Editor/Windows/HierarchyWindow.cpp#L22), [`src/Editor/Windows/HierarchyWindow.cpp:43`](../../src/Editor/Windows/HierarchyWindow.cpp#L43), [`src/Editor/Windows/HierarchyWindow.cpp:91`](../../src/Editor/Windows/HierarchyWindow.cpp#L91)
- 도킹 레이아웃은 `"Project Browser"` 창을 도킹하지만 실제 창 이름은 `"Project"` 이다. [`src/Editor/Editor.cpp:110`](../../src/Editor/Editor.cpp#L110), [`src/Editor/Windows/ProjectBrowserWindow.cpp:10`](../../src/Editor/Windows/ProjectBrowserWindow.cpp#L10), [`src/Editor/Windows/ProjectBrowserWindow.cpp:16`](../../src/Editor/Windows/ProjectBrowserWindow.cpp#L16)

영향:
- 에디터 기능 추가가 계속 `Editor.cpp` 에 쌓인다.
- 창 이름과 동작 규칙이 코드 상수로 흩어져 있다.
- "에디터가 있는 엔진" 이 아니라 "기능이 모인 단일 클래스" 형태로 굳어질 위험이 있다.

### 5. 씬/게임플레이 계층이 ECS 와 레거시 오브젝트를 혼용한다

- `GameScene` 은 타일맵, 스프라이트, UI, 파티클을 레거시 객체로 유지하면서 일부만 ECS `GameObject` 로도 구성한다. [`src/Scenes/GameScene.cpp:31`](../../src/Scenes/GameScene.cpp#L31) 부터 [`src/Scenes/GameScene.cpp:242`](../../src/Scenes/GameScene.cpp#L242)
- 플레이어는 `playerSprite` 를 직접 움직인 뒤 ECS `Transform` 에 다시 동기화한다. [`src/Scenes/GameScene.cpp:152`](../../src/Scenes/GameScene.cpp#L152) 부터 [`src/Scenes/GameScene.cpp:185`](../../src/Scenes/GameScene.cpp#L185)
- `SceneManager` 는 정적 전역 상태로 동작한다. [`src/Scene.cpp:4`](../../src/Scene.cpp#L4) 부터 [`src/Scene.cpp:75`](../../src/Scene.cpp#L75)

영향:
- 한 기능의 진짜 소스 오브 트루스가 어디인지 불명확하다.
- ECS 를 고쳐도 실제 게임플레이 코드가 그대로면 효과가 제한된다.
- 에디터 모드와 플레이 모드 간 상태 전환 로직이 복잡해진다.

### 6. 직렬화는 동작하지만 확장성이 낮고 데이터 손실 지점이 있다

- `SceneSerializer` 는 현재 `Transform`, `SpriteRenderer`, `BoxCollider2D` 세 가지 타입만 하드코딩 등록한다. [`src/Core/SceneSerializer.cpp:18`](../../src/Core/SceneSerializer.cpp#L18)
- 저장 데이터에는 `id` 가 들어가지만 로드 시 이를 복원하지 않는다. [`src/Core/SceneSerializer.cpp:38`](../../src/Core/SceneSerializer.cpp#L38), [`src/Core/SceneSerializer.cpp:107`](../../src/Core/SceneSerializer.cpp#L107)
- 부모/자식 관계는 저장하지 않는다. `GameObject` 는 계층 구조를 지원하지만 직렬화는 이름, id, active, components 만 저장한다. [`src/ECS/GameObject.h:82`](../../src/ECS/GameObject.h#L82), [`src/Core/SceneSerializer.cpp:38`](../../src/Core/SceneSerializer.cpp#L38)
- 스크립트 컴포넌트는 인스펙터에서 추가할 수 있지만, 로드 시 해당 타입을 복원할 경로가 없다. [`src/Editor/Windows/InspectorWindow.cpp:92`](../../src/Editor/Windows/InspectorWindow.cpp#L92), [`src/Scripting/Script.h:15`](../../src/Scripting/Script.h#L15), [`src/Core/SceneSerializer.cpp:115`](../../src/Core/SceneSerializer.cpp#L115)

영향:
- 씬 저장/로드를 반복할수록 계층과 스크립트 정보가 보존되지 않는다.
- 엔진 기능을 확장할수록 직렬화 수정이 병목이 된다.

### 7. 스크립팅 파이프라인이 취약하다

- `ScriptCompiler` 는 CMake 파일을 문자열로 직접 생성하고, 쉘 명령을 문자열로 조합해 실행한다. [`src/Scripting/ScriptCompiler.cpp:91`](../../src/Scripting/ScriptCompiler.cpp#L91), [`src/Scripting/ScriptCompiler.cpp:223`](../../src/Scripting/ScriptCompiler.cpp#L223)
- 실패 판단이 프로세스 결과뿐 아니라 출력 문자열 `"error"` 탐지에 의존한다. [`src/Scripting/ScriptCompiler.cpp:228`](../../src/Scripting/ScriptCompiler.cpp#L228), [`src/Scripting/ScriptCompiler.cpp:243`](../../src/Scripting/ScriptCompiler.cpp#L243)
- `ScriptManager::ReloadScriptLibraries()` 는 전체 `scriptFactories` 를 비워서 내장 스크립트까지 함께 지운다. [`src/Scripting/ScriptManager.cpp:87`](../../src/Scripting/ScriptManager.cpp#L87)
- `REGISTER_SCRIPT` 는 여전히 raw `new` 를 반환한다. [`src/Scripting/ScriptManager.h:53`](../../src/Scripting/ScriptManager.h#L53)

영향:
- 핫리로드가 엔진 기본 스크립트 등록 상태를 망가뜨릴 수 있다.
- 스크립트 빌드 실패 원인이 플랫폼/쉘/경로/출력 포맷 중 어디인지 분리되지 않는다.

## 리팩토링 원칙

1. 먼저 공통 부트스트랩과 경로 규칙을 정리한다.
2. 그 다음에 에디터 분해를 한다.
3. ECS 성능 개선은 씬 데이터 모델과 직렬화 규칙을 먼저 고정한 뒤 진행한다.
4. 큰 재작성보다 작게 쪼개서 병합 가능한 단위로 진행한다.
5. 각 단계는 "빌드 성공 + 최소 한 개의 검증 루틴 추가"를 완료 조건으로 둔다.

## 권장 실행 순서

### Phase 0. 기준선 확보

목표:
- 리팩토링을 안전하게 진행할 최소 안전장치를 만든다.

작업:
- `molga_core` 같은 공용 라이브러리 타깃을 도입해 `ENGINE_SOURCES` 중복 컴파일 구조를 줄인다.
- `CTest` 를 켜고 최소 smoke test 세트를 추가한다.
- 첫 테스트 후보는 `SceneSerializer`, `Project`, `GameBuilder` 처럼 GUI 의존이 적은 모듈로 잡는다.
- 빌드 경고도 기록한다. 현재 전체 빌드 시 `ld: warning: ignoring duplicate libraries: 'external/glfw/src/libglfw3.a'` 가 발생한다.

완료 기준:
- `cmake --build` 와 최소 smoke test 가 함께 통과한다.

### Phase 1. 부트스트랩 통합과 수명 모델 정리

목표:
- 에디터/런타임/`Application` 중복을 하나의 구조로 정리한다.

작업:
- `Application` 을 실제 진입점에서 사용하도록 올리거나, 반대로 제거한다. 둘 중 하나만 남긴다.
- `main.cpp` 와 `runtime_main.cpp` 의 공통 초기화 코드를 `Bootstrap` 또는 `RuntimeContext` 계층으로 추출한다.
- `Renderer`, `Shader`, `Camera2D`, `Audio` 를 RAII 기반으로 바꾼다.
- `goto cleanup` 와 수동 `delete` 를 제거한다.

완료 기준:
- 에디터와 런타임 모두 공통 초기화 경로를 사용한다.
- raw `new/delete` 가 엔트리 포인트와 `Audio` 에서 사라진다.

### Phase 2. 경로, 프로젝트 규칙, 패키징 규칙 정규화

목표:
- 프로젝트 내부 경로와 배포 산출물 경로를 하나의 규칙으로 맞춘다.

작업:
- `Assets`/`Scenes`/`Scripts` 같은 프로젝트 디렉터리 명과 패키징 산출물 규칙을 명시적으로 구분한다.
- 셰이더, 씬, 에셋, 실행 파일 위치를 상수 또는 설정 객체로 모은다.
- 런타임과 에디터의 셰이더 로딩 경로를 일치시킨다.
- `ScriptCompiler` 가 없는 외부 디렉터리를 가리키지 않도록 include 규칙을 고친다.

완료 기준:
- 대소문자에 민감한 환경에서도 동일 규칙으로 동작한다.
- `Project`, `GameBuilder`, `TextureManager`, `ScriptCompiler` 가 같은 경로 사전을 참조한다.

### Phase 3. 에디터 분해와 UX 완성

목표:
- `Editor.cpp` 를 orchestration 레이어로 축소한다.

작업:
- `WindowManager`, `SceneCommands`, `BuildController`, `ScriptTools`, `PlayModeController` 로 책임을 나눈다.
- 창 제목과 도킹 ID 를 상수화해 도킹 불일치를 없앤다.
- `HierarchyWindow` TODO 항목 중 생성/삭제/복제/이름변경을 실제 명령으로 연결한다.
- Scene View 는 placeholder 대신 실제 렌더 타깃 텍스처 또는 공유 프레임버퍼를 사용하도록 바꾼다.

완료 기준:
- `Editor.cpp` 는 창 등록, 상태 조율, top-level command 호출만 담당한다.
- 현재 비어 있는 메뉴 액션이 모두 실제 동작 또는 비활성 처리로 정리된다.

### Phase 4. 씬 데이터 모델과 ECS 정리

목표:
- "레거시 오브젝트 + ECS 혼합" 상태를 줄이고 씬 모델을 일관화한다.

작업:
- `GameScene` 에서 `playerSprite` 와 ECS `Transform` 이중 상태를 제거한다.
- `SceneManager` 전역 정적 상태를 `SceneService` 또는 `Application` 소유 서비스로 옮긴다.
- `GameObject` 조회 구조를 바로 갈아엎기보다, 먼저 직렬화와 씬 수명 규칙을 고정한 뒤 `dynamic_cast` 제거를 진행한다.
- `Transform` 의 월드 계산은 dirty flag 캐시로 바꾼다.

완료 기준:
- 주요 플레이 오브젝트가 하나의 데이터 모델만 사용한다.
- 씬 전환과 플레이/에디트 전환에서 상태 복제가 명시적이다.

### Phase 5. 직렬화/스크립트 파이프라인 강화

목표:
- 씬 저장/로드와 스크립트 핫리로드를 실제 제품 기능 수준으로 올린다.

작업:
- 컴포넌트 등록을 중앙 registry 로 옮겨 `SceneSerializer` 의 하드코딩을 제거한다.
- 씬 저장 시 부모-자식 관계, 스크립트 타입, 필요한 식별자를 함께 저장한다.
- 스크립트 로드 실패, 빌드 실패, 동적 라이브러리 실패를 분리된 에러 타입으로 다룬다.
- `ScriptManager` 에서 builtin registry 와 dynamic registry 를 분리한다.
- 가능하면 스크립트 컴파일은 쉘 문자열이 아니라 인자 기반 프로세스 실행으로 전환한다.

완료 기준:
- 스크립트가 포함된 씬을 저장 후 다시 열어도 구성 정보가 유지된다.
- 핫리로드 후 builtin script 목록이 사라지지 않는다.

### Phase 6. 품질 마감

목표:
- 리팩토링 이후 유지보수성을 실제로 유지할 수 있게 만든다.

작업:
- 테스트 대상을 `SceneSerializer`, `Project`, `GameBuilder`, `ScriptManager` 까지 확대한다.
- 코드 스타일과 include 정책을 정리한다.
- 로그/에러 리포팅 형식을 통일한다.
- 가능하면 에디터 기능별 수동 QA 체크리스트를 문서화한다.

완료 기준:
- CI 또는 로컬 표준 명령 한 번으로 build + test + 패키징 smoke check 가 가능하다.

## 우선순위 정리

가장 먼저 해야 할 일:
- Phase 0
- Phase 1
- Phase 2

중간 이후로 미뤄도 되는 일:
- `dynamic_cast` 제거 같은 ECS 미세 최적화
- 전체 렌더러 교체
- 외부 라이브러리 교체

## 비권장 접근

- 처음부터 ECS 전체를 다시 쓰는 것
- `Application` 을 유지한 채 `main.cpp` 와 `runtime_main.cpp` 도 계속 따로 키우는 것
- 테스트 없이 Scene/ECS/스크립트 직렬화를 동시에 뜯는 것
- 경로 규칙 정리 전에 패키저 기능부터 더 붙이는 것

## 요약

현재 프로젝트는 "망가진 상태" 는 아니다. 빌드는 되고 기능 축도 분명하다. 다만 엔진, 에디터, 런타임, 프로젝트 시스템이 공통 규칙 없이 병렬 진화한 상태라서, 지금 필요한 것은 대규모 재작성보다 먼저 부트스트랩 통합, 경로 표준화, 에디터 책임 분리, 직렬화/스크립트 신뢰성 보강이다. 이 순서로 가야 이후 ECS 개선도 비용 대비 효과가 나온다.
