# Molga Engine → Unity급 2D 게임 엔진 종합 계획서

> 작성일: 2026-03-23
> 기반 문서: `00_roadmap.md` ~ `05_platform.md` 설계 문서 6종
> 현재 상태: Phase 0-5 리팩토링 완료 (ECS, Scene, Editor, 코드 품질, 디렉토리 구조화)
> 최종 목표: Unity와 동등한 수준의 2D 게임 개발 환경

---

## 1. 현재 엔진 진단

### 1.1 보유 자산 (Phase 0-5 완료)

```
src/
├── Common/     Constants, Log, Types, linmath       ← 기반 유틸리티
├── Core/       Bootstrap, Scene, SceneSerializer,   ← 엔진 코어
│               MolgaTime, TextureManager, PathConstants
├── ECS/        GameObject, Component(O(1)),         ← ECS 프레임워크
│               ComponentFactory, Transform,
│               SpriteRenderer, BoxCollider2D
├── Editor/     ImGui Docking, 11개 에디터 창,       ← 에디터 셸
│               BuildManager, Project, WindowManager
├── Physics/    Collision (AABB/Circle 감지만)        ← 최소 물리
├── Platform/   macOS 전용 Platform                   ← 단일 플랫폼
├── Rendering/  Sprite, SpriteSheet, Animation,      ← 기본 렌더링
│               Camera2D, Shader, Renderer,
│               TextRenderer, Texture, Tilemap
├── Scenes/     GameScene, MenuScene                  ← 샘플 씬
├── Scripting/  Script, ScriptManager,               ← C++ 핫리로드
│               ScriptCompiler, BuiltinScripts
├── Systems/    Audio(miniaudio), Input(GLFW),       ← 시스템 서비스
│               Particle(CPU)
└── UI/         UI(기본)                              ← 런타임 UI
```

**빌드 구조**: CMake → `molga_core`(정적 라이브러리) + `molga_engine`(에디터) + `molga_runtime`(런타임)
**테스트**: CTest 4개 (cassert 기반)
**외부 의존성**: GLFW, glad, ImGui(Docking), stb, miniaudio, nlohmann_json

### 1.2 핵심 병목 분석

| 영역 | 현재 한계 | 영향도 |
|------|----------|--------|
| **렌더링** | 스프라이트당 1 Draw Call + 1 uniform 업데이트. 수백 개에서 성능 저하 | 🔴 Critical |
| **물리** | 충돌 감지만 있고 응답(반응) 없음. 물리 기반 게임 불가 | 🔴 Critical |
| **에디터** | SceneView가 placeholder. 오브젝트 조작/피킹 불가 | 🔴 Critical |
| **에셋** | GUID 없음, AssetDatabase 없음. 에셋 참조 불안정 | 🟡 High |
| **입력** | GLFW 직접 호출. 매핑/리바인딩 불가 | 🟡 High |
| **플랫폼** | macOS 전용. Windows/Linux/Web 미지원 | 🟡 High |
| **스크립팅** | C++ 전용. 비프로그래머 접근 불가 | 🟢 Medium |

### 1.3 Unity 대비 갭 분석 (44개 미구현 시스템)

```
┌─────────────────────────────────────────────────────────────────┐
│  A. 코어 엔진 (7개)     B. 렌더링 (8개)     C. 에디터 (10개)  │
│  D. 게임플레이 (9개)    E. 플랫폼/툴링 (10개)                  │
│                                                                 │
│  총 44개 시스템 · MVP까지 22-34주 · 전체 38-64주               │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 전략적 접근 방식

### 2.1 핵심 원칙

1. **의존성 순서 준수**: 기반 시스템 → 소비 시스템. 토대 없이 상위 시스템 구축 금지
2. **수직 슬라이스 검증**: 각 Phase 완료 시 데모 게임으로 실제 사용성 검증
3. **점진적 복잡도**: Small → Medium → Large → Very Large 순서로 자신감 축적
4. **외부 라이브러리 활용**: 물리(Box2D), 프로파일링(Tracy), 테스트(Catch2) 등 검증된 솔루션 채택
5. **병렬 개발 가능 식별**: 독립 시스템은 동시 개발하여 일정 단축

### 2.2 시스템 간 의존성 맵

```
이벤트 시스템 (A1) ─── 최상위 기반 ─── 태그 & 레이어 (D1) ─── 커스텀 셰이더 (B1)
    │                                       │                        │
    ├→ 물리 엔진 (A7)                       ├→ 소팅 레이어 (B5)      ├→ 디버그 렌더링 (B2)
    │   └→ Rigidbody2D (D3)                │   └→ 배치 렌더러 (B4)  ├→ 머티리얼 → 배치 (B4)
    │   └→ 추가 콜라이더 (D3)              │       └→ 아틀라스 (B3) └→ 포스트프로세싱 (B7)
    │                                       │                            └→ 2D 라이팅 (B8)
    ├→ 입력 시스템 (A4)                     └→ 카메라 (B6)
    │
    └→ 오브젝트 라이프사이클 (D2)       에셋 관리 (A5)
        ├→ 프리팹 (A6) ←── 에셋 관리       ├→ 에셋 임포트 (C10)
        └→ 풀링 (A2)                       ├→ 프리팹 (A6)
                                            └→ 씬 관리 확장 (D6)

독립 시스템 (병렬 개발 가능):
  코루틴(A3), 트위닝(D5), 전역 상태(D7),
  콘솔/로그(C3), Undo/Redo(C2), CI/CD(E1)
