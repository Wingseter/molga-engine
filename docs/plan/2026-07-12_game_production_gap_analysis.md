# 게임 제작 관점 갭 분석 — Unity/Godot 대비 (최초 2026-07-12, 상태 갱신 2026-07-25)

> 최초 분석 기준: `finetune` 브랜치 `bf1bb74`.
> 구현 재검증 기준: P0 기능 커밋 `804479f`와 2026-07-16 P1, 2026-07-17 P2,
> 2026-07-18 Post-processing MVP, 2026-07-21 멀티 카메라 출력,
> 2026-07-25 2D 라이팅 MVP 작업 트리. 코드와 테스트를 직접 대조했다.
> 관점: "이 엔진으로 실제 2D 게임 한 편을 만들어 출시할 수 있는가?"
> 기존 문서와의 관계: `2026-06-06_project_gap_analysis.md`의 P0와
> `user_experience/01_commercial_engine_gap_analysis.md`의 UX-1~5는 대부분 해결된 상태다.
> 이 문서는 **게임 콘텐츠 제작 기능**의 현재 구현선과 다음 갭을 추적한다.

---

## 0. 구현 현황 요약

| 우선순위 | 원래 범위 | 2026-07-25 상태 | 판정 |
|---|---|---|---|
| P0 | 씬 전환, 물리, 한글 텍스트, ECS UI, 저장 | **5/5 최소선 완료** | 기존 게임 제작 차단 조건 해소 |
| P1 | Animator, 텍스처 설정, 타일맵 저작, 파티클 텍스처, 오디오 버스 | **5/5 완료** | 캐릭터 스테이지 저작·패키징 경로 성립 |
| P2 선택 묶음 | Script 예외 격리, Game View, 멀티 오브젝트 편집 | **3/3 완료** | 제작 안정성과 반복 편집 경로 성립 |
| P2 표현력 후속 | Pixel Perfect, Y-sort, 소팅 레이어, Post-processing MVP, 멀티 카메라 출력, 2D 라이팅 MVP | **6/6 완료** | 픽셀 출력·소팅·화면 효과·분할/PIP·Camera별 조명 합성 경로 성립 |
| 그 밖의 P2~P3 | 표현력, 디버깅, 배포 | 일부 기반만 존재 | 상용 엔진 대비 큰 격차 유지 |

P0가 목표로 삼은 "타이틀 화면 + 스테이지 2개 + 한글 UI + 물리 + 저장"의 기술 경로는 성립한다.
패키징 E2E가 `main → stage1 → stage2` UI 전환, 실제 한글 글리프/아틀라스 쿼드, Box2D 접촉과 물리 재질,
`PlayerPrefs` 및 슬롯 세이브의 디스크 재로드까지 한 번에 검증한다. 다만 전체 빌드 씬 목록을 자유롭게
추가·삭제·정렬하는 UI가 제한적이므로, "모든 과정을 에디터 UI만으로 저작"하는 UX는 아직 완성형이 아니다.

P1에서는 [캐릭터 스테이지 저작 파이프라인](2026-07-16_game_production_p1_plan.md)을 구현했다. spritesheet slicing과
Animator FSM, 3-layer terrain tilemap, textured particle, Music/SFX bus와 crossfade가 동일한 GUID catalog와 패키징
경로를 사용하며, 독립 런타임 E2E에서 함께 검증된다.

P2에서는 [제작 안정성과 편집 생산성 묶음](2026-07-17_game_production_p2_plan.md)의 세 항목과
[Post-processing MVP](2026-07-18_game_production_p2_post_processing_plan.md),
[멀티 카메라 출력](2026-07-21_game_production_p2_multi_camera_output_plan.md),
[2D 라이팅 MVP](2026-07-25_game_production_p2_2d_lighting_plan.md)를 완료했다.
Pixel Perfect Camera, Y-sort와 소팅 레이어도 표현력 후속 구현선에 포함된다.
Script 예외는 인스턴스 단위로 격리되고, Game View와 Scene View가 분리됐으며, ordered multi-selection부터
공통 속성 batch Undo와 멀티 Transform까지 이어진다. 아래에 남긴 다른 P2~P3 후보는 완료 처리하지 않는다.

### 검증 기준

