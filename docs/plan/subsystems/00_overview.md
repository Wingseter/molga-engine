# 서브시스템 구현 계획 — 개요 (Subsystem Implementation Plans)

> 작성일: 2026-06-14
> 상태 갱신: 2026-07-16 — P1 통합 이후 현황 반영
> 범위: 이미지/애니메이션 이후 필요한 게임 구성 요소들의 **계획 문서 모음**
> 성격: 최초 구현 전 설계와 현재 성숙도 추적
> 관련 문서: [`../2026-06-06_project_gap_analysis.md`](../2026-06-06_project_gap_analysis.md), [`../../design/00_roadmap.md`](../../design/00_roadmap.md)
> P1 기준: [`../2026-07-16_game_production_p1_plan.md`](../2026-07-16_game_production_p1_plan.md)

---

## 0. 이 문서의 목적

스프라이트 렌더링(`SpriteRenderer` + 배치 렌더링)과 애니메이션(`Animation`, `MarrowRenderer`)이
**"에디터에서 만들고 → 저장하고 → 빌드된 게임에서 동작"**하는 수준까지 도달했다.

그 다음으로 게임을 구성하는 나머지 요소들(물리·사운드·이펙트·셰이더·텍스트 + 다음 티어)은
**대부분 "독립 유틸리티"로는 이미 존재하지만, ECS 컴포넌트·직렬화·런타임으로 연결되지 않아
사용자가 에디터에서 실제로 쓸 수 없다.** 이 문서 모음은 각 요소를 그 격차만큼 끌어올리는 계획이다.

---

## 1. 성숙도 사다리 (Maturity Ladder)

각 서브시스템의 상태를 다음 단계로 분류한다.

```
Designed → Utility 구현 → Engine 통합 → Editor 제작 가능 → 직렬화 → Runtime 검증
```

- **Utility 구현**: 동작하는 클래스/함수는 있으나 ECS·루프와 분리됨
- **Engine 통합**: 엔진 업데이트/렌더 루프가 자동으로 호출함
- **Editor 제작 가능**: Inspector에서 추가·편집 가능 (`OnInspectorGUI`)
- **직렬화**: 씬 JSON에 저장/로드 (`Serialize`/`Deserialize` + `REGISTER_COMPONENT`)
- **Runtime 검증**: 빌드된 독립 실행 파일에서 동일하게 동작

---

## 2. 현재 상태 매트릭스 (코드 증거 기준)

| 서브시스템 | Utility | Engine | Editor | 직렬화 | 현재 핵심 자산 | 문서 |
|---|:--:|:--:|:--:|:--:|---|---|
| 스프라이트 | ✅ | ✅ | ✅ | ✅ | `SpriteRenderer` (참조 구현) | — |
| 애니메이션 | ✅ | ✅ | ✅ | ✅ | `AnimationClip2D`, `AnimatorController2D`, `Animator2D` | [P1](../2026-07-16_game_production_p1_plan.md) |
| **물리** | 🟡 | ❌ | 🟡 | 🟡 | `Collision`, `Collider2D`/`BoxCollider2D`, `CollisionEvent`(미발화) | [01](01_physics.md) |
| **사운드** | ✅ | ✅ | ✅ | ✅ | `AudioService`, `AudioSource`, 고정 bus | [02](02_audio.md) |
| **이펙트(파티클)** | ✅ | ✅ | ✅ | ✅ | `ParticleSystem` v2 + multi-quad batch | [03](03_particle_effects.md) |
| **Shader/Material** | 🟡 | 🟡 | ❌ | ❌ | `Shader`(기본 1개), Material 없음 | [04](04_shader_material.md) |
| **텍스트/폰트** | 🟡 | ❌ | ❌ | ❌ | `TextRenderer`(에디터 전용) | [05](05_text.md) |
| **카메라** | 🟡 | 🟡 | ❌ | ❌ | `Camera2D`(렌더용 1개), Camera 컴포넌트 없음 | [06](06_camera.md) |
| **타일맵** | ✅ | ✅ | ✅ | ✅ | `TileSetAsset`, `TilemapRenderer` v2, Tile Palette | [07](07_tilemap.md) |
| **입력(액션)** | 🟡 | ✅ | ❌ | ❌ | `Input`(전역, GLFW 키코드 직접) | [08](08_input_actions.md) |
| **태그·레이어** | ❌ | ❌ | ❌ | ❌ | 없음 (`GameObject`에 필드 자체 없음) | [09](09_tags_layers.md) |

✅ 완료 · 🟡 부분 · ❌ 없음

---

## 3. ECS 컴포넌트 승격 표준 레시피

모든 "Utility → Editor 제작 가능 + 직렬화" 작업은 동일한 패턴을 따른다.
참조 구현: `src/ECS/Components/SpriteRenderer.{h,cpp}`.

