# Scene 뷰 & 생성 워크플로 보완 계획

> 작성일: 2026-06-14
> 대상 브랜치: `scheduler`
> 범위: Scene 창 렌더링, Hierarchy/Project 우클릭 생성, Script 생성 UX
> 관련 문서: [`docs/plan/2026-06-06_project_gap_analysis.md`](2026-06-06_project_gap_analysis.md) §6 (Scene View)

---

## 0. 요약 (TL;DR)

세 가지 증상은 각각 별개의 미완성 지점에서 비롯되며, **하나의 버그가 아니라 미구현**이다.

| 증상 | 근본 원인 | 위치 |
| --- | --- | --- |
| Scene 창에 환경이 안 보임 | SceneView가 placeholder. 씬은 메인 백버퍼에 그려지지만 ImGui 도크스페이스가 덮음. **FBO→텍스처 인프라 부재** | `src/Editor/Windows/SceneViewWindow.cpp` 전체 / `src/main.cpp:267-273` |
| 우클릭 "New"로 오브젝트 생성 불가 | Hierarchy 컨텍스트 메뉴의 "Create 2D Object" 핸들러가 **빈 `{}`** | `src/Editor/Windows/HierarchyWindow.cpp:55-59` |
| 우클릭으로 Script 생성 불가 | Project Browser 컨텍스트 메뉴에 **Create 항목 자체가 없음** (백엔드는 존재) | `src/Editor/Windows/ProjectBrowserWindow.cpp:290-302` |

**중요 — 2D vs 3D**: 이 엔진은 로드맵상 **"Unity급 2D 게임 엔진"**이다
([`docs/design/MASTER_PLAN.md`](../design/MASTER_PLAN.md)). `Camera2D`, `Sprite`,
`Tilemap`, `BoxCollider2D`만 존재하고 3D 렌더 파이프라인·메시·3D 카메라는 없다.
따라서 본 계획에서 "3D 환경"은 **2D Scene 뷰포트(그리드 + 카메라 + 오브젝트 시각화)**를
의미하는 것으로 해석한다. 실제 3D 엔진화는 별도의 대규모 트랙이며 본 계획 범위 밖이다.
(→ §6 결정 필요 사항 참고)

---

## 1. 진단 상세

### 1.1 Scene 창에 아무 환경도 안 나타남

`SceneViewWindow::OnGUI()`는 다음만 수행한다:

```cpp
// src/Editor/Windows/SceneViewWindow.cpp:9-30
ImGui::Text("Scene View");
ImGui::Text("Size: ...");
drawList->AddRectFilled(...);  // 어두운 사각형 placeholder
```

한편 실제 씬은 **메인 윈도우 백버퍼에 직접** 렌더된다:

```cpp
// src/main.cpp:267-273
renderer->Clear(0.15f, 0.15f, 0.2f, 1.0f);
{
    molga::RenderPass pass(*renderer, shader.get(), camera.get());
    for (auto& [order, sr] : drawList) { sr->RenderSprite(renderer.get()); }
}
```

그 위에서 `Editor::RenderGUI()`가 **전체 화면 도크스페이스**(`ImGuiWindowFlags_NoDocking`,
풀스크린, 불투명)를 그린다(`src/Editor/Editor.cpp:65-98`). 결과적으로 백버퍼에 그려진
스프라이트는 ImGui 패널 뒤에 가려져 보이지 않는다.

**핵심 구조 문제**: 씬을 패널 안에 표시하려면 **오프스크린 프레임버퍼(FBO)에 렌더 →
컬러 텍스처 → `ImGui::Image()`로 SceneView에 출력**해야 한다. 현재 이 인프라가 전혀 없다
(`grep glGenFramebuffers` 결과 0건, gap analysis §"Framebuffer/RenderTexture: 없음").
또한 그리드/축/카메라 패닝·줌도 전부 미구현.

### 1.2 우클릭 New로 오브젝트 생성 불가

Hierarchy 빈 공간 우클릭 메뉴는 존재하지만 하위 항목이 **빈 핸들러**다:

```cpp
// src/Editor/Windows/HierarchyWindow.cpp:51-61
if (ImGui::BeginPopupContextWindow("HierarchyContextMenu", ...)) {
    if (ImGui::MenuItem("Create Empty")) { CreateEmptyGameObject(); }   // OK
    if (ImGui::BeginMenu("Create 2D Object")) {
        if (ImGui::MenuItem("Sprite"))  {}   // ← 빈 핸들러: 아무 동작 없음
        if (ImGui::MenuItem("Tilemap")) {}   // ← 빈 핸들러
        ImGui::EndMenu();
    }
}
```

반면 상단 메뉴바 `GameObject → 2D Object → Sprite`는 정상 구현돼 있다:

```cpp
// src/Editor/Editor.cpp:179-187
auto obj = CreateGameObject("Sprite");
if (obj) { obj->AddComponent<SpriteRenderer>(); }
```

즉 로직은 이미 있으나 우클릭 메뉴에 **연결만 안 된** 상태. 또한 "Create Empty"는
`CreateObjectCommand`(undo 지원)를 쓰지만 메뉴바 경로는 `CreateGameObject`(undo 미지원)를
써서 일관성도 깨져 있다. `Tilemap`은 대응 ECS 컴포넌트가 없어(현재 `Tilemap`은
`src/Rendering`에만 존재) 별도 작업이 필요하다.

### 1.3 우클릭으로 Script 생성 불가

스크립트 생성 백엔드는 완비돼 있다:
- `ScriptCompiler::CreateScriptTemplate(name)` — `.h`/`.cpp` 템플릿 생성
  (`src/Scripting/ScriptCompiler.h:35`)
- `ScriptWindow`의 "Create Script" 버튼 + 모달로 호출 (`src/Editor/Windows/ScriptWindow.cpp:157-223`)

그러나 Unity식 **"Assets 영역 우클릭 → Create → C++ Script"** 경로가 없다. Project
Browser의 컨텍스트 메뉴는 Refresh/Show Hidden 뿐이다:

```cpp
// src/Editor/Windows/ProjectBrowserWindow.cpp:290-302
void ProjectBrowserWindow::DrawContextMenu() {
    if (ImGui::BeginPopupContextWindow("ProjectBrowserContext")) {
        if (ImGui::MenuItem("Refresh")) { Refresh(); }
        if (ImGui::MenuItem("Show Hidden Files", ...)) { ... }
    }
}
```

→ 사용자가 기대하는 "프로젝트 패널 우클릭 → New Script" 동작이 없다.

---

## 2. 보완 목표

1. Scene 창에서 현재 씬(스프라이트 + 그리드 + 카메라)이 실제로 보인다.
2. Scene/Hierarchy에서 우클릭으로 새 오브젝트(Empty, Sprite 등)를 만들 수 있다.
3. Project Browser에서 우클릭으로 Script/Folder/Scene을 만들 수 있다.
4. 위 모두 undo/redo·MarkModified와 일관되게 통합된다.

---

## 3. 단계별 작업 계획

규모 표기: S(≤0.5일) / M(1–2일) / L(3일+). 우선순위 P0(즉시) > P1 > P2.

### Phase 1 — 우클릭 생성 워크플로 정상화 (P0, ~1일)

> FBO 없이 즉시 가능한 "체감 가장 큰" 수정. 1.2 / 1.3 해소.

| # | 작업 | 변경 위치 | 규모 |
| --- | --- | --- | --- |
| 1-1 | Hierarchy 우클릭 "Sprite" 핸들러 구현 (`CreateObjectCommand` + `SpriteRenderer` 부착) | `HierarchyWindow.cpp:55-59` | S |
| 1-2 | 오브젝트 생성 경로를 커맨드 기반으로 통일 (메뉴바 `Editor.cpp:179-187`도 `CreateObjectCommand` 사용) | `Editor.cpp`, `ObjectCommands` | S |
| 1-3 | Project Browser 컨텍스트 메뉴에 `Create ▸` 서브메뉴 추가: **C++ Script / Folder / Scene** | `ProjectBrowserWindow.cpp:290-302` | M |
| 1-4 | Create Script는 `ScriptCompiler::CreateScriptTemplate` 재사용, 현재 폴더(`currentPath`)에 생성 후 `Refresh()` | `ProjectBrowserWindow.cpp` | S |
| 1-5 | Create Folder/Scene은 `std::filesystem::create_directory` / 빈 씬 직렬화(`SceneSerializer`) 사용 | `ProjectBrowserWindow.cpp`, `SceneSerializer` | M |