- 2026-07-25 2D 라이팅 최종 검증: Debug/Release/ASan/UBSan 각각 전체 build와
  CTest **78/78 통과**. 네 preset의 editor/runtime/build smoke와 패키지 E2E가
  통과했고, report에서 lit Camera 1, lighting/shadow fallback 0, selected/shadowed
  light 1, caster draw 1과 양수 pass count를 확인했다.
- 2026-07-21 멀티 카메라 최종 검증: Debug/Release/ASan/UBSan 각각 전체 build와 CTest **77/77 통과**.
- 네 preset 모두 `editor_smoke`, `runtime_smoke`와 패키징 `smoke_end_to_end`를 통과했다. 패키지 runtime report에서
  선택 2, 렌더 2, PostFX 1, fallback 0과 output camera pass 2를 확인했다.
- 현재 label은 unit 72, gl 2, smoke 4(그중 e2e 1)이며 ASan/UBSan에서도 전체 suite가 통과했다.
- 역사적 기준선인 2026-07-17 P2 최종 검증은 Debug/Release/ASan/UBSan 각각 전체 build와 CTest **74/74 통과**였다.
  당시 label은 unit 68, gl 2, smoke 4(그중 e2e 1)였고, 패키지 fault probe는 예외 callback 진입,
  fault 시 `OnDisable`, 접촉 상대 Script의 계속 실행을 확인했다.
- P0 기준선은 현재 suite 안에서 scene/runtime/physics/font/UI/storage 회귀로 계속 검증된다.
- 핵심 회귀 테스트: `tests/test_scene_runtime.cpp`, `tests/test_physics.cpp`, `tests/test_physics_query.cpp`,
  `tests/test_font.cpp`, `tests/test_ui.cpp`, `tests/test_storage.cpp`, `tests/test_script_invocation_boundary.cpp`,
  `tests/test_game_view.cpp`, `tests/test_camera_output_layout.cpp`, `tests/test_framebuffer_gl.cpp`,
  `tests/test_editor_property_descriptor.cpp`, `tests/smoke/run_end_to_end.cmake`.

### 이미 갖춰진 에디터 기반

- Scene View: FBO 뷰포트, 피킹, 선택 아웃라인, 이동/회전/스케일 기즈모, 그리드 (`src/Editor/Windows/SceneViewWindow.*`, `src/Editor/Gizmos/`)
- Play-in-editor + Stop 시 편집 상태 복원 (`SceneDocument`의 edit/play World 분리)
- Undo/Redo 커맨드와 dirty 추적 (`src/Editor/Commands/`)
- 에셋 GUID/.meta/임포터/파일 워처/런타임 카탈로그 (`src/Core/AssetDatabase.*`, `AssetMeta.h`)
- 프리팹, 오버라이드(diff/apply/revert/unpack), 중첩 프리팹과 순환 방어 (`src/Core/SceneSerializer.cpp`, `tests/test_prefab.cpp`)
- Unity 스타일 스크립트 생명주기, 인스턴스 예외 격리, 필드 리플렉션, 비동기 컴파일, last-good 핫리로드와 필드 보존 (`src/Scripting/`)
- 콘솔, 프로파일러, 렌더 큐와 스프라이트 배칭
- 별도 Game View와 ordered multi-selection/공통 속성 batch Undo
- staging→검증→원자적 스왑 패키징, 유저 스크립트 동봉, 헤드리스 smoke, CTest 78개

---

## 1. P0 구현 상태 — 최소선 완료

여기서 "완료"는 최초 문서에 적은 게임 성립 최소선을 만족한다는 뜻이며 Unity/Godot 기능 동등성을 뜻하지 않는다.

### 1.1 런타임 씬 전환 — **완료**

- `SceneRuntime`이 등록된 씬 ID→파일 카탈로그와 단일 활성 `World`를 소유한다. 요청은 즉시 교체하지 않고
  Update/LateUpdate/렌더/이벤트 처리가 끝난 프레임 경계에서 트랜잭션으로 커밋된다 (`src/Core/SceneRuntime.*`, `src/runtime_main.cpp`).
- 후보 씬 준비·에셋 해석 실패 시 기존 World를 유지하며, load/unload/failure 이벤트와 재진입 방어가 있다.
- `Script::LoadScene`, `GetActiveScenePath`, `IsSceneLoadPending`가 공개됐고, 직렬화 가능한 `SceneLoadButton`도 UIButton 클릭을 연결한다.
- `BuildProfile`/ProjectSettings가 `startupScene`과 다중 `scenes`를 저장·검증하고, 빌드 플랜·패키지·`game.json`의
  `startupSceneId`/`sceneCatalog`까지 이어진다. 에디터 Play 전환 후 Stop은 원래 편집 World를 복원한다.
