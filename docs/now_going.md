# Molga Engine - 프로젝트 현황

## 프로젝트 개요

**Molga Engine**은 C++17로 개발 중인 커스텀 2D 게임 엔진이다. 에디터(Editor)와 런타임(Runtime) 두 가지 실행 파일로 구성되며, Entity-Component-System(ECS) 아키텍처를 기반으로 한다.

SDL3 전환 범위와 그래픽 지원 주장 기준은
[`docs/plan/2026-08-16_sdl3_platform_migration.md`](plan/2026-08-16_sdl3_platform_migration.md)를 따른다.

- **에디터**: ImGui 기반의 GUI 에디터로, 씬 편집, 오브젝트 관리, 프로퍼티 인스펙션 지원
- **런타임**: 에디터 없이 빌드된 게임을 독립 실행하는 플레이어

## 기술 스택

| 항목 | 기술 |
|------|------|
| 언어 | C++17 |
| 빌드 시스템 | CMake (>= 3.27), CTest |
| 플랫폼/그래픽스 | SDL 3.4.14, OpenGL 3.3 Core + GLAD (생산), SDL_GPU capability gate |
| UI (에디터) | Dear ImGui (Docking) |
| 오디오 | miniaudio |
| JSON | nlohmann/json |
| 이미지 로딩 | stb_image |
| 플랫폼 | macOS 로컬 검증, Linux/Windows CI 게이트 구성 |

## 디렉토리 구조

```
src/
├── main.cpp              # 에디터 진입점
├── runtime_main.cpp       # 런타임 진입점
├── Common/               # 공통 유틸리티
│   ├── Types.h           # Vector2, Color, AABB, Circle 등
│   ├── Constants.h       # PI, TWO_PI, COLLISION_EPSILON 등 수학/물리 상수
│   ├── Log.h/cpp         # 통일된 로깅 유틸리티 (Info/Warn/Error)
│   └── linmath.h         # 행렬/벡터 수학 라이브러리
├── Core/                 # 엔진 코어
│   ├── Bootstrap.h/cpp   # SDL3 EngineHost/이벤트/그래픽 수명주기
│   ├── Scene.h/cpp       # Scene 기본 클래스 + SceneManager
│   ├── MolgaTime.h/cpp   # 프레임 타이밍 관리
│   ├── SceneSerializer.h/cpp  # JSON 씬 직렬화
│   ├── TextureManager.h/cpp   # 텍스처 캐싱
│   └── PathConstants.h   # 경로 상수 (Project/Build/Engine/Config)
├── Rendering/            # 렌더링 시스템
│   ├── Renderer.h/cpp    # OpenGL 렌더 파이프라인 (상태 머신)
│   ├── GraphicsDevice.h/cpp # OpenGL context 및 SDL_GPU capability 경계
│   ├── Shader.h/cpp      # GLSL 셰이더 (uniform 캐싱)
│   ├── Texture.h/cpp     # OpenGL 텍스처
│   ├── Sprite.h/cpp      # 2D 스프라이트
│   ├── SpriteSheet.h/cpp # 스프라이트시트 아틀라스
│   ├── Camera2D.h/cpp    # 2D 카메라
│   ├── TextRenderer.h/cpp # 텍스트 렌더링
│   ├── Animation.h/cpp   # 프레임 애니메이션
│   └── Tilemap.h/cpp     # 타일맵
├── Systems/              # 엔진 시스템
│   ├── Audio.h/cpp       # 오디오 (miniaudio)
│   ├── Input.h/cpp       # 엔진 소유 키/마우스/게임패드 입력과 action schema v2
│   └── Particle.h/cpp    # 파티클 이미터
├── Physics/              # 물리/충돌
│   └── Collision.h/cpp   # AABB, Circle, 포인트 충돌 감지
├── UI/                   # 런타임 UI
│   └── UI.h/cpp          # Panel, Button, ProgressBar
├── ECS/                  # Entity-Component-System
│   ├── Component.h/cpp   # 컴포넌트 기본 클래스 (생명주기, 직렬화)
│   ├── GameObject.h/cpp  # 게임 오브젝트 (O(1) 컴포넌트 조회)
│   ├── ComponentFactory.h # 컴포넌트 자동 등록 팩토리
│   └── Components/
│       ├── Transform.h/cpp      # 위치/회전/스케일
│       ├── SpriteRenderer.h/cpp # 스프라이트 렌더링
│       └── BoxCollider2D.h/cpp  # 박스 충돌체
├── Editor/               # 에디터 시스템
│   ├── Editor.h/cpp           # 에디터 코디네이터 (메뉴, 플레이 컨트롤)
│   ├── WindowManager.h/cpp    # 윈도우 등록/가시성/렌더링 관리
│   ├── SceneOperations.h/cpp  # 씬 CRUD (New/Save/Open)
│   ├── BuildManager.h/cpp     # 빌드 설정 UI + 빌드 실행
│   ├── EditorState.h/cpp      # Edit/Play/Pause 모드 관리
│   ├── EditorConstants.h      # UI 문자열 상수 (윈도우명, 파일명)
│   ├── EditorTheme.h          # 스타일 상수 (버튼/파일타입 색상)
│   ├── UIRegistry.h/cpp       # 파일타입/컴포넌트 아이콘 통합 레지스트리
│   ├── FontManager.h/cpp      # 에디터 폰트/아이콘 관리
│   ├── ImGuiLayer.h/cpp       # ImGui 초기화/테마
│   ├── VSCodeIntegration.h/cpp # VS Code 연동
│   ├── Project.h/cpp          # 프로젝트 관리 (생성/열기/닫기)
│   ├── GameBuilder.h/cpp      # 독립 실행 게임 빌드
│   └── Windows/
│       ├── EditorWindow.h        # 윈도우 기본 클래스
│       ├── HierarchyWindow.h/cpp # 오브젝트 트리 (생성/삭제/복제/이름변경)
│       ├── InspectorWindow.h/cpp # 컴포넌트 인스펙터
│       ├── ProjectBrowserWindow.h/cpp # 파일 브라우저
│       ├── ProjectWindow.h/cpp   # 프로젝트 선택 화면
│       ├── ScriptWindow.h/cpp    # 스크립트 관리
│       ├── SceneViewWindow.h/cpp # 씬 뷰포트
│       └── StatsWindow.h/cpp    # FPS/통계
├── Scripting/            # 스크립팅
│   ├── Script.h/cpp           # 스크립트 기본 클래스
│   ├── ScriptManager.h/cpp    # builtin/dynamic 이중 레지스트리
│   ├── ScriptCompiler.h/cpp   # CMake 기반 스크립트 컴파일
│   └── BuiltinScripts.h/cpp   # PlayerController, Rotator, Oscillator
├── Scenes/               # 예제 씬
│   ├── GameScene.h/cpp
│   └── MenuScene.h/cpp
├── Platform/             # 플랫폼 추상화
│   └── Platform.h/cpp
└── Shaders/              # GLSL 셰이더
    ├── default.vert
    └── default.frag

external/           # 서드파티 (SDL/imgui submodule, glad, miniaudio, nlohmann_json, stb)
assets/             # 게임 에셋
tests/              # CTest 81개 (unit/platform/gpu/smoke/e2e)
ui_images/          # 에디터 UI 아이콘 리소스
docs/               # 리팩토링 계획 및 현황 문서
```