```

---

## 3. 선행 정비 작업 (Phase 6 시작 전)

> 새 시스템 도입 전 반드시 완료해야 할 코드베이스 기초 정비

| # | 작업 | 상세 | 예상 기간 | 이유 |
|---|------|------|-----------|------|
| P0-1 | **Component에 `OnDestroy()` 추가** | 리소스 해제 라이프사이클 메서드. Box2D 바디 파괴 등에 필수 | 0.5일 | A7 물리 통합의 전제 |
| P0-2 | **Collider2D 추상 기반 클래스** | BoxCollider2D가 직접 Component 상속 중 → 다형성 충돌 처리 필요 | 1일 | A7, D3의 전제 |
| P0-3 | **FixedUpdate 고정 시간 루프** | accumulator 패턴. `while(acc >= fixedDT)` | 1일 | A7 물리 시뮬레이션 필수 |
| P0-4 | **Broad Phase 공간 분할** | Uniform Grid 또는 Quadtree. 충돌 감지 O(n²) → O(n log n) | 2일 | A7 성능, B4 컬링 |

**소계: 4-5일**

---

## 4. 7단계 실행 계획

### Phase 6: 기반 인프라 (4-6주)

> 다른 모든 시스템의 전제 조건이 되는 기반 시스템

```
Week 1          Week 2          Week 3          Week 4          Week 5-6
┌──────────┐   ┌──────────┐   ┌──────────────┐ ┌──────────┐   ┌──────────────┐
│A1 이벤트 │──→│D1 태그&  │──→│B1 커스텀     │→│A2 풀링   │──→│D2 오브젝트   │
│시스템    │   │레이어    │   │셰이더/머티리얼│ │(1-2일)   │   │라이프사이클  │
│(2-3일)   │   │(3-5일)   │   │(1-1.5주)     │ └──────────┘   │(1-1.5주)     │
└──────────┘   └──────────┘   └──────────────┘ ┌──────────┐   └──────────────┘
                                                │B2 디버그 │   ┌──────────────┐
                                      병렬 ──→ │렌더링    │   │E1 CI/CD      │
                                                │(3-5일)   │   │(1주)         │
                                                └──────────┘   └──────────────┘
