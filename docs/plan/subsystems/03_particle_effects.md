# 03. 이펙트 / 파티클 (Particle Effects) 구현 계획

> 작성일: 2026-06-14
> 범위: `ParticleSystem` ECS 컴포넌트화, 직렬화, 배치 렌더 통합, 프리셋 노출
> 관련 문서: [`00_overview.md`](00_overview.md) · [`04_shader_material.md`](04_shader_material.md)

---

## 1. 현재 상태 (코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| 파티클 데이터 | `src/Systems/Particle.h:13-23` (`Particle`: pos/vel/size/rot/life/color) | ✅ |
| 설정 구조체 | `src/Systems/Particle.h:25-60` (`ParticleConfig`: spawnRate, 속도/각도/중력/크기/수명/색) | ✅ 풍부함 |
| 에미터 | `src/Systems/Particle.h:62-90` (`ParticleEmitter`: Start/Stop/Burst/Update/Render) | ✅ 동작 |
| 프리셋 | `src/Systems/Particle.h:92-99` (`Fire/Smoke/Spark/Snow/Explosion`) | ✅ |
| 렌더 | `ParticleEmitter::Render(Renderer*, Shader*, Camera2D*)` | 🟡 자체 렌더, 배치 미통합 |

**핵심 격차:**
- `ParticleEmitter`는 **ECS 컴포넌트가 아니다.** 씬에 배치·저장할 수 없다.
- `ParticleConfig`가 **직렬화되지 않는다** (필드 수는 많아 매핑 필요).
- 렌더가 독립 경로 — 최근 추가된 **배치 렌더링과 통합되지 않음**(드로우콜·정렬 분리).
- 위치가 `SetPosition(x,y)` 수동 — Transform과 연동되지 않음.

---

## 2. 목표 (완료의 정의)

게임 제작자가 에디터에서:
- 오브젝트에 `ParticleSystem`을 붙이고 프리셋(Fire/Smoke/…) 선택 또는 파라미터 직접 편집.
- 에미터 위치가 오브젝트 `Transform`을 따라간다.
- playOnAwake/loop, `Emit(count)`(버스트)를 스크립트/이벤트에서 호출.
- 저장/로드/빌드에서 설정이 보존되고 동일하게 보인다.

---

## 3. 설계

### 3.1 신규 타입

```
ParticleSystem : Component (src/ECS/Components/ParticleSystem.{h,cpp})
  - config(ParticleConfig)        // 기존 구조체 재사용
  - playOnAwake(bool), looping(bool)
  - presetName(string)            // "Custom" 또는 프리셋 이름
  - 내부에 ParticleEmitter 보유 (또는 로직 흡수)
  - OnEnable: playOnAwake면 Start
  - Update(dt): Transform 위치 동기화 후 emitter.Update(dt)
  - RenderSprite(Renderer*) 또는 Render(): 활성 파티클 제출
  - Emit(int count) / Play() / Stop()
  - OnDestroy: 정리
  - COMPONENT_TYPE + REGISTER_COMPONENT + Serialize/Deserialize/OnInspectorGUI
```

> `ParticleEmitter`/`ParticleConfig`/프리셋은 그대로 두고 **`ParticleSystem`이 래핑**한다.
> 기존 코드 재사용 + 컴포넌트 책임 분리.

### 3.2 통합 지점

- **Transform 동기화:** `Update(dt)` 시작에서 소유 오브젝트 월드 위치를 `emitter.SetPosition`.
  (`Transform` 월드 좌표 계산은 기존 계층 시스템 사용)
- **렌더:** 1차에는 기존 `ParticleEmitter::Render` 경로를 `RenderPass` 안에서 호출(패스 경계는 프레임 루프 소유).
  2차에 **배치 렌더러로 통합**해 스프라이트와 같은 정렬/드로우콜 경로 사용([`04_shader_material.md`](04_shader_material.md) 연동).
- `Update(dt)`는 `World::Update`가 자동 호출.

### 3.3 직렬화

`ParticleConfig`의 모든 필드를 JSON 매핑. 필드가 많으므로 **헬퍼**(`to_json/from_json` 또는 일괄 매크로)로
실수 줄이기. presetName 저장 시: "Custom"이면 전체 config 저장, 프리셋명이면 프리셋+오버라이드만 저장(선택).
- `REGISTER_COMPONENT(ParticleSystem)` 필수

### 3.4 에디터

- Inspector: 프리셋 콤보(선택 시 config 채움), 주요 파라미터 그룹(Emission/Velocity/Size/Color/Life) 접이식,
  ▶ Play / ■ Stop / Burst 버튼, 활성 파티클 수 표시
- (이후) Scene View에서 에미터 미리보기(FBO Scene View 의존)

---

## 4. 작업 체크리스트

**1차: 컴포넌트화**
- [ ] `ParticleSystem` 컴포넌트가 `ParticleEmitter` 래핑 + 등록
- [ ] Transform 위치 동기화
- [ ] `ParticleConfig` 전체 직렬화 + 라운드트립 테스트
- [ ] Inspector: 프리셋 선택 + 파라미터 편집 + Play/Stop/Burst
- [ ] playOnAwake/looping 동작
- [ ] Play→Stop 시 파티클 정리

**2차: 렌더 통합·성능**
- [ ] 배치 렌더러로 파티클 제출 통합(정렬·드로우콜 일원화)
- [ ] (선택) 오브젝트 풀링으로 maxParticles 대량 처리
- [ ] 가산 블렌딩(Additive) 옵션 — Material/블렌드 모드([`04`](04_shader_material.md)) 연동

---

## 5. 완료 기준

- [ ] 오브젝트에 `ParticleSystem`을 붙이고 프리셋을 고르면 Play에서 효과가 보인다.
- [ ] 에미터가 오브젝트를 따라 이동한다.
- [ ] 설정이 저장·로드·빌드에서 보존된다.
- [ ] 스크립트/이벤트에서 `Emit(n)` 버스트가 동작한다.

---

## 6. 의존성·위험·결정 필요

- **위험:** `ParticleConfig` 필드가 많아 직렬화 매핑이 장황 → 헬퍼로 관리.
- **연동:** 가산 블렌딩/커스텀 셰이더는 [`04_shader_material.md`](04_shader_material.md) 진척에 의존(없이도 1차 가능).
- **연동(선택):** 충돌 시 폭발 이펙트는 [`01_physics.md`](01_physics.md) 이벤트로 `Emit` 트리거.