## 빌드 구조

```
CMakeLists.txt
├── molga_core (STATIC)    # 엔진 코어 라이브러리
│   └── Rendering/, Systems/, Physics/, UI/, ECS/, Core/, Common/, Scripting/, Platform/
├── molga_engine (EXE)     # 에디터 실행 파일
│   └── molga_core + imgui + Editor/ + Scenes/
├── molga_runtime (EXE)    # 런타임 실행 파일
│   └── molga_core
└── tests/                 # CTest 테스트
    ├── test_types
    ├── test_collision
    ├── test_ecs
    └── test_scene_serializer
```

## 구현된 기능

### 1. 코어 엔진 시스템

#### Bootstrap / EngineHost
- SDL3 윈도우와 RAII 플랫폼 수명주기
- 생산 OpenGL 3.3 context 및 SDL_GPU native capability device 생성
- GLAD 로더 초기화
- 알파 블렌딩 활성화
- Time, Input, Audio 서브시스템 초기화/종료
- high-DPI 논리/픽셀 크기, 포커스, main/detached close 이벤트 처리

#### Renderer (상태 머신 기반)
- OpenGL 3.3 Core Profile 렌더링 파이프라인
- Quad 기반 2D 스프라이트 렌더링 (VAO/VBO)
- Begin/DrawSprite/End 패턴 (State assertion으로 잘못된 호출 순서 방지)
- 알파 블렌딩, UV 매핑, 텍스처/색상 모드 전환

