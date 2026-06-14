# 05. 텍스트 / 폰트 (Text) 구현 계획

> 작성일: 2026-06-14
> 범위: 게임용 `TextRenderer2D`(월드 텍스트) 컴포넌트, 폰트 에셋, (이후) TTF 글리프 아틀라스
> 관련 문서: [`00_overview.md`](00_overview.md)

---

## 1. 현재 상태 (코드 증거)

| 자산 | 위치 | 상태 |
|---|---|---|
| 텍스트 렌더러 | `src/Rendering/TextRenderer.h` (싱글톤, 비트맵 폰트, `RenderText(...)`) | 🟡 게임 비통합 |
| 글리프 정보 | `TextRenderer.h:13-18` (`CharInfo`: UV/크기/advance) | ✅ |
| 내장 폰트 | `TextRenderer::GenerateBuiltinFont()` (비트맵 생성) | ✅ |
| 에디터 폰트 | `src/Editor/FontManager.{h,cpp}` + `assets/fonts/` (Inter, JetBrainsMono, FontAwesome) | ✅ ImGui 전용 |

**핵심 격차:**
- `TextRenderer`는 **싱글톤 + 내장 비트맵 폰트**로, 디버그/HUD 직접 호출용. **ECS 컴포넌트 아님.**
- 게임 오브젝트에 **텍스트를 붙여 씬에 저장할 수 없다.**
- `FontManager`는 **ImGui(에디터 UI) 전용** — 게임 월드 텍스트와 분리됨.
- **TTF 런타임 렌더링 없음**(내장 비트맵만). 폰트 크기/외곽선/CJK 제한.
- 폰트가 **에셋으로 참조·직렬화되지 않음.**

---

## 2. 목표 (완료의 정의)

게임 제작자가 에디터에서:
- 오브젝트에 `TextRenderer2D`(가칭) 컴포넌트를 붙이고 문자열/색/크기/정렬을 설정.
- 텍스트가 오브젝트 `Transform`을 따라 월드 공간에 렌더된다(점수·말풍선·라벨).
- 스크립트에서 `SetText("Score: " + n)`으로 갱신.
- 저장/로드/빌드에서 내용·스타일이 보존된다.

---

## 3. 설계

### 3.1 신규 타입

```
TextRenderer2D : Component (src/ECS/Components/TextRenderer2D.{h,cpp})
  - text(string), color(Color), scale(float)
  - alignment { Left, Center, Right }
  - fontName(string)   // 1차: 내장 폰트만 → "default"
  - sortingOrder(int)  // SpriteRenderer와 같은 정렬 키 체계
  - SetText(string) / GetText()
  - RenderSprite(Renderer*): 활성 패스 안에서 TextRenderer로 글리프 제출
  - COMPONENT_TYPE + REGISTER_COMPONENT + Serialize/Deserialize/OnInspectorGUI
```

### 3.2 폰트 자산 (단계적)

- **1차:** 기존 `TextRenderer` 내장 비트맵 폰트 재사용. fontName="default" 고정.
  컴포넌트는 `TextRenderer::Get().RenderText(...)`를 Transform 위치/스케일로 호출.
- **2차:** `Font` 에셋 + stb_truetype(이미 `external/stb` 보유)로 **TTF→글리프 아틀라스** 생성,
  크기별 캐시. `assets/fonts/`의 TTF를 게임에서도 사용. CJK는 글리프 범위 지정.

### 3.3 통합 지점

- `RenderSprite(Renderer*)`로 스프라이트와 **같은 패스/정렬 체계**에 합류
  (텍스트 셰이더가 필요하면 [`04_shader_material.md`](04_shader_material.md)의 머티리얼 경로 사용).
- Transform 월드 좌표 + 정렬(alignment)로 기준점 계산.
- `ResolveAssets()`에서 폰트(2차: 아틀라스) 확보.

### 3.4 직렬화

- text, color, scale, alignment, fontName, sortingOrder
- `REGISTER_COMPONENT(TextRenderer2D)` 필수

### 3.5 에디터

- Inspector: 멀티라인 text 입력, color picker, scale, alignment 콤보, font 콤보(2차)

---

## 4. 작업 체크리스트

**1차: 내장 폰트 컴포넌트**
- [ ] `TextRenderer2D` 컴포넌트 + 등록 + 직렬화 + Inspector
- [ ] Transform 위치/스케일로 `TextRenderer::RenderText` 호출
- [ ] alignment(Left/Center/Right) — `GetTextWidth` 활용
- [ ] sortingOrder를 스프라이트와 동일 정렬 경로에 반영
- [ ] 라운드트립 테스트 + Play/빌드 동작 확인

**2차: TTF 글리프 아틀라스**
- [ ] stb_truetype로 폰트 아틀라스 생성 + 크기별 캐시
- [ ] `Font` 에셋 + fontName 참조 직렬화
- [ ] CJK 글리프 범위/폴백
- [ ] 외곽선/그림자(머티리얼 연동, 선택)

---

## 5. 완료 기준

- [ ] 오브젝트에 `TextRenderer2D`를 붙이면 월드에 텍스트가 보이고 Transform을 따라간다.
- [ ] 스크립트에서 `SetText`로 실시간 갱신된다.
- [ ] 내용/색/크기/정렬이 저장·로드·빌드에서 보존된다.

---

## 6. 의존성·위험·결정 필요

- **결정:** 1차를 내장 비트맵 폰트로 빠르게 갈지, 처음부터 TTF 아틀라스로 갈지(품질 vs 작업량).
- **위험:** 내장 비트맵은 ASCII 위주 — 한글 표시는 2차(TTF) 필요. 게임 언어 요구에 따라 우선순위 조정.
- **구분:** 에디터 UI 폰트(`FontManager`/ImGui)와 **게임 월드 텍스트는 별도 경로** — 혼동 금지.
