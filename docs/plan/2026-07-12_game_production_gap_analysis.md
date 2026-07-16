# 게임 제작 관점 갭 분석 — Unity/Godot 대비 (2026-07-12)

> 기준: `finetune` 브랜치 (bf1bb74). 코드 직접 조사 기반.
> 관점: "이 엔진으로 실제 2D 게임 한 편을 만들어 출시할 수 있는가?"
> 기존 문서와의 관계: `2026-06-06_project_gap_analysis.md`의 P0(렌더러 계약, 씬 통합, 경로, 계층 안전성, 테스트)와
> `user_experience/01_commercial_engine_gap_analysis.md`의 UX-1~5(선택/언두/콘솔/에셋 GUID/프로파일러)는 **대부분 해결됨**.
> 이 문서는 그 다음 단계인 **게임 콘텐츠 제작 기능**의 부족점을 다룬다.

---

## 0. 현재 잘 갖춰진 것 (전제)

에디터 백본은 상용 엔진 패턴에 근접했다. 이 부분은 재작업 대상이 아니다.

- Scene View: FBO 뷰포트, 피킹, 선택 아웃라인, 이동/회전/스케일 기즈모, 그리드 (`src/Editor/Windows/SceneViewWindow.h`, `src/Editor/Gizmos/`)
- Play-in-editor + Stop 시 편집 상태 복원 (`SceneDocument`의 editWorld/playWorld 분리, `src/main.cpp:175-307`)
- Undo/Redo 커맨드 ~22종 + dirty 추적 (`src/Editor/Commands/`)
- 에셋 GUID/.meta/임포터/파일 워처/런타임 카탈로그 (`src/Core/AssetDatabase.*`, `AssetMeta.h`)
- 프리팹 + 오버라이드(diff/apply/revert/unpack) (`src/ECS/Components/PrefabInstance.h`, `src/Core/PrefabUtil.h`)
- 스크립트: Unity 스타일 생명주기 전체, 필드 리플렉션(직렬화+인스펙터 통합), 비동기 컴파일, last-good 폴백 핫리로드 + 필드 보존 (`src/Scripting/`)
- 콘솔(필터/collapse/소스 점프), 프로파일러(프레임 타임라인), 렌더 큐 + 스프라이트 배칭
- 빌드: staging→검증→원자적 스왑 패키징, 유저 스크립트 동봉, 헤드리스 smoke 테스트, doctest 64개

---

## 1. 게임 제작 차단급 결함 (P0 — 이것 때문에 게임을 완성할 수 없음)

### 1.1 런타임 씬 전환이 없다

- 런타임은 `game.json`의 `mainScene` 하나만 로드한다 (`src/runtime_main.cpp:227`).
- 스크립트에서 호출할 수 있는 씬 로드/전환 API가 없다. 레거시 `SceneManager`(`src/Core/Scene.*`)는 어느 엔트리포인트에서도 사용되지 않는 죽은 코드다.
- **결과: "타이틀 → 스테이지1 → 스테이지2" 구조의 게임 자체가 불가능하다.**
- Unity: `SceneManager.LoadScene/LoadSceneAsync(additive)`. Godot: `change_scene_to_file()` + 노드 트리 조합.
- 필요 최소선: `World` 기반 단일 씬 교체 API(스크립트 표면 포함) + 빌드 시 다중 씬 등록. additive/async는 그 다음.

### 1.2 물리 백엔드가 사실상 프로토타입이다

- 기본 백엔드는 legacy 커스텀 물리(`CMakeLists.txt:10`), Box2D는 opt-in.
- Box2D 경로는 **매 스텝마다 b2World를 생성/파괴**한다 (`src/Physics/PhysicsWorld.cpp:164-237`). 솔버 웜스타트/지속 컨택트가 없어 스택 안정성·관절·반발이 성립하지 않고, 중력도 `{0, 9.81}` 하드코딩으로 프로젝트 설정을 무시한다.
- 콜라이더 회전 미지원(AABB 전제, `src/Physics/Collider2D.h:11-14`), 관절(Joint) 전무, Polygon/Capsule enum만 있고 미구현.
- 충돌 콜백·트리거·레이어 매트릭스·Raycast/Overlap 쿼리는 잘 갖춰져 있으나 전부 legacy 좁은 단계 기준이다.
- Unity(Box2D 기반 2D)/Godot(자체 2D 물리): 회전 콜라이더, 관절, 물리 머티리얼, CCD 기본 제공.
- 필요 최소선: **영속 b2World로 재작성**(생성 1회, 바디 증분 동기화), 회전 지원, 물리 머티리얼(마찰/반발), 이후 관절.

### 1.3 텍스트가 8x8 내장 비트맵 폰트(ASCII 96자)뿐이다