- 검증: `tests/test_scene_runtime.cpp`의 생명주기·프레임 경계·실패 롤백·스크립트 API와 3씬 패키징 E2E.
- **잔여 갭:** additive/async/진행률/취소/독립 unload, `DontDestroyOnLoad`, 완전한 빌드 씬 목록 편집 UI.
  레거시 `src/Core/Scene.*`는 기능 경로가 아니라 정리 대상 잔존 코드다.

### 1.2 물리 백엔드 — **완료**

- Box2D 3가 항상 빌드·링크되는 단일 스텝 백엔드다. 각 `World`의 `PhysicsWorld`가 `b2World`를 한 번 만들고
  수명 끝까지 유지하며, 바디·shape·필터·재질·속성 변경을 증분 동기화한다 (`src/Physics/PhysicsWorld.cpp`).
- 프로젝트의 gravity/pixels-per-meter/substeps가 직렬화·에디터·런타임에 연결됐고, 회전/각속도/부모 Transform과
  Box/Circle 오프셋을 Box2D 형상에 반영한다.
- `Collider2D`의 friction/restitution이 직렬화·인스펙터·Box2D shape 갱신에 연결됐다.
  충돌/트리거/레이어 매트릭스/Raycast·Overlap도 현재 Box2D 월드를 사용한다.
- 검증: 증분 body/shape 수명, 회전 경사면, 반발, 다중 shape 콜백 중복 제거, 런타임 레이어 변경,
  회전 쿼리와 패키지 접촉을 `tests/test_physics*.cpp` 및 E2E에서 확인한다.
- **잔여 갭:** Joint 전부, Polygon/Capsule/Edge 콜라이더, 노출·직렬화된 CCD/bullet·sleep 설정,
  공유 `PhysicsMaterial2D`/combine mode, shape cast와 고급 쿼리. 레거시 `BoxCollider2D::CheckCollision*` 직접 헬퍼는
  회전 형상의 enclosing AABB를 사용하므로 정리 대상이다.

### 1.3 TTF/OTF·UTF-8 텍스트 — **완료**

- `.ttf`/`.otf`를 `FontImporter`와 `FontFace`(ImGui 번들의 stb_truetype)로 검증·로딩하고 GUID 에셋으로 관리한다.
- 엄격한 UTF-8 scalar 디코더, metrics/kerning/rasterization, `(fontGuid, pixelSize)`별 지연 다중 페이지 글리프 아틀라스,
  배칭 가능한 glyph quad 제출이 구현됐다 (`src/Rendering/Utf8.*`, `FontFace.*`, `FontAtlas.*`, `TextRenderer.cpp`).
- `TextRenderer2D`와 `UILabel`이 font GUID를 사용하며 인스펙터 drag/drop, Scene View/런타임 렌더,
  재임포트 시 아틀라스 무효화, 씬·프리팹 재귀 패키징 검증까지 연결된다.
- Noto Sans KR fixture로 실제 한글 codepoint, 글리프 bitmap, 아틀라스 texture quad와 패키지 해석을 검증한다.
- **잔여 갭:** SDF/MSDF, 폴백 폰트 체인, shaping/정규화, word wrap·clipping·rich text, 아틀라스 eviction/메모리 예산.
  OTF는 동일 경로로 허용되지만 현재 실제 폰트 회귀 fixture는 TTF뿐이다. `fontName`은 호환 필드로만 남아 있고,
  현재 엔트리포인트가 쓰지 않는 `TextRenderer2D::RenderSprite` 직접 호출 경로에는 레거시 ASCII 렌더가 남아 있다.

### 1.4 직렬화 가능한 ECS UI — **완료**

- `UICanvas`, `RectTransform`, `UIImage`, `UILabel`, `UIButton`이 등록된 ECS 컴포넌트이며 씬 직렬화된다.
- Canvas의 reference resolution/width-height match와 RectTransform의 anchor min/max, pivot, stretch, 중첩 rect가
  화면 크기에 맞춰 해석된다. Image tint/texture, Label TTF·정렬, Button 상태색·capture·release-inside click이 동작한다.
