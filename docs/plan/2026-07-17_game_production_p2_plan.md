# 게임 제작 P2 구현 계획 — 제작 안정성과 편집 생산성

> 작성일: 2026-07-17
>
> 상태: **완료 (2026-07-17)**
>
> 기준 문서: `docs/plan/2026-07-12_game_production_gap_analysis.md`
>
> 완료 규칙: 아래 세 축의 구현과 Debug/Release/ASan/UBSan 전체 빌드·CTest, 에디터 Play smoke,
> 패키지 런타임의 faulting Script 격리, `git diff --check`가 모두 통과한 뒤에만 갭 분석의 해당 세 항목을 완료 처리한다.

## 1. 범위와 구현 순서

이번 P2는 다음 세 항목으로 고정한다.

1. 스크립트 예외 격리
2. 별도 Game View
3. 멀티 오브젝트 편집

구현 순서는 예외 격리 → Game View → 멀티 편집 → 통합 검증이다. 기존 scene, prefab, build profile
형식은 바꾸지 않으며 Script fault 상태는 런타임 전용이다. 새로 저장되는 데이터는 버전이 있는 사용자 전용
Game View preference JSON뿐이다.

## 2. 스크립트 예외 격리

### 2.1 호출 경계와 fault 계약

- 모든 엔진 주도 Script 호출을 중앙 `ScriptInvocationBoundary`로 통합한다: `Awake`, `OnEnable`, `Start`,
  `Update`, `FixedUpdate`, `LateUpdate`, collision/trigger enter·stay·exit, Script가 등록한 `Invoke`, 반복 타이머,
  coroutine.
- `ScriptHandle { objectId, typeId, instanceId }`, `ScriptPhase`, 런타임 전용 `ScriptFaultInfo`를 사용한다.
  호출 직전에 핸들을 다시 해석해 삭제·교체된 인스턴스는 건너뛰며 raw owner 포인터에 의존하지 않는다.
- `std::exception`과 알 수 없는 C++ 예외를 잡는다. 첫 예외는 해당 Script 인스턴스만 fault/disabled로 만들고
  소유 예약 작업을 취소한다. 같은 프레임의 다른 Script와 접촉 상대 콜백은 계속 실행한다.
- 활성 Script가 fault로 전환될 때 `OnDisable`을 최대 한 번 경계 안에서 호출한다. 여기서 난 예외는 보조 오류로
  기록하되 전파하지 않는다.
- 진단에는 오브젝트 이름/ID, Script 타입/인스턴스 ID, 실행 단계, 메시지를 포함하고 동일 fault는 반복 출력하지 않는다.
  Console Error Pause가 켜져 있으면 현재 디스패치를 마친 프레임 경계에서 Play를 일시정지한다.
- Inspector에서 명시적으로 다시 활성화하면 fault를 해제하고 미완료 `Awake`/`Start`와 `OnEnable`을 경계 안에서
  재시도한다. 다시 실패하면 재격리한다.
- `Scheduler::Tick`은 RAII로 ticking 상태와 지연 삭제 큐를 항상 정리한다. Script가 아닌 엔진 Component 예외는
  기존 fail-loud 동작을 유지한다.

### 2.2 검증

- 모든 lifecycle/frame/contact 단계의 표준·알 수 없는 예외와 양쪽 contact callback 계속 실행
- timer/coroutine 예외, callback 중 자기 삭제·교체, Scheduler 정리
- fault 처리 중 `OnDisable` 재예외, 반복 로그 방지, 명시적 재활성화
- 비-Script Component 예외가 계속 전파되는지 확인

## 3. 별도 Game View와 공통 게임 출력 경로

### 3.1 렌더 계약

- 런타임과 Game View가 공유하는 `GameOutputRenderer`가 Main Camera 선택, 명시적 출력 크기에 따른 카메라 준비,
  render queue 수집, world/UI 렌더를 한 경로로 수행한다.
- Main Camera는 활성·enabled·`IsMain` 카메라 중 depth가 가장 높은 것을 선택하며, 동률은 씬 순서를 유지한다.
  카메라가 없으면 Game View를 비우고 에디터 오버레이로 알린다.
- `Camera::PrepareForViewport(PixelSize)`가 projection 준비의 유일한 출력 크기 입력이다. 런타임은 실제 framebuffer,
  Game View는 선택한 고정 해상도를 넘긴다.
