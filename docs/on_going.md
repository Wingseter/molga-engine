# Molga Engine - 프로젝트 현황

## 프로젝트 개요

**Molga Engine**은 C++17로 개발 중인 커스텀 2D 게임 엔진이다. 에디터(Editor)와 런타임(Runtime) 두 가지 실행 파일로 구성되며, Entity-Component-System(ECS) 아키텍처를 기반으로 한다.

- **에디터**: ImGui 기반의 GUI 에디터로, 씬 편집, 오브젝트 관리, 프로퍼티 인스펙션 지원
- **런타임**: 에디터 없이 빌드된 게임을 독립 실행하는 플레이어

## 기술 스택

| 항목 | 기술 |
|------|------|
| 언어 | C++17 |
| 빌드 시스템 | CMake (>= 3.27) |
| 그래픽스 | OpenGL 3.3 Core, GLFW, GLAD |
| UI (에디터) | Dear ImGui (Docking) |
| 오디오 | miniaudio |
| JSON | nlohmann/json |
| 이미지 로딩 | stb_image |
| 플랫폼 | macOS (CoreAudio/AudioToolbox), 크로스플랫폼 의도 |

## 디렉토리 구조

```
src/
├── Core/           # 엔진 코어 (Application, Project, GameBuilder, SceneSerializer, TextureManager)
├── ECS/            # Entity-Component-System
│   └── Components/ # Transform, SpriteRenderer, BoxCollider2D
├── Editor/         # 에디터 레이어
│   └── Windows/    # Hierarchy, Inspector, ProjectBrowser, ProjectWindow, ScriptWindow
├── Scripting/      # 스크립트 시스템 (Script, ScriptManager, ScriptCompiler, BuiltinScripts)
├── Scenes/         # 씬 관리 (GameScene, MenuScene)
├── Platform/       # 플랫폼 추상화
├── Shaders/        # GLSL 셰이더 (default.vert, default.frag)
└── Common/         # 공통 타입 (Types.h)

external/           # 서드파티 라이브러리 (imgui, glfw, glad, miniaudio, nlohmann_json, stb)
assets/             # 게임 에셋 (이미지, 씬 파일)
ui_images/          # 에디터 UI 아이콘 리소스
```

## 구현된 기능

### 1. 코어 엔진 시스템

#### Application (싱글톤)
- GLFW 윈도우 생성 및 관리
- 메인 루프 (Input → Update → Render)
- Delta Time 계산
- 윈도우 리사이즈, 키/마우스 콜백 처리

#### Renderer
- OpenGL 3.3 Core Profile 렌더링 파이프라인
- Quad 기반 2D 스프라이트 렌더링 (VAO/VBO/EBO)
- 알파 블렌딩 지원
- UV 매핑을 통한 텍스처 영역 지정

#### Shader
- GLSL 버텍스/프래그먼트 셰이더 로딩 및 컴파일
- Model/Projection 매트릭스 유니폼
- 텍스처/색상 모드 전환 (useTexture 유니폼)

#### Camera2D
- 2D 카메라 (위치, 줌, 회전)
- View/Projection 매트릭스 자동 갱신
- 스크린 크기 기반 Orthographic 프로젝션

#### Time
- 프레임 Delta Time 관리

#### Input
- 키보드 입력 (GetKey, GetKeyDown, GetKeyUp)
- 마우스 입력 (버튼, 위치, 델타, 스크롤)
- 프레임 단위 이전 상태 추적

### 2. ECS (Entity-Component-System)

#### GameObject
- 고유 ID 기반 엔티티
- 컴포넌트 추가/제거/조회 (템플릿 기반)
- 부모-자식 계층 구조
- Active 상태 관리
- Update/Render 루프

#### Component (기본 클래스)
- OnAttach/OnDetach 생명주기
- Update/Render 가상 함수
- Serialize/Deserialize (JSON 직렬화)
- OnInspectorGUI (에디터 인스펙터 연동)
- Enable/Disable 토글

#### Transform 컴포넌트
- Position (x, y), Rotation, Scale
- 월드 좌표 변환 (GetWorldPosition/Rotation/Scale)
- Translate 유틸리티
- 직렬화/역직렬화 지원
- 인스펙터 GUI 연동