- `src/Rendering/TextRenderer.cpp`가 하드코딩된 8x8 모노 폰트를 사용. TTF 로딩 없음, 유니코드 없음.
- **한글 표시가 불가능하다.** `fontName` 필드는 직렬화만 되고 렌더링에 쓰이지 않는다 (`src/ECS/Components/TextRenderer2D.cpp`).
- Unity: TextMeshPro(SDF). Godot: 동적 TTF + 폴백 체인 기본.
- 필요 최소선: stb_truetype(이미 ImGui에 번들됨) 기반 아틀라스 베이킹 + UTF-8 처리. SDF는 그 다음.

### 1.4 인게임 UI가 게임을 만들 수 없는 수준이다

- `src/UI/UI.{h,cpp}`: Panel/Button/ProgressBar 3종. **텍스트 라벨 위젯조차 없고**, 절대 픽셀 좌표, 앵커/레이아웃/나인슬라이스 없음. 씬에 직렬화되는 컴포넌트도 아니어서 에디터에서 제작 불가.
- Unity: uGUI(Canvas/anchor/layout group) + UI Toolkit. Godot: Control 노드 체계(anchor/container/theme)가 특히 강력.
- 필요 최소선: 앵커 기반 RectTransform류 + Label/Image/Button을 ECS 컴포넌트로 만들어 씬 직렬화/에디터 편집 가능하게. (1.3 텍스트가 선행 조건)

### 1.5 세이브/설정 저장이 없다

- PlayerPrefs/GamePrefs/SaveSystem에 해당하는 코드가 전혀 없다 (grep 확인).
- 필요 최소선: 키-값 prefs(JSON) + 슬롯 세이브 유틸. 작지만 없으면 게임을 출시할 수 없다.

---

## 2. 캐릭터 게임을 막는 것 (P1 — 콘텐츠 제작 파이프라인)

### 2.1 애니메이션 제작 파이프라인 부재

- `src/Rendering/Animation.*`는 단순 프레임 시퀀스이며 **ECS 컴포넌트가 아니어서 에디터에서 저작·직렬화되지 않는다** (렌더 경로에도 연결 안 됨).
- 상태 머신(Animator/AnimationTree), 파라미터 기반 전환, 블렌딩, 애니메이션 이벤트, 에디터 타임라인(도프시트/커브) 전부 없음.
- Marrow 스켈레탈 런타임(`MarrowRenderer`, 크로스페이드 믹싱 지원)은 존재하나 `MOLGA_MARROW_SUPPORT` 뒤에 있고 형제 디렉토리 프리빌드가 필요해 기본 빌드에선 스텁이다.
- 트위닝/이징(DOTween/Godot Tween류) 없음. 코루틴은 콜백 기반 `Scheduler::StartCoroutine`만 존재.
- 필요 최소선: AnimationClip 에셋 + Animator 컴포넌트(FSM, 파라미터) + 스프라이트시트 클립 에디터.

### 2.2 스프라이트/텍스처 임포트 설정이 없다

- `TextureImporter`는 `stbi_info`로 크기만 읽는다 (`src/Core/Importers/TextureImporter.cpp`). 필터(point/bilinear), wrap, 밉맵, sRGB, pixels-per-unit, 압축 설정 전무.
- 스프라이트 아틀라스 패킹 없음(런타임 `SpriteSheet` 수동 지정만). 피벗/나인슬라이스/스프라이트 에디터 없음.
- 픽셀아트 게임이면 point 필터와 PPU가 즉시 필요하다.

### 2.3 타일맵이 저작 도구 없이 단층 그리드만 지원

- `TilemapRenderer`: 단일 레이어, 오토타일링 없음, Tiled(.tmx) 임포트 없음, 에디터 타일 팔레트 없음.
- 타일별 개별 스프라이트 드로우로 **배칭도 안 된다** (`TilemapRenderer.cpp:105-137`) — 큰 맵에서 성능 문제.
- Unity Tilemap(rule tile)/Godot TileMap(terrain, 레이어) 대비 가장 격차가 큰 저작 영역 중 하나. 최소한 Tiled 임포터라도 있어야 실전 사용 가능.

### 2.4 파티클이 텍스처를 못 쓴다

- 파티클은 단색 사각형만 렌더 (`src/ECS/Components/ParticleSystem.cpp:111-132`), 배칭 없음, 커브 기반 파라미터/에디터 프리뷰 없음.

### 2.5 오디오 믹서 부재

- 마스터+개별 볼륨뿐, 버스/그룹(BGM/SFX/Voice), DSP 이펙트, 스냅샷 없음 (`src/Systems/Audio.cpp`). 2D 공간 오디오·스트리밍은 이미 있음.
- 스크립트 API에서 오디오 헬퍼도 미노출 (AudioSource 컴포넌트 직접 접근만).

---

## 3. 표현력 격차 (P2 — 있으면 상용급, 없어도 게임은 나옴)

