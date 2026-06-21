# 09. 태그 & 레이어 (Tags & Layers) 구현 계획

> 작성일: 2026-06-14
> 범위: `GameObject` 태그(문자열), 레이어(비트마스크), 충돌 매트릭스, 정렬 레이어
> 관련 문서: [`00_overview.md`](00_overview.md) · [`01_physics.md`](01_physics.md) · [`04_shader_material.md`](04_shader_material.md)

---

## 1. 현재 상태 (코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| GameObject 필드 | `src/ECS/GameObject.h:124-127` (`id`, `name`, `active`, `destroyed`) | — |
| 태그 | 없음 (`grep tag/layer src/ECS` → 0건) | ❌ |
| 레이어 | 없음 | ❌ |
| 정렬 | `SpriteRenderer.sortingOrder`(int) 1차원만 | 🟡 |

**핵심 격차:**
- `GameObject`에 **태그/레이어 필드 자체가 없다.**
- 충돌 필터링(어떤 레이어가 어떤 레이어와 충돌하는지) 기반이 없다 → 물리에서 전부 vs 전부.
- 렌더 정렬이 `sortingOrder` 정수 1차원뿐 — **정렬 레이어(배경/캐릭터/UI)** 개념 없음.
- 검색/조회(`FindWithTag`)가 불가.

> **작지만 파급이 큰 기반.** 물리 충돌 필터, 렌더 정렬, 게임플레이 조회의 공통 토대다.
> 그래서 [`00_overview.md`](00_overview.md) §4에서 **1순위**로 권장.

---

## 2. 목표 (완료의 정의)

- 오브젝트에 **태그**("Player","Enemy")와 **레이어**("Default","Ground","Projectile")를 지정.
- 스크립트에서 `FindWithTag("Player")`, `CompareTag("Enemy")` 사용.
- **충돌 매트릭스**로 레이어 쌍의 충돌 여부 정의 → 물리가 필터링.
- **정렬 레이어 + order**로 렌더 순서 결정.
- 태그/레이어가 씬에 저장/로드/빌드된다.

---

## 3. 설계

### 3.1 데이터 모델

```
GameObject (src/ECS/GameObject.h)에 필드 추가:
  - std::string tag = "Untagged";
  - int layer = 0;                 // 물리/컬링 레이어 인덱스
  // getter/setter + CompareTag(tag)

프로젝트 설정(ProjectSettings 또는 별도 에셋):
  - tagList: vector<string>
  - layerNames: 32개 슬롯(이름)        // 비트마스크 인덱스
  - collisionMatrix: 32x32 bool (대칭)  // layer i ↔ j 충돌 여부
  - sortingLayers: 정렬 레이어 순서 목록

World/Scene 헬퍼:
  - FindWithTag(tag) / FindAllWithTag(tag)
```

### 3.2 정렬 레이어 (렌더)

- `SpriteRenderer`(및 텍스트/타일맵)에 `sortingLayer`(이름/인덱스) 추가, 기존 `sortingOrder`와 결합.
- 최종 정렬 키(권장): `[sortingLayer][sortingOrder][y or material]` → 64비트 키로 안정 정렬
  (배치 렌더러/머티리얼 정렬과 함께 설계: [`04_shader_material.md`](04_shader_material.md)).

### 3.3 통합 지점

- **물리:** `PhysicsWorld`([`01_physics.md`](01_physics.md))가 페어 narrow phase 전에
  `collisionMatrix[a.layer][b.layer]`로 필터 → 비충돌 레이어 조기 제거.
- **렌더:** 제출 정렬에 sortingLayer 반영.
- **조회:** 게임 스크립트가 `FindWithTag` 사용.

### 3.4 직렬화

- `GameObject`: tag, layer를 씬 직렬화에 추가(기존 SceneSerializer 확장).
- 태그/레이어 이름·충돌 매트릭스·정렬 레이어는 **프로젝트 설정**(`project.json` 또는 별도)으로 저장.
- 기존 씬 호환: tag 누락 시 "Untagged", layer 누락 시 0.

### 3.5 에디터

- Inspector 상단에 tag 콤보 + layer 콤보(+ "Add Tag/Layer…").
- Project Settings 창에 **Tags & Layers** + **Collision Matrix** 체크박스 그리드(에디터 C9).

---

## 4. 작업 체크리스트

**1차: 태그/레이어 필드 + 직렬화**
- [ ] `GameObject`에 tag/layer 필드 + `CompareTag` + getter/setter
- [ ] SceneSerializer에 tag/layer 추가(누락 시 기본값 폴백)
- [ ] `FindWithTag`/`FindAllWithTag` (World)
- [ ] Inspector tag/layer 콤보
- [ ] 라운드트립 + 기존 씬 하위호환 테스트

**2차: 충돌 매트릭스 + 정렬 레이어**
- [ ] 프로젝트 설정에 layerNames + collisionMatrix
- [ ] Project Settings UI(레이어 이름, 충돌 매트릭스 그리드)
- [ ] `PhysicsWorld`가 매트릭스로 필터링
- [ ] `sortingLayer`를 렌더 정렬에 반영

**3차(이후)**
- [ ] 64비트 정렬 키로 배치/머티리얼 정렬 일원화([`04`](04_shader_material.md))
- [ ] 컬링 마스크(카메라별 레이어 표시, [`06`](06_camera.md))

---

## 5. 완료 기준

- [ ] 오브젝트에 태그/레이어를 지정하고 스크립트에서 `FindWithTag`/`CompareTag`가 동작.
- [ ] 충돌 매트릭스에서 끈 레이어 쌍은 물리 충돌하지 않는다.
- [ ] 정렬 레이어로 배경/캐릭터/전경 순서가 의도대로 렌더된다.
- [ ] 태그/레이어가 저장·로드·빌드에서 보존되고, 기존 씬도 문제없이 로드된다.

---

## 6. 의존성·위험·결정 필요

- **선행 위치:** 물리·렌더 정렬의 기반이므로 **물리/머티리얼보다 먼저** 1차(필드+직렬화)를 끝내는 게 유리.
- **결정:** 레이어 슬롯 수(8/16/32). Unity는 32. 32 권장(비트마스크 = `uint32_t`).
- **위험:** 기존 씬/직렬화 포맷 변경 → 누락 필드 기본값 폴백으로 하위호환 보장 필수.
- **의존:** Project Settings UI는 환경설정/프로젝트 설정(에디터 C9)과 함께.