#### Shader (Uniform 캐싱)
- GLSL 버텍스/프래그먼트 셰이더 로딩 및 컴파일
- `unordered_map` 기반 uniform location 캐싱 (매 프레임 GPU 쿼리 제거)
- SetInt, SetFloat, SetVec2/3/4, SetMat4, SetBool 유니폼 메서드

#### Camera2D
- 2D 카메라 (위치, 줌, 회전)
- View/Projection 매트릭스 자동 갱신 (dirty flag)
- 줌 범위 제한 (`Constants::Camera::MIN_ZOOM` / `MAX_ZOOM`)

#### Time
- 프레임 Delta Time, FPS, FrameCount 관리

#### Input
- 엔진 소유 `KeyCode` 기반 키보드 입력 (GetKey, GetKeyDown, GetKeyUp)
- window-ID 기반 마우스 입력 (버튼, 위치, 델타, 스크롤)
- SDL 표준 게임패드 hotplug/button/axis 입력
- symbolic action schema v2와 명시적 `molga_migrate input` 변환 도구
- 프레임 단위 이전 상태 추적

#### Log 유틸리티
- 통일된 로깅 API: `Log::Info(tag, msg)`, `Log::Warn(tag, msg)`, `Log::Error(tag, msg)`
- `[태그] [LEVEL] 메시지` 포맷으로 stdout/stderr 출력

#### PathConstants
- `Paths::Project` — 에디터 프로젝트 디렉토리 상수 (Assets, Scenes, Scripts 등)
- `Paths::Build` — 빌드 출력 경로 상수
- `Paths::Engine` — 엔진 셰이더 경로 상수
- `Paths::Config` — 사용자 설정 디렉토리 상수 (.molga)

#### Constants
- `Constants::PI`, `TWO_PI`, `DEG_TO_RAD` — 수학 상수
- `Constants::COLLISION_EPSILON` — 충돌 감지 엡실론
- `Constants::Camera::MIN_ZOOM`, `MAX_ZOOM` — 카메라 줌 범위

### 2. ECS (Entity-Component-System)

#### GameObject
- 고유 ID 기반 엔티티 (`SetID`로 직렬화 시 ID 복원)
- **O(1) 컴포넌트 조회** (`ComponentTypeID` + `unordered_map`)
- 부모-자식 계층 구조 (2-pass 직렬화로 관계 복원)
- Active 상태 관리
- Update/Render 루프

#### Component (기본 클래스)
- OnAttach/OnDetach 생명주기
- Update/Render 가상 함수
- Serialize/Deserialize (JSON 직렬화)
- OnInspectorGUI (에디터 인스펙터 연동)
- Enable/Disable 토글 (직렬화 지원)

#### ComponentFactory
- 매크로 기반 컴포넌트 자동 등록 (`REGISTER_COMPONENT`)
- 문자열 이름으로 컴포넌트 동적 생성 (역직렬화 지원)

#### Transform 컴포넌트
- Position (Vector2), Rotation, Scale
- 월드 좌표 변환 (GetWorldPosition/Rotation/Scale)
- Translate 유틸리티
- 직렬화/인스펙터 GUI 연동

#### SpriteRenderer 컴포넌트
- 텍스처 기반 스프라이트 렌더링
- 색상(RGBA) 틴팅, FlipX/FlipY
- Sorting Order
- 텍스처 경로 기반 로딩 (TextureManager 연동)
- 직렬화/인스펙터 GUI 지원

#### BoxCollider2D 컴포넌트
- AABB 충돌 감지
- Size/Offset 설정, Trigger 모드
- 월드 좌표 AABB 계산

### 3. 렌더링 & 그래픽스

#### Texture / TextureManager
- stb_image 기반 이미지 로딩, OpenGL 텍스처 관리
- TextureManager: 텍스처 캐싱 및 중복 로딩 방지

#### Sprite / SpriteSheet / Animation
- 스프라이트: 텍스처/색상 기반 2D 표현
- 스프라이트시트: 아틀라스에서 프레임 추출
- 애니메이션: 프레임 기반, Play/Pause/Stop/Reset, 루프/비루프

#### TextRenderer
- 빌트인 폰트 기반 텍스트 렌더링

#### Particle System
- ParticleEmitter: 위치 기반 파티클 방출, Burst 모드
- ParticleConfig: 스폰 레이트, 속도, 각도, 중력, 크기, 회전, 수명, 색상 설정
- 프리셋: Fire, Smoke, Spark, Snow, Explosion