- `UISystem`은 draw order 기반 hit test와 단일 pointer capture를 처리하고 스크립트 Update 전에 클릭 콜백을 발행한다.
- Hierarchy 생성 preset, 자동 Canvas/버튼 Label, undo/dirty, 전용 인스펙터, 에셋 drag/drop, Scene View 렌더·피킹·outline이 연결됐다.
- 검증: 스케일/앵커/중첩, topmost capture, 비활성 계층, 콜백 중 World 제거 안전성, 한글 직렬화와 UI 기반 2회 씬 전환 E2E.
- **잔여 갭:** camera/world-space canvas, layout group/content sizing, nine-slice, mask/clipping/scroll view,
  text wrap/overflow, 키보드·게임패드 focus/navigation, touch/multi-pointer, 직렬화된 선언형 이벤트, rect 전용 기즈모.
  기존 `src/UI/UI.*`는 새 ECS 경로로 대체됐지만 아직 컴파일되는 정리 대상이다.

### 1.5 PlayerPrefs·슬롯 저장 — **완료**

- `PlayerPrefs`는 bool/int/double/string, 엄격한 타입 읽기, dirty/delete/save/shutdown을 제공하고 `prefs.json`을 원자 교체한다.
- `SaveSystem`은 경로 순회를 막는 슬롯 이름 검증과 schema v1 JSON envelope의 save/load/delete/exists를 제공한다.
- 공통 `PersistentStorage`가 flush 후 원자 교체하며, 런타임은 `<플랫폼>/<company>/<game>`, 에디터 Play는
  프로젝트 hash로 격리된 경로를 쓴다. Windows `%LOCALAPPDATA%`, macOS Application Support,
  Linux XDG/`~/.local/share` 분기가 구현됐다.
- BuildProfile의 company/game 검증부터 `game.json`, 런타임 초기화와 정상 종료 flush까지 연결된다.
- 검증: 타입·dirty·손상 파일·원자 실패 보존·슬롯 traversal/schema와 패키지 프로세스의 디스크 재로드.
- **잔여 갭:** 슬롯 열거/메타데이터, 구 schema migration, backup/cloud/encryption/compression, async I/O와 thread safety.
  Windows/Linux 저장 경로 분기는 현재 macOS 검증에서 직접 실행되지는 않았다.

---

## 2. 캐릭터 스테이지 저작 파이프라인 (P1 — 5/5 완료)

완료 계약과 비목표, 상세 검증은 [P1 구현 계획](2026-07-16_game_production_p1_plan.md)에 기록했다.

### 2.1 AnimationClip2D·Animator2D — **완료**

- `.animclip`의 per-frame duration과 `.animator`의 typed parameter, stable state, 우선순위 transition, exit time과 trigger 소비 계약을 구현했다.
- `Animator2D`는 script Update 뒤 전용 phase에서 평가되며 `SpriteRenderer` runtime override를 사용하고 Stop/disable 시 authored sprite를 복구한다.
- Animation Window에서 clip frame 편집·scrub·preview와 controller state/transition 표 편집을 제공한다.
- **잔여 갭:** 그래프 에디터, AnyState, blend tree, sprite crossfade, animation event, Marrow 통합과 일반 tween 시스템.

### 2.2 텍스처 설정·안정적 SpriteRef — **완료**

- texture meta v2에 filter, U/V wrap, mipmap, color space, PPU, pivot과 stable slice ID를 저장하고 unknown key와 v1 화면 결과를 보존한다.
- `SpriteRef {textureGuid, sliceId}` 해석, grid/manual reslice, Native/Custom size, authored/runtime sprite 분리와 PPU/pivot 적용을 연결했다.
- 재임포트는 성공 시에만 기존 `Texture*` 내부 리소스를 교체하며 실패하면 pointer와 last-good GPU 리소스를 보존한다. Scene View와 runtime의 sRGB 상태도 명시적으로 관리한다.
- **잔여 갭:** 압축·플랫폼 override, atlas packing/extrusion, nine-slice.

### 2.3 네이티브 TileSet·다층 Tilemap — **완료**

- `.tileset`의 sprite/solid/terrain 규칙과 `TilemapRenderer` v2의 stable layer, RLE cell, visibility/lock/collision/opacity/sorting 설정을 구현했다.
- Tile Palette가 pencil, erase, rectangle, flood fill, eyedropper, ghost/grid와 한 drag당 단일 sparse Undo command를 제공한다. NESW 16-mask terrain은 변경 셀과 4-neighbor만 갱신한다.
- 32×32 dirty chunk geometry, view culling, 2,048-quad batching과 revision별 horizontal solid-run collision을 사용한다. legacy 단층 경로는 읽기 호환하며 명시적 변환만 수행한다.
- **잔여 갭:** Tiled import, weighted variant, 8방향/47-tile terrain, 회전·비균일/음수 scale 저작.