- Game View는 Edit/Play 모두 Main Camera 결과와 UI를, Scene View는 항상 editor camera/grid/outline/gizmo를 표시한다.
  Play 중 Scene View는 play world를 편집자 시점에서 보여주며 변경은 Stop 때 폐기한다.

### 3.2 해상도·레이아웃·상태 복구

- Game View FBO 크기는 패널이 아니라 선택 해상도와 정확히 일치한다. 프리셋은 Build Resolution, 320×180,
  640×360, 1280×720, 1920×1080, 사용자 프리셋을 제공한다.
- Fit은 종횡비를 유지해 letterbox/pillarbox하고, 100%는 화면 물리 픽셀 하나당 texel 하나로 표시하며 넘치면 스크롤한다.
  HiDPI를 포함한 화면 좌표→게임 픽셀 매핑은 순수 `GameViewLayout` 계산으로 분리한다.
- FBO 전환은 framebuffer, viewport, scissor, sRGB 상태를 복구한다. 잘못된 크기나 GL 최대 크기 초과 요청은 거부하고
  마지막 정상 FBO를 보존한다.
- 사용자 프리셋, 마지막 프리셋, Fit/100% 모드는 버전 있는 `EditorPreferences` JSON에 플랫폼 사용자 설정 경로로
  원자 저장한다. 파일 누락·손상은 기본 프리셋/Fit으로 복구하고 scene/project dirty에는 영향을 주지 않는다.

### 3.3 입력 포커스

- Play 입력은 Game View가 포커스를 가질 때만 전달한다. 이미지를 클릭하면 포커스를 얻고 이미지 내부 마우스만
  game pixel로 변환한다.
- 포커스를 잃으면 눌린 key/button과 UI capture를 즉시 해제한다. 분리 ImGui 플랫폼 창에서는 해당 Game View의
  native window를 사용하며 Scene View 조작은 gameplay 입력으로 전달하지 않는다.

### 3.4 검증

- Fit/100%, letterbox, HiDPI, 좌표 매핑과 경계 밖 판정
- Build/custom preset, preference round-trip·손상 복구
- Edit/Play/Stop world와 camera 선택, focus·분리 창·포커스 손실 입력
- Scene/Game 연속 FBO 렌더의 GL 상태 복구, 고정 해상도와 공통 출력 경로

## 4. 멀티 선택과 공통 속성 편집

### 4.1 선택 모델

- `SelectionService`는 ordered unique `selectedIds`, `primaryId`, `rangeAnchor`를 가진다.
- 일반 클릭은 교체, macOS Cmd/그 외 Ctrl 클릭은 토글, Hierarchy Shift는 visible DFS 범위 교체,
  Cmd/Ctrl+Shift는 범위 합치기, Scene View Shift는 추가, 빈 공간 일반 클릭은 전체 해제다.
- primary는 마지막 명시 선택 대상이다. Play/Stop rebind는 무효 ID를 제거하고 모든 상태 및 Inspector lock 변경을 알린다.
  Inspector lock은 단일 ID가 아니라 대상 집합 전체를 고정한다.

### 4.2 공통 property와 batch undo

- 중복 P0 필드 UI와 직접 `Component::OnInspectorGUI` 변경을 단일 `EditorPropertyDescriptor` 계층으로 통합한다.
  getter/setter, 변경 후 hook, bool/integer/real/string/enum, vector/color 축·채널, typed asset GUID,
  `ScriptFieldRegistry` 단순 필드를 지원한다.
- 모든 대상의 Component/속성 교집합만 표시한다. Component 구조 변경, 배열, tile cell, curve, layer 목록은 단일 선택에만 제공한다.
- float는 epsilon으로 비교하고 vector/color는 축별 mixed 상태를 표시한다. mixed 값을 편집하면 그 필드/축만 모든 대상에
  절대값으로 적용해 나머지를 보존한다.
- 한 UI gesture는 모든 대상 전후 snapshot을 담은 `BatchComponentSnapshotCommand` 하나다. drag는 live preview하고 종료 때
  한 번만 Undo/dirty 처리한다. asset은 타입 검증 후 `ResolveAssets`를 수행한다.
- 변경·Undo·Redo마다 각 대상에서 가장 가까운 prefab instance root override를 한 번씩 갱신한다.