```

#### A1. 이벤트/메시징 시스템 (2-3일) — 최우선

**목적**: 모든 시스템의 디커플링된 통신 기반

```cpp
// 핵심 설계: 타입 안전 이벤트 버스
EventBus::Subscribe<CollisionEvent>([](const CollisionEvent& e) { ... });
EventBus::Publish(CollisionEvent{objA, objB, contactPoint});
```

- 타입 안전 콜백 (std::function + type_index)
- 우선순위 기반 디스패치
- 지연 이벤트 큐 (프레임 끝 일괄 처리)
- **출력**: `src/Core/EventBus.h/cpp`

#### D1. 태그 & 레이어 시스템 (3-5일)

**목적**: 충돌 필터링, 렌더링 정렬, 오브젝트 분류의 기반

- 32비트 비트마스크 레이어 (최대 32개)
- 충돌 매트릭스 (레이어 간 충돌 여부)
- 문자열 태그 + 소팅 레이어
- **출력**: `src/ECS/TagLayer.h/cpp`

#### B1. 커스텀 셰이더/머티리얼 시스템 (1-1.5주)

**목적**: 라이팅, 포스트프로세싱, 비주얼 다양성의 전제

- ShaderManager: 셰이더 캐싱, 핫리로드
- Material: 셰이더 + 프로퍼티 묶음 (float, vec4, texture)
- Inspector 에디터 통합
- **출력**: `src/Rendering/ShaderManager.h/cpp`, `src/Rendering/Material.h/cpp`

#### B2. 디버그/기즈모 렌더링 (3-5일) — B1과 병렬 가능

**목적**: 이후 모든 개발의 시각적 디버깅 도구

- 즉시 모드 API: `DebugDraw::Line()`, `Circle()`, `Rect()`, `Text()`
- 와이어프레임 + 솔리드 모드
- 자동 프레임 끝 클리어
- **출력**: `src/Rendering/DebugDraw.h/cpp`

#### A2. 오브젝트 풀링 (1-2일) — 독립

**목적**: 파티클, 탄막, 적 등 빈번한 생성/파괴 최적화

- 템플릿 기반 `ObjectPool<T>`
- 자동 확장, 사전 할당
- **출력**: `src/Core/ObjectPool.h`

#### D2. 오브젝트 라이프사이클 (1-1.5주)

**목적**: Unity의 `Instantiate()/Destroy()` 패턴

- 지연 파괴 (프레임 끝 일괄 처리)
- `DontDestroyOnLoad` 마킹
- Clone 메커니즘 (깊은 복사 + 새 ID)
- **출력**: `src/ECS/GameObjectLifecycle.h/cpp`

#### E1. CI/CD 파이프라인 (1주) — 독립

**목적**: 자동 빌드/테스트로 회귀 방지

- GitHub Actions: macOS 빌드 + CTest
- PR 검증 자동화
- 아티팩트 캐싱 (GLFW, glad 등)
- **출력**: `.github/workflows/ci.yml`

#### Phase 6 검증 기준
- [ ] 이벤트 발행/구독이 동작하고, 콜리전 이벤트를 이벤트 버스로 전달
- [ ] 태그로 오브젝트 검색, 레이어 마스크로 충돌 필터링 가능
- [ ] 커스텀 셰이더를 Material에 바인딩하여 렌더링
- [ ] `DebugDraw::Line()`으로 콜라이더 경계를 시각화
- [ ] `Instantiate()`/`Destroy()`로 오브젝트 동적 생성/파괴
- [ ] CI에서 자동 빌드 + 테스트 통과

---

### Phase 7: 렌더링 파이프라인 (6-10주)

> 10,000+ 스프라이트 렌더링과 비주얼 품질 향상

```
Week 1-2       Week 3-5         Week 6-7        Week 8-9        Week 10-12     Week 13-15
┌──────────┐  ┌──────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌──────────┐
│B3 스프라이│→│B4 배치       │→│B5 소팅     │→│B6 카메라   │→│B7 포스트   │→│B8 2D     │
│트 아틀라스│  │렌더러       │  │레이어      │  │고도화      │  │프로세싱    │  │라이팅    │
│(1-2주)    │  │(2-3주)       │  │(1-1.5주)   │  │(1.5-2주)   │  │(2-3주)     │  │(3-5주)   │
└──────────┘  └──────────────┘  └────────────┘  └────────────┘  └────────────┘  └──────────┘
```

#### B3. 스프라이트 아틀라스 (1-2주)

**목적**: 배치 렌더러의 전제. 텍스처 바인딩 전환 최소화

- MaxRects 빈 패킹 알고리즘
- 빌드타임/에디터 내 아틀라스 생성
- UV 매핑 자동화
- **목표**: 개별 스프라이트 → 하나의 아틀라스 텍스처로 통합
- **출력**: `src/Rendering/SpriteAtlas.h/cpp`

#### B4. 배치 렌더러 (2-3주) — 핵심 성능 업그레이드

**목적**: 현재 "1 스프라이트 = 1 Draw Call" → "10,000 스프라이트 = ~10 Draw Call"

```
현재: sprite → glUniform × 4 → glDrawArrays     (×1000 = 1000 Draw Calls)
목표: 1000 sprites → CPU 정점 생성 → glBufferSubData → glDrawElements (×1 = 1 Draw Call)
```

- Dynamic VBO 방식 `SpriteBatcher`
- `MAX_SPRITES = 10,000`, 쿼드 인덱스 사전 생성
- 텍스처/셰이더 변경 시 자동 Flush
- 파티클 인스턴싱 (1 Draw Call로 수천 파티클)
- **성능 목표**: 10,000 스프라이트 @ 60fps (현재 대비 ~50배 향상)
- **출력**: `src/Rendering/SpriteBatcher.h/cpp`

#### B5. 소팅 레이어 (1-1.5주)

**목적**: 정확한 렌더링 순서 보장

- 64비트 정렬 키: `[카메라 depth 8][레이어 8][순서 16][머티리얼 16][텍스처 16]`
- 배치 렌더러와 연동된 자동 정렬
- **출력**: `src/Rendering/SortingLayer.h/cpp`

#### B6. 카메라 시스템 고도화 (1.5-2주)

**목적**: 멀티 카메라, Pixel-Perfect, 시네마틱 추적

- Camera 컴포넌트화 (기존 Camera2D → ECS Component)
- Culling Mask (레이어 기반)
- Pixel-Perfect 렌더링 (정수 좌표 스냅)
- Cinemachine 스타일 Follow/Deadzone
- **출력**: `src/ECS/Components/Camera.h/cpp`, `src/Rendering/CameraSystem.h/cpp`

#### B7. 포스트 프로세싱 (2-3주)

**목적**: Bloom, Vignette, 색수차, Screen Shake 등 비주얼 이펙트

- Ping-Pong FBO 체인
- 이펙트 스택 (순서 조절 가능)
- 기본 이펙트: Bloom, Vignette, ChromaticAberration, ScreenShake, ColorGrading
- **출력**: `src/Rendering/PostProcessing/`

#### B8. 2D 라이팅 (3-5주) — 가장 복잡, 비주얼 임팩트 최대

**목적**: 동적 조명과 그림자로 분위기 있는 2D 장면

- Point Light, Global Light, Spot Light
- 노멀맵 지원 (스프라이트에 깊이감 추가)
- 2D 그림자 (레이마칭 또는 Shadow Map 기반)
- Light Blend 모드 (Additive, Multiply)
- **출력**: `src/Rendering/Lighting2D/`

#### Phase 7 검증 기준
- [ ] 10,000 스프라이트를 60fps로 렌더링 (Draw Call ≤ 20)
- [ ] 스프라이트 아틀라스가 빌드타임에 자동 생성
- [ ] 소팅 레이어로 배경/캐릭터/UI 순서가 정확히 구분
- [ ] Pixel-Perfect 카메라로 픽셀 아트가 깨끗하게 표시
- [ ] Bloom + Vignette 포스트프로세싱이 적용된 씬
- [ ] Point Light 2개 + 그림자가 있는 동적 라이팅 데모

---

### Phase 8: 에디터 핵심 (4-6주)

> 게임 개발 워크플로우에 필수적인 에디터 기능

```
Week 1-2        Week 3          Week 4         Week 5          Week 6
┌────────────┐  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐
│C1 씬뷰 +  │→│C2 Undo/  │→ │C3 콘솔   │→ │C7 D&D    │→ │C4 프로   │
│기즈모      │  │Redo      │   │로그      │   │C8 복사   │   │파일러    │
│(5-9일)     │  │(5-7일)   │   │(3일)     │   │붙여넣기  │   │(5일)     │
└────────────┘  └──────────┘   └──────────┘   │(5-7일)   │   └──────────┘
                                               └──────────┘