#### SpriteRenderer 컴포넌트
- 텍스처 기반 스프라이트 렌더링
- 색상(RGBA) 틴팅
- 크기(width, height) 조절
- FlipX/FlipY
- Sorting Order (렌더링 순서)
- 텍스처 경로 기반 로딩
- 직렬화/인스펙터 GUI 지원

#### BoxCollider2D 컴포넌트
- AABB 충돌 감지
- Size/Offset 설정
- Trigger 모드 (물리적 충돌 vs 이벤트만)
- 월드 좌표 AABB 계산
- 충돌 결과 반환 (CheckCollisionWithResult)

### 3. 렌더링 & 그래픽스

#### Texture
- stb_image 기반 이미지 로딩
- OpenGL 텍스처 관리

#### TextureManager (싱글톤)
- 텍스처 캐싱 및 중복 로딩 방지
- 로드/언로드/클리어

#### Sprite
- 텍스처 기반 2D 스프라이트 표현

#### SpriteSheet
- 스프라이트 시트에서 개별 프레임 추출

#### Animation
- 프레임 기반 스프라이트 애니메이션
- Play/Pause/Stop/Reset 제어
- 루프/비루프 모드
- 프레임 타임 설정

#### TextRenderer (싱글톤)
- 빌트인 폰트 기반 텍스트 렌더링
- 텍스트 너비/높이 계산
- 라인 스페이싱 설정

#### Particle System
- ParticleEmitter: 위치 기반 파티클 방출
- ParticleConfig: 스폰 레이트, 속도, 각도, 중력, 크기, 회전, 수명, 색상 등 상세 설정
- 프리셋: Fire, Smoke, Spark, Snow, Explosion
- Burst 모드 지원

### 4. 물리 & 충돌

#### Collision (유틸리티)
- AABB vs AABB 충돌 감지
- Circle vs Circle 충돌 감지
- AABB vs Circle 충돌 감지
- Point-in-AABB / Point-in-Circle 테스트
- 충돌 결과(방향, 깊이) 반환 지원

#### Tilemap
- 타일 기반 맵 시스템
- 타일별 Solid 설정 (충돌 타일)
- 월드-타일 좌표 변환
- 타일 AABB 충돌 감지
- SpriteSheet 기반 타일 렌더링

### 5. 오디오

#### Audio (정적 클래스)
- miniaudio 기반 오디오 엔진
- 사운드 이펙트 로드/재생/정지
- 배경 음악 로드/재생/정지/일시정지/재개
- 마스터 볼륨 / 음악 볼륨 제어

### 6. 스크립팅 시스템

#### Script (기본 클래스)
- Unity 스타일 생명주기: Start, Update, FixedUpdate, LateUpdate
- 활성화 콜백: OnEnable, OnDisable
- 충돌 콜백: OnCollisionEnter/Stay/Exit, OnTriggerEnter/Stay/Exit
- Transform 접근 유틸리티

#### ScriptManager (싱글톤)
- 스크립트 팩토리 패턴 등록/생성
- 동적 라이브러리(.dylib/.dll) 로딩/언로딩/리로딩
- 런타임 스크립트 바인딩

#### ScriptCompiler (싱글톤)
- 프로젝트 내 스크립트 자동 탐색
- CMakeLists.txt 자동 생성
- 스크립트 템플릿(헤더/소스) 생성
- 변경 감지 및 자동 재컴파일

#### BuiltinScripts
- PlayerController: 이동 속도 기반 키보드 제어
- Rotator: 지속 회전
- Oscillator: 사인 함수 기반 왕복 운동

### 7. 씬 시스템

#### SceneSerializer
- JSON 기반 씬 직렬화/역직렬화
- GameObject 계층 구조 보존
- 모든 컴포넌트 데이터 저장/복원

#### SceneManager
- 다중 씬 관리 (AddScene, RemoveScene, ChangeScene)
- 씬 전환 처리

#### GameScene / MenuScene
- 게임 씬: 타일맵, 플레이어, UI, 파티클 시스템 통합 데모
- 메뉴 씬: 게임 시작 화면

### 8. 에디터