#### Tilemap
- 타일 기반 맵 시스템, Solid 타일 충돌
- 월드-타일 좌표 변환, SpriteSheet 기반 렌더링

### 4. 물리 & 충돌

#### Collision
- AABB vs AABB, Circle vs Circle, AABB vs Circle 충돌 감지
- Point-in-AABB / Point-in-Circle 테스트
- 충돌 결과(방향, 깊이) 반환

### 5. 오디오

#### Audio
- miniaudio 기반 오디오 엔진
- 사운드 이펙트 / 배경 음악 로드/재생/정지/일시정지/재개
- 마스터/음악 볼륨 제어

### 6. 스크립팅 시스템

#### Script (기본 클래스)
- Unity 스타일 생명주기: Start, Update, FixedUpdate, LateUpdate
- 활성화 콜백: OnEnable, OnDisable
- 충돌 콜백: OnCollisionEnter/Stay/Exit, OnTriggerEnter/Stay/Exit

#### ScriptManager (builtin/dynamic 이중 레지스트리)
- **builtin 레지스트리**: 엔진 내장 스크립트 (핫리로드 시 보존)
- **dynamic 레지스트리**: 사용자 스크립트 (핫리로드 시 교체)
- 동적 라이브러리(.dylib/.dll/.so) 로딩/언로딩/리로딩
- `RegisterScript()` — backward-compatible alias for `RegisterDynamic()`

#### ScriptCompiler
- 프로젝트 내 스크립트 자동 탐색 (변경 감지)
- CMakeLists.txt + ScriptExports.cpp 자동 생성
- **exit code 기반 빌드 실패 감지** (문자열 매칭 제거)
- 스크립트 템플릿(헤더/소스) 생성

#### BuiltinScripts
- PlayerController: 키보드 이동
- Rotator: 지속 회전
- Oscillator: 사인 함수 왕복 운동

### 7. 씬 시스템

#### SceneSerializer
- JSON 기반 씬 직렬화/역직렬화
- GameObject ID 복원 (`SetID`)
- 부모-자식 관계 2-pass 직렬화
- ComponentFactory 기반 동적 컴포넌트 복원
- enabled 상태 직렬화

#### SceneManager
- 다중 씬 관리 (AddScene, RemoveScene, ChangeScene)
- `ChangeScene()` → `bool` 반환 (씬 존재 검증, `Log::Error` 보고)
- 지연된 씬 전환 패턴 (pendingScene)

### 8. 에디터

#### Editor (코디네이터)
- 도킹 기반 멀티 윈도우 레이아웃 (기본 4패널 레이아웃)
- 메뉴 바: File (New/Open/Save/SaveAs), Edit (Undo/Redo), GameObject, Window, Scripting, Build
- Play/Pause/Stop 컨트롤 (EditorTheme 색상 적용)
- 미구현 메뉴 아이템 경고 로깅 (Undo, Redo, Exit)
- `WindowManager`, `SceneOperations`, `BuildManager`로 책임 위임

#### WindowManager
- 윈도우 등록/조회/가시성 토글
- `Register()`, `GetAs<T>()`, `Toggle()`, `RenderAll()`, `RenderWindowMenu()`
- 모든 에디터 윈도우를 통합 관리

#### SceneOperations
- `NewScene()`, `SaveScene()`, `SaveSceneAs()`, `OpenScene()`
- 씬 경로 및 수정 상태(dirty flag) 관리

#### BuildManager
- 빌드 설정 UI (게임 이름, 출력 경로, 윈도우 크기, 풀스크린)
- 빌드 실행 및 진행률 추적

#### EditorConstants / EditorTheme
- 윈도우 이름, 도킹 ID, 기본 파일명 등 문자열 상수 통일
- 플레이 버튼, 파일 타입, 경고/에러 색상 등 스타일 상수 통일

#### UIRegistry
- 파일 확장자 → 아이콘/색상 매핑 (GetFileTypeInfo)
- 컴포넌트 타입 → 아이콘 매핑 (GetComponentInfo)
- ProjectBrowserWindow, InspectorWindow, HierarchyWindow에서 공유

#### ImGuiLayer
- Dear ImGui 초기화/종료/프레임 관리
- 도킹 지원, 다크/모던 테마

#### EditorState
- Edit / Play / Pause 모드 관리, TimeScale 제어