```

#### C1. 씬뷰 + 기즈모 (5-9일) — 에디터의 심장

**목적**: 현재 placeholder SceneView를 기능적인 오브젝트 편집 환경으로

- **FBO 기반 렌더링**: 에디터 전용 카메라 → FBO → ImGui::Image()
- **카메라 조작**: 패닝(중간 버튼), 줌(휠), 커서 중심 줌
- **오브젝트 피킹**: AABB 교차 테스트 (2D에서는 레이캐스트보다 효율적)
- **트랜스폼 기즈모**: Translate(화살표), Rotate(원), Scale(사각형) 핸들
- **그리드 오버레이**: 좌표 기준선, 스냅핑
- **좌표 변환**: ScreenToWorld / WorldToScreen
- **출력**: `src/Editor/Windows/SceneViewWindow.h/cpp` 대폭 개편

#### C2. Undo/Redo (5-7일)

**목적**: 안전한 편집의 기본. 실수 복구 가능

- Command 패턴 + 직렬화 스냅샷 하이브리드
- 스택 기반 이력 관리 (기본 100단계)
- Transform 변경, 컴포넌트 추가/삭제, 프로퍼티 수정 지원
- 단축키: Ctrl+Z / Ctrl+Shift+Z
- **출력**: `src/Editor/UndoSystem.h/cpp`

#### C3. 콘솔/로그 윈도우 (3일)

**목적**: 기존 Log 시스템을 에디터 내에서 확인

- 기존 `Log.h` 연동 (콜백 기반 메시지 수집)
- 레벨별 필터링 (Info, Warning, Error)
- 가상 스크롤 (대량 로그 성능)
- 더블 클릭 → 소스 위치 이동
- **출력**: `src/Editor/Windows/ConsoleWindow.h/cpp`

#### C7. 드래그 & 드롭 (3-4일) — C8과 병렬

**목적**: 에디터 사용성 대폭 향상

- ImGui DragDropSource/DragDropTarget API 활용
- 에셋 → 씬 (텍스처 드래그로 스프라이트 생성)
- 컴포넌트 → 오브젝트 (드래그로 컴포넌트 추가)
- Hierarchy 내 오브젝트 재정렬 (부모-자식 변경)
- **출력**: 기존 에디터 윈도우들에 통합

#### C8. 컴포넌트 복사/붙여넣기 (2-3일) — C7과 병렬

**목적**: Inspector 컨텍스트 메뉴를 통한 컴포넌트 복제

- Serialize → 클립보드(JSON) → Deserialize
- Inspector 우클릭 메뉴: Copy, Paste, Paste As New, Remove
- **출력**: Inspector 윈도우에 통합

#### C4. 프로파일러 (5일)

**목적**: 성능 병목 진단 도구

- RAII `ProfileScope` 매크로
- 프레임 타임라인 시각화
- 링 버퍼 (최근 300프레임)
- CPU 시간: Update, Physics, Render 분리
- **출력**: `src/Editor/Windows/ProfilerWindow.h/cpp`, `src/Core/Profiler.h/cpp`

#### Phase 8 검증 기준
- [ ] 씬뷰에서 오브젝트 클릭 선택 → 기즈모로 이동/회전/스케일
- [ ] Undo/Redo로 씬뷰 조작을 되돌리기/다시 하기
- [ ] 콘솔 창에서 Log 메시지 실시간 확인, 필터링
- [ ] 텍스처를 씬뷰로 드래그하여 스프라이트 생성
- [ ] 프로파일러에서 프레임 시간 분석 가능

---

### Phase 9: 게임플레이 시스템 (8-12주)

> 실제 게임을 만들 수 있는 핵심 역학 시스템

```
Week 1-6           Week 7-10          Week 11-13        Week 14-15     Week 16      Week 17
┌───────────────┐  ┌───────────────┐  ┌─────────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐
│A7 2D 물리     │→│D3 빌트인      │→│D4 애니메이션│→│A4 고급   │→│D5 트위닝 │→│A3 코루 │
│엔진 (Box2D)   │  │컴포넌트       │  │상태 머신    │  │입력      │  │이징      │  │틴      │
│(4-6주)         │  │(3-4주)         │  │(2-3주)       │  │(1-2주)   │  │(1-1.5주) │  │(3-5일) │
└───────────────┘  └───────────────┘  └─────────────┘  └──────────┘  └──────────┘  └────────┘
```

#### A7. 2D 물리 엔진 — Box2D 3.x 통합 (4-6주) — 가장 큰 단일 시스템

**목적**: 게임 역학의 핵심. 중력, 충돌 반응, 관절, 레이캐스트

**디렉토리 구조**:
```
src/Physics/
├── PhysicsWorld.h/cpp          // Box2D b2WorldId 래핑, Simulate 루프
├── Rigidbody2D.h/cpp           // Component: 바디 래핑
├── PhysicsMaterial2D.h/cpp     // friction, restitution
├── PhysicsQuery.h/cpp          // Raycast, OverlapCircle 등
├── Collision.h/cpp             // 기존 유지 (간단한 쿼리용)
└── Joints/
    ├── Joint2D.h               // 기본 클래스
    ├── DistanceJoint2D.h/cpp
    ├── HingeJoint2D.h/cpp
    └── SpringJoint2D.h/cpp