**구현 메모**
- `CreateObjectCommand`에 컴포넌트 프리셋 인자를 추가하거나, 생성 후 `created()`로 받은
  포인터에 `AddComponent<SpriteRenderer>()`를 붙이는 헬퍼(`Editor::CreateSpriteObject()`)를 둔다.
- Script 생성 시 이름 입력은 인라인 rename 위젯(Hierarchy의 `renameBuffer` 패턴 재사용) 또는
  기존 `ScriptWindow` 모달 재호출 중 택1. **인라인 권장**(Unity 동작과 일치).
- 새 폴더/스크립트 생성 후 `Refresh()` + `BuildFolderTree()` 호출로 트리 갱신 필수.

**완료 기준**: Hierarchy/Project 빈 공간 우클릭 → 메뉴에서 Sprite·Script·Folder 생성이
실제로 파일/오브젝트를 만들고, undo(오브젝트)·새로고침(파일)이 반영된다.

### Phase 2 — Scene 뷰 렌더링 (FBO → 패널 출력) (P0, ~3일)

> 1.1의 본질 해결. "환경이 보이게" 하는 핵심.

| # | 작업 | 변경 위치 | 규모 |
| --- | --- | --- | --- |
| 2-1 | `Framebuffer` 클래스 신설 (color texture + resize) | `src/Rendering/Framebuffer.{h,cpp}` (신규) | M |
| 2-2 | `Renderer`에 FBO 바인드/언바인드 또는 타깃 지정 지원 | `Renderer.{h,cpp}` | S |
| 2-3 | 씬 렌더 로직을 main 루프에서 **SceneView가 호출하는 렌더 함수**로 이동/공유 | `main.cpp:256-273` → `SceneRenderer`/`SceneViewWindow` | M |
| 2-4 | `SceneViewWindow::OnGUI()`에서 패널 크기에 맞춰 FBO 렌더 후 `ImGui::Image(texId, size)` 출력 (UV flip 주의) | `SceneViewWindow.cpp` | M |
| 2-5 | 무한 그리드 + 원점 축 셰이더/드로우 추가 | `src/Shaders/`, `SceneViewWindow` | M |
| 2-6 | 에디터 전용 Scene 카메라(별도 `Camera2D`) + 마우스 휠 줌 / 중클릭·스페이스 패닝 | `SceneViewWindow`, `Camera2D` | M |

**구현 메모 (FBO 렌더 순서)**
```
[프레임]
 1. SceneView 패널 가용 크기 측정 → FBO 크기와 다르면 resize
 2. FBO 바인드 → Clear → 에디터 카메라로 그리드 + 스프라이트 렌더 → 언바인드
 3. ImGui::Image(fbo.ColorTexture(), avail, uv0=(0,1), uv1=(1,0))  // GL 텍스처 상하반전 보정
```
- 메인 백버퍼 직접 렌더(`main.cpp:267-273`)는 제거하거나, Play 모드 전용 Game View로 분리한다
  (gap analysis §6.2 "Scene/Game View 분리" 항목과 정렬).
- 좌표 변환 헬퍼(스크린↔월드)를 함께 만들어 두면 Phase 3 피킹/기즈모에 재사용된다.

**완료 기준**: 에디터를 켜면 Scene 창 안에 그리드와 현재 씬의 스프라이트가 보이고,
휠로 줌·드래그로 팬이 된다.

### Phase 3 — Scene 뷰 상호작용 (P1, ~3일+)

> "거기서 우클릭 New / 직접 배치·조작"의 완전체. Phase 2 좌표계 위에 구축.

| # | 작업 | 비고 |
| --- | --- | --- |
| 3-1 | 클릭 피킹: 스크린→월드 변환 후 스프라이트 AABB 교차로 선택 | gap analysis §"오브젝트 피킹 AABB" |
| 3-2 | Scene 창 빈 공간 우클릭 → Hierarchy와 동일한 Create 메뉴(생성 위치 = 마우스 월드 좌표) | Phase 1 메뉴 재사용 |
| 3-3 | Move 기즈모(2D 평행이동 핸들) + 드래그로 Transform 갱신(커맨드화) | undo 통합 |
| 3-4 | (선택) Rotate/Scale 기즈모, 그리드 스냅 | |
| 3-5 | (선택) Project Browser 텍스처를 Scene으로 드래그&드롭 → Sprite 생성 (드래그 소스는 이미 존재: `ProjectBrowserWindow.cpp:237-244`) | |