#### ImGuiLayer
- Dear ImGui 초기화/종료/프레임 관리
- 도킹(Docking) 지원
- 다크 테마 / 모던 테마 적용
- 마우스/키보드 캡처 상태 관리

#### Editor (싱글톤)
- 도킹 기반 멀티 윈도우 레이아웃
- 메뉴 바 (File: New/Open/Save/SaveAs, Edit, Build, Scripting)
- Play/Pause/Stop 컨트롤
- 씬 뷰 렌더링
- 빌드 설정 윈도우
- Stats 윈도우

#### EditorState (싱글톤)
- 에디터 모드 관리: Edit / Play / Pause
- TimeScale 제어

#### HierarchyWindow
- 게임 오브젝트 트리 뷰 표시
- 오브젝트 선택 콜백
- 부모-자식 계층 렌더링

#### InspectorWindow
- 선택된 오브젝트의 컴포넌트 프로퍼티 표시/편집
- 컴포넌트별 OnInspectorGUI 호출

#### ProjectBrowserWindow
- 파일 시스템 탐색 (폴더 트리 + 파일 그리드)
- 브레드크럼 네비게이션
- 파일 선택/더블클릭 콜백
- 컨텍스트 메뉴
- 파일 확장자 아이콘

#### ProjectWindow
- 새 프로젝트 생성 (이름, 경로 지정)
- 기존 프로젝트 열기 (폴더 브라우저)
- 최근 프로젝트 목록

#### ScriptWindow
- 스크립트 목록 표시
- 스크립트 생성 다이얼로그
- 컴파일 상태 표시 (성공/실패)
- 컴파일 실행

#### FontManager (싱글톤)
- 에디터 폰트 로딩 및 관리
- 폰트 타입별 전환 (Push/Pop)
- 아이콘 폰트 지원

#### VSCodeIntegration (싱글톤)
- VS Code 프로젝트 설정 자동 생성 (tasks.json, c_cpp_properties.json, launch.json)
- VS Code에서 파일/프로젝트 열기
- 엔진 인클루드 경로 설정

### 9. 프로젝트 & 빌드 시스템

#### Project (싱글톤)
- 프로젝트 생성/열기/닫기
- 디렉토리 구조 자동 생성 (assets, scenes, settings, scripts)
- 프로젝트 파일(.molga) 저장/로드
- 최근 프로젝트 관리 (최대 개수 제한)
- 절대/상대 경로 변환

#### GameBuilder (싱글톤)
- 독립 실행 게임 빌드
- 에셋/셰이더/씬 복사
- game.json 설정 파일 생성
- 빌드 진행률 추적

### 10. UI 시스템 (런타임)

#### UIElement / Panel / Button / ProgressBar
- 기본 UI 요소 (위치, 크기, 색상, 가시성)
- Panel: 배경 패널 (테두리 지원)
- Button: 호버/프레스 상태, 클릭 콜백
- ProgressBar: 값 기반 게이지 바

#### UIManager
- UI 요소 관리 (추가/제거/업데이트/렌더링)

### 11. 에디터 UI 리소스

`ui_images/` 디렉토리에 에디터용 아이콘 이미지 포함:
- 앱 아이콘, 로고, 스플래시 스크린
- 컴포넌트 아이콘 (Transform, SpriteRenderer, BoxCollider, Rigidbody, Script, Audio)
- 파일 타입 아이콘 (Scene, Script, Texture, Audio, Prefab)
- 에디터 윈도우 아이콘 (Hierarchy, Inspector, Project, Console, Scene, Game, Script Editor)
- 플레이 컨트롤 아이콘 (Play, Pause, Stop)
- 도구 아이콘 (Move, Rotate, Scale, Center, ZoomIn, ZoomOut)
- 상태 아이콘 (Info, Warning, Error, Success)

## 현재 브랜치

`game_build` - 게임 빌드 기능 개발 중

## 최근 커밋 히스토리

| 커밋 | 내용 |
|------|------|
| `02d57d2` | 이미지 임포트 및 엔진 내 사용 |
| `9ea73d4` | Visual Studio 스크립트 연동 |
| `9649588` | .gitignore 리팩토링 및 빌드 파일 추적 제거 |
| `285ee68` | ProjectWindow 패널 네비게이션 개선 및 버튼 고유 ID 추가 |