#### HierarchyWindow
- 게임 오브젝트 트리 뷰 (부모-자식 계층)
- 검색 필터
- **컨텍스트 메뉴**: 오브젝트 생성, 삭제, 복제, 이름변경
- 컴포넌트 기반 아이콘 표시 (UIRegistry 연동)
- 인라인 이름 편집 (InputText)

#### InspectorWindow
- 선택된 오브젝트의 이름/Active 상태 편집
- 컴포넌트별 OnInspectorGUI 호출
- 컴포넌트 추가 팝업 (Transform, SpriteRenderer, BoxCollider2D, Scripts)
- 컴포넌트 아이콘 (UIRegistry 연동)

#### ProjectBrowserWindow
- 파일 시스템 탐색 (폴더 트리 + 파일 그리드)
- 브레드크럼 네비게이션, 파일 아이콘/색상 (UIRegistry 연동)
- 텍스처 드래그 앤 드롭 지원
- 도킹 이름 일치 ("Project Browser")

#### ProjectWindow
- 새 프로젝트 생성, 기존 프로젝트 열기, 최근 프로젝트 목록

#### ScriptWindow
- 스크립트 목록, 생성 다이얼로그, 컴파일 상태

#### SceneViewWindow / StatsWindow
- SceneViewWindow: 씬 뷰포트 (현재 placeholder)
- StatsWindow: FPS, Delta Time, Frame Count 표시

#### FontManager / VSCodeIntegration
- FontManager: 에디터 폰트/아이콘 폰트 관리
- VSCodeIntegration: VS Code 프로젝트 설정 자동 생성

### 9. 프로젝트 & 빌드

#### Project
- 프로젝트 생성/열기/닫기
- 디렉토리 구조 자동 생성 (Assets, Scenes, ProjectSettings, Scripts)
- 프로젝트 파일(.molga) JSON 저장/로드
- 최근 프로젝트 관리 (~/.molga/recent_projects.json)

#### GameBuilder
- 독립 실행 게임 빌드 (에셋/셰이더/씬 복사)
- game.json 설정 파일 생성, 빌드 진행률 추적
- 모든 경로에 `fs::exists()` 검증

### 10. 런타임 UI 시스템

#### UIElement / Panel / Button / ProgressBar
- Panel: 배경 패널 (테두리 지원)
- Button: 호버/프레스 상태, 클릭 콜백
- ProgressBar: 값 기반 게이지 바
- UIManager: UI 요소 추가/제거/업데이트/렌더링

### 11. 테스트

- 81개 CTest: 순수 unit, 실제 OpenGL, SDL 플랫폼, SDL_GPU, smoke/E2E로 분리
- `test_input_migrator`: dry-run/apply/backup/idempotence 계약
- `test_platform_sdl`: event/focus/close/window/HiDPI/OpenGL host 계약
- `test_gpu_sdl`: native Metal/Vulkan/D3D12 device와 shader/texture/draw/present 계약
- `smoke_end_to_end`: 에디터 build, package, 실제 숨김 런타임 렌더 리포트 계약

## 리팩토링 이력

| Phase | 내용 | 상태 |
|-------|------|------|
| Phase 0 | 기준선 확보 (molga_core 추출, CTest, 빌드 경고 해소) | ✅ 완료 |
| Phase 1 | 메모리 안전성 (unique_ptr, RAII, goto 제거) | ✅ 완료 |
| Phase 2 | 아키텍처 통일 (Application→Bootstrap, 경로 상수화) | ✅ 완료 |
| Phase 3 | ECS/씬 모델 개선 (O(1) 조회, ComponentFactory, ID 복원) | ✅ 완료 |
| Phase 4 | 에디터 구조 개선 (God Class 분리, Log, UIRegistry, 핫리로드 수정) | ✅ 완료 |
| Phase 5 | 코드 품질 (#pragma once, Constants.h, Shader 캐싱, 디렉토리 재구성) | ✅ 완료 |

## 현재 브랜치

`finetune` — SDL3 플랫폼 전환 작업 반영

## 최근 커밋 히스토리

| 커밋 | 내용 |
|------|------|
| `4685a93` | feat: 에디터 인프라 전면 구현 (프로젝트 관리, 빌드, UIRegistry, 로깅) |
| `9e9910b` | Merge pull request #21 - game_build |
| `64d567e` | refactor: Phase 3 - ECS/Scene 모델 업그레이드 (O(1) 조회, 자동 팩토리) |
| `c214198` | refactor: Application → Bootstrap 모듈 추출 |
| `654df76` | refactor: 리소스 관리 unique_ptr 전환 |
