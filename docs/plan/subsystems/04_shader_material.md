# 04. Shader / Material 구현 계획

> 작성일: 2026-06-14
> 범위: `ShaderManager`, `Material`(셰이더 + 프로퍼티 + 블렌드), 셰이더 핫리로드, 배치 렌더러 연동
> 관련 문서: [`00_overview.md`](00_overview.md) · [`03_particle_effects.md`](03_particle_effects.md)

---

## 1. 현재 상태 (코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| 셰이더 클래스 | `src/Rendering/Shader.h` (Use, SetInt/Float/Vec*/Mat4, uniform 캐시) | ✅ 동작 |
| 기본 셰이더 | `src/Shaders/default.{vert,frag}` | ✅ 1개 |
| 그리드 셰이더 | `src/Shaders/grid.{vert,frag}` | ✅ 에디터용 |
| 렌더러 | `src/Rendering/Renderer.h` (`Begin(Shader*, Camera2D*)`, `DrawSprite`, `End`) | ✅ 패스가 셰이더 1개 받음 |
| Marrow 머티리얼 | `assets/marrow/player_idle.matl` | 🟡 Marrow 전용, 범용 아님 |

**핵심 격차:**
- **`Material` 클래스가 없다**(`grep "class Material"` → 0건). 오브젝트별 셰이더/프로퍼티 지정 불가.
- 셰이더가 **에셋이 아니다** — 코드에서 경로로 1개 로드. `ShaderManager`/캐시 없음.
- `SpriteRenderer`는 색만 가질 뿐 **커스텀 셰이더/블렌드 모드를 지정할 수 없다.**
- 셰이더 **핫리로드 없음.**
- 배치 렌더러는 단일 셰이더 가정 — 머티리얼별 배치 분기 없음.

---

## 2. 목표 (완료의 정의)

- 셰이더를 에셋으로 로드/캐시하고 이름으로 참조한다(`ShaderManager`).
- `Material`로 (셰이더 + 유니폼 프로퍼티 + 블렌드 모드 + 메인 텍스처)를 묶는다.
- `SpriteRenderer`(및 파티클)에 `Material`을 지정 → 틴트/디졸브/아웃라인 등 효과 적용.
- 셰이더 파일 저장 시 에디터에서 핫리로드되어 즉시 반영(에디터 한정).
- Material이 직렬화되어 저장/로드/빌드에서 보존된다.

---

## 3. 설계

### 3.1 신규 타입

```
ShaderManager (src/Rendering/ShaderManager.{h,cpp})  // 싱글톤/서비스
  - Load(name, vertPath, fragPath) -> Shader*
  - Get(name) -> Shader*
  - ReloadAll() / ReloadChanged()   // 핫리로드(에디터)
  - 소유권 보유(현재 Shader는 raw 포인터로 렌더러에 전달됨)

Material (src/Rendering/Material.{h,cpp})  // 에셋
  - shaderName(string)
  - properties: map<string, Value>  (Value = float | vec2/3/4 | int | texture ref)
  - mainTexturePath(string), tint(Color)
  - blendMode { Alpha, Additive, Multiply, Opaque }
  - Apply(Shader*, Renderer*) : Use + 프로퍼티 업로드 + glBlendFunc 설정
  - Serialize/Deserialize (에셋 파일 .mat 또는 인라인)
```

### 3.2 렌더러 변경

- 현재 `Renderer::Begin(Shader*, Camera2D*)`는 패스 1개당 셰이더 1개.
  → **머티리얼 인지 제출**로 확장: 제출 항목이 `Material*`를 동반, 렌더러가 **머티리얼/텍스처별로 배치 그룹화**.
- 1차 단순화: 패스를 머티리얼별로 분리(같은 머티리얼끼리 모아 그림). 정렬은
  [`09_tags_layers.md`](09_tags_layers.md)의 sorting layer/order와 함께 64비트 정렬 키로 설계(이후).

### 3.3 통합 지점

- `SpriteRenderer`에 `materialName`(또는 `Material*`) 필드 추가, `RenderSprite`가 머티리얼 경유.
  머티리얼 없으면 **기본 머티리얼**(현재 default 셰이더)로 폴백 → 하위 호환.
- `ResolveAssets()`에서 `ShaderManager`로 셰이더 확보 + 텍스처 로드.
- 핫리로드: 에디터에서 파일 watcher 또는 수동 "Reload Shaders" 버튼 → `ShaderManager::ReloadChanged`.

### 3.4 직렬화

- `Material`: shaderName, blendMode, tint, mainTexturePath, properties(타입 태그 포함)
- `SpriteRenderer`에 materialName 추가(기본값 비우면 기본 머티리얼)
- 셰이더 소스 자체는 빌드 시 dependency manifest로 포함(현재 빌드 경로 이슈는 gap analysis §P0-3 참조)

### 3.5 에디터

- Material Inspector: 셰이더 콤보, 블렌드 모드, tint, 텍스처, 동적 프로퍼티 편집(셰이더 유니폼 리플렉션은 이후)
- "Reload Shaders" 버튼 + 컴파일 에러를 Console에 출력

---

## 4. 작업 체크리스트

**1차: ShaderManager + 기본 Material**
- [ ] `ShaderManager` (로드/캐시/Get) — 현재 코드의 셰이더 로딩을 여기로 이전
- [ ] `Material` (셰이더명 + tint + 블렌드 모드 + 메인 텍스처)
- [ ] `SpriteRenderer.materialName` + 머티리얼 경유 렌더 + 기본 머티리얼 폴백
- [ ] `Material` 직렬화 + 라운드트립 테스트
- [ ] 블렌드 모드 동작(Alpha/Additive) 시각 확인

**2차: 핫리로드 + 배치 통합**
- [ ] "Reload Shaders" + 컴파일 에러 Console 출력
- [ ] (선택) 파일 watcher 기반 자동 리로드
- [ ] 머티리얼/텍스처별 배치 그룹화(드로우콜 감소)
- [ ] 커스텀 셰이더 프로퍼티 직렬화·편집

**3차(이후): 정렬 키·고급**
- [ ] 64비트 정렬 키(layer/order/material/texture) — [`09`](09_tags_layers.md) 연동
- [ ] 포스트프로세싱/2D 라이팅의 기반으로 확장(로드맵 B7/B8)

---

## 5. 완료 기준

- [ ] `Material`을 만들어 `SpriteRenderer`에 지정하면 틴트/블렌드가 화면에 반영된다.
- [ ] 머티리얼 없는 기존 스프라이트는 그대로 동작(하위 호환).
- [ ] Material이 저장·로드·빌드에서 보존된다.
- [ ] (에디터) 셰이더 리로드 시 변경이 반영되고 컴파일 에러가 Console에 보인다.

---

## 6. 의존성·위험·결정 필요

- **규모:** 본 문서는 다른 서브시스템보다 규모가 크고(렌더러 변경 포함) 파급이 넓다 → 진행 순서상 후반 권장.
- **위험:** 배치 렌더러 변경은 회귀 위험 — 기본 머티리얼 폴백으로 하위 호환 유지하며 점진 도입.
- **연동:** 가산 블렌딩은 [`03_particle_effects.md`](03_particle_effects.md), 정렬 키는 [`09_tags_layers.md`](09_tags_layers.md).
- **결정:** Material을 별도 `.mat` 에셋 파일로 둘지, `SpriteRenderer`에 인라인 저장할지(AssetDatabase 부재 고려 시 1차는 인라인 권장).