### 4.3 계층 명령과 Transform gizmo

- delete/duplicate는 선택된 조상이 있는 자식을 제외한 root-most 집합에만 적용하며 한 번의 동작은 Undo 한 단계다.
  subtree/부모/순서/안정적인 redo ID/이전·결과 선택을 복원한다.
- W/E/R은 실제 Move/Rotate/Scale에 매핑한다. 단일 선택은 Local/World 및 자체 pivot, 멀티는 root-most Transform의
  월드 위치 중심 pivot과 World 방향을 사용한다.
- Move는 같은 world delta, Rotate는 중심 주변 위치와 world rotation, Scale은 중심 기준 offset과 world scale을 함께 적용한다.
  `Transform::TrySetWorldScale`은 부모를 고려하며 부모 scale의 0 근접 축은 gesture 전체를 거부하고 경고한다.
- snap 기본값은 Move grid, Rotate 15°, Scale 0.1이다. drag 하나는 `MultiTransformCommand` 하나이고 Escape는 시작 상태로 복원한다.

### 4.4 검증

- replace/add/toggle/range/primary/anchor/lock/rebind
- 공통 Component 교집합, 축별 mixed, scalar/vector/color/asset/Script field batch 적용
- 단일 Undo/Redo, 대상 삭제 안전성, prefab override 저장/재로드
- 서로 다른 부모와 parent-child 선택의 Move/Rotate/Scale 수학
- root-most delete/duplicate와 안정적인 redo ID

## 5. 통합 게이트와 제외 범위

테스트 label은 순수 `unit`, 실제 컨텍스트가 필요한 `gl`, `smoke`, `e2e`로 구분한다. 구현 전 기준선은 70개지만
최종 테스트 수는 완료 조건으로 고정하지 않는다. 최종 게이트는 Debug·Release·ASan·UBSan 전체 build/CTest,
에디터 Play smoke, 패키지 런타임의 faulting Script 격리, `git diff --check`다.

이번 범위에는 segfault/abort/프로세스 격리, owner 정보 없는 raw EventBus/UI callback, pixel-perfect camera,
다중 camera 합성, post-processing, box selection, 멀티 Component 구조 변경, 배열·tile·curve 편집,
Play 변경 영구 반영, Windows/Linux 배포 CI, archive/VFS/서명은 포함하지 않는다.

## 6. 구현 결과와 최종 검증

- `ScriptInvocationBoundary`가 lifecycle/frame/contact/timer/coroutine의 엔진 주도 Script 호출을 격리한다.
  fault 인스턴스만 비활성화하고 예약 작업을 취소하며, 안전한 `OnDisable`과 명시적 재활성화를 지원한다.
  저장된 enabled 상태는 lifecycle을 호출하지 않는 역직렬화 전용 경로로 복원한다.
- 런타임과 Game View가 `GameOutputRenderer`를 공유한다. Game View는 고정 해상도 FBO, Fit/100%,
  HiDPI 좌표 매핑, 분리 창 입력 포커스와 사용자 preference를 제공하고 Scene View는 editor camera 경로로 분리됐다.
- ordered multi-selection, 공통 descriptor Inspector, typed asset/Script field batch 편집, root-most 계층 명령과
  world-space 멀티 Move/Rotate/Scale을 구현했다. live edit와 비활성 asset drop 모두 한 번의 batch Undo로 기록되며
  prefab override를 갱신한다.

| 최종 게이트 | 결과 |
|---|---|
| Debug 전체 build / CTest | 성공 / **74/74** |
| Release 전체 build / CTest | 성공 / **74/74** |
| ASan 전체 build / CTest | 성공 / **74/74** |
| UBSan 전체 build / CTest | 성공 / **74/74** |
| label 분리 | unit 68, gl 2, smoke 4 (그중 e2e 1) |
| 에디터 Play smoke | 네 preset의 `editor_smoke` 통과 |
| 패키지 Script fault 격리 | `faultCallbackEntered=true`, `faultOnDisable=true`, `peerContinued=true` |
| 정적 마감 | 독립 재감사 지적 3건 회귀 보강, `git diff --check` 통과 |

구현 전 70개 기준선은 74개 테스트로 확장됐으며, 완료 판정은 개수가 아니라 위 네 구성의 전체 통과와
실제 smoke/E2E 결과를 기준으로 했다.