| 영역 | Molga 현재 | Unity | Godot |
|---|---|---|---|
| 2D 라이팅/노멀맵/그림자 | 없음 (셰이더가 texture*color 뿐) | URP 2D Lights | 2D Light/Occluder 기본 |
| 포스트 프로세싱 | 없음 (Framebuffer는 에디터 뷰포트 전용) | Volume/URP | WorldEnvironment |
| 멀티 카메라 | 메인 카메라 1개만 렌더 (`runtime_main.cpp:281-299`) | 다중 카메라+스택 | Viewport 다중 |
| 픽셀 퍼펙트/레터박스 | 없음 | Pixel Perfect Camera | 프로젝트 설정 스케일 모드 |
| Y-sort | 정렬 키에 필드는 있으나 항상 0 (미배선) | Transparency Sort Axis | Y-sort 토글 |
| 소팅 레이어 | ProjectSettings에 UI는 있으나 렌더 제출에 미적용 | O | O (CanvasLayer/z-index) |
| 커스텀 머티리얼 | 있음, 단 커스텀 속성 있으면 배칭 제외 | SRP Batcher | 자동 |
| 내비게이션/A* | 없음 | NavMesh | NavigationServer2D |
| 스프라이트 마스크/IK/Effector | 없음 | O | 일부 O |

## 4. 개발자 경험 격차 (P2)

- **스크립트 크래시 = 에디터 크래시.** Update/충돌 디스패치에 예외 방어가 없고(`World.cpp`, `PhysicsWorld.cpp` 확인) Play 모드가 에디터 프로세스 안에서 돈다. C++ 특성상 segfault는 못 막지만, 최소한 예외 try/catch + 해당 스크립트 비활성화 + 콘솔 에러 보고는 가능하다. Unity/Godot은 예외가 에디터를 죽이지 않는다.
- 스크립트 필드 타입에 배열/리스트, enum 드롭다운, 텍스처·오디오 등 일반 에셋 참조가 없다 (`ScriptField.h`: Float/Int/Bool/String/Vector2/Color/ObjectRef/PrefabRef 8종).
- 디버거 스토리 부재(어태치 가이드, 심볼, 브레이크포인트 워크플로 문서화 없음).
- C++ 단일 언어: CMake 툴체인이 게임 제작자의 진입 장벽. Unity=C#, Godot=GDScript/C#과 반복 속도 격차가 크다. (Lua/AngelScript 등 보조 스크립트 언어는 장기 검토 항목)
- 멀티 오브젝트 편집 없음(`SelectionService`가 단일 선택 전제), 중첩 프리팹 없음.
- **별도 Game View 없음** — 해상도/종횡비 프리셋으로 실제 게임 화면을 확인할 수단이 없다 (Scene View가 겸용).

## 5. 배포 격차 (P2~P3)

- 빌드 타깃이 `"host"`만 허용 (`src/Core/BuildPlan.cpp:18-19`) — 크로스 빌드 불가. 모바일/웹(Emscripten)/콘솔 없음. Unity/Godot의 최대 강점 영역.
- 에셋이 루즈 파일 그대로 복사됨 — 아카이브/압축(LZ4)/무결성 없음.
- vsync 토글, 런타임 해상도 변경 API 없음 (`game.json`의 width/height/fullscreen 고정뿐).
- CI가 macOS 중심. Windows/Linux `#ifdef`는 있으나 검증 게이트 없음.
- 네트워킹/로컬라이제이션 없음 (둘 다 장르에 따라 선택 사항이지만 로드맵 E9와 함께 인지 필요).

---

## 6. 권장 우선순위 — "게임 한 편 완성" 기준

검증 시나리오를 **"타이틀 화면 + 스테이지 2개짜리 한글 UI 플랫포머를 에디터만으로 제작·패키징"**으로 잡으면 순서가 명확해진다:

1. **씬 전환 API** (1.1) — 다중 씬 게임의 성립 조건. 규모 작음.
2. **영속 Box2D 월드** (1.2) — 플랫포머 점프/충돌 품질의 성립 조건.
3. **TTF 텍스트 + 유니코드(한글)** (1.3) — UI/대사의 성립 조건.
4. **직렬화 가능한 게임 UI 컴포넌트** (1.4) — 타이틀/HUD의 성립 조건. 3번 선행 필요.
5. **세이브/Prefs** (1.5) — 규모 작음.
6. **Animator(FSM) + 클립 저작** (2.1) — 캐릭터 표현의 성립 조건.
7. 텍스처 임포트 설정(PPU/필터) (2.2) → 타일맵 저작(2.3) → 파티클 텍스처(2.4) → 오디오 버스(2.5).
8. 그 다음에 라이팅/포스트/멀티카메라(3), 멀티셀렉션/중첩 프리팹/Game View(4), 크로스 플랫폼(5).

이미 반영된 교훈과 동일한 원칙: 표현력(라이팅 등)보다 **성립 조건(씬 전환, 물리, 텍스트, UI)** 이 먼저다. 현재 엔진은 "에디터 인프라는 상용 패턴에 근접했으나, 게임 콘텐츠를 담는 그릇(씬 흐름·물리·텍스트·UI·애니메이션)이 아직 비어 있는 상태"로 요약된다.