```

**핵심 API**:
- `Rigidbody2D`: BodyType(Dynamic/Kinematic/Static), mass, drag, gravityScale, AddForce(), SetVelocity()
- `PhysicsWorld`: Step() (FixedUpdate에서 호출), 콜백(OnCollisionEnter/Stay/Exit, OnTriggerEnter/Stay/Exit)
- `PhysicsQuery`: Raycast(), OverlapCircle(), OverlapBox()
- 레이어 마스크 기반 충돌 필터링 (D1 연동)

**Box2D 3.x 선택 이유**: MIT 라이선스, 업계 표준, SIMD 최적화, C API로 래핑 용이

#### D3. 추가 빌트인 컴포넌트 (3-4주)

**목적**: Unity와 유사한 컴포넌트 세트 확보

| 컴포넌트 | 기반 시스템 | 기간 |
|----------|------------|------|
| Rigidbody2D | A7 물리 | (A7에 포함) |
| CircleCollider2D | P0-2 Collider2D 기반 | 2-3일 |
| PolygonCollider2D | P0-2 Collider2D 기반 | 3-5일 |
| CapsuleCollider2D | P0-2 Collider2D 기반 | 2-3일 |
| AudioSource | 기존 Audio 래핑 | 3-5일 |
| AudioListener | 카메라 연동 | 1-2일 |
| Camera (Component) | B6 카메라 시스템 | (B6에 포함) |
| ParticleSystem (Component) | 기존 Particle 래핑 | 1주 |
| Animator | D4 상태 머신 | (D4에 포함) |

#### D4. 애니메이션 상태 머신 (2-3주)

**목적**: 데이터 주도 애니메이션 전환 (if/else 지옥 탈출)

- AnimatorController: 상태 그래프 에셋
- AnimState: 이름 + Animation + 전환 목록
- AnimTransition: 조건(파라미터 비교) + exitTime + 우선순위
- AnimParameter: Bool/Int/Float/Trigger 4종
- AnimLayer: 독립 상태 머신 (레이어별 블렌딩)
- 2D 특화: 스프라이트 스왑 방식 (3D 보간과 다름)

```cpp
// 사용 예시
animator->SetBool("isRunning", true);
animator->SetTrigger("jump");
// → 상태 머신이 자동으로 Idle → Run, AnyState → Jump 전환
```

#### A4. 고급 입력 시스템 (1-2주)

**목적**: Input Action 매핑, 리바인딩, 디바이스 추상화

- InputAction: 이름 + 바인딩 목록 (키보드/마우스/게임패드)
- InputMap: 액션 그룹 (Menu, Gameplay, UI)
- 컴포지트 입력: WASD → Vector2
- 런타임 리바인딩 API
- **출력**: `src/Systems/InputSystem.h/cpp`, `src/Systems/InputAction.h/cpp`

#### D5. 트위닝/이징 (1-1.5주)

**목적**: DOTween 스타일 프로퍼티 애니메이션

- 체이닝 API: `Tween::To(&obj.x, 100, 1.0f).SetEase(EaseType::OutBounce).OnComplete(cb)`
- 30개 이징 함수 (Linear, Quad, Cubic, Elastic, Bounce 등)
- Sequence: 트윈 그룹 (직렬/병렬)
- **출력**: `src/Core/Tween.h/cpp`

#### A3. 코루틴/태스크 (3-5일)

**목적**: 시간 기반 시퀀스 제어

- C++17 빌더 패턴 (C++20 코루틴 불가이므로)
- `StartCoroutine()`, `WaitForSeconds()`, `WaitUntil()`
- `YieldInstruction` 기반 체인
- **출력**: `src/Core/Coroutine.h/cpp`

#### Phase 9 검증 기준 — 🎮 **데모 게임: 2D 플랫포머**
- [ ] 캐릭터에 Rigidbody2D 적용, 중력으로 자연 낙하
- [ ] 지면/벽 BoxCollider2D와 충돌 반응 (튕김/밀림)
- [ ] Animator로 Idle/Walk/Jump/Fall 자동 전환
- [ ] InputAction으로 이동/점프 매핑, 설정 화면에서 키 리바인딩
- [ ] 트윈으로 코인 수집 UI 연출
- [ ] 코루틴으로 적 스폰 타이밍 제어

---

### Phase 10: 에셋 파이프라인 & 고급 기능 (6-10주)

> 대규모 프로젝트를 위한 에셋 관리 인프라

```
Week 1-3           Week 4-5          Week 6-8          Week 9-10       Week 11-12
┌───────────────┐  ┌─────────────┐  ┌──────────────┐  ┌────────────┐  ┌──────────────┐
│A5 에셋 관리   │→│C10 에셋     │→│A6 프리팹     │→│D6 씬 관리  │→│C9 환경설정   │
│시스템         │  │임포트       │  │시스템         │  │확장         │  │프로젝트 설정 │
│(2-3주)         │  │(8-12일)     │  │(2-3주)        │  │(1.5-2주)   │  │(5-8일)       │
└───────────────┘  └─────────────┘  └──────────────┘  └────────────┘  └──────────────┘
```

#### A5. 에셋 관리 시스템 (2-3주)

**목적**: GUID 기반 에셋 참조, AssetDatabase, 비동기 로딩

- **GUID**: 128비트 UUID (crossguid 라이브러리). 파일 이동/이름 변경에도 참조 유지
- **Meta 파일**: `.meta` JSON (GUID, import 설정). 에셋과 1:1
- **AssetDatabase**: GUID → 파일 경로 매핑, 에셋 검색/필터링
- **비동기 로딩**: std::future 기반 백그라운드 로딩, 로딩 콜백
- **외부 라이브러리**: crossguid (MIT), efsw (파일 감시, MIT)
- **출력**: `src/Core/AssetDatabase.h/cpp`, `src/Core/GUID.h`

#### C10. 에셋 임포트 파이프라인 (8-12일)

**목적**: 파일 변경 자동 감지, 타입별 임포터, Meta 파일 관리

- FileWatcher (efsw): 에셋 폴더 변경 실시간 감지
- Importer 인터페이스: TextureImporter, AudioImporter, SceneImporter
- Import Settings: 텍스처 필터링, 오디오 압축 등
- **출력**: `src/Core/AssetImporter.h/cpp`

#### A6. 프리팹 시스템 (2-3주)

**목적**: 재사용 가능한 GameObject 템플릿

- 직렬화 기반 인스턴스화 (JSON → 복원)
- 프로퍼티 오버라이드 (인스턴스별 커스터마이징)
- 중첩 프리팹 (프리팹 안의 프리팹)
- 프리팹 수정 → 모든 인스턴스 자동 업데이트
- Inspector에서 오버라이드 표시 (Bold), 되돌리기
- **출력**: `src/Core/Prefab.h/cpp`

#### D6. 씬 관리 확장 (1.5-2주)

**목적**: Additive 로딩, 비동기 씬 전환

- Additive 씬 로딩 (여러 씬 동시 활성)
- 비동기 로딩 (로딩 화면 지원)
- 씬 언로딩
- **출력**: Scene/SceneManager 확장

#### C9. 환경설정/프로젝트 설정 (5-8일)

**목적**: Preferences vs ProjectSettings 분리

- Editor Preferences: 에디터 테마, 키맵, 그리드 설정
- Project Settings: 물리 설정, 충돌 매트릭스, 태그/레이어 편집
- 설정 윈도우 UI (ImGui Tab Bar)
- **출력**: `src/Editor/Windows/SettingsWindow.h/cpp`

#### Phase 10 검증 기준
- [ ] 에셋 파일 이름을 변경해도 씬 내 참조가 유지 (GUID)
- [ ] 텍스처 파일을 에셋 폴더에 드롭하면 자동 임포트
- [ ] 프리팹을 만들고, 인스턴스 3개 배치 후, 프리팹 수정 시 3개 모두 업데이트
- [ ] Additive 씬으로 UI 씬 + 게임 씬 동시 로딩
- [ ] Project Settings에서 충돌 매트릭스 편집 가능

---

### Phase 11: 플랫폼 확장 (4-8주)

> Windows/Linux/Web 배포 지원

```
Week 1-3              Week 4          Week 5-7            Week 8
┌─────────────────┐  ┌──────────┐   ┌─────────────────┐  ┌──────────┐
│E2 크로스 플랫폼 │→│E3 Catch2 │→ │E6 WebGL/        │→│E4 버전   │
│(Win/Linux)      │  │테스트    │   │Emscripten       │  │관리 통합 │
│(2-3주)           │  │(1주)     │   │(2-3주)           │  │(3-5일)   │
└─────────────────┘  └──────────┘   └─────────────────┘  └──────────┘
```

#### E2. 크로스 플랫폼 빌드 (2-3주)

**목적**: Windows + Linux 지원 (Steam 유저 96% + 1.5%)

- 플랫폼 추상화 레이어 강화 (`IPlatformWindow`, `IPlatformFileSystem`)
- CMake 크로스 컴파일 툴체인 (Windows MinGW/MSVC, Linux GCC)
- CI에 Windows/Linux 빌드 추가
- GLFW/miniaudio 이미 크로스 플랫폼 → 주요 이슈는 파일 경로/동적 로딩

#### E3. 테스팅 프레임워크 (1주)

**목적**: 회귀 방지. 현재 cassert → Catch2 v3 마이그레이션

- Catch2 v3 도입 (BSL-1.0)
- 기존 4개 테스트 마이그레이션
- ECS, 물리, 직렬화 테스트 추가
- **목표**: 핵심 시스템 커버리지 70%+

#### E6. WebGL/Emscripten (2-3주)

**목적**: 웹 브라우저에서 게임 실행

- GLSL → WebGL 전처리 (#version 300 es 호환)
- 메인 루프 변경: `emscripten_set_main_loop()`
- glad → Emscripten GL 추상화
- 파일 시스템: Emscripten VFS
- 오디오: miniaudio의 Emscripten 백엔드

#### E4. 버전 관리 통합 (3-5일)

**목적**: .gitignore, Git LFS, UUID 기반 ID 정책

- .gitignore 표준 템플릿 (빌드 산출물, 에디터 설정 등)
- Git LFS 설정 (텍스처, 오디오 등 바이너리 에셋)
- UUID 기반 에셋 ID 정책 문서화

#### Phase 11 검증 기준
- [ ] Windows에서 에디터 + 런타임 빌드 성공
- [ ] Linux에서 런타임 빌드 성공
- [ ] CI에서 macOS + Windows + Linux 3 플랫폼 자동 빌드
- [ ] Catch2로 핵심 시스템 테스트 70%+ 커버리지
- [ ] WebGL 빌드로 브라우저에서 데모 게임 실행
- [ ] Git LFS로 바이너리 에셋 관리

---

### Phase 12: 고급 에디터 & 전문 시스템 (6-12주)

> 성숙한 엔진을 위한 고급 기능

```
Week 1-2        Week 3-4          Week 5-7        Week 8-9       Week 10-11     Week 12+
┌──────────┐   ┌─────────────┐   ┌───────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐
│C5 멀티   │→ │C6 애니메이션│→ │D8 2D      │→ │D7 전역   │→ │E5 Tracy  │→ │D9 2D     │
│오브젝트  │   │에디터       │   │내비게이션  │   │게임 상태 │   │프로파일러│   │특화      │
│(5-7일)   │   │(9-13일)     │   │(2-3주)     │   │(1-1.5주) │   │(1-2주)   │   │(필요 시) │
└──────────┘   └─────────────┘   └───────────┘   └──────────┘   └──────────┘   └──────────┘
```

#### C5. 멀티 오브젝트 편집 (5-7일)

- SelectionManager: Ctrl+클릭 다중 선택
- Inspector에서 혼합 값 표시 (다른 값 → "-")
- 일괄 수정 (선택된 모든 오브젝트에 적용)

#### C6. 애니메이션 에디터 (9-13일)

- Dope Sheet: 키프레임 타임라인 뷰
- Curve Editor: 이징 커브 시각적 편집
- 키프레임 추가/삭제/이동
- 실시간 미리보기

#### D8. 2D 내비게이션 (2-3주)

- 그리드 기반 A* 경로 탐색
- Tilemap에서 NavGrid 자동 생성
- 장애물 동적 업데이트
- 에이전트: 경로 따라가기, 장애물 회피

#### D7. 전역 게임 상태 (1-1.5주)

- GamePrefs: 키-값 저장 (PlayerPrefs 스타일)
- SaveSystem: 슬롯 기반 세이브/로드 (JSON 직렬화)

#### E5. 성능 모니터링 — Tracy (1-2주)

- Tracy 프로파일러 통합 (BSD 라이선스)
- CPU/GPU 프레임 타이밍
- 메모리 할당 추적
- Tracy 전용 빌드 프로파일

#### D9. 2D 특화 시스템 (필요 시)

- Sprite Shape: 절차적 스프라이트 변형
- 2D Effectors: 부력, 바람 등 영역 효과
- Sprite Mask: 마스킹 렌더링
- 2D IK: 역운동학 (체인/림 해석)

#### Phase 12 검증 기준
- [ ] 10개 오브젝트를 한 번에 선택하고 위치를 일괄 변경
- [ ] 애니메이션 에디터에서 키프레임 편집 후 실시간 미리보기
- [ ] 적 AI가 A* 경로를 따라 플레이어를 추적
- [ ] 게임 세이브/로드 (3개 슬롯)
- [ ] Tracy로 프레임 단위 성능 분석

---

## 5. 타임라인 총괄

```
2026
Apr         May         Jun         Jul         Aug         Sep         Oct
├───────────┼───────────┼───────────┼───────────┼───────────┼───────────┤
│ P0 선행정비│                                                           │
│ (1주)     │                                                           │
├───────────┤                                                           │
│ Phase 6: 기반 인프라 (4-6주)                                          │
│ ██████████████████████████████                                        │
├─────────────────────────────────┤                                     │
│           Phase 7: 렌더링 파이프라인 (6-10주)                         │
│           ██████████████████████████████████████████████               │
│                     ├─────────────────────────────────┤               │
│                     Phase 8: 에디터 핵심 (4-6주)      │               │
│                     ██████████████████████████████     │               │
│                                 ├─────────────────────┼───────────────┤
│                                 Phase 9: 게임플레이 (8-12주)          │
│                                 ██████████████████████████████████████│

