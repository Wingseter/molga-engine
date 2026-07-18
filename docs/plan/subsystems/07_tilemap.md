# 07. 타일맵 (Tilemap) 컴포넌트 구현 계획

> 작성일: 2026-06-14
> 상태 갱신: 2026-07-16 — TileSet/Tilemap v2와 Tile Palette까지 완료
> 범위: `TilemapRenderer` ECS 컴포넌트, 타일맵 에셋 직렬화, 물리 충돌 통합, (이후) 타일 팔레트 편집기
> 관련 문서: [`00_overview.md`](00_overview.md) · [`01_physics.md`](01_physics.md)

> **현재 구현 기준:** `.tileset`, stable multi-layer RLE, NESW 4-way terrain, 32×32 dirty chunk batch,
> revision별 solid-run collision, Tile Palette와 stroke 단위 Undo가 구현됐다. 아래 본문은 2026-06-14의 최초 계획
> 기록이며, 현재 계약과 잔여 범위는 [P1 구현 계획](../2026-07-16_game_production_p1_plan.md)을 따른다.

---

## 1. 최초 상태 (2026-06-14 코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| 타일맵 클래스 | `src/Rendering/Tilemap.h` (그리드, SetTile/GetTile, Render, 자체 VAO/VBO) | ✅ 동작 |
| 충돌 질의 | `Tilemap.h:25-32` (`IsSolid`, `CheckCollision(AABB)`, `GetCollidingTiles`) | ✅ |
| 좌표 변환 | `Tilemap.h:29-32` (World↔Tile, `GetTileAABB`) | ✅ |
| 데이터 파일 | `assets/tilemaps/level1.json` | 🟡 독립 포맷 |

**핵심 격차:**
- `Tilemap`은 **독립 클래스** — ECS 컴포넌트가 아니라 씬에 배치·저장할 수 없다.
- 타일 데이터가 **씬 직렬화와 분리**(별도 json) — 씬 일부로 통합 안 됨.
- 충돌 질의는 있으나 **물리 루프에 연결 안 됨**([`01_physics.md`](01_physics.md)).
- **타일 편집기(팔레트, 브러시) 없음** — 코드/JSON으로만 작성.

---

## 2. 목표 (완료의 정의)

게임 제작자가 에디터에서:
- 오브젝트에 `TilemapRenderer`를 붙이고 스프라이트시트와 타일 데이터를 지정 → 화면에 렌더.
- 타일맵이 물리 충돌에 참여(캐릭터가 솔리드 타일 위에 선다).
- 타일맵이 씬과 함께 저장/로드/빌드된다.
- (이후) Scene View에서 타일을 직접 칠한다.

---

## 3. 설계

### 3.1 신규 타입

```
TilemapRenderer : Component (src/ECS/Components/TilemapRenderer.{h,cpp})
  - 내부에 Tilemap 보유 (또는 데이터 흡수)
  - spriteSheetPath(string), tileSize, width, height
  - tileData(int 배열), solidTiles(타일ID→solid)
  - sortingOrder
  - RenderSprite/Render: Tilemap::Render 호출(패스는 프레임 루프 소유)
  - GetWorldBounds()/충돌 질의 노출(물리 연동)
  - COMPONENT_TYPE + REGISTER_COMPONENT + Serialize/Deserialize/ResolveAssets/OnInspectorGUI
```

### 3.2 통합 지점

- **렌더:** `Tilemap`의 자체 셰이더/카메라 렌더를 프레임 루프 `RenderPass` 안으로 통합
  (현재 `Tilemap::Render(Shader*, Camera2D*)`를 활성 패스에서 호출).
- **물리:** `PhysicsWorld`([`01_physics.md`](01_physics.md))가 `TilemapRenderer`의 솔리드 타일을
  정적 콜라이더처럼 질의. `Tilemap::GetCollidingTiles(AABB)` 재사용 → Rigidbody 분리에 사용.
- **ResolveAssets:** 스프라이트시트 텍스처 + GL 버퍼 셋업(GL 컨텍스트 확보 후).

### 3.3 직렬화

- spriteSheetPath, tileSize, width, height, tileData(평탄 배열), solidTiles
- 대형 타일맵은 **런길이 인코딩(RLE)** 또는 별도 `.tilemap` 에셋 참조 고려(1차는 인라인 배열로 단순화)
- 기존 `assets/tilemaps/level1.json` 포맷을 import하는 변환기(선택)
- `REGISTER_COMPONENT(TilemapRenderer)` 필수

### 3.4 에디터

- Inspector: 스프라이트시트 지정, 그리드 크기, 솔리드 타일 토글
- (이후) **타일 팔레트 창** + Scene View 브러시 페인팅(FBO Scene View + 피킹에 의존)

---

## 4. 최초 작업 체크리스트 (역사적 기록)

**1차: 컴포넌트화 + 렌더**
- [ ] `TilemapRenderer` 컴포넌트가 `Tilemap` 래핑 + 등록
- [ ] 스프라이트시트/타일 데이터 직렬화 + `ResolveAssets`
- [ ] 프레임 루프 패스에서 렌더
- [ ] Inspector 기본 편집(시트/크기/솔리드)
- [ ] 라운드트립 + Play/빌드 동작 확인

**2차: 물리 통합**
- [ ] `PhysicsWorld`가 타일맵 솔리드 타일과 Rigidbody 충돌 처리
- [ ] 캐릭터가 솔리드 타일 위에 서고 통과 못 함 테스트

**3차: 타일 편집기(이후)**
- [ ] 타일 팔레트 창
- [ ] Scene View 브러시/지우개/사각 채우기
- [ ] 다중 레이어(배경/충돌/전경)

---

## 5. 최초 완료 기준 (역사적 기록)

- [ ] `TilemapRenderer`를 붙이고 시트/데이터를 지정하면 타일맵이 렌더된다.
- [ ] 타일맵이 씬과 함께 저장·로드·빌드된다.
- [ ] 캐릭터(Rigidbody)가 솔리드 타일과 충돌해 위에 선다.

---

## 6. 의존성·위험·결정 필요

- **의존:** 물리 통합은 [`01_physics.md`](01_physics.md) `PhysicsWorld` 진척에 의존(렌더 1차는 독립 가능).
- **의존:** 타일 페인팅은 FBO Scene View + 피킹에 의존.
- **결정:** 타일 데이터를 씬에 인라인 저장할지, 별도 `.tilemap` 에셋으로 분리할지(대형 맵은 분리 유리, 1차는 인라인).
- **위험:** 대형 타일맵 직렬화 크기 → RLE/청크 고려.