1. `Component` 상속, 클래스 본문에 `COMPONENT_TYPE(MyComponent)` 매크로
2. `.cpp` 최상단에 `REGISTER_COMPONENT(MyComponent)` (팩토리 자동 등록)
3. 동작은 `Update(float dt)` / `RenderSprite(Renderer*)` / `Render()` 중 적절한 훅에 구현
4. `Serialize(json&)` / `Deserialize(const json&)` 오버라이드
5. GL/외부 리소스 지연 로드는 `ResolveAssets()` (GL 컨텍스트 확보 후 호출됨)
6. 외부 리소스 해제는 `OnDestroy()` (파괴 시 1회 보장)
7. 에디터 편집 UI는 `OnInspectorGUI()`
8. 컴포넌트 추가 메뉴(Inspector "Add Component")에 노출

> 업데이트 순서는 `World`가 소유한다: `StartPending → FixedStep → Update → LateUpdate`
> (`src/Core/World.h:25-29`). 렌더는 프레임 루프가 `RenderPass`로 패스 경계를 소유한다.

---

## 4. 권장 진행 순서 (의존성 기반)

선행 결함과 의존성을 고려한 권장 순서. 각 항목은 독립 마일스톤이다.

| 순서 | 작업 | 근거 / 선행 |
|:--:|---|---|
| 0 | **공통 선행 결함 해소** | `EventBus::ProcessQueue()` 미호출(아래 §5), 컴포넌트 직렬화 등록 누락 점검 |
| 1 | **태그·레이어** ([09](09_tags_layers.md)) | 충돌 필터·렌더 정렬·검색의 공통 기반. 작고 파급 큼 |
| 2 | **물리** ([01](01_physics.md)) | 게임 역학 핵심. 충돌 이벤트/Rigidbody. 레이어에 의존 |
| 3 | **사운드** ([02](02_audio.md)) | 독립적. 충돌/스크립트 이벤트와 연동하면 효과 큼 |
| 4 | **이펙트** ([03](03_particle_effects.md)) | 독립적. 물리 충돌·이벤트와 연동 시 시너지 |
| 5 | **카메라 컴포넌트** ([06](06_camera.md)) | 멀티 카메라·추적. 렌더 파이프라인과 연동 |
| 6 | **텍스트 컴포넌트** ([05](05_text.md)) | 독립적. UI/HUD 기반 |
| 7 | **타일맵 컴포넌트** ([07](07_tilemap.md)) | 물리 충돌과 연동. 레벨 제작 |
| 8 | **Shader/Material** ([04](04_shader_material.md)) | 비주얼 고도화. 배치 렌더러와 연동 (규모 큼) |
| 9 | **입력 액션 맵** ([08](08_input_actions.md)) | 편의·리바인딩. 게임 동작엔 기존 `Input`으로도 가능 |

> **YAGNI 원칙**: 각 문서는 "지금 게임을 만들 수 있는 최소 통합"을 1차 목표로 하고,
> 고급 기능(관절/2D 라이팅/AssetDatabase 등)은 "이후 확장"으로 명시 분리한다.

---

## 5. 모든 서브시스템에 영향을 주는 공통 선행 결함

코드에서 확인된, 여러 서브시스템의 전제가 되는 결함:

1. **`EventBus::ProcessQueue()`가 루프에서 호출되지 않음**
   `src/Core/EventBus.cpp:30`에 정의는 있으나 `src/main.cpp` 프레임 루프에 호출 지점이 없다.
   → `QueueEvent`로 큐잉된 충돌/트리거 이벤트가 영영 처리되지 않는다. 물리·이펙트·사운드가
   이벤트 기반으로 연동되려면 프레임 끝(또는 FixedStep 직후)에 `ProcessQueue()` 호출이 선행돼야 한다.

2. **`World::FixedStep`이 스크립트만 호출**
   `src/Core/World.h:26` 주석대로 현재 FixedStep은 스크립트 `FixedUpdate`만 돈다.
   물리 스텝(충돌 감지·응답·이벤트 발화)이 들어갈 자리가 비어 있다.

3. **추상 베이스가 팩토리에 등록되지 않음(정상)**, 단 신규 컴포넌트 추가 시
   `REGISTER_COMPONENT` 누락이 흔한 직렬화 버그 → 각 문서 체크리스트에 등록 단계를 명시한다.

---

## 6. 문서 공통 구조

각 서브시스템 문서는 다음 구조를 따른다.

1. 현재 상태 (코드 증거: `파일:라인`)
2. 목표 (완료의 정의)
3. 설계 (신규 타입, 통합 지점, 직렬화, 에디터)
4. 작업 체크리스트 (순서 있는 단계)
5. 완료 기준 (검증 가능한 수용 조건)
6. 의존성·위험·결정 필요 사항