### 2.4 텍스처·곡선 기반 CPU 파티클 — **완료**

- SpriteRef frame, Start/Random/OverLife 모드, `FloatCurve`, `ColorGradient`, Alpha/Additive blend와 local/world simulation을 구현했다.
- emitter별 seed의 결정적 RNG와 active/free pool을 사용하고 emitter 단위 immutable multi-quad command로 제출한다.
- 편집 preview는 직렬화되지 않는 runtime copy를 사용해 scene dirty와 Undo 기록을 오염시키지 않는다.
- **잔여 갭:** GPU simulation, particle collision, noise, sub-emitter.

### 2.5 AudioService·고정 버스·fade — **완료**

- 전역 경로와 `AudioSource`를 generation 검증 `VoiceHandle`을 쓰는 단일 `AudioService`로 통합했다.
- `Master/{Music,SFX,Voice,UI}` volume/mute 저장, GUID one-shot/music/crossfade/stop, bus fade API와 `AudioSource` output bus를 구현했다.
- AudioImporter가 decoder header로 손상 파일을 거부하고 duration/channel/sample rate와 load mode를 기록한다. no-device backend가 중첩·fade·해제 수명을 검증한다.
- **잔여 갭:** 임의 bus graph, ducking, DSP effect, snapshot.

---

## 3. 표현력 격차 (P2 — 있으면 상용급, 없어도 게임은 나옴)

| 영역 | Molga 현재 | Unity | Godot |
|---|---|---|---|
| 2D 라이팅/노멀맵/그림자 | **완료 (MVP)** — Camera별 opt-in Ambient/PointLight2D, Sprite normal map·Tilemap flat normal, convex hard shadow, 결정적 8/4/64 예산과 PostFX·전역 UI 통합 | URP 2D Lights | 2D Light/Occluder 기본 |
| 포스트 프로세싱 | **완료 (MVP)** — Camera opt-in `.postfx`, ordered Bloom/Color Adjust/Vignette, HDR world·UI 분리, Scene View FX preview와 패키지 runtime | Volume/URP | WorldEnvironment |
| 멀티 카메라 | **완료 (MVP)** — 최대 8개 출력 카메라, normalized viewport·depth 합성, 독립 layer mask·PostFX, 전역 UI 1회와 IntegerFit 출력 | 다중 카메라+스택 | Viewport 다중 |
| 픽셀 퍼펙트/레터박스 | **완료** — opt-in Pixel Perfect Camera와 IntegerFit 논리 FBO, nearest 정수배 출력, bars/crop 입력 매핑 | Pixel Perfect Camera | 프로젝트 설정 스케일 모드 |
| Y-sort | **완료** — Sprite/Text/Particle emitter/Marrow 컴포넌트 Y-sort, Tilemap은 고정 layer/order 유지 | Transparency Sort Axis | Y-sort 토글 |
| 소팅 레이어 | **완료** — ProjectSettings 순서를 월드 렌더 제출·Inspector·Scene Picker에 연결 | O | O (CanvasLayer/z-index) |
| 커스텀 머티리얼 | 있음, 단 커스텀 속성 있으면 배칭 제외 | SRP Batcher | 자동 |
| 내비게이션/A* | 없음 | NavMesh | NavigationServer2D |
| 스프라이트 마스크/IK/Effector | 없음 | O | 일부 O |

## 4. 개발자 경험 격차 (P2)

- **Script C++ 예외 격리 — 완료.** `ScriptInvocationBoundary`가 Awake/OnEnable/Start, frame update, contact,
  timer/coroutine을 인스턴스 handle로 재해석한다. 표준·알 수 없는 예외는 해당 인스턴스만 fault/disabled 처리하고
  예약 작업을 취소하며, 안전한 OnDisable·단일 진단·Error Pause·명시적 재활성화를 지원한다. 저장된 enabled 상태는
  lifecycle 없이 복원한다. **잔여 갭:** segfault/abort와 프로세스 격리, owner 없는 raw EventBus/UI callback.