**완료 기준**: Scene 창에서 오브젝트를 클릭 선택, 우클릭 생성, 드래그로 이동할 수 있다.

---

## 4. 작업 순서 권장

```
Phase 1 (1-1 → 1-2 → 1-3 → 1-4 → 1-5)   ← 즉시 체감, 위험 낮음
        ↓
Phase 2 (2-1 → 2-2 → 2-3 → 2-4 → 2-5 → 2-6)   ← 핵심 인프라
        ↓
Phase 3 (3-1 → 3-2 → 3-3 → 3-4/3-5)   ← 상호작용 완성
```

Phase 1은 Phase 2와 독립적이므로 먼저 머지 가능. Phase 3-2(Scene 우클릭 생성)는 Phase 1의
Create 메뉴와 Phase 2의 좌표 변환에 모두 의존한다.

---

## 5. 검증 계획

- **Phase 1**: 수동 — 우클릭 각 항목이 파일/오브젝트를 만드는지. 자동 — `tests/`에
  `ProjectBrowser`의 Create Script가 `.h/.cpp`를 생성하는 단위 테스트, `CreateObjectCommand`
  +SpriteRenderer 부착 테스트 추가.
- **Phase 2**: 기존 헤드리스 스모크(`--smoke-build`, `tests/smoke/`)에 FBO 컬러 어태치먼트가
  유효한지(완성도 검사) 확인하는 경로 추가. 수동으로 그리드/스프라이트 가시성 확인.
- **Phase 3**: 피킹 좌표 변환 단위 테스트(알려진 입력→기대 선택). 기즈모 드래그는 수동.
- 모든 단계: `cmake --build` 통과 + 기존 테스트 회귀 없음.

> 3D 공간 추론 한계상, Phase 2~3의 좌표/그리드 정렬은 **스크린샷으로 시각 확인**을 요청할 것.

---

## 6. 결정 필요 사항 (사용자 확인)

1. ~~**2D 확정?**~~ → **확정(2026-06-14): 2D Scene 뷰포트로 진행.** 엔진을 2D로 보고
   "2D Scene 뷰포트"를 만든다. 실제 3D 메시/카메라/조명은 렌더 파이프라인 전면 재설계
   (수주~수개월)로 별도 계획 대상이며 본 계획 범위 밖.
2. **Game View 분리 여부** — Phase 2에서 메인 백버퍼 직접 렌더를 제거할지, Play 모드용
   Game View로 남길지. → 권장: **Scene/Game View 분리**(gap analysis §6.2와 정렬).
3. **Script 생성 이름 입력 UX** — 인라인 rename vs 모달. → 권장: **인라인**.

---

## 7. 변경 파일 요약 (체크리스트)

- [x] `src/Editor/Windows/HierarchyWindow.cpp` — Sprite/Tilemap 우클릭 핸들러 (1-1) ✅
- [x] `src/Editor/Editor.cpp` / `Commands/ObjectCommands.*` — 생성 커맨드 통일·프리셋 (1-2) ✅
- [x] `src/Editor/Windows/ProjectBrowserWindow.{h,cpp}` — Create 서브메뉴 (1-3~1-5) ✅
- [x] `src/Rendering/Framebuffer.{h,cpp}` (신규) — FBO (2-1) ✅
- [ ] `src/Rendering/Renderer.{h,cpp}` — 렌더 타깃 (2-2, SceneView에서 직접 처리로 대체)
- [x] `src/Editor/Windows/SceneViewWindow.{h,cpp}` — FBO 출력·그리드·카메라·패닝·줌 (2-3~2-6) ✅
- [x] `src/Shaders/` — 그리드 셰이더 grid.vert / grid.frag (2-5) ✅
- [ ] `tests/` — Create/피킹 단위 테스트 (Phase 3)
```
