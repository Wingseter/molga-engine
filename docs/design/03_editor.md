# Molga Engine 에디터 미구현 시스템 리서치

> 대상: C++17 / ImGui (Docking branch) / OpenGL 기반 2D 게임 엔진
> 비교 대상: Unity Editor
> 날짜: 2026-03-22

---

## 목차

1. [Scene View와 기즈모 시스템](#1-scene-view와-기즈모-시스템)
2. [Undo/Redo 시스템](#2-undoredo-시스템)
3. [콘솔/로그 윈도우](#3-콘솔로그-윈도우)
4. [프로파일러 윈도우](#4-프로파일러-윈도우)
5. [멀티 오브젝트 편집](#5-멀티-오브젝트-편집)
6. [애니메이션 에디터](#6-애니메이션-에디터)
7. [드래그 & 드롭 시스템](#7-드래그--드롭-시스템)
8. [컴포넌트 복사/붙여넣기](#8-컴포넌트-복사붙여넣기)
9. [에디터 환경설정 / 프로젝트 설정](#9-에디터-환경설정--프로젝트-설정)
10. [에셋 임포트 파이프라인](#10-에셋-임포트-파이프라인)
11. [구현 우선순위 로드맵](#11-구현-우선순위-로드맵)
12. [시스템 간 의존성 그래프](#12-시스템-간-의존성-그래프)

---

## 1. Scene View와 기즈모 시스템

### 개요 및 필요성

Scene View는 게임 에디터의 핵심 윈도우다. 개발자가 씬 내 오브젝트를 시각적으로 확인하고, 직접 선택하여 이동/회전/스케일링하는 인터랙션의 중심이다. 현재 Molga Engine의 SceneViewWindow는 placeholder 상태(`ImDrawList::AddRectFilled`로 배경만 그리는 수준)이므로, 이것을 기능적인 Scene View로 만드는 것이 가장 급선무다.

Unity에서 Scene View는 다음을 제공한다:
- **카메라 조작**: 패닝(마우스 중간 버튼 드래그), 줌(마우스 휠), 2D 모드에서의 직교 투영
- **오브젝트 선택**: 씬 뷰 위에서 클릭하여 오브젝트 피킹
- **트랜스폼 기즈모**: Move(화살표), Rotate(원), Scale(사각형) 핸들
- **그리드 오버레이**: 좌표 기준선 표시
- **스냅핑**: 그리드 단위로 정렬

### Unity의 구현 방식 (핵심 개념)

#### 렌더링 파이프라인
Unity는 Scene View 전용 카메라를 별도로 운영한다. 게임 카메라와 독립적인 에디터 카메라가 씬을 렌더링하며, 이 결과를 Render Texture(FBO)에 그린 뒤 에디터 UI에 이미지로 표시한다.

#### 오브젝트 피킹
Unity는 두 가지 방식을 병행한다:
1. **Color-based picking (ID 렌더링)**: 각 오브젝트를 고유 색상(ID 인코딩)으로 별도 FBO에 렌더링한 후, 마우스 위치의 픽셀 색상을 읽어 오브젝트를 식별
2. **Ray casting**: 2D에서는 마우스 좌표를 월드 좌표로 변환한 뒤 각 오브젝트의 AABB와 교차 검사

#### 기즈모 시스템
Unity의 `Handles` 클래스가 기즈모 렌더링과 인터랙션을 담당한다. 기즈모는 항상 화면 공간에서 일정 크기를 유지하며(스케일 불변), 마우스와의 거리 기반으로 핫 컨트롤을 관리한다.

### Molga Engine 권장 구현 방법

#### 단계 1: FBO 기반 씬 렌더링

```
렌더링 흐름:
  1. FBO 생성 (color attachment + depth)
  2. FBO에 바인드
  3. 에디터 카메라 기준으로 씬의 모든 GameObject 렌더링
  4. FBO 언바인드
  5. ImGui::Image()로 FBO의 color texture를 Scene View 윈도우에 표시
```

핵심 구현:

```cpp
class SceneViewWindow : public EditorWindow {
public:
    void OnGUI() override;

private:
    // FBO (Framebuffer Object)
    GLuint fbo = 0;
    GLuint colorTexture = 0;
    GLuint depthRBO = 0;
    int fboWidth = 0, fboHeight = 0;

    // 에디터 전용 카메라 (게임 카메라와 독립)
    Camera2D editorCamera;

    // 카메라 조작 상태
    bool isPanning = false;
    ImVec2 lastMousePos;

    // 기즈모 상태
    enum class GizmoMode { None, Translate, Rotate, Scale };
    GizmoMode gizmoMode = GizmoMode::Translate;

    // 그리드 설정
    float gridSize = 32.0f;
    bool showGrid = true;
    bool snapToGrid = false;

    void CreateOrResizeFBO(int width, int height);
    void RenderSceneToFBO();
    void HandleCameraControls();
    void HandleObjectPicking();
    void RenderGrid(ImDrawList* drawList, ImVec2 viewportPos);
    void RenderGizmos(ImDrawList* drawList, ImVec2 viewportPos);

    // 좌표 변환
    Vector2 ScreenToWorld(ImVec2 screenPos);
    ImVec2 WorldToScreen(Vector2 worldPos);
};
```

#### 단계 2: 카메라 조작

```cpp
void SceneViewWindow::HandleCameraControls() {
    // ImGui 윈도우가 포커스되어 있을 때만 처리
    if (!ImGui::IsWindowHovered()) return;

    ImGuiIO& io = ImGui::GetIO();

    // 패닝: 마우스 중간 버튼 드래그 또는 Alt+좌클릭 드래그
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
        // 줌 레벨에 따라 이동량 보정
        float zoomFactor = 1.0f / editorCamera.GetZoom();
        editorCamera.Move(-delta.x * zoomFactor, -delta.y * zoomFactor);
    }

    // 줌: 마우스 휠
    if (io.MouseWheel != 0.0f) {
        float zoomFactor = 1.0f + io.MouseWheel * 0.1f;
        // 마우스 커서 위치를 중심으로 줌
        ImVec2 mousePos = ImGui::GetMousePos();
        Vector2 worldBefore = ScreenToWorld(mousePos);
        editorCamera.Zoom(zoomFactor);
        Vector2 worldAfter = ScreenToWorld(mousePos);
        Vector2 diff = worldBefore - worldAfter;
        editorCamera.Move(diff.x, diff.y);
    }
}
```

#### 단계 3: 오브젝트 피킹 (2D 권장: AABB 기반)

2D 엔진에서는 color-based picking보다 **AABB 교차 테스트**가 더 간단하고 효율적이다.

```cpp
void SceneViewWindow::HandleObjectPicking() {
    if (!ImGui::IsWindowHovered()) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    // 기즈모 조작 중이면 피킹 무시
    if (isGizmoActive) return;

    ImVec2 mousePos = ImGui::GetMousePos();
    Vector2 worldPos = ScreenToWorld(mousePos);

    GameObject* picked = nullptr;
    float closestDist = FLT_MAX;

    // 역순으로 검사 (나중에 그려진 오브젝트가 위에 있음)
    for (auto it = gameObjects->rbegin(); it != gameObjects->rend(); ++it) {
        auto* obj = it->get();
        auto* transform = obj->GetComponent<Transform>();
        auto* sprite = obj->GetComponent<SpriteRenderer>();
        if (!transform) continue;

        Vector2 pos = transform->GetWorldPosition();
        Vector2 scale = transform->GetWorldScale();

        // SpriteRenderer가 있으면 실제 크기 사용, 없으면 기본 크기
        float width = sprite ? sprite->GetWidth() * scale.x : 32.0f * scale.x;
        float height = sprite ? sprite->GetHeight() * scale.y : 32.0f * scale.y;

        AABB bounds(pos.x - width * 0.5f, pos.y - height * 0.5f, width, height);

        if (bounds.Contains(worldPos)) {
            // Z-order 또는 렌더 순서 기반으로 가장 위의 오브젝트 선택
            picked = obj;
            break;  // 역순이므로 첫 번째 히트가 최상위
        }
    }

    Editor::Get().SetSelectedObject(picked);
}
```

**Color-based picking이 필요한 경우** (회전된 스프라이트, 비정형 콜라이더):

```
구현 방법:
  1. 별도 FBO (picking buffer) 생성
  2. 각 오브젝트를 uint32 ID를 RGBA로 인코딩한 색상으로 렌더링
     - R = (id >> 0)  & 0xFF
     - G = (id >> 8)  & 0xFF
     - B = (id >> 16) & 0xFF
     - A = 0xFF
  3. glReadPixels()로 마우스 위치 픽셀 읽기
  4. RGBA를 다시 uint32 ID로 디코딩하여 오브젝트 식별
  5. 매 프레임이 아닌, 클릭 시에만 picking buffer 갱신 (성능)
```

#### 단계 4: 트랜스폼 기즈모

2D 기즈모는 3D에 비해 상당히 단순하다. ImGui의 `ImDrawList`로 직접 렌더링할 수 있다.

```cpp
void SceneViewWindow::RenderGizmos(ImDrawList* drawList, ImVec2 viewportPos) {
    auto* selected = Editor::Get().GetSelectedObject();
    if (!selected) return;

    auto* transform = selected->GetComponent<Transform>();
    if (!transform) return;

    ImVec2 center = WorldToScreen(transform->GetWorldPosition());

    switch (gizmoMode) {
    case GizmoMode::Translate:
        RenderTranslateGizmo(drawList, center);
        break;
    case GizmoMode::Rotate:
        RenderRotateGizmo(drawList, center);
        break;
    case GizmoMode::Scale:
        RenderScaleGizmo(drawList, center);
        break;
    }
}

void SceneViewWindow::RenderTranslateGizmo(ImDrawList* drawList, ImVec2 center) {
    const float arrowLength = 80.0f;  // 화면 픽셀 단위 (줌 불변)
    const float arrowHeadSize = 10.0f;
    const float hitThreshold = 8.0f;

    // X축 (빨강)
    ImVec2 xEnd(center.x + arrowLength, center.y);
    drawList->AddLine(center, xEnd, IM_COL32(255, 60, 60, 255), 2.0f);
    // 화살표 머리
    drawList->AddTriangleFilled(
        ImVec2(xEnd.x + arrowHeadSize, xEnd.y),
        ImVec2(xEnd.x - 4, xEnd.y - arrowHeadSize * 0.5f),
        ImVec2(xEnd.x - 4, xEnd.y + arrowHeadSize * 0.5f),
        IM_COL32(255, 60, 60, 255));

    // Y축 (초록)
    ImVec2 yEnd(center.x, center.y - arrowLength);  // 위쪽이 음의 Y
    drawList->AddLine(center, yEnd, IM_COL32(60, 255, 60, 255), 2.0f);
    drawList->AddTriangleFilled(
        ImVec2(yEnd.x, yEnd.y - arrowHeadSize),
        ImVec2(yEnd.x - arrowHeadSize * 0.5f, yEnd.y + 4),
        ImVec2(yEnd.x + arrowHeadSize * 0.5f, yEnd.y + 4),
        IM_COL32(60, 255, 60, 255));

    // 중앙 사각형 (양축 동시 이동)
    float rectSize = 12.0f;
    drawList->AddRectFilled(center,
        ImVec2(center.x + rectSize, center.y - rectSize),
        IM_COL32(255, 255, 60, 128));
}
```

기즈모 인터랙션 처리:

```cpp
enum class GizmoAxis { None, X, Y, XY };

GizmoAxis HitTestGizmo(ImVec2 center, ImVec2 mousePos) {
    const float arrowLength = 80.0f;
    const float hitThreshold = 8.0f;
    const float rectSize = 12.0f;

    // 중앙 사각형 체크 (XY 동시)
    if (mousePos.x >= center.x && mousePos.x <= center.x + rectSize &&
        mousePos.y <= center.y && mousePos.y >= center.y - rectSize) {
        return GizmoAxis::XY;
    }

    // X축 체크
    if (mousePos.x >= center.x && mousePos.x <= center.x + arrowLength &&
        fabsf(mousePos.y - center.y) < hitThreshold) {
        return GizmoAxis::X;
    }

    // Y축 체크
    if (mousePos.y <= center.y && mousePos.y >= center.y - arrowLength &&
        fabsf(mousePos.x - center.x) < hitThreshold) {
        return GizmoAxis::Y;
    }

    return GizmoAxis::None;
}
```

#### 단계 5: 그리드 오버레이

```cpp
void SceneViewWindow::RenderGrid(ImDrawList* drawList, ImVec2 viewportPos, ImVec2 viewportSize) {
    float zoom = editorCamera.GetZoom();
    float gridScreenSize = gridSize * zoom;

    // 줌 레벨에 따라 그리드 밀도 자동 조절
    while (gridScreenSize < 16.0f) gridScreenSize *= 2.0f;
    while (gridScreenSize > 128.0f) gridScreenSize *= 0.5f;

    ImU32 gridColor = IM_COL32(255, 255, 255, 20);
    ImU32 axisColorX = IM_COL32(255, 80, 80, 100);  // X축 (빨강)
    ImU32 axisColorY = IM_COL32(80, 255, 80, 100);  // Y축 (초록)

    Vector2 camPos = {editorCamera.GetX(), editorCamera.GetY()};

    // 월드 좌표 기준 시작점 계산
    float startWorldX = camPos.x - viewportSize.x / (2.0f * zoom);
    float startWorldY = camPos.y - viewportSize.y / (2.0f * zoom);

    float worldGridSize = gridScreenSize / zoom;
    startWorldX = floorf(startWorldX / worldGridSize) * worldGridSize;
    startWorldY = floorf(startWorldY / worldGridSize) * worldGridSize;

    // 수직선
    for (float wx = startWorldX; ; wx += worldGridSize) {
        ImVec2 screenPos = WorldToScreen({wx, 0});
        if (screenPos.x > viewportPos.x + viewportSize.x) break;
        if (screenPos.x < viewportPos.x) continue;

        ImU32 color = (fabsf(wx) < 0.01f) ? axisColorY : gridColor;
        drawList->AddLine(
            ImVec2(screenPos.x, viewportPos.y),
            ImVec2(screenPos.x, viewportPos.y + viewportSize.y),
            color);
    }

    // 수평선 (유사 패턴)
    // ...
}
```

#### 단계 6: 스냅핑

```cpp
Vector2 SnapToGrid(Vector2 pos, float gridSize) {
    return Vector2(
        roundf(pos.x / gridSize) * gridSize,
        roundf(pos.y / gridSize) * gridSize
    );
}

// 기즈모 드래그 처리 시:
if (snapToGrid) {
    newPosition = SnapToGrid(newPosition, gridSize);
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::GetWindowDrawList()` | 기즈모, 그리드 등 커스텀 렌더링 |
| `ImGui::Image()` / `ImGui::ImageButton()` | FBO 텍스처를 윈도우에 표시 |
| `ImGui::GetContentRegionAvail()` | 뷰포트 사용 가능 크기 |
| `ImGui::GetCursorScreenPos()` | 렌더링 시작 화면 좌표 |
| `ImGui::IsWindowHovered()` | 입력 처리 범위 제한 |
| `ImGui::IsMouseDragging()` | 카메라 패닝, 기즈모 드래그 |
| `ImGui::GetMouseDragDelta()` | 드래그 이동량 |
| `ImGui::GetIO().MouseWheel` | 줌 입력 |
| `ImGui::IsMouseClicked()` | 오브젝트 피킹 |
| `ImGui::InvisibleButton()` | 뷰포트 전체 영역 입력 캡처 |
| `ImGui::IsKeyPressed()` | W/E/R 키로 기즈모 모드 전환 |

### 복잡도

**Very Large** - 에디터에서 가장 복잡한 시스템

구성 요소별 예상 시간:
- FBO 설정 및 씬 렌더링: 1-2일
- 카메라 조작 (패닝/줌): 0.5일
- 그리드 오버레이: 0.5일
- AABB 기반 오브젝트 피킹: 1일
- 트랜스폼 기즈모 (이동): 1-2일
- 트랜스폼 기즈모 (회전/스케일): 1-2일
- 스냅핑: 0.5일
- 키보드 단축키 통합: 0.5일
- **총 예상: 5-9일**

### 의존성

- `Renderer`, `Shader`, `Camera2D` (FBO 렌더링)
- `Transform` 컴포넌트 (기즈모 조작 대상)
- `SpriteRenderer` (오브젝트 바운딩 박스 계산)
- `Editor::SetSelectedObject()` (피킹 결과 연동)

---

## 2. Undo/Redo 시스템

### 개요 및 필요성

실수를 되돌릴 수 없는 에디터는 사용할 수 없다. Undo/Redo는 모든 전문 에디터의 기본 기능이며, 사용자가 안심하고 실험할 수 있게 해주는 안전망이다. 특히 트랜스폼 조작, 오브젝트 생성/삭제, 컴포넌트 추가/제거 등 모든 편집 동작에 대해 작동해야 한다.

### Unity의 구현 방식 (핵심 개념)

Unity는 `Undo` 클래스를 통해 모든 에디터 작업을 기록한다:

- `Undo.RecordObject(obj, "description")`: 오브젝트 수정 전에 호출하여 이전 상태 스냅샷 저장
- `Undo.RegisterCreatedObjectUndo(obj, "description")`: 새 오브젝트 생성 시 undo에 등록
- `Undo.DestroyObjectImmediate(obj)`: undo 가능한 삭제
- `Undo.SetTransformParent()`: 계층 구조 변경

Unity는 내부적으로 **직렬화 기반 스냅샷**을 사용한다. 오브젝트의 전체 직렬화 상태를 저장하고, undo 시 이전 직렬화 데이터로 복원한다. 관련 작업들을 그룹으로 묶어 하나의 undo 단위로 처리한다.

### 구현 접근법 비교

#### 접근법 A: Command 패턴 (권장)

각 편집 동작을 `Command` 객체로 캡슐화한다. 각 커맨드는 `Execute()`와 `Undo()` 메서드를 가진다.

**장점**: 메모리 효율적, 명확한 구조, 세밀한 제어 가능
**단점**: 모든 편집 동작마다 별도 커맨드 클래스 필요

#### 접근법 B: 직렬화 기반 스냅샷

변경 전 오브젝트의 전체 JSON 직렬화 상태를 저장한다.

**장점**: 구현 단순, 새 컴포넌트 추가 시 별도 커맨드 불필요
**단점**: 메모리 사용량 높음, 큰 씬에서 비효율적

#### 접근법 C: Diff 기반 (하이브리드)

변경 전후의 직렬화 상태 차이(diff)만 저장한다.

**장점**: 메모리 효율적이면서 범용적
**단점**: diff 계산 로직 복잡

### Molga Engine 권장 구현 (Command 패턴)

Molga Engine은 이미 `SceneSerializer::SerializeGameObject()`를 가지고 있으므로, 핵심 커맨드에는 Command 패턴을, 복잡한 작업에는 직렬화 스냅샷을 병용하는 하이브리드 방식이 적합하다.

```cpp
// ---- 기본 인터페이스 ----

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual std::string GetDescription() const = 0;
};

// ---- Undo 매니저 ----

class UndoManager {
public:
    static UndoManager& Get();

    // 커맨드 실행 및 기록
    void ExecuteCommand(std::unique_ptr<ICommand> cmd);

    // Undo/Redo
    void Undo();
    void Redo();

    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }

    std::string GetUndoDescription() const;
    std::string GetRedoDescription() const;

    // 메모리 관리
    void SetMaxStackSize(size_t size) { maxStackSize = size; }
    void Clear();

    // 그룹 작업 (여러 커맨드를 하나의 undo 단위로)
    void BeginGroup(const std::string& description);
    void EndGroup();

private:
    std::vector<std::unique_ptr<ICommand>> undoStack;
    std::vector<std::unique_ptr<ICommand>> redoStack;
    size_t maxStackSize = 100;

    // 그룹 지원
    bool isGrouping = false;
    std::unique_ptr<CommandGroup> currentGroup;
};

// ---- 그룹 커맨드 ----

class CommandGroup : public ICommand {
public:
    CommandGroup(const std::string& desc) : description(desc) {}

    void AddCommand(std::unique_ptr<ICommand> cmd) {
        cmd->Execute();
        commands.push_back(std::move(cmd));
    }

    void Execute() override {
        for (auto& cmd : commands) cmd->Execute();
    }

    void Undo() override {
        // 역순으로 undo
        for (auto it = commands.rbegin(); it != commands.rend(); ++it) {
            (*it)->Undo();
        }
    }

    std::string GetDescription() const override { return description; }

private:
    std::string description;
    std::vector<std::unique_ptr<ICommand>> commands;
};
```

#### 구체적 커맨드 예시

```cpp
// ---- Transform 수정 커맨드 ----

class TransformChangeCommand : public ICommand {
public:
    TransformChangeCommand(GameObject* obj, Vector2 newPos, float newRot, Vector2 newScale)
        : target(obj), newPosition(newPos), newRotation(newRot), newScale(newScale) {
        auto* t = obj->GetComponent<Transform>();
        oldPosition = t->GetPosition();
        oldRotation = t->GetRotation();
        oldScale = t->GetScale();
    }

    void Execute() override {
        auto* t = target->GetComponent<Transform>();
        t->SetPosition(newPosition);
        t->SetRotation(newRotation);
        t->SetScale(newScale);
    }

    void Undo() override {
        auto* t = target->GetComponent<Transform>();
        t->SetPosition(oldPosition);
        t->SetRotation(oldRotation);
        t->SetScale(oldScale);
    }

    std::string GetDescription() const override {
        return "Transform 변경: " + target->GetName();
    }

private:
    GameObject* target;
    Vector2 oldPosition, newPosition;
    float oldRotation, newRotation;
    Vector2 oldScale, newScale;
};

// ---- GameObject 생성 커맨드 ----

class CreateGameObjectCommand : public ICommand {
public:
    CreateGameObjectCommand(std::vector<std::shared_ptr<GameObject>>* objects,
                           std::shared_ptr<GameObject> newObj)
        : gameObjects(objects), object(newObj) {}

    void Execute() override {
        gameObjects->push_back(object);
    }

    void Undo() override {
        auto it = std::find(gameObjects->begin(), gameObjects->end(), object);
        if (it != gameObjects->end()) {
            gameObjects->erase(it);
        }
        // 선택 해제
        if (Editor::Get().GetSelectedObject() == object.get()) {
            Editor::Get().SetSelectedObject(nullptr);
        }
    }

    std::string GetDescription() const override {
        return "오브젝트 생성: " + object->GetName();
    }

private:
    std::vector<std::shared_ptr<GameObject>>* gameObjects;
    std::shared_ptr<GameObject> object;
};

// ---- GameObject 삭제 커맨드 ----

class DeleteGameObjectCommand : public ICommand {
public:
    DeleteGameObjectCommand(std::vector<std::shared_ptr<GameObject>>* objects,
                           GameObject* target)
        : gameObjects(objects) {
        // shared_ptr 찾아서 보관 (undo 시 복원 위해)
        for (auto& obj : *objects) {
            if (obj.get() == target) {
                object = obj;
                break;
            }
        }
        // 인덱스 기억 (동일 위치에 복원 위해)
        auto it = std::find(objects->begin(), objects->end(), object);
        insertIndex = std::distance(objects->begin(), it);
    }

    void Execute() override {
        auto it = std::find(gameObjects->begin(), gameObjects->end(), object);
        if (it != gameObjects->end()) {
            gameObjects->erase(it);
        }
    }

    void Undo() override {
        if (insertIndex <= gameObjects->size()) {
            gameObjects->insert(gameObjects->begin() + insertIndex, object);
        } else {
            gameObjects->push_back(object);
        }
    }

    std::string GetDescription() const override {
        return "오브젝트 삭제: " + object->GetName();
    }

private:
    std::vector<std::shared_ptr<GameObject>>* gameObjects;
    std::shared_ptr<GameObject> object;
    size_t insertIndex;
};

// ---- 범용 프로퍼티 변경 (직렬화 기반) ----

class PropertyChangeCommand : public ICommand {
public:
    PropertyChangeCommand(GameObject* obj, const std::string& desc)
        : target(obj), description(desc) {
        // 변경 전 전체 직렬화 스냅샷
        beforeJson = SceneSerializer::SerializeGameObject(obj);
    }

    // Execute 전에 실제 변경이 이미 발생한 후 호출
    void CaptureAfterState() {
        afterJson = SceneSerializer::SerializeGameObject(target);
    }

    void Execute() override {
        // afterJson 상태로 복원
        // (실제로는 target의 컴포넌트들을 afterJson으로 디시리얼라이즈)
    }

    void Undo() override {
        // beforeJson 상태로 복원
    }

    std::string GetDescription() const override { return description; }

private:
    GameObject* target;
    std::string description;
    std::string beforeJson;
    std::string afterJson;
};
```

#### 기존 코드 통합 포인트

현재 `HierarchyWindow::CreateEmptyGameObject()`, `DeleteSelectedObject()`, `DuplicateSelectedObject()`에 직접 오브젝트를 추가/삭제하는 코드가 있다. 이들을 UndoManager를 통해 실행하도록 변경해야 한다:

```cpp
// 변경 전 (현재 코드):
void HierarchyWindow::CreateEmptyGameObject() {
    auto obj = std::make_shared<GameObject>("New GameObject");
    obj->AddComponent<Transform>();
    gameObjects->push_back(obj);
    // ...
}

// 변경 후 (Undo 통합):
void HierarchyWindow::CreateEmptyGameObject() {
    auto obj = std::make_shared<GameObject>("New GameObject");
    obj->AddComponent<Transform>();

    auto cmd = std::make_unique<CreateGameObjectCommand>(gameObjects, obj);
    UndoManager::Get().ExecuteCommand(std::move(cmd));
    // ...
}
```

#### 메모리 관리

```cpp
void UndoManager::ExecuteCommand(std::unique_ptr<ICommand> cmd) {
    cmd->Execute();

    // Redo 스택 클리어 (새 작업 시 분기 삭제)
    redoStack.clear();

    undoStack.push_back(std::move(cmd));

    // 최대 크기 초과 시 가장 오래된 것부터 제거
    while (undoStack.size() > maxStackSize) {
        undoStack.erase(undoStack.begin());
    }
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::IsKeyPressed(ImGuiKey_Z)` + `ImGui::GetIO().KeyCtrl` | Ctrl+Z (Undo) |
| `ImGui::IsKeyPressed(ImGuiKey_Y)` + `ImGui::GetIO().KeyCtrl` | Ctrl+Y (Redo) |
| `ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)` | Edit 메뉴 |
| `ImGui::BeginDisabled()` / `ImGui::EndDisabled()` | 비활성 상태 표시 |

### 복잡도

**Large**

- ICommand 인터페이스 및 UndoManager: 0.5일
- Transform 변경 커맨드: 0.5일
- 생성/삭제 커맨드: 1일
- 컴포넌트 추가/제거 커맨드: 1일
- 프로퍼티 변경 (직렬화 기반): 1일
- 기존 코드 통합 리팩터링: 1-2일
- 단축키 및 메뉴 통합: 0.5일
- **총 예상: 5-7일**

### 의존성

- `SceneSerializer` (직렬화 기반 스냅샷)
- 모든 편집 가능한 시스템에 통합 필요 (Hierarchy, Inspector, Scene View 기즈모)
- `Editor` 클래스 (전역 접근 및 단축키 처리)

---

## 3. 콘솔/로그 윈도우

### 개요 및 필요성

콘솔 윈도우는 개발자가 엔진 내부 상태, 에러, 경고를 실시간으로 모니터링하는 창이다. 현재 Molga Engine의 `Log` 네임스페이스는 `std::cout`/`std::cerr`로만 출력하므로, 에디터 내에서 로그를 확인할 수 없다. Unity의 Console은:

- 로그 레벨 필터링 (Info/Warning/Error)
- 클릭 가능한 스택 트레이스
- 중복 메시지 접기 (collapse)
- Play 시 자동 클리어 옵션
- 검색/필터 기능

### Unity의 구현 방식

Unity Console은 내부 로그 시스템에 콜백을 등록하여 모든 `Debug.Log()`, `Debug.LogWarning()`, `Debug.LogError()` 호출을 캡처한다. 로그 엔트리에는 메시지, 스택 트레이스, 타임스탬프, 로그 타입이 포함된다. 동일 메시지가 반복되면 카운터를 증가시키는 방식으로 접는다.

### Molga Engine 권장 구현

#### 로그 시스템 확장

현재 `Log.h`를 확장하여 에디터 콘솔과 연결:

```cpp
// ---- 로그 엔트리 구조체 ----

enum class LogLevel { Info, Warning, Error };

struct LogEntry {
    LogLevel level;
    std::string tag;
    std::string message;
    double timestamp;      // 엔진 시작 이후 초
    uint32_t count = 1;    // 중복 접기용
    size_t hash = 0;       // 빠른 중복 비교용

    bool operator==(const LogEntry& other) const {
        return hash == other.hash && level == other.level &&
               tag == other.tag && message == other.message;
    }
};

// ---- 링 버퍼 기반 로그 저장소 ----

class LogBuffer {
public:
    static LogBuffer& Get();

    void AddEntry(LogLevel level, const std::string& tag, const std::string& msg);
    void Clear();

    const std::vector<LogEntry>& GetEntries() const { return entries; }
    size_t GetInfoCount() const { return infoCount; }
    size_t GetWarnCount() const { return warnCount; }
    size_t GetErrorCount() const { return errorCount; }

    // 콜백 (새 로그 추가 시 알림)
    using LogCallback = std::function<void(const LogEntry&)>;
    void SetCallback(LogCallback cb) { callback = cb; }

private:
    static constexpr size_t MAX_ENTRIES = 10000;
    std::vector<LogEntry> entries;  // 링 버퍼로 전환 가능
    size_t infoCount = 0;
    size_t warnCount = 0;
    size_t errorCount = 0;
    LogCallback callback;

    // 중복 접기: 마지막 엔트리와 같으면 count++
    bool TryCollapse(LogLevel level, const std::string& tag, const std::string& msg);
};
```

링 버퍼 구현:

```cpp
template<typename T, size_t Capacity>
class RingBuffer {
public:
    void push_back(T&& item) {
        if (size_ < Capacity) {
            buffer[tail_] = std::move(item);
            tail_ = (tail_ + 1) % Capacity;
            size_++;
        } else {
            buffer[tail_] = std::move(item);
            tail_ = (tail_ + 1) % Capacity;
            head_ = (head_ + 1) % Capacity;
        }
    }

    const T& operator[](size_t i) const {
        return buffer[(head_ + i) % Capacity];
    }

    size_t size() const { return size_; }
    void clear() { head_ = tail_ = size_ = 0; }

    // 이터레이터 지원
    class iterator { /* ... */ };
    iterator begin() const;
    iterator end() const;

private:
    T buffer[Capacity];
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;
};
```

#### 기존 Log 네임스페이스 연결

```cpp
// Log.cpp 수정
namespace Log {

void Info(const std::string& tag, const std::string& msg) {
    std::cout << "[" << tag << "] " << msg << std::endl;
    LogBuffer::Get().AddEntry(LogLevel::Info, tag, msg);  // 추가
}

void Warn(const std::string& tag, const std::string& msg) {
    std::cout << "[" << tag << "] [WARN] " << msg << std::endl;
    LogBuffer::Get().AddEntry(LogLevel::Warning, tag, msg);  // 추가
}

void Error(const std::string& tag, const std::string& msg) {
    std::cerr << "[" << tag << "] [ERROR] " << msg << std::endl;
    LogBuffer::Get().AddEntry(LogLevel::Error, tag, msg);  // 추가
}

}
```

#### 콘솔 윈도우

```cpp
class ConsoleWindow : public EditorWindow {
public:
    ConsoleWindow();
    void OnGUI() override;

private:
    bool showInfo = true;
    bool showWarnings = true;
    bool showErrors = true;
    bool collapse = false;
    bool autoScroll = true;
    bool clearOnPlay = true;
    char searchBuffer[256] = "";

    void DrawToolbar();
    void DrawLogEntries();
    ImU32 GetLevelColor(LogLevel level);
    const char* GetLevelIcon(LogLevel level);
};

void ConsoleWindow::OnGUI() {
    if (!isOpen) return;
    ImGui::Begin(title.c_str(), &isOpen);

    DrawToolbar();
    ImGui::Separator();
    DrawLogEntries();

    ImGui::End();
}

void ConsoleWindow::DrawToolbar() {
    // 클리어 버튼
    if (ImGui::Button("Clear")) {
        LogBuffer::Get().Clear();
    }
    ImGui::SameLine();

    // 검색
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##ConsoleSearch", "Filter...", searchBuffer, sizeof(searchBuffer));
    ImGui::SameLine();

    // 옵션
    ImGui::Checkbox("Collapse", &collapse);
    ImGui::SameLine();
    ImGui::Checkbox("Clear on Play", &clearOnPlay);
    ImGui::SameLine();

    // 레벨 토글 버튼 (카운트 표시)
    auto& buf = LogBuffer::Get();

    ImGui::SameLine(ImGui::GetWindowWidth() - 250);

    // Info 토글
    if (showInfo) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button(("Info: " + std::to_string(buf.GetInfoCount())).c_str())) {
        showInfo = !showInfo;
    }
    if (showInfo) ImGui::PopStyleColor();
    ImGui::SameLine();

    // Warning 토글
    if (showWarnings) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.0f, 1.0f));
    if (ImGui::Button(("Warn: " + std::to_string(buf.GetWarnCount())).c_str())) {
        showWarnings = !showWarnings;
    }
    if (showWarnings) ImGui::PopStyleColor();
    ImGui::SameLine();

    // Error 토글
    if (showErrors) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
    if (ImGui::Button(("Error: " + std::to_string(buf.GetErrorCount())).c_str())) {
        showErrors = !showErrors;
    }
    if (showErrors) ImGui::PopStyleColor();
}

void ConsoleWindow::DrawLogEntries() {
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    auto& entries = LogBuffer::Get().GetEntries();
    ImGuiListClipper clipper;
    // 필터된 엔트리 리스트 구성 (미리 인덱스 배열 구축)
    std::vector<size_t> filteredIndices;
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& entry = entries[i];
        // 레벨 필터
        if (entry.level == LogLevel::Info && !showInfo) continue;
        if (entry.level == LogLevel::Warning && !showWarnings) continue;
        if (entry.level == LogLevel::Error && !showErrors) continue;
        // 검색 필터
        if (searchBuffer[0] != '\0') {
            if (entry.message.find(searchBuffer) == std::string::npos &&
                entry.tag.find(searchBuffer) == std::string::npos) continue;
        }
        filteredIndices.push_back(i);
    }

    clipper.Begin(static_cast<int>(filteredIndices.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            const auto& entry = entries[filteredIndices[row]];
            ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(entry.level));

            // [태그] 메시지 (카운트가 2이상이면 표시)
            std::string display = "[" + entry.tag + "] " + entry.message;
            if (collapse && entry.count > 1) {
                display += " (" + std::to_string(entry.count) + ")";
            }

            ImGui::TextUnformatted(display.c_str());
            ImGui::PopStyleColor();
        }
    }
    clipper.End();

    // 자동 스크롤
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGuiListClipper` | 대량 로그 엔트리 가상 스크롤 (필수, 성능 핵심) |
| `ImGui::BeginChild()` | 스크롤 가능한 로그 영역 |
| `ImGui::SetScrollHereY()` | 자동 스크롤 |
| `ImGui::PushStyleColor()` / `PopStyleColor()` | 레벨별 색상 |
| `ImGui::TextUnformatted()` | 빠른 텍스트 렌더링 (`Text()`보다 효율적) |
| `ImGui::InputTextWithHint()` | 검색 필터 |
| `ImGui::Selectable()` | 클릭 가능한 로그 행 (선택하여 상세 보기) |

### 복잡도

**Medium**

- LogBuffer / 링 버퍼: 0.5일
- Log 네임스페이스 통합: 0.5일
- ConsoleWindow UI: 1일
- 필터링/검색: 0.5일
- 중복 접기: 0.5일
- **총 예상: 3일**

### 의존성

- `Log` 네임스페이스 (확장 필요)
- `EditorState` (Play 시 클리어)
- `WindowManager` (등록)

---

## 4. 프로파일러 윈도우

### 개요 및 필요성

프로파일러는 엔진의 성능 병목을 식별하는 데 필수적이다. 현재 `StatsWindow`가 FPS와 델타 타임만 보여주는 반면, 프로파일러는 프레임별 각 시스템의 소요 시간, CPU/GPU 사용량, 메모리, 드로우 콜 수 등을 제공해야 한다.

Unity의 Profiler는 다음을 제공한다:
- 프레임 타임라인 그래프
- CPU/GPU 사용량 분석
- 계층적 함수별 시간 분석
- 메모리 프로파일링
- 렌더링 통계 (드로우 콜, 배치, 삼각형 수)

### Unity의 구현 방식

Unity Profiler는 `ProfilerMarker`를 사용한 코드 계측(instrumentation) 방식이다. 엔진의 주요 코드 영역에 마커를 삽입하고, 각 마커의 시작/종료 시간을 기록한다. 이 데이터를 프레임별로 수집하여 시각화한다. 계층적 구조(부모 마커 내 자식 마커)를 지원한다.

### Molga Engine 권장 구현

#### 프로파일링 인프라

```cpp
// ---- 프로파일러 마커 ----

struct ProfileSample {
    std::string name;
    double startTime;      // 고해상도 타이머
    double endTime;
    int depth;             // 중첩 깊이 (계층 구조)
    uint32_t color;        // 시각화 색상
};

struct FrameProfile {
    int frameNumber;
    double frameStartTime;
    double frameEndTime;
    double frameDuration;
    std::vector<ProfileSample> samples;

    // 렌더링 통계
    int drawCalls = 0;
    int triangles = 0;
    int batchCount = 0;
    size_t memoryUsage = 0;
};

// ---- 프로파일러 코어 ----

class Profiler {
public:
    static Profiler& Get();

    void BeginFrame();
    void EndFrame();

    void BeginSample(const std::string& name);
    void EndSample();

    // 렌더링 통계 기록
    void RecordDrawCall() { currentFrame.drawCalls++; }
    void RecordTriangles(int count) { currentFrame.triangles += count; }

    // 프레임 히스토리 접근
    const FrameProfile& GetFrame(int index) const;
    int GetFrameCount() const { return static_cast<int>(frameHistory.size()); }

    bool IsEnabled() const { return enabled; }
    void SetEnabled(bool e) { enabled = e; }

private:
    bool enabled = false;
    FrameProfile currentFrame;
    std::vector<FrameProfile> frameHistory;
    static constexpr int MAX_HISTORY = 300;  // 약 5초 분량 (60fps)

    int currentDepth = 0;
    int frameCounter = 0;
};

// ---- RAII 스코프 마커 ----

class ProfileScope {
public:
    ProfileScope(const std::string& name) {
        if (Profiler::Get().IsEnabled()) {
            Profiler::Get().BeginSample(name);
            active = true;
        }
    }
    ~ProfileScope() {
        if (active) {
            Profiler::Get().EndSample();
        }
    }
private:
    bool active = false;
};

#define PROFILE_SCOPE(name) ProfileScope _profileScope##__LINE__(name)
#define PROFILE_FUNCTION() ProfileScope _profileScope##__LINE__(__FUNCTION__)
```

#### 고해상도 타이머

```cpp
#include <chrono>

class HighResTimer {
public:
    static double Now() {
        using namespace std::chrono;
        auto now = high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        return duration_cast<nanoseconds>(duration).count() / 1e9;
    }
};
```

#### 엔진 코드 계측 예시

```cpp
// main loop에서:
void Engine::Update(float dt) {
    PROFILE_SCOPE("Engine::Update");

    {
        PROFILE_SCOPE("Physics");
        // 물리 업데이트
    }

    {
        PROFILE_SCOPE("Scripts");
        for (auto& obj : gameObjects) {
            obj->Update(dt);
        }
    }

    {
        PROFILE_SCOPE("Rendering");
        renderer.Begin(shader, camera);
        for (auto& obj : gameObjects) {
            PROFILE_SCOPE("DrawSprite");
            obj->Render();
        }
        renderer.End();
    }

    {
        PROFILE_SCOPE("Editor GUI");
        editor.RenderGUI();
    }
}
```

#### 프로파일러 윈도우

```cpp
class ProfilerWindow : public EditorWindow {
public:
    ProfilerWindow();
    void OnGUI() override;

private:
    int selectedFrame = -1;  // -1 = 최신 프레임
    bool recording = true;

    void DrawFrameTimeline();
    void DrawFrameDetail();
    void DrawRenderingStats();
    void DrawMemoryInfo();
};

void ProfilerWindow::DrawFrameTimeline() {
    auto& profiler = Profiler::Get();
    int frameCount = profiler.GetFrameCount();
    if (frameCount == 0) return;

    ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 80);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 배경
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                            IM_COL32(30, 30, 30, 255));

    // 16ms (60fps) 기준선
    float targetLine = size.y * (1.0f - 16.666f / 33.333f);  // 33ms를 최대로
    drawList->AddLine(
        ImVec2(pos.x, pos.y + targetLine),
        ImVec2(pos.x + size.x, pos.y + targetLine),
        IM_COL32(0, 255, 0, 80));

    // 프레임 바 그리기
    float barWidth = size.x / std::min(frameCount, 300);
    for (int i = 0; i < std::min(frameCount, 300); i++) {
        const auto& frame = profiler.GetFrame(i);
        float height = static_cast<float>(frame.frameDuration / 0.033333) * size.y;
        height = std::min(height, size.y);

        ImU32 color;
        if (frame.frameDuration > 0.033333)      color = IM_COL32(255, 50, 50, 255);   // 빨강: >33ms
        else if (frame.frameDuration > 0.016666) color = IM_COL32(255, 200, 50, 255);  // 노랑: >16ms
        else                                      color = IM_COL32(50, 200, 50, 255);   // 초록: <16ms

        float x = pos.x + i * barWidth;
        float y = pos.y + size.y - height;
        drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + barWidth - 1, pos.y + size.y), color);

        // 선택된 프레임 하이라이트
        if (i == selectedFrame) {
            drawList->AddRect(ImVec2(x, pos.y), ImVec2(x + barWidth, pos.y + size.y),
                             IM_COL32(255, 255, 255, 255));
        }
    }

    // 마우스 클릭으로 프레임 선택
    ImGui::InvisibleButton("##FrameTimeline", size);
    if (ImGui::IsItemClicked()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        selectedFrame = static_cast<int>((mousePos.x - pos.x) / barWidth);
        selectedFrame = std::clamp(selectedFrame, 0, frameCount - 1);
    }
}

void ProfilerWindow::DrawFrameDetail() {
    auto& profiler = Profiler::Get();
    int idx = (selectedFrame >= 0) ? selectedFrame : profiler.GetFrameCount() - 1;
    if (idx < 0 || idx >= profiler.GetFrameCount()) return;

    const auto& frame = profiler.GetFrame(idx);

    ImGui::Text("Frame %d  |  %.2f ms  |  Draw Calls: %d  |  Triangles: %d",
                frame.frameNumber,
                frame.frameDuration * 1000.0,
                frame.drawCalls,
                frame.triangles);

    ImGui::Separator();

    // 계층적 샘플 테이블
    if (ImGui::BeginTable("ProfileSamples", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", 0, 3.0f);
        ImGui::TableSetupColumn("Total (ms)", 0, 1.0f);
        ImGui::TableSetupColumn("Self (ms)", 0, 1.0f);
        ImGui::TableSetupColumn("Calls", 0, 0.5f);
        ImGui::TableHeadersRow();

        for (const auto& sample : frame.samples) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            // 깊이에 따른 들여쓰기
            ImGui::Indent(sample.depth * 16.0f);
            ImGui::TextUnformatted(sample.name.c_str());
            ImGui::Unindent(sample.depth * 16.0f);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", (sample.endTime - sample.startTime) * 1000.0);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", (sample.endTime - sample.startTime) * 1000.0);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("1");
        }

        ImGui::EndTable();
    }
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::GetWindowDrawList()` | 프레임 타임라인 그래프 커스텀 렌더링 |
| `ImGui::BeginTable()` / `ImGui::EndTable()` | 샘플 데이터 테이블 표시 |
| `ImGui::InvisibleButton()` | 타임라인 클릭 영역 |
| `ImGui::PlotLines()` / `ImGui::PlotHistogram()` | 간단한 성능 그래프 (대안) |
| `ImGuiListClipper` | 대량 샘플 데이터 가상 스크롤 |
| `ImGui::ProgressBar()` | 메모리 사용량 등 시각화 |
| `ImGui::TreeNodeEx()` | 계층적 샘플 트리 (대안) |

### 복잡도

**Large**

- Profiler 코어 (BeginSample/EndSample, 프레임 히스토리): 1일
- 고해상도 타이머: 0.5일
- PROFILE_SCOPE 매크로 및 엔진 계측: 1일
- 프레임 타임라인 그래프: 1일
- 샘플 상세 테이블: 1일
- 렌더링 통계 수집: 0.5일
- **총 예상: 5일**

### 의존성

- 엔진 전반의 코드 계측이 필요 (Renderer, Physics, Scripting 등)
- `MolgaTime` (프레임 타이밍)
- `Renderer` (드로우 콜, 삼각형 수 카운팅)

---

## 5. 멀티 오브젝트 편집

### 개요 및 필요성

여러 오브젝트를 동시에 선택하고 프로퍼티를 일괄 변경하는 기능이다. 예를 들어 적 10마리를 모두 선택하여 이동 속도를 동시에 변경하거나, 모든 선택 오브젝트의 위치를 일괄 이동할 수 있어야 한다. 이 기능이 없으면 반복적인 개별 수정이 필요하여 생산성이 크게 저하된다.

### Unity의 구현 방식

Unity는 `Selection.objects`로 다중 선택을 관리한다:
- **동일 프로퍼티**: 모든 선택 오브젝트의 값이 같으면 정상 표시
- **혼합 값 (Mixed Values)**: 값이 다르면 대시(`-`) 또는 빈 필드로 표시, `EditorGUI.showMixedValue = true`
- **변경 시**: 수정하면 모든 선택 오브젝트에 동일하게 적용
- **Prefab 처리**: 멀티 편집 시에도 개별 오브젝트의 prefab 오버라이드 추적

### Molga Engine 권장 구현

#### 선택 시스템 확장

현재 `Editor`는 단일 `selectedObject`만 관리한다. 이를 다중 선택으로 확장해야 한다.

```cpp
class SelectionManager {
public:
    static SelectionManager& Get();

    // 선택 조작
    void Select(GameObject* obj);            // 단일 선택 (기존 선택 대체)
    void AddToSelection(GameObject* obj);     // Ctrl+클릭
    void RemoveFromSelection(GameObject* obj);
    void ToggleSelection(GameObject* obj);    // Ctrl+클릭 토글
    void SelectRange(GameObject* from, GameObject* to);  // Shift+클릭

    void ClearSelection();
    void SelectAll(const std::vector<std::shared_ptr<GameObject>>& objects);

    // 쿼리
    bool IsSelected(GameObject* obj) const;
    size_t GetSelectionCount() const { return selection.size(); }
    const std::vector<GameObject*>& GetSelection() const { return selection; }

    // 단일 선택 호환 (기존 코드와의 호환)
    GameObject* GetPrimarySelection() const {
        return selection.empty() ? nullptr : selection.front();
    }

    // 콜백
    using SelectionChangedCallback = std::function<void()>;
    void SetCallback(SelectionChangedCallback cb) { callback = cb; }

private:
    std::vector<GameObject*> selection;
    SelectionChangedCallback callback;
};
```

#### Inspector 멀티 편집

```cpp
void InspectorWindow::OnGUI() {
    // ...
    auto& sel = SelectionManager::Get();

    if (sel.GetSelectionCount() == 0) {
        ImGui::TextDisabled("No object selected");
        ImGui::End();
        return;
    }

    if (sel.GetSelectionCount() == 1) {
        DrawSingleObjectInspector(sel.GetPrimarySelection());
    } else {
        DrawMultiObjectInspector(sel.GetSelection());
    }

    ImGui::End();
}

void InspectorWindow::DrawMultiObjectInspector(const std::vector<GameObject*>& objects) {
    ImGui::Text("%zu objects selected", objects.size());
    ImGui::Separator();

    // 모든 오브젝트에 공통으로 존재하는 컴포넌트 타입 찾기
    auto commonComponents = FindCommonComponentTypes(objects);

    for (const auto& typeName : commonComponents) {
        DrawMultiComponentEditor(objects, typeName);
    }
}

void InspectorWindow::DrawMultiComponentEditor(
    const std::vector<GameObject*>& objects,
    const std::string& componentType)
{
    // 예: Transform 멀티 편집
    if (componentType == "Transform") {
        DrawMultiTransformEditor(objects);
    }
}

void InspectorWindow::DrawMultiTransformEditor(const std::vector<GameObject*>& objects) {
    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // 모든 오브젝트의 Transform 수집
    std::vector<Transform*> transforms;
    for (auto* obj : objects) {
        if (auto* t = obj->GetComponent<Transform>()) {
            transforms.push_back(t);
        }
    }
    if (transforms.empty()) return;

    // Position
    Vector2 firstPos = transforms[0]->GetPosition();
    bool mixedX = false, mixedY = false;
    for (size_t i = 1; i < transforms.size(); i++) {
        Vector2 pos = transforms[i]->GetPosition();
        if (pos.x != firstPos.x) mixedX = true;
        if (pos.y != firstPos.y) mixedY = true;
    }

    // Mixed 값 표시를 위한 헬퍼
    float posX = firstPos.x;
    float posY = firstPos.y;

    ImGui::Text("Position");

    // X 필드
    if (mixedX) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::InputText("X##Pos", "---", 4, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
        // 사용자가 값을 직접 입력하면 모든 오브젝트에 적용
        // (DragFloat 사용 시)
    } else {
        if (ImGui::DragFloat("X##Pos", &posX, 1.0f)) {
            float delta = posX - firstPos.x;
            for (auto* t : transforms) {
                Vector2 p = t->GetPosition();
                t->SetPosition(p.x + delta, p.y);
            }
        }
    }

    // Y 필드 (유사 패턴)
    // ...
}

// ---- Mixed 값 DragFloat 헬퍼 ----

bool DragFloatMixed(const char* label, float* value, bool isMixed,
                    float speed = 1.0f, float min = 0.0f, float max = 0.0f) {
    if (isMixed) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        // 대시 표시
        char buf[32] = "---";
        bool changed = ImGui::InputText(label, buf, sizeof(buf),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        if (changed) {
            *value = std::atof(buf);
        }
        return changed;
    } else {
        return ImGui::DragFloat(label, value, speed, min, max);
    }
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::DragFloat()` | 프로퍼티 편집 |
| `ImGui::PushStyleColor()` | Mixed 값 시각적 구분 |
| `ImGui::InputText()` | Mixed 상태에서 직접 입력 |
| `ImGui::GetIO().KeyCtrl` | Ctrl+클릭 다중 선택 |
| `ImGui::GetIO().KeyShift` | Shift+클릭 범위 선택 |

### 복잡도

**Medium-Large**

- SelectionManager: 1일
- Hierarchy 다중 선택 통합: 1일
- Inspector 멀티 편집 기본 UI: 1-2일
- Mixed 값 표시 시스템: 1일
- Scene View 멀티 기즈모: 1일
- Undo 통합 (그룹 커맨드): 1일
- **총 예상: 5-7일**

### 의존성

- `Editor` (현재 단일 선택 -> 다중 선택 리팩터링)
- `InspectorWindow` (멀티 편집 UI)
- `HierarchyWindow` (Ctrl/Shift 클릭)
- `SceneViewWindow` (다중 오브젝트 기즈모)
- `UndoManager` (그룹 커맨드로 일괄 변경 undo)

---

## 6. 애니메이션 에디터

### 개요 및 필요성

애니메이션 에디터는 스프라이트 애니메이션의 키프레임을 시각적으로 편집하는 도구다. 현재 Molga Engine에 `Animation` 클래스가 있지만 코드로만 프레임을 추가할 수 있다. Unity의 Animation Window처럼 타임라인 위에서 시각적으로 키프레임을 배치하고, 재생 속도를 조절하며, 애니메이션 클립을 관리할 수 있어야 한다.

Unity Animation Window의 핵심 기능:
- **Dope Sheet 모드**: 타임라인 위 다이아몬드 마커로 키프레임 표시
- **Curves 모드**: 프로퍼티 보간 곡선 편집 (Bezier curves)
- **프로퍼티 기록**: 녹화 버튼 → 프로퍼티 변경이 자동으로 키프레임 생성
- **다중 프로퍼티 트랙**: Position.x, Position.y, Rotation 등 개별 트랙
- **프리뷰 재생**: 에디터 내에서 애니메이션 미리보기

### Unity의 구현 방식

Unity의 AnimationClip은 프로퍼티 경로(예: `"Transform.position.x"`)별로 AnimationCurve를 가진다. 각 커브는 Keyframe 배열로, 각 키프레임은 시간(time), 값(value), 접선(inTangent, outTangent)을 포함한다. Dope Sheet는 이 커브 데이터의 단순화된 뷰이고, Curves 뷰는 Bezier 보간을 직접 편집하는 뷰다.

### Molga Engine 권장 구현

#### 애니메이션 데이터 구조

```cpp
// ---- 키프레임 ----

struct Keyframe {
    float time;            // 초 단위
    float value;
    float inTangent = 0;   // 들어오는 접선 기울기
    float outTangent = 0;  // 나가는 접선 기울기

    // 보간 모드
    enum class Interpolation { Linear, Bezier, Constant };
    Interpolation interpolation = Interpolation::Linear;
};

// ---- 애니메이션 커브 ----

class AnimationCurve {
public:
    void AddKeyframe(float time, float value);
    void RemoveKeyframe(int index);
    void SetKeyframe(int index, const Keyframe& kf);

    float Evaluate(float time) const;  // 보간된 값
    const std::vector<Keyframe>& GetKeyframes() const { return keyframes; }

private:
    std::vector<Keyframe> keyframes;  // time 순으로 정렬 유지

    float EvaluateLinear(float time, const Keyframe& a, const Keyframe& b) const;
    float EvaluateBezier(float time, const Keyframe& a, const Keyframe& b) const;
};

// ---- 애니메이션 클립 ----

struct AnimationTrack {
    std::string propertyPath;    // "Transform.position.x", "SpriteRenderer.color.r"
    AnimationCurve curve;
};

class AnimationClip {
public:
    std::string name;
    float duration = 1.0f;
    bool loop = true;
    float sampleRate = 60.0f;

    std::vector<AnimationTrack> tracks;

    void AddTrack(const std::string& propertyPath);
    void RemoveTrack(const std::string& propertyPath);
    AnimationTrack* GetTrack(const std::string& propertyPath);
};
```

#### 애니메이션 윈도우

```cpp
class AnimationWindow : public EditorWindow {
public:
    AnimationWindow();
    void OnGUI() override;

private:
    enum class ViewMode { DopeSheet, Curves };
    ViewMode viewMode = ViewMode::DopeSheet;

    AnimationClip* currentClip = nullptr;
    float currentTime = 0.0f;
    bool isPlaying = false;
    bool isRecording = false;
    float zoom = 1.0f;        // 타임라인 확대/축소
    float scrollX = 0.0f;     // 타임라인 수평 스크롤

    // 선택 상태
    int selectedTrackIndex = -1;
    std::vector<int> selectedKeyframes;

    void DrawToolbar();
    void DrawTimeline();
    void DrawDopeSheet();
    void DrawCurvesView();
    void DrawPropertyList();

    // 좌표 변환
    float TimeToScreenX(float time);
    float ScreenXToTime(float screenX);
};

void AnimationWindow::DrawToolbar() {
    // 녹화 버튼
    ImVec4 recordColor = isRecording ?
        ImVec4(1, 0, 0, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::PushStyleColor(ImGuiCol_Text, recordColor);
    if (ImGui::Button("REC")) {
        isRecording = !isRecording;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // 재생 컨트롤
    if (ImGui::Button(isPlaying ? "||" : ">")) {
        isPlaying = !isPlaying;
    }
    ImGui::SameLine();
    if (ImGui::Button("<<")) {
        currentTime = 0;
    }
    ImGui::SameLine();

    // 현재 시간 표시
    ImGui::SetNextItemWidth(100);
    ImGui::DragFloat("##Time", &currentTime, 0.01f, 0, currentClip ? currentClip->duration : 1.0f);
    ImGui::SameLine();

    // 뷰 모드 전환
    if (ImGui::Button("Dope Sheet")) viewMode = ViewMode::DopeSheet;
    ImGui::SameLine();
    if (ImGui::Button("Curves")) viewMode = ViewMode::Curves;
}

void AnimationWindow::DrawDopeSheet() {
    if (!currentClip) return;

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    float propertyListWidth = 200.0f;
    float timelineHeight = 30.0f;  // 타임 눈금

    // 좌측: 프로퍼티 리스트
    ImGui::BeginChild("PropertyList", ImVec2(propertyListWidth, 0), true);
    ImGui::Dummy(ImVec2(0, timelineHeight));  // 타임라인 높이만큼 여백
    for (int i = 0; i < currentClip->tracks.size(); i++) {
        bool selected = (i == selectedTrackIndex);
        if (ImGui::Selectable(currentClip->tracks[i].propertyPath.c_str(), selected)) {
            selectedTrackIndex = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // 우측: 타임라인 + 키프레임
    ImGui::BeginChild("DopeSheetArea", ImVec2(0, 0), true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float trackHeight = 24.0f;

    // 타임 눈금 그리기
    DrawTimeRuler(drawList, pos, contentSize.x - propertyListWidth, timelineHeight);

    // 현재 시간 인디케이터 (빨간 세로선)
    float indicatorX = TimeToScreenX(currentTime);
    drawList->AddLine(
        ImVec2(indicatorX, pos.y),
        ImVec2(indicatorX, pos.y + timelineHeight + currentClip->tracks.size() * trackHeight),
        IM_COL32(255, 0, 0, 255), 2.0f);

    // 각 트랙의 키프레임 다이아몬드 그리기
    for (int t = 0; t < currentClip->tracks.size(); t++) {
        float trackY = pos.y + timelineHeight + t * trackHeight + trackHeight * 0.5f;
        const auto& track = currentClip->tracks[t];

        for (int k = 0; k < track.curve.GetKeyframes().size(); k++) {
            const auto& kf = track.curve.GetKeyframes()[k];
            float kfX = TimeToScreenX(kf.time);

            // 다이아몬드 모양 키프레임 마커
            ImVec2 center(kfX, trackY);
            float size = 5.0f;
            ImU32 color = IM_COL32(100, 150, 255, 255);

            drawList->AddQuadFilled(
                ImVec2(center.x, center.y - size),
                ImVec2(center.x + size, center.y),
                ImVec2(center.x, center.y + size),
                ImVec2(center.x - size, center.y),
                color);

            // 키프레임 드래그 (시간 이동)
            ImGui::SetCursorScreenPos(ImVec2(center.x - size, center.y - size));
            ImGui::InvisibleButton(("##kf" + std::to_string(t) + "_" + std::to_string(k)).c_str(),
                                   ImVec2(size * 2, size * 2));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                float newTime = ScreenXToTime(ImGui::GetMousePos().x);
                // 키프레임 시간 업데이트
            }
        }
    }

    ImGui::EndChild();
}
```

#### Curves 뷰 (Bezier 커브 편집)

```cpp
void AnimationWindow::DrawCurvesView() {
    if (!currentClip || selectedTrackIndex < 0) return;

    auto& track = currentClip->tracks[selectedTrackIndex];
    auto& keyframes = track.curve.GetKeyframes();
    if (keyframes.empty()) return;

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 배경
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(30, 30, 40, 255));

    // 값 범위 자동 계산
    float minVal = FLT_MAX, maxVal = -FLT_MAX;
    for (const auto& kf : keyframes) {
        minVal = std::min(minVal, kf.value);
        maxVal = std::max(maxVal, kf.value);
    }
    float valueRange = maxVal - minVal;
    if (valueRange < 0.001f) valueRange = 1.0f;

    // 커브 그리기 (샘플링 방식)
    float duration = currentClip->duration;
    ImVec2 prevPoint;
    for (int i = 0; i <= 200; i++) {
        float t = (float)i / 200.0f * duration;
        float val = track.curve.Evaluate(t);

        float x = canvasPos.x + (t / duration) * canvasSize.x;
        float y = canvasPos.y + canvasSize.y - ((val - minVal) / valueRange) * canvasSize.y;

        ImVec2 point(x, y);
        if (i > 0) {
            drawList->AddLine(prevPoint, point, IM_COL32(100, 200, 100, 255), 2.0f);
        }
        prevPoint = point;
    }

    // 키프레임 포인트 + 접선 핸들
    for (int k = 0; k < keyframes.size(); k++) {
        const auto& kf = keyframes[k];
        float x = canvasPos.x + (kf.time / duration) * canvasSize.x;
        float y = canvasPos.y + canvasSize.y - ((kf.value - minVal) / valueRange) * canvasSize.y;

        // 키프레임 점
        drawList->AddCircleFilled(ImVec2(x, y), 5.0f, IM_COL32(255, 255, 100, 255));

        // 접선 핸들 (선 + 작은 원)
        float handleLen = 30.0f;
        float inX = x - handleLen;
        float inY = y + kf.inTangent * handleLen;
        float outX = x + handleLen;
        float outY = y - kf.outTangent * handleLen;

        drawList->AddLine(ImVec2(x, y), ImVec2(inX, inY), IM_COL32(200, 200, 200, 128));
        drawList->AddCircleFilled(ImVec2(inX, inY), 3.0f, IM_COL32(200, 200, 200, 255));
        drawList->AddLine(ImVec2(x, y), ImVec2(outX, outY), IM_COL32(200, 200, 200, 128));
        drawList->AddCircleFilled(ImVec2(outX, outY), 3.0f, IM_COL32(200, 200, 200, 255));
    }

    ImGui::InvisibleButton("##CurvesCanvas", canvasSize);
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::GetWindowDrawList()` | 타임라인, 키프레임, 커브 커스텀 렌더링 |
| `ImGui::InvisibleButton()` | 키프레임/핸들 드래그 영역 |
| `ImGui::BeginChild()` | 프로퍼티 리스트와 타임라인 영역 분리 |
| `ImGui::IsItemActive()` + `IsMouseDragging()` | 키프레임 드래그 |
| `ImGui::SetCursorScreenPos()` | 커스텀 위치에 위젯 배치 |
| `ImDrawList::AddQuadFilled()` | 다이아몬드 키프레임 마커 |
| `ImDrawList::AddBezierCubic()` | Bezier 커브 렌더링 (ImGui 내장) |
| `ImGui::Splitter()` (커스텀) | 프로퍼티 리스트 / 타임라인 분할 |

### 복잡도

**Very Large**

- AnimationClip/Curve 데이터 구조: 1일
- Dope Sheet 뷰: 2-3일
- Curves 뷰: 2-3일
- 키프레임 편집 (추가/삭제/이동): 1-2일
- 재생 시스템 통합: 1일
- 프로퍼티 녹화: 1-2일
- 직렬화 (JSON 저장/로드): 1일
- **총 예상: 9-13일**

### 의존성

- `Animation` 클래스 (리팩터링 필요 - 현재 인덱스 기반에서 시간/커브 기반으로)
- `Transform`, `SpriteRenderer` 등 (애니메이션 대상 프로퍼티)
- 프로퍼티 시스템 또는 리플렉션 (프로퍼티 경로로 값 접근)
- `SceneSerializer` (애니메이션 클립 직렬화)

---

## 7. 드래그 & 드롭 시스템

### 개요 및 필요성

드래그 & 드롭은 에디터의 직관적 워크플로우를 완성하는 기능이다. 에셋 브라우저에서 스프라이트를 Scene View로 끌어다 놓아 오브젝트를 생성하거나, 텍스처를 SpriteRenderer에 드래그하여 할당하는 등의 작업이 가능해야 한다.

Unity의 D&D 시나리오:
- Project 윈도우 텍스처 → Scene View: 해당 텍스처의 Sprite 오브젝트 생성
- Project 윈도우 스크립트 → Inspector: 오브젝트에 스크립트 컴포넌트 추가
- Hierarchy 오브젝트 → Hierarchy 오브젝트: 부모-자식 관계 설정
- Project 윈도우 텍스처 → Inspector의 Sprite 필드: 텍스처 할당

### Unity의 구현 방식

Unity는 `DragAndDrop` 클래스를 통해 전역 D&D 상태를 관리한다:
- `DragAndDrop.StartDrag(label)`: 드래그 시작
- `DragAndDrop.SetGenericData(key, data)`: 페이로드 데이터 설정
- `DragAndDrop.objectReferences`: 드래그 중인 오브젝트 참조
- `DragAndDrop.AcceptDrag()`: 드롭 수락
- `DragAndDrop.visualMode`: 드래그 시 커서 모양 (Copy, Move, Rejected)

### Molga Engine 권장 구현

ImGui는 자체 드래그 & 드롭 시스템을 가지고 있으며, 이것이 매우 적합하다.

#### ImGui 드래그 & 드롭 기본 구조

```
ImGui D&D 흐름:
  1. 소스 위젯에서 BeginDragDropSource() → SetDragDropPayload(type, data, size) → EndDragDropSource()
  2. 타겟 위젯에서 BeginDragDropTarget() → AcceptDragDropPayload(type) → EndDragDropTarget()
  3. 페이로드 타입 문자열로 호환성 체크
```

#### 페이로드 타입 정의

```cpp
namespace DragDropTypes {
    constexpr const char* TEXTURE_ASSET    = "DND_TEXTURE";
    constexpr const char* SCRIPT_ASSET     = "DND_SCRIPT";
    constexpr const char* GAMEOBJECT       = "DND_GAMEOBJECT";
    constexpr const char* COMPONENT        = "DND_COMPONENT";
    constexpr const char* ANIMATION_CLIP   = "DND_ANIM_CLIP";
    constexpr const char* AUDIO_CLIP       = "DND_AUDIO";
    constexpr const char* SCENE_FILE       = "DND_SCENE";
    constexpr const char* FILE_PATH        = "DND_FILE_PATH";
}

// 페이로드 데이터 구조체
struct TexturePayload {
    char path[256];
    GLuint textureId;
};

struct GameObjectPayload {
    unsigned int objectId;
};
```

#### ProjectBrowserWindow에서 드래그 시작 (소스)

```cpp
// ProjectBrowserWindow.cpp 에서 파일 아이콘 렌더링 시:
void ProjectBrowserWindow::DrawFileItem(const std::string& filename, const std::string& fullPath) {
    // ... 기존 파일 아이콘 렌더링 ...

    // 드래그 소스 등록
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        // 파일 확장자에 따라 페이로드 타입 결정
        std::string ext = GetExtension(filename);

        if (ext == ".png" || ext == ".jpg" || ext == ".bmp") {
            TexturePayload payload;
            strncpy(payload.path, fullPath.c_str(), sizeof(payload.path));
            payload.textureId = 0;  // 나중에 로드

            ImGui::SetDragDropPayload(DragDropTypes::TEXTURE_ASSET,
                                      &payload, sizeof(payload));

            // 드래그 중 미리보기
            ImGui::Text("Texture: %s", filename.c_str());
            // 썸네일 표시 가능
        }
        else if (ext == ".cpp" || ext == ".h") {
            ImGui::SetDragDropPayload(DragDropTypes::SCRIPT_ASSET,
                                      fullPath.c_str(), fullPath.size() + 1);
            ImGui::Text("Script: %s", filename.c_str());
        }

        ImGui::EndDragDropSource();
    }
}
```

#### Scene View에서 드롭 수신 (타겟)

```cpp
// SceneViewWindow.cpp에서:
void SceneViewWindow::OnGUI() {
    // ... FBO 이미지 표시 후 ...

    // Scene View를 드롭 타겟으로 등록
    if (ImGui::BeginDragDropTarget()) {
        // 텍스처 에셋 드롭
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDropTypes::TEXTURE_ASSET)) {
            auto* texPayload = static_cast<const TexturePayload*>(payload->Data);

            // 드롭 위치를 월드 좌표로 변환
            ImVec2 mousePos = ImGui::GetMousePos();
            Vector2 worldPos = ScreenToWorld(mousePos);

            // 새 GameObject 생성 (텍스처가 할당된 SpriteRenderer)
            auto obj = std::make_shared<GameObject>("New Sprite");
            auto* transform = obj->AddComponent<Transform>();
            transform->SetPosition(worldPos);
            auto* spriteRenderer = obj->AddComponent<SpriteRenderer>();
            spriteRenderer->SetTexture(texPayload->path);

            // UndoManager를 통해 등록
            auto cmd = std::make_unique<CreateGameObjectCommand>(gameObjects, obj);
            UndoManager::Get().ExecuteCommand(std::move(cmd));
        }

        ImGui::EndDragDropTarget();
    }
}
```

#### Hierarchy에서 부모-자식 D&D

```cpp
void HierarchyWindow::DrawGameObjectNode(GameObject* obj) {
    // ... 기존 TreeNodeEx 렌더링 ...

    // 드래그 소스: 오브젝트를 드래그할 수 있게
    if (ImGui::BeginDragDropSource()) {
        GameObjectPayload payload;
        payload.objectId = obj->GetID();
        ImGui::SetDragDropPayload(DragDropTypes::GAMEOBJECT,
                                  &payload, sizeof(payload));
        ImGui::Text("%s", obj->GetName().c_str());
        ImGui::EndDragDropSource();
    }

    // 드롭 타겟: 다른 오브젝트를 이 오브젝트의 자식으로
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DragDropTypes::GAMEOBJECT)) {
            auto* goPayload = static_cast<const GameObjectPayload*>(payload->Data);
            // ID로 소스 오브젝트 찾기
            GameObject* source = FindObjectByID(goPayload->objectId);
            if (source && source != obj && !IsChildOf(obj, source)) {
                // 부모 설정 (Undo 커맨드로)
                source->SetParent(obj);
            }
        }
        ImGui::EndDragDropTarget();
    }
}
```

#### Inspector 필드에서 드롭 수신

```cpp
// SpriteRenderer의 Inspector GUI에서:
void SpriteRenderer::OnInspectorGUI() {
    // 텍스처 필드
    ImGui::Text("Texture");
    ImGui::SameLine();
    ImGui::Button(texturePath.empty() ? "(None)" : texturePath.c_str(),
                  ImVec2(-1, 0));

    // 텍스처 필드가 드롭 타겟
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(DragDropTypes::TEXTURE_ASSET)) {
            auto* texPayload = static_cast<const TexturePayload*>(payload->Data);
            SetTexture(texPayload->path);
        }
        ImGui::EndDragDropTarget();
    }
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::BeginDragDropSource()` | 드래그 시작 (소스 위젯) |
| `ImGui::SetDragDropPayload()` | 드래그 데이터 설정 |
| `ImGui::EndDragDropSource()` | 드래그 소스 종료 |
| `ImGui::BeginDragDropTarget()` | 드롭 수신 대기 (타겟 위젯) |
| `ImGui::AcceptDragDropPayload()` | 드롭 데이터 수신 및 타입 필터 |
| `ImGui::EndDragDropTarget()` | 드롭 타겟 종료 |
| `ImGuiDragDropFlags_SourceAllowNullID` | ID 없는 위젯에서도 드래그 허용 |
| `ImGuiDragDropFlags_AcceptBeforeDelivery` | 호버 시 미리보기 |

### 복잡도

**Medium**

- 페이로드 타입/구조체 정의: 0.5일
- ProjectBrowser → Scene View (텍스처 드래그): 1일
- ProjectBrowser → Inspector (텍스처 할당): 0.5일
- Hierarchy 부모-자식 D&D: 1일
- 스크립트 에셋 D&D: 0.5일
- **총 예상: 3-4일**

### 의존성

- `ProjectBrowserWindow` (드래그 소스)
- `SceneViewWindow` (드롭 타겟 - FBO 렌더링 이후)
- `InspectorWindow` (드롭 타겟)
- `HierarchyWindow` (양방향)
- `TextureManager` (텍스처 로딩)
- `UndoManager` (드롭으로 생성된 오브젝트 undo)

---

## 8. 컴포넌트 복사/붙여넣기

### 개요 및 필요성

Inspector에서 컴포넌트를 복사하여 다른 오브젝트에 붙여넣거나, 컴포넌트의 순서를 변경하고, 값을 리셋하는 기능이다. 반복적으로 동일한 설정의 컴포넌트를 여러 오브젝트에 적용할 때 필수적이다.

Unity의 컴포넌트 컨텍스트 메뉴:
- **Copy Component**: 컴포넌트 값 복사
- **Paste Component Values**: 같은 타입의 컴포넌트에 값만 붙여넣기
- **Paste Component As New**: 새 컴포넌트로 추가
- **Remove Component**: 컴포넌트 삭제
- **Move Up / Move Down**: Inspector 내 순서 변경
- **Reset**: 기본값으로 초기화

### Molga Engine 권장 구현

기존 `Component`의 `Serialize()`/`Deserialize()`를 활용하면 구현이 비교적 간단하다.

```cpp
// ---- 컴포넌트 클립보드 ----

class ComponentClipboard {
public:
    static ComponentClipboard& Get();

    // 컴포넌트 복사 (JSON 직렬화)
    void Copy(const Component* component) {
        if (!component) return;
        copiedTypeName = component->GetTypeName();
        nlohmann::json j;
        component->Serialize(j);
        copiedData = j;
        hasCopied = true;
    }

    // 값 붙여넣기 (같은 타입의 기존 컴포넌트에)
    bool PasteValues(Component* target) {
        if (!hasCopied || !target) return false;
        if (target->GetTypeName() != copiedTypeName) return false;
        target->Deserialize(copiedData);
        return true;
    }

    // 새 컴포넌트로 붙여넣기
    Component* PasteAsNew(GameObject* target) {
        if (!hasCopied || !target) return nullptr;
        // ComponentFactory를 통해 새 컴포넌트 생성
        auto* comp = ComponentFactory::Create(copiedTypeName, target);
        if (comp) {
            comp->Deserialize(copiedData);
        }
        return comp;
    }

    bool HasCopiedData() const { return hasCopied; }
    const std::string& GetCopiedTypeName() const { return copiedTypeName; }

private:
    bool hasCopied = false;
    std::string copiedTypeName;
    nlohmann::json copiedData;
};
```

#### Inspector 컨텍스트 메뉴 통합

현재 `InspectorWindow::DrawComponent()`에서 컴포넌트 헤더를 그리는 부분에 컨텍스트 메뉴를 추가한다:

```cpp
void InspectorWindow::DrawComponent(Component* component) {
    if (!component) return;

    std::string typeName = component->GetTypeName();
    const char* icon = UIRegistry::GetComponentInfo(typeName).icon;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;

    std::string label = std::string(icon) + " " + typeName;
    bool open = ImGui::TreeNodeEx((void*)component, flags, "%s", label.c_str());

    // ---- 컨텍스트 메뉴 추가 ----
    if (ImGui::BeginPopupContextItem(("##CompCtx_" + typeName).c_str())) {
        auto& clipboard = ComponentClipboard::Get();

        if (ImGui::MenuItem("Copy Component")) {
            clipboard.Copy(component);
        }

        if (ImGui::MenuItem("Paste Component Values",
                            nullptr, false,
                            clipboard.HasCopiedData() &&
                            clipboard.GetCopiedTypeName() == typeName)) {
            // Undo 등록
            UndoManager::Get().BeginGroup("Paste Component Values");
            clipboard.PasteValues(component);
            UndoManager::Get().EndGroup();
        }

        if (ImGui::MenuItem("Paste Component As New",
                            nullptr, false,
                            clipboard.HasCopiedData())) {
            auto* newComp = clipboard.PasteAsNew(target);
            // Undo 등록
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reset")) {
            // 기본 생성자 값으로 리셋
            nlohmann::json defaultJson;
            // 빈 JSON으로 Deserialize하면 기본값으로 리셋되도록 구현
            component->Deserialize(defaultJson);
        }

        ImGui::Separator();

        // Transform은 삭제 불가
        bool canRemove = (typeName != "Transform");
        if (ImGui::MenuItem("Remove Component", nullptr, false, canRemove)) {
            componentToRemove = component;  // 다음 프레임에서 안전하게 제거
        }

        // 순서 변경 (componentMap이 unordered_map이라 순서 개념 없음)
        // 순서를 지원하려면 별도의 순서 리스트 필요
        // if (ImGui::MenuItem("Move Up")) { ... }
        // if (ImGui::MenuItem("Move Down")) { ... }

        ImGui::EndPopup();
    }

    // ... 기존 코드 (Enable 체크박스, TreePop 등) ...
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::BeginPopupContextItem()` | 컴포넌트 헤더 우클릭 컨텍스트 메뉴 |
| `ImGui::MenuItem()` | 메뉴 항목 (비활성 상태 지원) |
| `ImGui::Separator()` | 메뉴 구분선 |
| `ImGui::BeginPopupContextWindow()` | 빈 영역 우클릭 메뉴 |

### 복잡도

**Small-Medium**

- ComponentClipboard: 0.5일
- Inspector 컨텍스트 메뉴: 0.5일
- Copy/Paste 기능: 0.5일
- Reset 기능: 0.5일
- Remove Component: 0.5일
- Undo 통합: 0.5일
- **총 예상: 2-3일**

### 의존성

- `Component::Serialize()` / `Deserialize()` (이미 존재)
- `ComponentFactory` (Paste As New에서 타입 이름으로 생성)
- `InspectorWindow` (컨텍스트 메뉴 위치)
- `UndoManager` (변경사항 undo 지원)

---

## 9. 에디터 환경설정 / 프로젝트 설정

### 개요 및 필요성

에디터 환경설정(Editor Preferences)은 사용자별 에디터 동작 설정을, 프로젝트 설정(Project Settings)은 프로젝트 단위의 엔진/게임 설정을 관리한다. 이 구분이 중요한 이유는 환경설정은 버전 관리에서 제외하고(사용자마다 다름), 프로젝트 설정은 팀원 간 공유되어야 하기 때문이다.

Unity의 구조:
- **Preferences**: 에디터 테마/색상, 외부 도구 경로, 레이아웃, 단축키
- **Project Settings**: Physics 설정, Input Manager, Tags & Layers, Quality Settings, Player Settings (해상도, 빌드 플랫폼)

### Molga Engine 권장 구현

#### 설정 데이터 구조

```cpp
// ---- 에디터 환경설정 (사용자별, ~/.molga/preferences.json) ----

struct EditorPreferences {
    // 외관
    int themeIndex = 0;              // 0=Dark, 1=Light, 2=Classic
    float fontSize = 14.0f;
    bool showGrid = true;
    float gridSize = 32.0f;
    ImVec4 gridColor = {1, 1, 1, 0.1f};

    // Scene View
    float gizmoSize = 1.0f;
    bool snapToGrid = false;
    float snapIncrement = 16.0f;

    // 외부 도구
    std::string codeEditorPath;      // VSCode 등
    std::string imageEditorPath;

    // 동작
    int undoHistorySize = 100;
    bool autoSave = false;
    float autoSaveInterval = 300.0f; // 초

    // 직렬화
    void Save(const std::string& path);
    void Load(const std::string& path);
    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& j);
};

// ---- 프로젝트 설정 (프로젝트별, ProjectDir/settings.json) ----

struct ProjectSettings {
    // Physics 2D
    struct Physics2D {
        Vector2 gravity = {0, 9.8f};
        int velocityIterations = 8;
        int positionIterations = 3;
        bool enableCollisions = true;
    } physics2D;

    // Rendering
    struct Rendering {
        int targetFPS = 60;
        bool vsync = true;
        Vector2 referenceResolution = {1920, 1080};
        Color backgroundColor = Color(0.1f, 0.1f, 0.15f);
    } rendering;

    // Input
    struct InputMapping {
        std::string name;
        int keyPositive;
        int keyNegative;
        std::string description;
    };
    std::vector<InputMapping> inputAxes;

    // Tags & Layers
    std::vector<std::string> tags = {"Untagged", "Player", "Enemy", "Ground"};
    std::vector<std::string> sortingLayers = {"Default", "Background", "Foreground", "UI"};
    std::vector<std::string> layers = {"Default"};

    // 2D용 레이어별 충돌 매트릭스
    // layers[i]와 layers[j]가 충돌하는지 여부
    std::vector<std::vector<bool>> collisionMatrix;

    // Player / Build
    struct PlayerSettings {
        std::string companyName;
        std::string productName;
        std::string version = "1.0.0";
        int windowWidth = 1280;
        int windowHeight = 720;
        bool fullscreen = false;
        bool resizable = true;
    } player;

    void Save(const std::string& projectDir);
    void Load(const std::string& projectDir);
};
```

#### 설정 윈도우

```cpp
class PreferencesWindow : public EditorWindow {
public:
    PreferencesWindow();
    void OnGUI() override;

private:
    int selectedCategory = 0;
    EditorPreferences tempPrefs;  // 편집 중인 임시 사본

    void DrawCategoryList();
    void DrawGeneralSettings();
    void DrawAppearanceSettings();
    void DrawExternalToolsSettings();
    void DrawSceneViewSettings();
};

void PreferencesWindow::OnGUI() {
    if (!isOpen) return;
    ImGui::Begin(title.c_str(), &isOpen, ImGuiWindowFlags_NoDocking);

    // 좌측: 카테고리 리스트
    ImGui::BeginChild("Categories", ImVec2(180, 0), true);
    const char* categories[] = {"General", "Appearance", "External Tools", "Scene View"};
    for (int i = 0; i < 4; i++) {
        if (ImGui::Selectable(categories[i], selectedCategory == i)) {
            selectedCategory = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // 우측: 설정 내용
    ImGui::BeginChild("Settings", ImVec2(0, 0), true);
    switch (selectedCategory) {
        case 0: DrawGeneralSettings(); break;
        case 1: DrawAppearanceSettings(); break;
        case 2: DrawExternalToolsSettings(); break;
        case 3: DrawSceneViewSettings(); break;
    }
    ImGui::EndChild();

    ImGui::End();
}

void PreferencesWindow::DrawAppearanceSettings() {
    ImGui::Text("Appearance");
    ImGui::Separator();

    const char* themes[] = {"Dark", "Light", "Classic"};
    if (ImGui::Combo("Theme", &tempPrefs.themeIndex, themes, 3)) {
        // 테마 즉시 적용
        EditorTheme::Apply(tempPrefs.themeIndex);
    }

    ImGui::DragFloat("Font Size", &tempPrefs.fontSize, 0.5f, 10.0f, 24.0f);
    // 폰트 크기는 재시작 필요할 수 있음
}

// ---- 프로젝트 설정 윈도우 ----

class ProjectSettingsWindow : public EditorWindow {
public:
    ProjectSettingsWindow();
    void OnGUI() override;

private:
    int selectedCategory = 0;
    ProjectSettings tempSettings;

    void DrawPhysics2D();
    void DrawRendering();
    void DrawInput();
    void DrawTagsAndLayers();
    void DrawPlayerSettings();
};

void ProjectSettingsWindow::DrawPhysics2D() {
    ImGui::Text("Physics 2D");
    ImGui::Separator();

    ImGui::DragFloat2("Gravity", &tempSettings.physics2D.gravity.x, 0.1f);
    ImGui::DragInt("Velocity Iterations", &tempSettings.physics2D.velocityIterations, 1, 1, 20);
    ImGui::DragInt("Position Iterations", &tempSettings.physics2D.positionIterations, 1, 1, 20);
    ImGui::Checkbox("Enable Collisions", &tempSettings.physics2D.enableCollisions);
}

void ProjectSettingsWindow::DrawTagsAndLayers() {
    ImGui::Text("Tags & Layers");
    ImGui::Separator();

    // Tags
    if (ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < tempSettings.tags.size(); i++) {
            char buf[128];
            strncpy(buf, tempSettings.tags[i].c_str(), sizeof(buf));
            ImGui::PushID(i);
            if (ImGui::InputText("##Tag", buf, sizeof(buf))) {
                tempSettings.tags[i] = buf;
            }
            ImGui::SameLine();
            if (ImGui::Button("-") && i > 0) {  // "Untagged" 삭제 방지
                tempSettings.tags.erase(tempSettings.tags.begin() + i);
            }
            ImGui::PopID();
        }
        if (ImGui::Button("+ Add Tag")) {
            tempSettings.tags.push_back("New Tag");
        }
    }

    // Sorting Layers
    if (ImGui::CollapsingHeader("Sorting Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < tempSettings.sortingLayers.size(); i++) {
            // 드래그로 순서 변경 가능하게
            char buf[128];
            strncpy(buf, tempSettings.sortingLayers[i].c_str(), sizeof(buf));
            ImGui::PushID(1000 + i);
            if (ImGui::InputText("##SortLayer", buf, sizeof(buf))) {
                tempSettings.sortingLayers[i] = buf;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("+ Add Sorting Layer")) {
            tempSettings.sortingLayers.push_back("New Layer");
        }
    }

    // 충돌 매트릭스
    if (ImGui::CollapsingHeader("Collision Matrix")) {
        int layerCount = static_cast<int>(tempSettings.layers.size());
        for (int i = 0; i < layerCount; i++) {
            ImGui::Text("%s", tempSettings.layers[i].c_str());
            ImGui::SameLine(120);
            for (int j = i; j < layerCount; j++) {
                ImGui::PushID(i * 100 + j);
                bool collides = tempSettings.collisionMatrix[i][j];
                if (ImGui::Checkbox("##col", &collides)) {
                    tempSettings.collisionMatrix[i][j] = collides;
                    tempSettings.collisionMatrix[j][i] = collides;
                }
                ImGui::SameLine();
                ImGui::PopID();
            }
            ImGui::NewLine();
        }
    }
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::BeginChild()` | 좌측 카테고리 / 우측 설정 분할 |
| `ImGui::Selectable()` | 카테고리 선택 |
| `ImGui::Combo()` | 테마 선택 등 드롭다운 |
| `ImGui::DragFloat()` / `DragInt()` | 숫자 설정 |
| `ImGui::InputText()` | 문자열 설정 (태그, 레이어 이름) |
| `ImGui::Checkbox()` | 불리언 설정, 충돌 매트릭스 |
| `ImGui::CollapsingHeader()` | 설정 그룹 접기/펴기 |
| `ImGui::ColorEdit4()` | 색상 설정 |

### 복잡도

**Medium-Large**

- 데이터 구조 설계: 1일
- EditorPreferences 윈도우: 1-2일
- ProjectSettings 윈도우: 2-3일
- JSON 직렬화/역직렬화: 1일
- 설정 적용 로직 (테마, 물리 등): 1-2일
- **총 예상: 5-8일**

### 의존성

- `EditorTheme` (외관 설정 적용)
- `Physics/Collision` (물리 설정 적용)
- `Input` 시스템 (입력 매핑)
- `Project` 클래스 (프로젝트별 경로)
- `BuildManager` (Player 설정)
- nlohmann/json (이미 사용 중)

---

## 10. 에셋 임포트 파이프라인

### 개요 및 필요성

에셋 임포트 파이프라인은 원본 에셋 파일(PNG, WAV, JSON 등)을 엔진이 최적으로 사용할 수 있는 형태로 변환/관리하는 시스템이다. 단순히 파일을 읽는 것이 아니라, 파일별 임포트 설정(예: 텍스처 필터링 모드, 오디오 압축 방식)을 관리하고, 파일이 변경되면 자동으로 재임포트한다.

Unity의 에셋 파이프라인:
- **Meta 파일**: 각 에셋 파일 옆에 `.meta` 파일 생성. GUID, 임포트 설정 저장
- **AssetDatabase**: 모든 에셋의 경로, GUID, 타입, 의존성을 인덱싱
- **AssetImporter**: 파일 타입별 임포터 (TextureImporter, AudioImporter 등)
- **AssetPostprocessor**: 임포트 전후 처리 콜백
- **파일 감시**: FileSystemWatcher로 에셋 폴더 변경 감지 → 자동 재임포트

### Molga Engine 권장 구현

#### GUID 및 Meta 파일 시스템

```cpp
// ---- 에셋 메타 데이터 ----

struct AssetMeta {
    std::string guid;           // 고유 식별자 (UUID v4)
    std::string assetType;      // "Texture", "Audio", "Script", "Scene" 등
    uint64_t lastModified;      // 파일 수정 시간
    nlohmann::json importSettings;  // 타입별 임포트 설정

    void Save(const std::string& metaPath);
    void Load(const std::string& metaPath);
};

// ---- GUID 생성 유틸리티 ----

class GUIDGenerator {
public:
    static std::string Generate() {
        // 간단한 UUID v4 구현
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);

        const char* hex = "0123456789abcdef";
        std::string uuid(32, '0');
        for (auto& c : uuid) c = hex[dis(gen)];

        // UUID v4 포맷: 8-4-4-4-12
        return uuid.substr(0, 8) + "-" + uuid.substr(8, 4) + "-" +
               uuid.substr(12, 4) + "-" + uuid.substr(16, 4) + "-" +
               uuid.substr(20, 12);
    }
};
```

#### 에셋 데이터베이스

```cpp
class AssetDatabase {
public:
    static AssetDatabase& Get();

    // 초기화: 프로젝트 에셋 폴더 스캔
    void Initialize(const std::string& assetsPath);

    // 에셋 조회
    std::string GetGUID(const std::string& assetPath) const;
    std::string GetPath(const std::string& guid) const;
    const AssetMeta* GetMeta(const std::string& guid) const;

    // 에셋 임포트
    void ImportAsset(const std::string& assetPath);
    void ImportAll();
    void Refresh();  // 변경된 파일 탐지 및 재임포트

    // 파일 감시
    void StartFileWatcher();
    void StopFileWatcher();
    void ProcessFileChanges();

    // 에셋 타입 판별
    static std::string DetectAssetType(const std::string& path);

private:
    std::string assetsRootPath;
    std::unordered_map<std::string, AssetMeta> guidToMeta;   // GUID → Meta
    std::unordered_map<std::string, std::string> pathToGuid; // 경로 → GUID

    // 파일 감시 (플랫폼별)
    struct FileChange {
        enum class Type { Created, Modified, Deleted, Renamed };
        Type type;
        std::string path;
        std::string oldPath;  // Renamed일 때
    };
    std::vector<FileChange> pendingChanges;
    std::mutex changesMutex;

    void ScanDirectory(const std::string& dir);
    void CreateMetaFile(const std::string& assetPath);
    AssetMeta LoadOrCreateMeta(const std::string& assetPath);
};
```

#### 파일 감시 (크로스 플랫폼)

```cpp
// macOS: FSEvents / kqueue
// Windows: ReadDirectoryChangesW
// Linux: inotify
// 간단한 폴링 방식 (크로스 플랫폼 대안):

class FileWatcher {
public:
    using Callback = std::function<void(const std::string& path, FileChangeType type)>;

    FileWatcher(const std::string& watchPath, Callback cb);
    ~FileWatcher();

    void Start();  // 별도 스레드에서 실행
    void Stop();

private:
    std::string watchPath;
    Callback callback;
    std::thread watchThread;
    std::atomic<bool> running{false};

    // 파일 상태 캐시 (폴링 방식)
    struct FileState {
        uint64_t lastModified;
        uintmax_t fileSize;
    };
    std::unordered_map<std::string, FileState> fileStates;

    void PollChanges();
};

// 폴링 구현 (500ms 간격)
void FileWatcher::PollChanges() {
    while (running) {
        std::unordered_map<std::string, FileState> currentStates;

        // 디렉토리 재귀 순회
        for (const auto& entry : std::filesystem::recursive_directory_iterator(watchPath)) {
            if (!entry.is_regular_file()) continue;
            std::string path = entry.path().string();

            // .meta 파일 자체는 건너뜀
            if (path.ends_with(".meta")) continue;

            auto lastWrite = std::filesystem::last_write_time(entry);
            auto time = lastWrite.time_since_epoch().count();
            auto size = entry.file_size();

            currentStates[path] = {static_cast<uint64_t>(time), size};

            // 이전에 없던 파일 → Created
            if (fileStates.find(path) == fileStates.end()) {
                callback(path, FileChangeType::Created);
            }
            // 수정 시간 변경 → Modified
            else if (fileStates[path].lastModified != static_cast<uint64_t>(time)) {
                callback(path, FileChangeType::Modified);
            }
        }

        // 이전에 있었지만 지금 없는 파일 → Deleted
        for (const auto& [path, state] : fileStates) {
            if (currentStates.find(path) == currentStates.end()) {
                callback(path, FileChangeType::Deleted);
            }
        }

        fileStates = currentStates;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
```

#### 타입별 임포터

```cpp
// ---- 임포터 인터페이스 ----

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;
    virtual bool CanImport(const std::string& extension) const = 0;
    virtual bool Import(const std::string& path, const nlohmann::json& settings) = 0;
    virtual nlohmann::json GetDefaultSettings() const = 0;
    virtual void DrawImportSettingsGUI(nlohmann::json& settings) = 0;  // Inspector에서 표시
};

// ---- 텍스처 임포터 ----

class TextureImporter : public IAssetImporter {
public:
    bool CanImport(const std::string& ext) const override {
        return ext == ".png" || ext == ".jpg" || ext == ".bmp" || ext == ".tga";
    }

    nlohmann::json GetDefaultSettings() const override {
        return {
            {"filterMode", "Point"},       // Point (pixel art) 또는 Linear
            {"wrapMode", "Clamp"},         // Clamp 또는 Repeat
            {"pixelsPerUnit", 100},
            {"spriteMode", "Single"},       // Single 또는 Multiple (sprite sheet)
            {"generateMipmaps", false},
            {"maxSize", 2048},
            {"compression", "None"}
        };
    }

    bool Import(const std::string& path, const nlohmann::json& settings) override {
        // 텍스처 로드 및 설정 적용
        auto* tex = TextureManager::Get().Load(path);
        if (!tex) return false;

        // 필터 모드 적용
        std::string filter = settings.value("filterMode", "Point");
        if (filter == "Point") {
            tex->SetFilterMode(GL_NEAREST);  // 픽셀 아트에 적합
        } else {
            tex->SetFilterMode(GL_LINEAR);
        }

        // 랩 모드
        std::string wrap = settings.value("wrapMode", "Clamp");
        tex->SetWrapMode(wrap == "Repeat" ? GL_REPEAT : GL_CLAMP_TO_EDGE);

        return true;
    }

    void DrawImportSettingsGUI(nlohmann::json& settings) override {
        // Filter Mode
        const char* filterModes[] = {"Point", "Linear"};
        std::string current = settings.value("filterMode", "Point");
        int selected = (current == "Linear") ? 1 : 0;
        if (ImGui::Combo("Filter Mode", &selected, filterModes, 2)) {
            settings["filterMode"] = filterModes[selected];
        }

        // Wrap Mode
        const char* wrapModes[] = {"Clamp", "Repeat"};
        current = settings.value("wrapMode", "Clamp");
        selected = (current == "Repeat") ? 1 : 0;
        if (ImGui::Combo("Wrap Mode", &selected, wrapModes, 2)) {
            settings["wrapMode"] = wrapModes[selected];
        }

        // Pixels Per Unit
        int ppu = settings.value("pixelsPerUnit", 100);
        if (ImGui::DragInt("Pixels Per Unit", &ppu, 1, 1, 1000)) {
            settings["pixelsPerUnit"] = ppu;
        }

        // Sprite Mode
        const char* spriteModes[] = {"Single", "Multiple"};
        current = settings.value("spriteMode", "Single");
        selected = (current == "Multiple") ? 1 : 0;
        if (ImGui::Combo("Sprite Mode", &selected, spriteModes, 2)) {
            settings["spriteMode"] = spriteModes[selected];
        }

        // Apply 버튼
        ImGui::Spacing();
        if (ImGui::Button("Apply")) {
            // 재임포트 트리거
        }
    }
};

// ---- 임포터 레지스트리 ----

class ImporterRegistry {
public:
    static ImporterRegistry& Get();

    void Register(std::unique_ptr<IAssetImporter> importer) {
        importers.push_back(std::move(importer));
    }

    IAssetImporter* FindImporter(const std::string& extension) {
        for (auto& imp : importers) {
            if (imp->CanImport(extension)) return imp.get();
        }
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<IAssetImporter>> importers;
};
```

#### Inspector에서 에셋 선택 시 임포트 설정 표시

```cpp
// ProjectBrowserWindow에서 파일 선택 시:
void ProjectBrowserWindow::OnFileSelected(const std::string& path) {
    auto* importer = ImporterRegistry::Get().FindImporter(GetExtension(path));
    if (importer) {
        // Inspector에 임포트 설정 표시
        auto& meta = AssetDatabase::Get().GetMeta(path);
        selectedImporter = importer;
        selectedSettings = meta.importSettings;
    }
}

// Inspector에서:
void InspectorWindow::DrawAssetImportSettings() {
    if (selectedImporter && !selectedSettings.empty()) {
        ImGui::Text("Import Settings");
        ImGui::Separator();
        selectedImporter->DrawImportSettingsGUI(selectedSettings);
    }
}
```

### 사용할 핵심 ImGui API

| API | 용도 |
|-----|------|
| `ImGui::Combo()` | 임포트 설정 드롭다운 |
| `ImGui::DragInt()` / `DragFloat()` | 숫자 설정 |
| `ImGui::Checkbox()` | 불리언 설정 |
| `ImGui::Button("Apply")` | 재임포트 트리거 |
| `ImGui::CollapsingHeader()` | 설정 그룹 |

### 복잡도

**Large-Very Large**

- GUID/Meta 시스템: 1-2일
- AssetDatabase 코어: 2-3일
- FileWatcher (폴링 방식): 1-2일
- TextureImporter: 1일
- AudioImporter: 1일
- SceneImporter: 0.5일
- ImporterRegistry: 0.5일
- Inspector 임포트 설정 UI: 1일
- ProjectBrowser 통합: 1일
- **총 예상: 8-12일**

### 의존성

- `TextureManager` (텍스처 임포트 적용)
- `Audio` 시스템 (오디오 임포트)
- `ProjectBrowserWindow` (파일 선택/표시)
- `InspectorWindow` (임포트 설정 UI)
- `SceneSerializer` (GUID 기반 에셋 참조로 전환)
- `std::filesystem` (C++17)
- `std::thread` (파일 감시)

---

## 11. 구현 우선순위 로드맵

시스템 간 의존성, 사용자 가치, 구현 난이도를 종합적으로 고려한 권장 구현 순서:

### Phase 1: 핵심 인프라 (2-3주)

| 순서 | 시스템 | 근거 |
|------|--------|------|
| 1 | **Scene View + 기즈모** | 에디터의 가장 기본적인 기능. 현재 placeholder 상태이므로 최우선 |
| 2 | **Undo/Redo** | 모든 편집 동작의 안전망. 다른 시스템 구현 시 함께 통합해야 이중 작업 방지 |
| 3 | **콘솔/로그** | 디버깅 필수. 나머지 시스템 구현 시 디버깅에 활용 |

### Phase 2: 편의성 향상 (2주)

| 순서 | 시스템 | 근거 |
|------|--------|------|
| 4 | **드래그 & 드롭** | ImGui 내장 시스템 활용으로 구현 빠르며, 워크플로우 대폭 개선 |
| 5 | **컴포넌트 복사/붙여넣기** | 기존 직렬화 시스템 활용, 작업량 적음 |
| 6 | **프로파일러** | 성능 최적화 시 필수, 엔진 코드 계측은 조기에 삽입하는 것이 유리 |

### Phase 3: 설정 및 관리 (2-3주)

| 순서 | 시스템 | 근거 |
|------|--------|------|
| 7 | **에디터 환경설정 / 프로젝트 설정** | 빌드 시스템과 연동, 프로젝트 규모 확대 시 필수 |
| 8 | **에셋 임포트 파이프라인** | 에셋 관리 체계화. 현재 단순 파일 로드에서 체계적 관리로 전환 |

### Phase 4: 고급 기능 (3-4주)

| 순서 | 시스템 | 근거 |
|------|--------|------|
| 9 | **멀티 오브젝트 편집** | 생산성 향상이지만 Inspector 리팩터링 필요 |
| 10 | **애니메이션 에디터** | 가장 복잡하며, 기존 Animation 시스템 리팩터링 선행 필요 |

### 총 예상 기간: 9-12주 (1인 기준)

---

## 12. 시스템 간 의존성 그래프

```
                    ┌──────────────┐
                    │  Scene View  │
                    │  + Gizmos    │
                    └──────┬───────┘
                           │ uses
                    ┌──────▼───────┐
              ┌─────│  Undo/Redo   │─────┐
              │     │  System      │     │
              │     └──────────────┘     │
              │                          │
     ┌────────▼───────┐       ┌──────────▼──────────┐
     │   Drag & Drop  │       │ Component Copy/Paste│
     │   System       │       │                     │
     └────────┬───────┘       └─────────────────────┘
              │ uses
     ┌────────▼───────┐       ┌─────────────────────┐
     │  Asset Import  │       │  Multi-Object Edit  │
     │  Pipeline      │       │                     │
     └────────────────┘       └─────────────────────┘

     ┌────────────────┐       ┌─────────────────────┐
     │  Console/Log   │       │  Profiler Window    │
     │  Window        │       │                     │
     └────────────────┘       └─────────────────────┘
           (독립적)               (독립적, 엔진 계측 필요)

     ┌────────────────┐       ┌─────────────────────┐
     │  Preferences / │       │  Animation Editor   │
     │  Project       │       │  (Animation 리팩터    │
     │  Settings      │       │   필요)              │
     └────────────────┘       └─────────────────────┘

의존성 요약:
  Scene View ← Undo (기즈모 조작 시 undo 등록)
  Scene View ← Drag & Drop (에셋을 씬에 드롭)
  Undo ← Component Copy/Paste (붙여넣기 undo)
  Undo ← Multi-Object Edit (일괄 변경 undo)
  Undo ← Drag & Drop (드롭으로 생성된 오브젝트 undo)
  Asset Import ← Drag & Drop (임포트된 에셋 드래그)
  Animation Editor ← Scene View (애니메이션 미리보기)
  Animation Editor ← Undo (키프레임 편집 undo)
  Console, Profiler, Preferences: 대체로 독립적
```

---

## 부록: 현재 Molga Engine 아키텍처 참고

### 현재 에디터 구조

```
Editor (싱글턴)
├── WindowManager (윈도우 등록/관리)
│   ├── HierarchyWindow
│   ├── InspectorWindow
│   ├── SceneViewWindow (placeholder)
│   ├── ProjectBrowserWindow
│   ├── StatsWindow
│   └── ScriptWindow
├── SceneOperations (씬 파일 I/O)
└── BuildManager (빌드 시스템)
```

### 신규 시스템 추가 시 파일 위치 제안

```
src/Editor/
├── Systems/
│   ├── UndoManager.h / .cpp
│   ├── SelectionManager.h / .cpp
│   ├── ComponentClipboard.h / .cpp
│   └── DragDropTypes.h
├── Windows/
│   ├── ConsoleWindow.h / .cpp
│   ├── ProfilerWindow.h / .cpp
│   ├── AnimationWindow.h / .cpp
│   ├── PreferencesWindow.h / .cpp
│   └── ProjectSettingsWindow.h / .cpp
├── Settings/
│   ├── EditorPreferences.h / .cpp
│   └── ProjectSettings.h / .cpp
└── (기존 파일들)

src/Core/
├── Profiler.h / .cpp
├── AssetDatabase.h / .cpp
├── AssetMeta.h / .cpp
└── FileWatcher.h / .cpp

src/Assets/
├── IAssetImporter.h
├── TextureImporter.h / .cpp
├── AudioImporter.h / .cpp
└── ImporterRegistry.h / .cpp
```