- 스크립트 필드에는 단순 enum까지 추가됐지만 배열/리스트와 텍스처·오디오 등 일반 typed asset ref는 아직 없다.
- 디버거 스토리 부재(어태치 가이드, 심볼, 브레이크포인트 워크플로 문서화 없음).
- C++ 단일 언어: CMake 툴체인이 게임 제작자의 진입 장벽. Unity=C#, Godot=GDScript/C#과 반복 속도 격차가 크다. (Lua/AngelScript 등 보조 스크립트 언어는 장기 검토 항목)
- **멀티 오브젝트 편집 — 완료.** ordered unique 선택/primary/range/lock, 공통 Component와 축별 mixed 속성,
  typed asset·단순 Script field batch Undo, root-most delete/duplicate, 중심 pivot의 멀티 Move/Rotate/Scale을 제공한다.
  **잔여 갭:** box selection, 멀티 구조 편집, 배열·tile cell·curve 편집, Play 변경 영구 반영.
- **별도 Game View — 완료.** Edit/Play의 출력 카메라 합성+전역 UI를 공통 출력 경로로 렌더하고, 고정 해상도 preset,
  Fit/100%, HiDPI 매핑, 분리 창 focus 입력과 사용자 preference를 제공한다. Scene View는 editor camera로 분리됐다.
  **잔여 갭:** RenderTexture/사용자 target, 투명 camera stack/clear mode, 카메라 귀속 UI.

## 5. 배포 격차 (P2~P3)

- 빌드 타깃이 `"host"`만 허용 (`src/Core/BuildPlan.cpp:18-19`) — 크로스 빌드 불가. 모바일/웹(Emscripten)/콘솔 없음. Unity/Godot의 최대 강점 영역.
- 에셋이 루즈 파일 그대로 복사됨 — 아카이브/압축(LZ4)/무결성 없음.
- vsync 토글, 런타임 해상도 변경 API 없음 (`game.json`의 width/height/fullscreen 고정뿐).
- CI가 macOS 중심. Windows/Linux `#ifdef`는 있으나 검증 게이트 없음.
- 네트워킹/로컬라이제이션 없음 (둘 다 장르에 따라 선택 사항이지만 로드맵 E9와 함께 인지 필요).

---

## 6. 갱신된 권장 우선순위 — P2 선택 묶음 이후

최초 검증 시나리오인 **"타이틀 화면 + 스테이지 2개짜리 한글 UI 플랫포머를 제작·패키징"**의
런타임·패키징 기술 조건은 P0 E2E로 충족했고, P1 E2E가 캐릭터 콘텐츠 저작 파이프라인을,
P2 suite가 선택된 제작 안정성·편집 생산성 묶음을 검증했다.
다음 우선순위는 실제 게임 장르와 출시 플랫폼에 맞춰 선택한다.

1. 장르에 맞춰 P0 후속선 보강 — Joint/CCD, UI layout·navigation, 폰트 fallback·wrap,
   additive/async 씬, 저장 migration 중 실제 게임에 필요한 것부터 선택
2. **완료:** Script 예외 격리, 별도 Game View, 멀티 오브젝트 편집으로 제작 안정성과 반복 속도 개선 (4)
3. **완료:** pixel-perfect 출력, Post-processing MVP, 멀티 카메라 합성과 2D 라이팅/노멀맵/hard shadow MVP (3)
4. Windows/Linux CI, archive/integrity, 해상도 API 등 배포선 강화 (5)
5. 필요할 때 P1 후속 범위인 animation graph/blend, 8-way terrain, GPU particle, DSP mixer를 선택적으로 확장

현재 엔진은 더 이상 "씬 흐름·물리·텍스트·UI가 비어 있는 상태"가 아니다. 작은 2D 게임의 런타임 기반은
갖췄고 캐릭터 스테이지의 핵심 콘텐츠를 저작·패키징하는 경로도 성립한다. P2의 선택된 제작 안정성 묶음도
완료됐고 Post-processing MVP와 멀티 카메라 출력까지 패키지 런타임의 공통 경로에서 검증된다.
Camera별 2D 라이팅, normal map과 hard shadow도 같은 출력·PostFX·전역 UI 경로에서 검증된다.
남은 큰 격차는 **고급 표현력,
디버깅·스크립팅 편의, 플랫폼별 배포 검증**이며, P1/P2 비목표는
게임 요구가 생길 때 후속 마일스톤으로 다룬다.
