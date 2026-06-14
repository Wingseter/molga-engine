# 06. 카메라 컴포넌트 (Camera Component) 구현 계획

> 작성일: 2026-06-14
> 범위: `Camera` ECS 컴포넌트, 메인 카메라 지정, 추적/뷰포트, (이후) 멀티 카메라/픽셀퍼펙트
> 관련 문서: [`00_overview.md`](00_overview.md) · [`05_text.md`](05_text.md)(UI 카메라)

---

## 1. 현재 상태 (코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| 렌더 카메라 | `src/Rendering/Camera2D.h` (위치/줌/회전, view·projection 행렬) | ✅ 동작 |
| 사용처 | 프레임 루프가 `Camera2D` 1개를 `RenderPass`에 전달(`src/main.cpp` 렌더 섹션) | 🟡 단일·코드 소유 |

**핵심 격차:**
- `Camera2D`는 **렌더 유틸리티**일 뿐, **ECS 컴포넌트가 아니다.** 씬 데이터로 저장·전환 불가.
- 게임 카메라가 **코드에 1개 하드코딩** — 오브젝트를 따라가는 추적, 여러 카메라, 게임별 배경색/뷰포트 설정 불가.
- 에디터 편집 카메라(Scene View)와 **게임 카메라의 구분/연결이 없음**.

---

## 2. 목표 (완료의 정의)

게임 제작자가 에디터에서:
- 오브젝트에 `Camera` 컴포넌트를 붙이고 "Main Camera"로 지정 → Play/런타임 렌더가 이 카메라를 사용.
- 카메라가 `Transform`을 따라 이동(타깃 추적 옵션).
- 배경색, 줌(orthographic size), 뷰포트를 카메라별로 설정.
- 저장/로드/빌드에서 보존된다.

---

## 3. 설계

### 3.1 신규 타입

```
Camera : Component (src/ECS/Components/Camera.{h,cpp})
  - orthoSize(float), backgroundColor(Color), depth(int, 멀티카메라 우선순위)
  - isMain(bool)
  - viewport(Rect, 정규화 0~1; 1차는 전체)
  - follow target(GameObject ref) + smoothing (선택)
  - 내부에 Camera2D 보유 or 매 프레임 Transform→view 행렬 갱신
  - GetViewMatrix/GetProjectionMatrix (Camera2D 위임)
  - COMPONENT_TYPE + REGISTER_COMPONENT + Serialize/Deserialize/OnInspectorGUI

CameraSystem / World 헬퍼
  - World에서 isMain==true인 Camera를 찾아 "활성 게임 카메라"로 제공
  - 없으면 폴백(기존 기본 Camera2D)
```

### 3.2 통합 지점

- **프레임 루프 렌더:** 현재 코드가 직접 만든 `Camera2D` 대신 **활성 `Camera` 컴포넌트의 행렬**을 `RenderPass`에 전달.
  Play 모드면 게임 메인 카메라, 에디터 Scene View면 편집 카메라(FBO Scene View 작업과 연동).
- **추적:** `LateUpdate(dt)`(`src/Core/World.h:28`)에서 타깃 위치로 카메라 위치 보간 — 렌더 직전 최신화.
- 화면 리사이즈 시 카메라 `SetScreenSize`/projection 갱신.

### 3.3 직렬화

- orthoSize, backgroundColor, depth, isMain, viewport, follow target(오브젝트 ID 참조), smoothing
- `REGISTER_COMPONENT(Camera)` 필수
- **주의:** target은 같은 씬 오브젝트 ID 참조 → 로드 후 `ResolveAssets`/후처리에서 ID→포인터 해석.

### 3.4 에디터

- Inspector: orthoSize, backgroundColor, isMain 토글(중복 시 경고), depth, follow target 선택, smoothing
- (이후) Scene View에 카메라 시야 프레임 기즈모 표시

---

## 4. 작업 체크리스트

**1차: 단일 메인 카메라**
- [ ] `Camera` 컴포넌트 + 등록 + 직렬화 + Inspector
- [ ] World에서 메인 카메라 조회 헬퍼 + 폴백
- [ ] 프레임 루프 렌더가 메인 카메라 행렬 사용하도록 전환
- [ ] Transform 연동(카메라 이동) + 화면 리사이즈 대응
- [ ] 라운드트립 + Play/빌드 동작 확인

**2차: 추적·멀티 카메라**
- [ ] follow target + smoothing(데드존/룩어헤드는 선택)
- [ ] depth 기준 멀티 카메라 렌더(뷰포트 분할, split-screen)
- [ ] 픽셀 퍼펙트 옵션
- [ ] (이후) Cinemachine 스타일 추적 — 로드맵 B6

---

## 5. 완료 기준

- [ ] `Camera` 컴포넌트를 메인으로 지정하면 Play/런타임이 그 시점으로 렌더한다.
- [ ] 카메라가 타깃 오브젝트를 따라 이동한다.
- [ ] 배경색/줌 설정이 저장·로드·빌드에서 보존된다.
- [ ] 메인 카메라가 없으면 기본 카메라로 안전하게 폴백한다.

---

## 6. 의존성·위험·결정 필요

- **연동:** Scene View(편집 카메라)와 게임 카메라 분리는
  [`../2026-06-14_scene-view-and-creation-workflow-plan.md`](../2026-06-14_scene-view-and-creation-workflow-plan.md)의 FBO 작업과 맞물림.
- **결정:** 1차에서 "isMain 카메라 1개"만 지원하고 멀티 카메라는 2차로 미룰지(권장).
- **위험:** target ID 참조 해석 시점 — 로드 직후 미해결 참조 처리 규칙 필요.