Nov         Dec         2027 Jan    Feb         Mar
├───────────┼───────────┼───────────┼───────────┤
Phase 10: 에셋 파이프라인 (6-10주)              │
██████████████████████████████████████           │
│           Phase 11: 플랫폼 확장 (4-8주)       │
│           ████████████████████████████         │
│                       Phase 12: 고급 기능 (6-12주)
│                       ████████████████████████████████
```

### 마일스톤 정의

| 마일스톤 | 시점 | 기준 |
|----------|------|------|
| **M1: 개발 도구 확보** | Phase 6 완료 (~6주) | 이벤트 버스, 디버그 렌더링, CI 동작 |
| **M2: 렌더링 성능** | Phase 7 완료 (~16주) | 10K 스프라이트 60fps, 라이팅 동작 |
| **M3: 에디터 사용 가능** | Phase 8 완료 (~22주) | 씬뷰 기즈모, Undo/Redo, D&D |
| **🎮 M4: MVP (게임 제작 가능)** | Phase 9 완료 (~34주) | 2D 플랫포머 데모 완성 |
| **M5: 프로덕션 파이프라인** | Phase 10 완료 (~44주) | 프리팹, GUID 에셋, 비동기 로딩 |
| **M6: 멀티 플랫폼** | Phase 11 완료 (~52주) | Win/Linux/Web 빌드 |
| **M7: 완전체** | Phase 12 완료 (~64주) | 애니메이션 에디터, A*, 세이브/로드 |

---

## 6. 병렬 개발 전략

일정 단축을 위해 독립 시스템을 식별하여 동시 개발 가능:

### 동시 개발 가능한 조합

| 시기 | 트랙 A (주) | 트랙 B (보조) | 절약 기간 |
|------|------------|--------------|----------|
| Phase 6 | B1 셰이더/머티리얼 | B2 디버그 렌더링 | 3-5일 |
| Phase 7-8 | B7 포스트프로세싱 | C3 콘솔/로그 | 3일 |
| Phase 8 | C7 드래그&드롭 | C8 복사/붙여넣기 | 2-3일 |
| Phase 9 | D4 애니메이션 FSM | D5 트위닝/이징 | 1-1.5주 |
| Phase 9-10 | A7 물리 후반 | A3 코루틴 | 3-5일 |
| Phase 11 | E2 크로스플랫폼 | E3 Catch2 테스트 | 1주 |

**병렬 개발 시 예상 절약: 약 4-6주** → MVP 30주, 전체 56주 가능

---

## 7. 외부 라이브러리 계획

| 시스템 | 라이브러리 | 버전 | 라이선스 | 도입 Phase |
|--------|-----------|------|---------|-----------|
| 2D 물리 | [Box2D](https://github.com/erincatto/box2d) | 3.x | MIT | Phase 9 |
| 프로파일링 | [Tracy](https://github.com/wolfpld/tracy) | Latest | BSD | Phase 12 |
| 테스팅 | [Catch2](https://github.com/catchorg/Catch2) | v3 | BSL-1.0 | Phase 11 |
| GUID 생성 | [crossguid](https://github.com/graeme-hill/crossguid) | Latest | MIT | Phase 10 |
| 파일 감시 | [efsw](https://github.com/SpartanJ/efsw) | Latest | MIT | Phase 10 |
| WebGL | [Emscripten](https://emscripten.org/) | Latest | MIT | Phase 11 |

**통합 방식**: 모두 CMake `add_subdirectory()` 또는 `FetchContent`로 `external/` 디렉토리에 관리

---

## 8. 리스크 분석 및 대응

| 리스크 | 확률 | 영향 | 대응 전략 |
|--------|------|------|----------|
| **Box2D 3.x API 불안정** | 중 | 높음 | 얇은 래퍼 레이어로 격리. API 변경 시 래퍼만 수정 |
| **배치 렌더러 성능 미달** | 낮 | 높음 | 텍스처 배열(GL_TEXTURE_2D_ARRAY) 백업 전략. 멀티 텍스처 슬롯 |
| **WebGL 호환성 문제** | 높 | 중 | OpenGL ES 3.0 서브셋으로 제한. 셰이더 전처리기로 분기 |
| **에디터 복잡도 폭발** | 중 | 중 | EditorWindow 패턴 엄격 준수. 윈도우 간 결합도 최소화 |
| **1인 개발 번아웃** | 높 | 높음 | Phase 단위 완결. 각 Phase 후 데모 게임으로 성취감 확보 |
| **크로스 플랫폼 빌드 깨짐** | 중 | 중 | CI/CD 우선 구축(E1). PR마다 3 플랫폼 자동 빌드 검증 |

---

## 9. 품질 게이트

각 Phase 완료 시 반드시 통과해야 할 검증 항목:

### 자동 검증
- [ ] CI 빌드 성공 (macOS, 추후 Win/Linux)
- [ ] 전체 테스트 통과 (CTest → Catch2)
- [ ] 메모리 누수 없음 (AddressSanitizer)
- [ ] 컴파일 경고 0개 (-Wall -Wextra)

### 수동 검증
- [ ] 데모 게임/씬이 새 기능을 실제 사용
- [ ] 에디터에서 새 기능의 Inspector 통합 동작
- [ ] 10분 이상 연속 실행 시 크래시 없음
- [ ] 성능 회귀 없음 (이전 Phase 대비)

---

## 10. 최종 목표 아키텍처

Phase 12 완료 시 예상 디렉토리 구조:

```
src/
├── Common/          Constants, Log, Types, linmath, GUID
├── Core/
│   ├── AssetDatabase, AssetImporter, Prefab
│   ├── Bootstrap, MolgaTime, PathConstants
│   ├── Coroutine, EventBus, ObjectPool, Tween
│   ├── Profiler, Scene, SceneSerializer, TextureManager
│   └── SaveSystem, GamePrefs
├── ECS/
│   ├── Component, ComponentFactory, GameObject
│   ├── GameObjectLifecycle, TagLayer
│   └── Components/
│       ├── Transform, SpriteRenderer, BoxCollider2D
│       ├── CircleCollider2D, PolygonCollider2D, CapsuleCollider2D
│       ├── Rigidbody2D, Camera, AudioSource, AudioListener
│       ├── ParticleSystem, Animator
│       └── NavAgent
├── Editor/
│   ├── BuildManager, Editor, EditorConstants, EditorState
│   ├── EditorTheme, FontManager, GameBuilder, ImGuiLayer
│   ├── Project, SceneOperations, UIRegistry, UndoSystem
│   ├── VSCodeIntegration, WindowManager
│   └── Windows/
│       ├── EditorWindow, SceneViewWindow(FBO+기즈모)
│       ├── HierarchyWindow, InspectorWindow
│       ├── ProjectBrowserWindow, ProjectWindow
│       ├── ScriptWindow, StatsWindow
│       ├── ConsoleWindow, ProfilerWindow
│       ├── AnimationEditorWindow, SettingsWindow
│       └── (각 윈도우 .h/.cpp)
├── Physics/
│   ├── PhysicsWorld, PhysicsQuery, PhysicsMaterial2D
│   ├── Collision(기존)
│   └── Joints/ (Distance, Hinge, Spring)
├── Platform/
│   ├── Platform(기존), PlatformWindow, PlatformFileSystem
│   ├── Desktop/, Web/
│   └── PlatformInput, PlatformAudio
├── Rendering/
│   ├── Animation, Camera2D, CameraSystem
│   ├── Renderer, Shader, ShaderManager, Material
│   ├── Sprite, SpriteSheet, SpriteAtlas, SpriteBatcher
│   ├── SortingLayer, DebugDraw
│   ├── Texture, TextRenderer, Tilemap
│   ├── PostProcessing/ (Bloom, Vignette, ChromaticAberration, ...)
│   └── Lighting2D/ (PointLight, GlobalLight, Shadow)
├── Navigation/
│   ├── NavGrid, Pathfinder(A*), NavAgent
│   └── ObstacleAvoidance
├── Scenes/          (샘플 씬들)
├── Scripting/       (Script, ScriptManager, ScriptCompiler, BuiltinScripts)
├── Systems/
│   ├── Audio, Input, InputSystem, InputAction
│   └── Particle
├── UI/              (런타임 UI)
├── Animation/       (AnimatorController, AnimState, AnimTransition, AnimParameter)
├── main.cpp
└── runtime_main.cpp
```

**예상 규모**: ~200+ 소스 파일, ~50,000+ LOC (현재 ~107파일 대비 2배)

---

## 11. 성공 지표

| 지표 | MVP (Phase 9) | 완전체 (Phase 12) |
|------|---------------|-------------------|
| Draw Call (10K 스프라이트) | ≤ 20 | ≤ 10 |
| 프레임레이트 (10K 스프라이트) | 60fps | 60fps |
| 에디터 기능 수 | 15+ 윈도우/패널 | 20+ 윈도우/패널 |
| 지원 컴포넌트 | 12+ 빌트인 | 18+ 빌트인 |
| 테스트 커버리지 | 50%+ | 70%+ |
| 지원 플랫폼 | macOS | macOS + Win + Linux + Web |
| 데모 게임 | 2D 플랫포머 | 2D 플랫포머 + 탑다운 RPG |

---

## 부록 A: 상세 설계 문서 참조

| 문서 | 내용 | 시스템 수 |
|------|------|----------|
| [01_core_engine.md](01_core_engine.md) | 이벤트, 풀링, 코루틴, 입력, 에셋, 프리팹, 물리 | 7개 |
| [02_rendering.md](02_rendering.md) | 배치, 아틀라스, 셰이더, 소팅, 카메라, 포스트, 라이팅, 디버그 | 8개 |
| [03_editor.md](03_editor.md) | 씬뷰, Undo, 콘솔, 프로파일러, 멀티편집, 애니에디터, D&D 등 | 10개 |
| [04_gameplay.md](04_gameplay.md) | 애니FSM, 네비, 태그/레이어, 라이프사이클, 트윈, 세이브 등 | 9개 |
| [05_platform.md](05_platform.md) | 크로스플랫폼, WebGL, CI/CD, Catch2, Tracy, 에셋번들, 로컬라이제이션 | 10개 |

각 문서에는 시스템별로 다음이 포함됨:
- Unity 구현 방식 분석
- C++17 구현 설계 (코드 포함)
- 권장 라이브러리/알고리즘
- 복잡도 및 의존성
- 에디터 통합 방법

---

## 부록 B: Phase별 예상 소요 요약

| Phase | 기간 (순차) | 병렬 최적화 시 | 누적 |
|-------|------------|---------------|------|
| P0 선행 정비 | 1주 | 1주 | 1주 |
| Phase 6 기반 인프라 | 4-6주 | 4-5주 | 5-6주 |
| Phase 7 렌더링 | 6-10주 | 5-8주 | 10-14주 |
| Phase 8 에디터 | 4-6주 | 3-5주 | 13-19주 |
| Phase 9 게임플레이 | 8-12주 | 7-10주 | 20-29주 |
| Phase 10 에셋 | 6-10주 | 5-8주 | 25-37주 |
| Phase 11 플랫폼 | 4-8주 | 3-6주 | 28-43주 |
| Phase 12 고급 | 6-12주 | 5-10주 | 33-53주 |

**MVP (게임 제작 가능)**: Phase 0-9 → 약 20-29주 (5-7개월)
**전체 완성**: Phase 0-12 → 약 33-53주 (8-13개월)

---

*이 문서는 `docs/design/` 하위의 6개 설계 문서(00_roadmap.md ~ 05_platform.md)를 종합하여 작성되었습니다. 각 시스템의 상세 설계와 코드 예시는 해당 문서를 참조하세요.*
