// Task 12.2: 신원 키 포커스, 포인터 캡처, 방향 내비게이션.
//
// 이 파일의 스냅샷도 하나도 손으로 조립하지 않는다 — 진짜 World 위에서
// UILayoutSystem::Build가 게시한 값이다. 장면은 640x640 캔버스 위에 십자로 놓은
// 선택 가능한 버튼 다섯 개다(UIButton + UISelectable). 라우터는 동작 대상을,
// UIFocusSystem은 포커스 대상을 각자의 단계에서 처리한다.

#include "doctest.h"

#include "Assets/FontArtifactStore.h"
#include "Common/Fixed26_6.h"
#include "Core/AssetDatabase.h"
#include "Core/World.h"
#include "ECS/Component.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UISelectable.h"
#include "UI/UINavigationTypes.h"
#include "ECS/GameObject.h"
#include "Platform/TextInputOwner.h"
#include "SmokeTestSupport.h"
#include "Systems/Input.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextShapingService.h"
#include "UI/UIFocusSystem.h"
#include "UI/UIInputEvent.h"
#include "UI/UIInputRouter.h"
#include "UI/UIInteractionState.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UILayoutSystem.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UIRuntimeInvalidation.h"
#include "UI/UITextInputVisualState.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using molga::Fixed26_6;
using molga::FixedPoint;
using molga::FixedSize;
using molga::text::TextDiagnosticCode;
using molga::ui::PlannedUIEvent;
using molga::ui::UIEventAction;
using molga::ui::UIEventActionBit;
using molga::ui::UIEventDispatchAccumulator;
using molga::ui::UIEventDispatchRecord;
using molga::ui::UIEventHandlerResult;
using molga::ui::UIEventMergeStatus;
using molga::ui::UIEventStage;
using molga::ui::UIFocusSystem;
using molga::ui::UIInputEvent;
using molga::ui::UIInputEventKind;
using molga::ui::UIInputRouter;
using molga::ui::UIInteractionStateTable;
using molga::ui::UINavigationDirection;
using molga::ui::UIPlanningState;
using molga::ui::UIRuntimeTargetIdentity;
using molga::ui::UISnapshot;
using molga::ui::UISnapshotPtr;
using molga::ui::UISurfaceKey;

namespace {

constexpr molga::WindowId kSurfaceWindow = 10;
constexpr molga::WindowId kDetachedWindow = 20;
constexpr float kViewportPx = 640.0f;
constexpr const char* kNotoSansFontGuid = "44444444444444444444444444444444";
constexpr const char* kInputFamily = "7e57a11c7e57a11c7e57a11c7e57a11c";

constexpr unsigned int kCanvasId = 1;
constexpr unsigned int kLeftId = 2;
constexpr unsigned int kCenterId = 3;
constexpr unsigned int kRightId = 4;
constexpr unsigned int kUpId = 5;
constexpr unsigned int kDownId = 6;
constexpr unsigned int kExplicitId = 10;

FixedSize RawSize(std::int32_t width, std::int32_t height) {
    return FixedSize{Fixed26_6::FromRaw(width), Fixed26_6::FromRaw(height)};
}

FixedPoint PxPoint(float x, float y) {
    return FixedPoint{Fixed26_6::FromRaw(static_cast<std::int32_t>(x * 64.0f)),
                      Fixed26_6::FromRaw(static_cast<std::int32_t>(y * 64.0f))};
}

// 진단을 코드/심각도별로 센다. 억제도 중복 제거도 하지 않는다.
class CountingDiagnosticSink final : public molga::text::TextDiagnosticSink {
public:
    void Report(molga::text::TextDiagnostic diagnostic) override {
        records_.push_back(std::move(diagnostic));
    }
    std::size_t CountFrom(TextDiagnosticCode code,
                          const std::string& subsystem) const {
        std::size_t total = 0;
        for (const auto& record : records_) {
            if (record.code == code && record.subsystem == subsystem) ++total;
        }
        return total;
    }
    std::size_t CountSeverity(molga::text::TextSeverity severity) const {
        std::size_t total = 0;
        for (const auto& record : records_) {
            if (record.severity == severity) ++total;
        }
        return total;
    }
    std::size_t Total() const noexcept { return records_.size(); }
    void Clear() { records_.clear(); }

private:
    std::vector<molga::text::TextDiagnostic> records_;
};

// test_ui_input.cpp와 같은 이유로 케이스마다 새 애셋 트리와 런타임을 만든다.
// 이 장면은 글자를 그리지 않지만 Build의 서명이 TextLayoutService를 요구한다.
class SlimAssetTree {
public:
    SlimAssetTree()
        : temp_("ui-focus-assets"),
          projectRoot_(temp_.Path()),
          assetsRoot_(projectRoot_ / "Assets") {
        const std::filesystem::path sourceRoot =
            MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT;
        for (const char* relative :
             {"fonts/NotoSans-Regular.ttf",
              "licenses/NotoFonts-ffebf8c1-OFL.txt"}) {
            CopyPair(sourceRoot, relative);
        }
        test_support::WriteText(
            assetsRoot_ / "families" / "input.fontfamily",
            std::string(R"({"schemaVersion":1,"faces":[{"fontGuid":")") +
                kNotoSansFontGuid +
                R"(","faceIndex":0,"weight":400,"stretchPercent":100,)"
                R"("slant":"Upright"}],"fallbackFamilyGuids":[]})");
        test_support::WriteText(
            assetsRoot_ / "families" / "input.fontfamily.meta",
            std::string(R"({"guid":")") + kInputFamily +
                R"(","importer":"FontFamilyImporter","importerVersion":1,)"
                R"("settings":{}})");
    }
    const std::filesystem::path& ProjectRoot() const { return projectRoot_; }
    const std::filesystem::path& AssetsRoot() const { return assetsRoot_; }

private:
    void CopyPair(const std::filesystem::path& sourceRoot,
                  const std::string& relative) {
        for (const std::string& name : {relative, relative + ".meta"}) {
            const auto source = sourceRoot / name;
            const auto destination = assetsRoot_ / name;
            REQUIRE_MESSAGE(std::filesystem::is_regular_file(source),
                            "missing fixture pair member: " << source.string());
            std::filesystem::create_directories(destination.parent_path());
            REQUIRE(std::filesystem::copy_file(
                source, destination, std::filesystem::copy_options::none));
        }
    }

    test_support::TempDirectory temp_;
    std::filesystem::path projectRoot_;
    std::filesystem::path assetsRoot_;
};

class FocusTextRuntime {
private:
    SlimAssetTree tree_;

public:
    molga::AssetDatabase database;

private:
    std::shared_ptr<const molga::FontArtifactStore> store_;
    bool bound_;

public:
    molga::text::FontRepository repository;
    molga::text::FontFamilyResolver resolver;
    molga::text::TextShapingService shaper;
    molga::text::TextLayoutCache cache;
    molga::text::TextLayoutService service;

    FocusTextRuntime()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))),
          bound_(BindAndScan()),
          repository(database),
          resolver(database, repository),
          cache(molga::text::TextLayoutCacheLimits::Production()),
          service(resolver, shaper, cache) {}

private:
    bool BindAndScan() {
        std::string bindError;
        REQUIRE_MESSAGE(database.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database.ScanProject(tree_.AssetsRoot());
        return true;
    }
};

GameObject* AddObject(World& world, unsigned int id, GameObject* parent) {
    auto object = std::make_shared<GameObject>("ui" + std::to_string(id));
    object->SetID(id);
    GameObject* raw = world.Add(object);
    REQUIRE(raw != nullptr);
    if (parent) REQUIRE(raw->SetParent(parent));
    return raw;
}

RectTransform* AddOffsetRect(GameObject& object, float x, float y, float width,
                             float height) {
    auto* rect = object.AddComponent<RectTransform>();
    REQUIRE(rect != nullptr);
    rect->SetAnchorMin({0.0f, 0.0f});
    rect->SetAnchorMax({0.0f, 0.0f});
    rect->SetPivot({0.0f, 0.0f});
    rect->SetAnchoredPosition({x, y});
    rect->SetSizeDelta({width, height});
    return rect;
}

struct Dispatched {
    UIEventHandlerResult input;
    UIEventHandlerResult focus;
    UIEventMergeStatus inputMerge = UIEventMergeStatus::RejectedSpent;
    UIEventMergeStatus focusMerge = UIEventMergeStatus::RejectedSpent;
    UIEventDispatchRecord record;
};

// 십자로 놓은 선택 가능한 버튼 다섯 개: 각 오브젝트가 UIButton(동작 대상)과
// UISelectable(포커스 대상)을 함께 든다.
class UIFocusFixture {
public:
    UIFocusFixture() {
        GameObject* canvas = AddObject(world_, kCanvasId, nullptr);
        AddOffsetRect(*canvas, 0.0f, 0.0f, kViewportPx, kViewportPx);
        auto* canvasComponent = canvas->AddComponent<UICanvas>();
        REQUIRE(canvasComponent != nullptr);
        canvasComponent->SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
        canvasComponent->SetSortingOrder(0);

        AddSelectable(kLeftId, 100.0f, 300.0f);
        AddSelectable(kCenterId, 300.0f, 300.0f);
        AddSelectable(kRightId, 500.0f, 300.0f);
        AddSelectable(kUpId, 300.0f, 100.0f);
        AddSelectable(kDownId, 300.0f, 500.0f);
        state_ = InitialPlanningState();
        Rebuild();
    }

    // ── 장면 ────────────────────────────────────────────────────────────────
    // 50x50 선택 가능 버튼 하나를 캔버스 아래에 둔다.
    GameObject* AddSelectable(unsigned int id, float x, float y,
                              float size = 50.0f) {
        GameObject* canvas = world_.FindById(kCanvasId);
        REQUIRE(canvas != nullptr);
        GameObject* object = AddObject(world_, id, canvas);
        AddOffsetRect(*object, x, y, size, size);
        REQUIRE(object->AddComponent<UIButton>() != nullptr);
        REQUIRE(object->AddComponent<UISelectable>() != nullptr);
        return object;
    }

    ::World& World() { return world_; }
    CountingDiagnosticSink& Diagnostics() { return sink_; }
    UIInputRouter& Router() { return router_; }
    UIFocusSystem& Focus() { return focus_; }
    molga::ui::UILayoutSystem& Layout() { return layout_; }

    UISnapshotPtr Snapshot() {
        REQUIRE(snapshot_ != nullptr);
        return snapshot_;
    }
    UISnapshotPtr Rebuild(molga::WindowId window = kSurfaceWindow) {
        snapshot_ = layout_.Build(
            world_, window,
            RawSize(static_cast<std::int32_t>(kViewportPx * 64.0f),
                    static_cast<std::int32_t>(kViewportPx * 64.0f)),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            runtime_.service, sink_);
        REQUIRE(snapshot_ != nullptr);
        return snapshot_;
    }

    UIPlanningState InitialPlanningState(
        molga::WindowId window = kSurfaceWindow) const {
        UIPlanningState state;
        state.surfaceWindowId = window;
        state.surfaceWorldGeneration = world_.Generation();
        state.nativeWindowFocused = true;
        return state;
    }
    // 투사 상태를 표에서 다시 세운다(Task 12.3이 프레임마다 하게 될 일).
    void SyncStateFromTable() {
        const auto surface = Surface();
        state_.focused = surface.focused;
        state_.pointerCapture = surface.pointerCapture;
        state_.hovered = surface.hovered;
    }
    UIPlanningState& State() { return state_; }

    // ── 식별자 ──────────────────────────────────────────────────────────────
    GameObject& Object(unsigned int id) {
        GameObject* object = world_.FindById(id);
        REQUIRE(object != nullptr);
        return *object;
    }
    UISelectable& SelectableOf(unsigned int id) {
        auto* selectable = Object(id).GetComponent<UISelectable>();
        REQUIRE(selectable != nullptr);
        return *selectable;
    }
    UIButton& ButtonOf(unsigned int id) {
        auto* button = Object(id).GetComponent<UIButton>();
        REQUIRE(button != nullptr);
        return *button;
    }
    UIRuntimeTargetIdentity SelectableIdentity(unsigned int id) {
        return molga::ui::CaptureTarget(world_, SelectableOf(id));
    }
    UIRuntimeTargetIdentity ButtonIdentity(unsigned int id) {
        return molga::ui::CaptureTarget(world_, ButtonOf(id));
    }
    void ReplaceSelectable(unsigned int id) {
        Object(id).RemoveComponent<UISelectable>();
        REQUIRE(Object(id).AddComponent<UISelectable>() != nullptr);
        Rebuild();
    }

    // 명시 내비게이션 장면(id 10). 위는 Center, 오른쪽은 상호작용 불가
    // selectable(11), 아래는 selectable 없는 오브젝트(12, 게시가 거절한다),
    // 왼쪽은 비워 둔다. 자동 내비게이션이었다면 위는 Left가 뽑혔을 자리다.
    void AddExplicit() {
        GameObject* source = AddSelectable(kExplicitId, 100.0f, 500.0f);
        GameObject* canvas = world_.FindById(kCanvasId);
        GameObject* blocked = AddObject(world_, 11, canvas);
        AddOffsetRect(*blocked, 500.0f, 500.0f, 50.0f, 50.0f);
        REQUIRE(blocked->AddComponent<UIImage>() != nullptr);
        auto* blockedSelectable = blocked->AddComponent<UISelectable>();
        REQUIRE(blockedSelectable != nullptr);
        blockedSelectable->SetInteractable(false);
        GameObject* plain = AddObject(world_, 12, canvas);
        AddOffsetRect(*plain, 300.0f, 560.0f, 50.0f, 50.0f);
        REQUIRE(plain->AddComponent<UIImage>() != nullptr);
        auto* selectable = source->GetComponent<UISelectable>();
        selectable->SetNavigationMode(UINavigationMode::Explicit);
        selectable->SetNavigateUp(SceneObjectRef{kCenterId});
        selectable->SetNavigateRight(SceneObjectRef{11});
        selectable->SetNavigateDown(SceneObjectRef{12});
        Rebuild();
    }
    // 네 방향이 모두 서로 다른 유효한 참조인 명시 출발점(id 13, 왼쪽 위 구석).
    // 자동이었다면 어느 방향도 같은 답이 나오지 않는 자리다.
    void AddFourWaySource() {
        GameObject* source = AddSelectable(13, 20.0f, 20.0f);
        auto* selectable = source->GetComponent<UISelectable>();
        selectable->SetNavigationMode(UINavigationMode::Explicit);
        selectable->SetNavigateUp(SceneObjectRef{kUpId});
        selectable->SetNavigateDown(SceneObjectRef{kDownId});
        selectable->SetNavigateLeft(SceneObjectRef{kLeftId});
        selectable->SetNavigateRight(SceneObjectRef{kRightId});
        Rebuild();
    }
    // 오른쪽 후보를 모두 치운다(Right를 비활성).
    void RemoveRight() {
        Object(kRightId).SetActive(false);
        Rebuild();
    }

    const molga::ui::UIHitTargetSnapshot& RecordOfSelectable(
        const UISnapshot& n, unsigned int id) {
        const auto identity = SelectableIdentity(id);
        for (const auto& hit : n.hitTargets) {
            if (hit.focusTarget && hit.focusTarget->runtimeTarget == identity) {
                return hit;
            }
        }
        FAIL("no focusable hit record for object " << id);
        return n.hitTargets.front();
    }

    // ── 표 ──────────────────────────────────────────────────────────────────
    UISurfaceKey Key(molga::WindowId window = kSurfaceWindow) const {
        return UISurfaceKey{window, world_.Generation()};
    }
    molga::ui::UISurfaceInteractionState Surface(
        molga::WindowId window = kSurfaceWindow) const {
        return UIInteractionStateTable::Get().SurfaceOrEmpty(Key(window));
    }
    std::optional<UIRuntimeTargetIdentity> Focused(
        molga::WindowId window = kSurfaceWindow) const {
        return focus_.Focused(window, world_.Generation());
    }

    // ── 이벤트 조립 ─────────────────────────────────────────────────────────
    FixedPoint CenterOf(unsigned int id) {
        const auto* rect = Object(id).GetComponent<RectTransform>();
        REQUIRE(rect != nullptr);
        const auto screen = rect->GetScreenRect({kViewportPx, kViewportPx});
        return PxPoint(screen.x + screen.width * 0.5f,
                       screen.y + screen.height * 0.5f);
    }
    UIInputEvent Pointer(UIInputEventKind kind, FixedPoint point, bool active,
                         molga::WindowId window = kSurfaceWindow) {
        UIInputEvent event;
        event.sequence = ++sequence_;
        event.traceOrdinal = sequence_;
        event.windowId = window;
        event.kind = kind;
        event.logicalPoint = point;
        event.logicalPointValid = true;
        event.active = active;
        return event;
    }
    UIInputEvent PointerDown(FixedPoint p,
                             molga::WindowId w = kSurfaceWindow) {
        return Pointer(UIInputEventKind::PointerButton, p, true, w);
    }
    UIInputEvent PointerUp(FixedPoint p, molga::WindowId w = kSurfaceWindow) {
        return Pointer(UIInputEventKind::PointerButton, p, false, w);
    }
    UIInputEvent PointerDeparture(molga::WindowId window = kSurfaceWindow) {
        UIInputEvent event;
        event.sequence = ++sequence_;
        event.traceOrdinal = sequence_;
        event.windowId = window;
        event.kind = UIInputEventKind::PointerMotion;
        event.logicalPointValid = false;
        return event;
    }
    UIInputEvent KeyEvent(Input::KeyCode key, bool active = true,
                          molga::WindowId window = kSurfaceWindow) {
        UIInputEvent event;
        event.sequence = ++sequence_;
        event.traceOrdinal = sequence_;
        event.windowId = window;
        event.kind = UIInputEventKind::Key;
        event.control = static_cast<std::uint32_t>(key);
        event.active = active;
        return event;
    }
    UIInputEvent KeyRight() { return KeyEvent(Input::KeyCode::Right); }
    UIInputEvent PadButton(Input::GamepadButton button, bool active = true) {
        UIInputEvent event;
        event.sequence = ++sequence_;
        event.traceOrdinal = sequence_;
        event.windowId = kSurfaceWindow;
        event.kind = UIInputEventKind::GamepadButton;
        event.control = static_cast<std::uint32_t>(button);
        event.active = active;
        return event;
    }
    UIInputEvent DpadRight() { return PadButton(Input::GamepadButton::DpadRight); }
    UIInputEvent PadAxis(Input::GamepadAxis axis, std::int32_t valueRaw) {
        UIInputEvent event;
        event.sequence = ++sequence_;
        event.traceOrdinal = sequence_;
        event.windowId = kSurfaceWindow;
        event.kind = UIInputEventKind::GamepadAxis;
        event.control = static_cast<std::uint32_t>(axis);
        event.axisValue = Fixed26_6::FromRaw(valueRaw);
        // 활성이어도 축은 방향이 아니다 — active 검사가 대신 막지 못하게 한다.
        event.active = true;
        return event;
    }
    UIInputEvent TextCommit(const std::string& utf8,
                            const molga::platform::TextInputOwnerStamp& stamp) {
        UIInputEvent event;
        event.sequence = ++sequence_;
        event.traceOrdinal = sequence_;
        event.windowId = kSurfaceWindow;
        event.kind = UIInputEventKind::TextCommit;
        molga::ui::UITextInputEventPayload payload;
        payload.utf8 = utf8;
        payload.ownerAtIngest = stamp;
        event.text = std::move(payload);
        return event;
    }
    UIInputEvent WindowFocus(bool active,
                             molga::WindowId window = kSurfaceWindow) {
        UIInputEvent event;
        event.sequence = ++sequence_;
        event.traceOrdinal = sequence_;
        event.windowId = window;
        event.kind = UIInputEventKind::WindowFocus;
        event.active = active;
        return event;
    }

    // ── 계획과 발송 ─────────────────────────────────────────────────────────
    PlannedUIEvent PlanOn(const UISnapshot& n, UIPlanningState& state,
                          const UIInputEvent& event) {
        PlannedUIEvent plan =
            router_.PlanNext(n, state.surfaceWindowId, event, state);
        focus_.ProjectEvent(n, plan, state);
        return plan;
    }
    PlannedUIEvent Plan(const UIInputEvent& event) {
        return PlanOn(*snapshot_, state_, event);
    }

    // 라우터 단계와 포커스 단계를 한 누산기에 합친다.
    Dispatched DispatchOn(const UISnapshot& n, const PlannedUIEvent& plan) {
        Dispatched out;
        UIEventDispatchAccumulator accumulator(plan, sink_);
        out.input = router_.HandleEvent(world_, n, plan, sink_);
        out.inputMerge = accumulator.Merge(out.input);
        out.focus = focus_.HandleEvent(world_, n, plan, sink_);
        out.focusMerge = accumulator.Merge(out.focus);
        out.record = std::move(accumulator).Finish();
        return out;
    }
    Dispatched Dispatch(const PlannedUIEvent& plan) {
        return DispatchOn(*snapshot_, plan);
    }

    // 계획 + 발송.
    Dispatched Send(const UIInputEvent& event) { return Dispatch(Plan(event)); }

    // 오브젝트 중심을 눌렀다 놓는다 — 실제 포인터 경로로 포커스를 세운다.
    void FocusByPointer(unsigned int id) {
        const FixedPoint center = CenterOf(id);
        const auto down = Send(PointerDown(center));
        REQUIRE(down.inputMerge == UIEventMergeStatus::Merged);
        REQUIRE(down.focusMerge == UIEventMergeStatus::Merged);
        const auto up = Send(PointerUp(center));
        REQUIRE(up.inputMerge == UIEventMergeStatus::Merged);
        REQUIRE(up.focusMerge == UIEventMergeStatus::Merged);
        REQUIRE(Focused() == SelectableIdentity(id));
    }

private:
    FocusTextRuntime runtime_;
    ::World world_;
    molga::ui::UILayoutSystem layout_;
    CountingDiagnosticSink sink_;
    UIInputRouter router_;
    UIFocusSystem focus_;
    UISnapshotPtr snapshot_;
    UIPlanningState state_;
    std::uint64_t sequence_ = 0;
};

std::uint64_t SemanticGeneration() {
    return molga::ui::UIRuntimeInvalidationClock::Current()
        .semanticDirtyGeneration;
}

}  // namespace

// ── Step C ──────────────────────────────────────────────────────────────────
TEST_CASE("component replacement cannot inherit focus") {
    UIFocusFixture f;
    f.FocusByPointer(kLeftId);
    const auto old = f.Focused();
    REQUIRE(old.has_value());

    f.ReplaceSelectable(kLeftId);
    const auto fresh = f.SelectableIdentity(kLeftId);
    CHECK(fresh != *old);
    // 교체는 표를 건드리지 않는다. 낡은 신원이 아직 거기 있고, 정리는
    // Revalidate의 일이다.
    CHECK(f.Focused() == old);
    f.Focus().Revalidate(f.World(), *f.Snapshot(), f.Diagnostics());
    CHECK_FALSE(f.Focused().has_value());

    // 신원 상실 자체가 막는다: 낡은 포커스를 든 투사 상태에서도 키 계획은
    // N에서 그 포커스의 기록을 찾지 못해 대상이 없다.
    UIPlanningState stale = f.InitialPlanningState();
    stale.focused = old;
    const auto plan = f.PlanOn(*f.Snapshot(), stale, f.KeyRight());
    CHECK_FALSE(plan.targetFromSnapshotN.has_value());
    CHECK_FALSE(plan.TargetFor(UIEventStage::Focus).has_value());
}

TEST_CASE("pointer departure differs from native focus loss") {
    UIFocusFixture f;
    f.FocusByPointer(kLeftId);
    const auto left = f.SelectableIdentity(kLeftId);

    SUBCASE("a departure while pressed drops capture and pressed but keeps focus") {
        const auto press = f.Send(f.PointerDown(f.CenterOf(kLeftId)));
        REQUIRE(press.focusMerge == UIEventMergeStatus::Merged);
        REQUIRE(f.ButtonOf(kLeftId).IsPressed());
        REQUIRE(f.Focus().CapturedPointer(kSurfaceWindow, f.World().Generation()) ==
                f.ButtonIdentity(kLeftId));

        const auto departure = f.Send(f.PointerDeparture());
        CHECK(departure.inputMerge == UIEventMergeStatus::Merged);
        CHECK(departure.focusMerge == UIEventMergeStatus::Merged);
        CHECK_FALSE(f.ButtonOf(kLeftId).IsPressed());
        CHECK_FALSE(f.Focus()
                        .CapturedPointer(kSurfaceWindow, f.World().Generation())
                        .has_value());
        CHECK(f.Focused() == left);
    }
    SUBCASE("ClearPointerState keeps keyboard focus") {
        REQUIRE(f.Send(f.PointerDown(f.CenterOf(kLeftId))).focusMerge ==
                UIEventMergeStatus::Merged);
        REQUIRE(f.ButtonOf(kLeftId).IsPressed());
        f.Focus().ClearPointerState(kSurfaceWindow, f.World().Generation(),
                                    f.Diagnostics());
        CHECK_FALSE(f.ButtonOf(kLeftId).IsPressed());
        CHECK_FALSE(f.Surface().pointerCapture.has_value());
        CHECK(f.Focused() == left);
    }
    SUBCASE("native window focus loss drops focus as well") {
        REQUIRE(f.Send(f.PointerDown(f.CenterOf(kLeftId))).focusMerge ==
                UIEventMergeStatus::Merged);
        f.Focus().OnNativeWindowFocusLost(kSurfaceWindow, f.Diagnostics());
        CHECK_FALSE(f.Focused().has_value());
        CHECK_FALSE(f.ButtonOf(kLeftId).IsPressed());
        CHECK_FALSE(f.Surface().pointerCapture.has_value());
        CHECK(UIInteractionStateTable::Get().SurfaceCountForWorld(
                  f.World().Generation()) == 0);
    }
    SUBCASE("native window focus loss clears every generation of that window only") {
        // 같은 창의 다른 세대(옛 월드)와 다른 창의 표면을 직접 심는다.
        auto& table = UIInteractionStateTable::Get();
        const UISurfaceKey otherGeneration{kSurfaceWindow,
                                           f.World().Generation() + 1000};
        const UISurfaceKey otherWindow{kDetachedWindow, f.World().Generation()};
        molga::ui::UISurfaceInteractionState seed;
        seed.focused = left;
        REQUIRE(table.Apply(otherGeneration, seed, nullptr, "test", 0));
        REQUIRE(table.Apply(otherWindow, seed, nullptr, "test", 0));
        f.Focus().OnNativeWindowFocusLost(kSurfaceWindow, f.Diagnostics());
        CHECK(table.Surface(otherGeneration) == nullptr);
        // 클릭 비트는 보이는 상태가 아니라 남을 수 있다 — 포커스·캡처만 본다.
        CHECK_FALSE(f.Focused().has_value());
        CHECK_FALSE(f.Surface().pointerCapture.has_value());
        CHECK(table.Surface(otherWindow) != nullptr);
        CHECK(table.Surface(otherWindow)->focused == left);
        table.OnWorldReleased(otherGeneration.worldGeneration);
        table.OnWorldReleased(f.World().Generation());
    }
}

TEST_CASE("focus and capture lifecycle") {
    UIFocusFixture f;
    const auto leftSel = f.SelectableIdentity(kLeftId);
    const auto leftBtn = f.ButtonIdentity(kLeftId);

    SUBCASE("a pointer press sets focus and capture; the release keeps focus") {
        const auto down = f.Send(f.PointerDown(f.CenterOf(kLeftId)));
        CHECK(down.inputMerge == UIEventMergeStatus::Merged);
        CHECK(down.focusMerge == UIEventMergeStatus::Merged);
        CHECK(down.focus.resultingFocus == leftSel);
        CHECK((down.focus.actionMask & UIEventActionBit(UIEventAction::Focus)) != 0);
        CHECK(down.focus.stage == UIEventStage::Focus);
        REQUIRE(down.focus.resolvedStageTarget.has_value());
        CHECK(down.focus.resolvedStageTarget->runtimeTarget == leftSel);
        // 감사 기록의 주 대상은 동작 대상(버튼)이고 포커스는 자기 칸에 산다.
        CHECK(down.record.runtimeTarget == leftBtn);
        CHECK(down.record.resultingFocus == leftSel);
        CHECK(f.Focused() == leftSel);
        CHECK(f.Focus().CapturedPointer(kSurfaceWindow, f.World().Generation()) ==
              leftBtn);

        const auto up = f.Send(f.PointerUp(f.CenterOf(kLeftId)));
        CHECK(up.focusMerge == UIEventMergeStatus::Merged);
        CHECK(up.focus.actionMask == 0);
        CHECK(f.Focused() == leftSel);
        CHECK_FALSE(f.Focus()
                        .CapturedPointer(kSurfaceWindow, f.World().Generation())
                        .has_value());
    }
    SUBCASE("a press on a different widget moves focus; a press over nothing keeps it") {
        f.FocusByPointer(kLeftId);
        const auto empty = f.Send(f.PointerDown(PxPoint(600.0f, 600.0f)));
        CHECK(empty.focusMerge == UIEventMergeStatus::Merged);
        CHECK(f.Focused() == leftSel);
        f.FocusByPointer(kRightId);
        CHECK(f.Focused() == f.SelectableIdentity(kRightId));
    }
    SUBCASE("a non-interactable selectable is cleared by Revalidate") {
        f.FocusByPointer(kLeftId);
        f.SelectableOf(kLeftId).SetInteractable(false);
        f.Focus().Revalidate(f.World(), *f.Snapshot(), f.Diagnostics());
        CHECK_FALSE(f.Focused().has_value());
    }
    SUBCASE("deactivating the object clears focus, capture and its button entry") {
        REQUIRE(f.Send(f.PointerDown(f.CenterOf(kLeftId))).focusMerge ==
                UIEventMergeStatus::Merged);
        REQUIRE(f.Focused() == leftSel);
        REQUIRE(f.Surface().pointerCapture == leftBtn);
        // 실행 중이 아닌 월드는 OnDisable을 부르지 않는다 — 정리는 Revalidate의 몫.
        f.Object(kLeftId).SetActive(false);
        f.Focus().Revalidate(f.World(), *f.Snapshot(), f.Diagnostics());
        const auto surface = f.Surface();
        CHECK_FALSE(surface.focused.has_value());
        CHECK_FALSE(surface.pointerCapture.has_value());
        CHECK_FALSE(surface.hovered.has_value());
        CHECK(surface.buttons.count(leftBtn) == 0);
    }
    SUBCASE("a disabled button's capture is cleared while keyboard focus on its selectable stays") {
        REQUIRE(f.Send(f.PointerDown(f.CenterOf(kLeftId))).focusMerge ==
                UIEventMergeStatus::Merged);
        // SetEnabled(false)는 OnDisable로 스스로 치운다. 훅을 타지 않는 직렬화
        // 경로로 꺼서 정리가 Revalidate의 몫이 되게 한다.
        f.ButtonOf(kLeftId).SetEnabledFromSerializedState(false);
        f.Focus().Revalidate(f.World(), *f.Snapshot(), f.Diagnostics());
        CHECK_FALSE(f.Surface().pointerCapture.has_value());
        CHECK(f.Focused() == leftSel);
    }
    SUBCASE("releasing the world drops focus and every surface") {
        f.FocusByPointer(kLeftId);
        const auto generation = f.World().Generation();
        f.Focus().OnWorldReleased(generation);
        CHECK_FALSE(f.Focus().Focused(kSurfaceWindow, generation).has_value());
        CHECK(UIInteractionStateTable::Get().SurfaceCountForWorld(generation) ==
              0);
    }
    SUBCASE("WindowFocus(false) through HandleEvent clears focus, capture and pressed") {
        REQUIRE(f.Send(f.PointerDown(f.CenterOf(kLeftId))).focusMerge ==
                UIEventMergeStatus::Merged);
        REQUIRE(f.ButtonOf(kLeftId).IsPressed());
        REQUIRE(f.Focused() == leftSel);

        const auto lost = f.Send(f.WindowFocus(false));
        CHECK(lost.inputMerge == UIEventMergeStatus::Merged);
        CHECK(lost.focusMerge == UIEventMergeStatus::Merged);
        // 대상 없는 결과는 dirty 플래그만 가진다.
        CHECK(lost.focus.visualDirty);
        CHECK(lost.focus.actionMask == 0);
        CHECK_FALSE(lost.focus.consumed);
        CHECK_FALSE(lost.focus.resolvedStageTarget.has_value());
        CHECK(lost.record.visualDirty);
        CHECK_FALSE(f.ButtonOf(kLeftId).IsPressed());
        CHECK_FALSE(f.Surface().pointerCapture.has_value());
        CHECK_FALSE(f.Focused().has_value());
    }
    SUBCASE("a foreign-window WindowFocus(false) changes nothing") {
        f.FocusByPointer(kLeftId);
        const auto before = SemanticGeneration();
        const auto lost = f.Send(f.WindowFocus(false, kDetachedWindow));
        CHECK_FALSE(lost.input.visualDirty);
        CHECK_FALSE(lost.focus.visualDirty);
        CHECK(lost.focus.actionMask == 0);
        CHECK(lost.focus.sequence == lost.record.sequence);
        CHECK(lost.focusMerge == UIEventMergeStatus::Merged);
        CHECK(f.Focused() == leftSel);
        CHECK(SemanticGeneration() == before);
    }
    SUBCASE("a detached window keeps its own focus") {
        f.FocusByPointer(kLeftId);
        const auto n20 = f.Rebuild(kDetachedWindow);
        UIPlanningState state20 = f.InitialPlanningState(kDetachedWindow);
        const FixedPoint center = f.CenterOf(kRightId);
        const auto down =
            f.DispatchOn(*n20, f.PlanOn(*n20, state20,
                                        f.PointerDown(center, kDetachedWindow)));
        CHECK(down.focusMerge == UIEventMergeStatus::Merged);
        CHECK(f.Focused(kDetachedWindow) == f.SelectableIdentity(kRightId));
        CHECK(f.Focused(kSurfaceWindow) == leftSel);
        // 한 창의 창 포커스 상실은 다른 창의 표면을 건드리지 않는다.
        f.Focus().OnNativeWindowFocusLost(kDetachedWindow, f.Diagnostics());
        CHECK_FALSE(f.Focused(kDetachedWindow).has_value());
        CHECK(f.Focused(kSurfaceWindow) == leftSel);
    }
}

TEST_CASE("a focus change acquires one semantic generation, a repeat none, exhaustion keeps the prior focus") {
    UIFocusFixture f;
    const FixedPoint left = f.CenterOf(kLeftId);

    // 한 누름은 라우터에서 한 번, 포커스 핸들러에서 한 번 세대를 올린다 —
    // 각자 자기 표 변경이 있으므로. 핸들러 호출마다 따로 잰다.
    const auto press = f.Plan(f.PointerDown(left));
    const auto before = SemanticGeneration();
    UIEventDispatchAccumulator accumulator(press, f.Diagnostics());
    const auto inputResult =
        f.Router().HandleEvent(f.World(), *f.Snapshot(), press, f.Diagnostics());
    const auto afterRouter = SemanticGeneration();
    const auto focusResult =
        f.Focus().HandleEvent(f.World(), *f.Snapshot(), press, f.Diagnostics());
    const auto afterFocus = SemanticGeneration();
    CHECK(accumulator.Merge(inputResult) == UIEventMergeStatus::Merged);
    CHECK(accumulator.Merge(focusResult) == UIEventMergeStatus::Merged);
    (void)std::move(accumulator).Finish();
    CHECK(afterRouter == before + 1);
    CHECK(afterFocus == afterRouter + 1);
    CHECK(f.Focused() == f.SelectableIdentity(kLeftId));

    // 같은 계획을 다시: 두 핸들러 모두 달라질 것이 없다.
    const auto repeatBefore = SemanticGeneration();
    const auto again = f.Dispatch(press);
    CHECK(SemanticGeneration() == repeatBefore);
    CHECK_FALSE(again.focus.visualDirty);
    CHECK(again.focus.actionMask == 0);

    // 소진: 오른쪽 버튼을 눌러도 이전 포커스가 그대로이고 Blocker는 한 번이다.
    f.Diagnostics().Clear();
    const auto toRight = f.Plan(f.PointerDown(f.CenterOf(kRightId)));
    {
        molga::ui::ScopedUIRuntimeGenerationForTesting guard(
            molga::ui::UIRuntimeGenerationKind::SemanticDirty, UINT64_MAX);
        const auto exhausted = f.Dispatch(toRight);
        CHECK(exhausted.inputMerge == UIEventMergeStatus::Merged);
        CHECK(exhausted.focusMerge == UIEventMergeStatus::Merged);
        CHECK(exhausted.focus.actionMask == 0);
        CHECK_FALSE(exhausted.focus.visualDirty);
        CHECK_FALSE(exhausted.focus.resultingFocus.has_value());
    }
    CHECK(f.Focused() == f.SelectableIdentity(kLeftId));
    CHECK(f.Diagnostics().CountSeverity(molga::text::TextSeverity::Blocker) == 1);
}

TEST_CASE("UINavigationDirectionOf maps the declared control domain") {
    UIFocusFixture f;
    using molga::ui::UINavigationDirectionOf;
    const auto right = UINavigationDirectionOf(f.KeyRight());
    REQUIRE(right.has_value());
    CHECK(*right == UINavigationDirection::Right);
    CHECK(UINavigationDirectionOf(f.KeyEvent(Input::KeyCode::Left)) ==
          UINavigationDirection::Left);
    CHECK(UINavigationDirectionOf(f.KeyEvent(Input::KeyCode::Up)) ==
          UINavigationDirection::Up);
    CHECK(UINavigationDirectionOf(f.KeyEvent(Input::KeyCode::Down)) ==
          UINavigationDirection::Down);
    // 방향이 아닌 키, 놓음, 다른 종류는 방향이 아니다.
    CHECK_FALSE(UINavigationDirectionOf(f.KeyEvent(Input::KeyCode::A)).has_value());
    CHECK_FALSE(UINavigationDirectionOf(
                    f.KeyEvent(Input::KeyCode::Right, false))
                    .has_value());
    CHECK(UINavigationDirectionOf(f.PadButton(Input::GamepadButton::DpadLeft)) ==
          UINavigationDirection::Left);
    CHECK(UINavigationDirectionOf(f.DpadRight()) == UINavigationDirection::Right);
    CHECK_FALSE(UINavigationDirectionOf(
                    f.PadButton(Input::GamepadButton::DpadUp, false))
                    .has_value());
    CHECK_FALSE(UINavigationDirectionOf(f.PadButton(Input::GamepadButton::South))
                    .has_value());
    // 스틱은 포커스를 옮기지 않는다(사용자 결정 2026-10-18).
    CHECK_FALSE(UINavigationDirectionOf(f.PadAxis(Input::GamepadAxis::LeftX, 64))
                    .has_value());
    CHECK_FALSE(UINavigationDirectionOf(f.PadAxis(Input::GamepadAxis::RightY, -64))
                    .has_value());
    CHECK_FALSE(UINavigationDirectionOf(f.PointerDown(f.CenterOf(kLeftId)))
                    .has_value());
    // 같은 숫자라도 종류가 다르면 방향이 아니다: 키 코드 Right(79)를 게임패드
    // 버튼으로 읽으면 안 된다.
    UIInputEvent crossed = f.KeyRight();
    crossed.kind = UIInputEventKind::GamepadButton;
    CHECK_FALSE(UINavigationDirectionOf(crossed).has_value());
}

TEST_CASE("ProjectEvent writes nothing into an ineligible plan") {
    UIFocusFixture f;
    const auto n = f.Snapshot();
    // 모든 단계 필드가 채워진 부적격 계획을 손으로 만든다.
    PlannedUIEvent plan;
    plan.event = f.KeyRight();
    plan.surfaceEligible = false;
    for (const auto& hit : n->hitTargets) {
        if (!hit.focusTarget) continue;
        plan.focusTargetFromSnapshotN = hit.focusTarget;
        plan.targetFromSnapshotN = hit.target;
        plan.canonicalTargetFromSnapshotN = hit.canonicalTarget;
        break;
    }
    REQUIRE(plan.focusTargetFromSnapshotN.has_value());
    UIPlanningState projected = f.InitialPlanningState();
    projected.focused = plan.focusTargetFromSnapshotN->runtimeTarget;
    const auto focusedBefore = projected.focused;

    f.Focus().ProjectEvent(*n, plan, projected);
    CHECK_FALSE(plan.focusDestinationFromSnapshotN.has_value());
    CHECK(projected.focused == focusedBefore);
    CHECK_FALSE(plan.TargetFor(UIEventStage::Focus).has_value());

    // 핸들러도 부적격 계획에는 빈 결과다.
    const auto result =
        f.Focus().HandleEvent(f.World(), *n, plan, f.Diagnostics());
    CHECK(result.actionMask == 0);
    CHECK_FALSE(result.consumed);
    CHECK_FALSE(result.visualDirty);
    CHECK_FALSE(result.resolvedStageTarget.has_value());
    CHECK(f.Diagnostics().Total() == 0);
}

// ── Step D ──────────────────────────────────────────────────────────────────
TEST_CASE("UINavigationCandidateLess ranks primary, secondary, sibling path, object id, instance id") {
    using molga::ui::UINavigationCandidateKey;
    using molga::ui::UINavigationCandidateLess;
    const auto key = [](std::int64_t primary, std::int64_t secondary,
                        std::vector<std::uint32_t> path, unsigned int objectId,
                        std::uint64_t instance) {
        UINavigationCandidateKey k;
        k.primary = primary;
        k.secondary = secondary;
        k.siblingPath = std::move(path);
        k.objectId = objectId;
        k.componentInstanceId = instance;
        return k;
    };
    const auto base = key(200, 100, {0, 2}, 5, 50);

    // 각 단계가 **자기 뒤의 모든 단계를 이긴다**: 앞 단계가 더 나쁘면 뒤 단계가
    // 아무리 좋아도 진다.
    CHECK(UINavigationCandidateLess(key(100, 900, {9, 9}, 99, 99), base));
    CHECK_FALSE(UINavigationCandidateLess(base, key(100, 900, {9, 9}, 99, 99)));
    CHECK(UINavigationCandidateLess(key(200, 50, {9, 9}, 99, 99), base));
    CHECK_FALSE(UINavigationCandidateLess(base, key(200, 50, {9, 9}, 99, 99)));
    CHECK(UINavigationCandidateLess(key(200, 100, {0, 1}, 99, 99), base));
    CHECK_FALSE(UINavigationCandidateLess(base, key(200, 100, {0, 1}, 99, 99)));
    // 사전식: 짧은 접두사가 더 앞선다.
    CHECK(UINavigationCandidateLess(key(200, 100, {0}, 99, 99), base));

    // 형제 경로가 같으면 오브젝트 id, 그것도 같으면 인스턴스 id. 장면으로는
    // 닿지 않는 단계라(캔버스 뿌리마다 경로가 다르고 오브젝트당 selectable은
    // 하나) 손으로 만든 키로 본다.
    const auto sameObject = key(200, 100, {0, 2}, 4, 99);
    CHECK(UINavigationCandidateLess(sameObject, base));
    CHECK_FALSE(UINavigationCandidateLess(base, sameObject));
    const auto sameInstance = key(200, 100, {0, 2}, 5, 49);
    CHECK(UINavigationCandidateLess(sameInstance, base));
    CHECK_FALSE(UINavigationCandidateLess(base, sameInstance));

    // 완전히 같은 키는 서로보다 작지 않다(엄격 약순서).
    CHECK_FALSE(UINavigationCandidateLess(base, base));
}

TEST_CASE("explicit navigation follows the frozen ref table") {
    UIFocusFixture f;
    f.AddExplicit();
    // 게시 시각 진단: 아래 참조(selectable 없음)는 레이아웃이 거절한다.
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                    "ui.layout") >= 1);
    f.Diagnostics().Clear();
    f.FocusByPointer(kExplicitId);
    const auto center = f.SelectableIdentity(kCenterId);
    const auto source = f.SelectableIdentity(kExplicitId);
    const auto n = f.Snapshot();
    const auto& record = f.RecordOfSelectable(*n, kExplicitId);

    SUBCASE("up reaches the frozen target field for field, not the nearest neighbour") {
        const auto plan = f.Plan(f.KeyEvent(Input::KeyCode::Up));
        REQUIRE(plan.focusDestinationFromSnapshotN.has_value());
        REQUIRE(record.navigation.explicitTargets[0].has_value());
        REQUIRE(record.navigation.canonicalTargets[0].has_value());
        CHECK(plan.focusDestinationFromSnapshotN->runtimeTarget ==
              *record.navigation.explicitTargets[0]);
        CHECK(plan.focusDestinationFromSnapshotN->canonicalTarget ==
              *record.navigation.canonicalTargets[0]);
        CHECK(plan.focusDestinationFromSnapshotN->runtimeTarget == center);
        // 단계 대상은 목적지다. 동작 대상은 출발 경로의 버튼으로 남는다.
        CHECK(plan.TargetFor(UIEventStage::Focus)->runtimeTarget == center);
        CHECK(plan.targetFromSnapshotN == f.ButtonIdentity(kExplicitId));
        CHECK(f.State().focused == center);

        const auto sent = f.Dispatch(plan);
        CHECK(sent.focusMerge == UIEventMergeStatus::Merged);
        CHECK((sent.focus.actionMask & UIEventActionBit(UIEventAction::Navigate)) != 0);
        CHECK((sent.focus.actionMask & UIEventActionBit(UIEventAction::Focus)) != 0);
        CHECK(sent.focus.consumed);
        CHECK(sent.focus.resultingFocus == center);
        CHECK(sent.record.resultingFocus == center);
        CHECK(f.Focused() == center);
        // 감사 기록의 주 대상은 여전히 출발 버튼이다.
        CHECK(sent.record.runtimeTarget == f.ButtonIdentity(kExplicitId));
    }
    SUBCASE("a reference frozen in N but not focusable there reports once and stays") {
        REQUIRE(record.navigation.explicitTargets[3].has_value());
        const auto plan = f.Plan(f.KeyRight());
        CHECK_FALSE(plan.focusDestinationFromSnapshotN.has_value());
        const auto sent = f.Dispatch(plan);
        CHECK(sent.focusMerge == UIEventMergeStatus::Merged);
        CHECK((sent.focus.actionMask & UIEventActionBit(UIEventAction::Navigate)) == 0);
        CHECK_FALSE(sent.focus.resultingFocus.has_value());
        CHECK(f.Focused() == source);
        CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                        "ui.focus") == 1);
    }
    SUBCASE("a reference the layout rejected is silent here") {
        CHECK_FALSE(record.navigation.explicitTargets[1].has_value());
        const auto plan = f.Plan(f.KeyEvent(Input::KeyCode::Down));
        CHECK_FALSE(plan.focusDestinationFromSnapshotN.has_value());
        const auto sent = f.Dispatch(plan);
        CHECK(sent.focus.actionMask == 0);
        CHECK(f.Focused() == source);
        CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                        "ui.focus") == 0);
    }
    SUBCASE("an unset direction does not fall back to automatic navigation") {
        const auto plan = f.Plan(f.KeyEvent(Input::KeyCode::Left));
        CHECK_FALSE(plan.focusDestinationFromSnapshotN.has_value());
        const auto sent = f.Dispatch(plan);
        CHECK(sent.focus.actionMask == 0);
        CHECK(f.Focused() == source);
        CHECK(f.Diagnostics().Total() == 0);
    }
}

TEST_CASE("each explicit direction resolves its own distinct frozen reference") {
    UIFocusFixture f;
    f.AddFourWaySource();
    const auto n = f.Snapshot();
    const auto& record = f.RecordOfSelectable(*n, 13);
    const std::array<unsigned int, 4> expected = {kUpId, kDownId, kLeftId, kRightId};
    const std::array<Input::KeyCode, 4> keys = {
        Input::KeyCode::Up, Input::KeyCode::Down, Input::KeyCode::Left,
        Input::KeyCode::Right};
    for (std::size_t axis = 0; axis < 4; ++axis) {
        CAPTURE(axis);
        REQUIRE(record.navigation.explicitTargets[axis].has_value());
        REQUIRE(record.navigation.canonicalTargets[axis].has_value());
        UIPlanningState state = f.InitialPlanningState();
        state.focused = f.SelectableIdentity(13);
        const auto plan = f.PlanOn(*n, state, f.KeyEvent(keys[axis]));
        REQUIRE(plan.focusDestinationFromSnapshotN.has_value());
        CHECK(plan.focusDestinationFromSnapshotN->runtimeTarget ==
              *record.navigation.explicitTargets[axis]);
        CHECK(plan.focusDestinationFromSnapshotN->canonicalTarget ==
              *record.navigation.canonicalTargets[axis]);
        CHECK(plan.focusDestinationFromSnapshotN->runtimeTarget ==
              f.SelectableIdentity(expected[axis]));
        CHECK(state.focused == f.SelectableIdentity(expected[axis]));
    }
}

TEST_CASE("automatic navigation ranks half-plane, primary, secondary, sibling path") {
    UIFocusFixture f;

    const auto destinationOf = [&](unsigned int fromId, Input::KeyCode key) {
        UIPlanningState state = f.InitialPlanningState();
        state.focused = f.SelectableIdentity(fromId);
        const auto plan = f.PlanOn(*f.Snapshot(), state, f.KeyEvent(key));
        return plan.focusDestinationFromSnapshotN
                   ? std::optional<UIRuntimeTargetIdentity>(
                         plan.focusDestinationFromSnapshotN->runtimeTarget)
                   : std::nullopt;
    };

    SUBCASE("each direction picks the neighbour in its own half-plane") {
        CHECK(destinationOf(kCenterId, Input::KeyCode::Left) ==
              f.SelectableIdentity(kLeftId));
        CHECK(destinationOf(kCenterId, Input::KeyCode::Right) ==
              f.SelectableIdentity(kRightId));
        CHECK(destinationOf(kCenterId, Input::KeyCode::Up) ==
              f.SelectableIdentity(kUpId));
        CHECK(destinationOf(kCenterId, Input::KeyCode::Down) ==
              f.SelectableIdentity(kDownId));
        // 게임패드 D-pad도 같은 답이다.
        UIPlanningState state = f.InitialPlanningState();
        state.focused = f.SelectableIdentity(kCenterId);
        const auto plan = f.PlanOn(*f.Snapshot(), state, f.DpadRight());
        REQUIRE(plan.focusDestinationFromSnapshotN.has_value());
        CHECK(plan.focusDestinationFromSnapshotN->runtimeTarget ==
              f.SelectableIdentity(kRightId));
    }
    SUBCASE("the primary-axis distance wins over a perfectly aligned far candidate") {
        // Right(500)는 정렬(보조축 0)이지만 id 7(400)이 주축으로 더 가깝다.
        f.AddSelectable(7, 400.0f, 250.0f);
        f.Rebuild();
        CHECK(destinationOf(kCenterId, Input::KeyCode::Right) ==
              f.SelectableIdentity(7));
    }
    SUBCASE("equal primary: the smaller secondary distance wins even for a later higher id") {
        f.RemoveRight();
        // 8은 먼저 만들었고 (500,210)이라 보조축 180(두 배 단위), 9는 나중에 만들었고
        // (500,350)이라 100이다. id도 형제 순서도 9가 불리한데 9가 이겨야 한다.
        f.AddSelectable(8, 500.0f, 210.0f);
        f.AddSelectable(9, 500.0f, 350.0f);
        f.Rebuild();
        CHECK(destinationOf(kCenterId, Input::KeyCode::Right) ==
              f.SelectableIdentity(9));
    }
    SUBCASE("equal distances: the earlier sibling wins even with the higher object id") {
        f.RemoveRight();
        // 9를 먼저 만들어 8보다 앞 형제로 둔다. 미러 위치라 거리가 같다.
        f.AddSelectable(9, 500.0f, 250.0f);
        f.AddSelectable(8, 500.0f, 350.0f);
        f.Rebuild();
        CHECK(destinationOf(kCenterId, Input::KeyCode::Right) ==
              f.SelectableIdentity(9));
    }
    SUBCASE("no candidate in the half-plane leaves focus alone without a diagnostic") {
        f.FocusByPointer(kRightId);
        f.Diagnostics().Clear();
        const auto plan = f.Plan(f.KeyRight());
        CHECK_FALSE(plan.focusDestinationFromSnapshotN.has_value());
        // 단계 대상은 출발 포커스 그대로다.
        REQUIRE(plan.TargetFor(UIEventStage::Focus).has_value());
        CHECK(plan.TargetFor(UIEventStage::Focus)->runtimeTarget ==
              f.SelectableIdentity(kRightId));
        const auto before = SemanticGeneration();
        const auto sent = f.Dispatch(plan);
        CHECK(sent.focusMerge == UIEventMergeStatus::Merged);
        CHECK(sent.focus.actionMask == 0);
        CHECK_FALSE(sent.focus.consumed);
        CHECK_FALSE(sent.focus.resultingFocus.has_value());
        CHECK(f.Focused() == f.SelectableIdentity(kRightId));
        CHECK(f.Diagnostics().Total() == 0);
        CHECK(SemanticGeneration() == before);
    }
}

TEST_CASE("navigation mode None neither navigates nor is navigated to") {
    UIFocusFixture f;
    SUBCASE("a source with mode None produces no destination and no diagnostic") {
        f.SelectableOf(kCenterId).SetNavigationMode(UINavigationMode::None);
        f.Rebuild();
        f.FocusByPointer(kCenterId);
        f.Diagnostics().Clear();
        for (const auto key : {Input::KeyCode::Left, Input::KeyCode::Right,
                               Input::KeyCode::Up, Input::KeyCode::Down}) {
            const auto plan = f.Plan(f.KeyEvent(key));
            CHECK_FALSE(plan.focusDestinationFromSnapshotN.has_value());
            const auto sent = f.Dispatch(plan);
            CHECK(sent.focus.actionMask == 0);
        }
        CHECK(f.Focused() == f.SelectableIdentity(kCenterId));
        CHECK(f.Diagnostics().Total() == 0);
    }
    SUBCASE("a mode None selectable is excluded as an automatic destination") {
        f.SelectableOf(kRightId).SetNavigationMode(UINavigationMode::None);
        f.Rebuild();
        f.FocusByPointer(kCenterId);
        const auto plan = f.Plan(f.KeyRight());
        // Right가 유일한 오른쪽 후보였다.
        CHECK_FALSE(plan.focusDestinationFromSnapshotN.has_value());
        // 그래도 아래쪽 후보는 그대로 닿는다.
        const auto down = f.Plan(f.KeyEvent(Input::KeyCode::Down));
        REQUIRE(down.focusDestinationFromSnapshotN.has_value());
        CHECK(down.focusDestinationFromSnapshotN->runtimeTarget ==
              f.SelectableIdentity(kDownId));
    }
}

TEST_CASE("navigation projects key focus but stamped text keeps its owner") {
    UIFocusFixture f;
    f.FocusByPointer(kLeftId);
    const auto leftButton = f.ButtonIdentity(kLeftId);
    const auto centerSelectable = f.SelectableIdentity(kCenterId);

    const auto nav = f.Plan(f.DpadRight());
    // 동작 대상은 출발 경로의 버튼, 포커스 단계 대상은 목적지다.
    CHECK(nav.targetFromSnapshotN == leftButton);
    REQUIRE(nav.focusDestinationFromSnapshotN.has_value());
    CHECK(nav.focusDestinationFromSnapshotN->runtimeTarget == centerSelectable);
    CHECK(f.State().focused == centerSelectable);
    const auto sent = f.Dispatch(nav);
    CHECK(sent.focus.resultingFocus == centerSelectable);
    CHECK(f.Focused() == centerSelectable);

    // 같은 배치의 다음 키는 투사된 새 포커스를 본다. 동작 대상은 Center 버튼이고
    // 포커스 단계 대상은 Center selectable이다(둘은 다른 컴포넌트).
    const auto key = f.Plan(f.KeyEvent(Input::KeyCode::A));
    CHECK(key.targetFromSnapshotN == f.ButtonIdentity(kCenterId));
    REQUIRE(key.TargetFor(UIEventStage::Focus).has_value());
    CHECK(key.TargetFor(UIEventStage::Focus)->runtimeTarget == centerSelectable);
    CHECK_FALSE(key.focusDestinationFromSnapshotN.has_value());

    // 도장 받은 텍스트는 투사된 포커스를 보지 않는다: 소유자는 도장이다.
    molga::platform::TextInputOwnerStamp stamp;
    stamp.kind = molga::platform::TextInputOwnerKind::RuntimeUITextInput;
    stamp.runtimeTarget = leftButton;
    stamp.generation = 7;
    const auto text = f.Plan(f.TextCommit("a", stamp));
    CHECK(text.targetFromSnapshotN == leftButton);
    CHECK_FALSE(text.focusDestinationFromSnapshotN.has_value());
    CHECK_FALSE(text.TargetFor(UIEventStage::Focus).has_value());
    CHECK(f.State().focused == centerSelectable);

    // 도장 없는 텍스트는 대상이 없고 투사된 포커스를 건드리지 않는다.
    const auto none =
        f.Plan(f.TextCommit("b", molga::platform::TextInputOwnerStamp{}));
    CHECK_FALSE(none.targetFromSnapshotN.has_value());
    CHECK_FALSE(none.TargetFor(UIEventStage::Focus).has_value());
    CHECK(f.State().focused == centerSelectable);
    const auto noneSent = f.Dispatch(none);
    CHECK(noneSent.focus.actionMask == 0);
    CHECK(f.Focused() == centerSelectable);
}

TEST_CASE("navigation commits only the frozen destination") {
    UIFocusFixture f;
    f.FocusByPointer(kLeftId);
    const auto n = f.Snapshot();
    const auto plan = f.Plan(f.KeyRight());
    REQUIRE(plan.focusDestinationFromSnapshotN.has_value());
    const auto frozen = *plan.focusDestinationFromSnapshotN;
    // 계획과 발송 사이에 목적지 selectable이 교체된다.
    f.ReplaceSelectable(kCenterId);
    f.Diagnostics().Clear();

    const auto sent = f.DispatchOn(*n, plan);
    CHECK(sent.focusMerge == UIEventMergeStatus::Merged);
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid,
                                    "ui.focus") == 1);
    CHECK(f.Focused() == f.SelectableIdentity(kLeftId));
    CHECK_FALSE(sent.focus.resultingFocus.has_value());
    CHECK(sent.focus.actionMask == 0);
    // 해석하지 못한 단계 대상은 그대로 남아 누산기가 검증한다.
    REQUIRE(sent.focus.resolvedStageTarget.has_value());
    CHECK(*sent.focus.resolvedStageTarget == frozen);
    // 새 selectable로 다시 해석해 옮기지 않았다.
    CHECK(f.Focused() != f.SelectableIdentity(kCenterId));
}

TEST_CASE("a gamepad axis never navigates") {
    UIFocusFixture f;
    f.FocusByPointer(kLeftId);
    f.Diagnostics().Clear();
    for (const auto axis :
         {Input::GamepadAxis::LeftX, Input::GamepadAxis::LeftY,
          Input::GamepadAxis::RightX, Input::GamepadAxis::RightY}) {
        const auto plan = f.Plan(f.PadAxis(axis, 64 * 8));
        CHECK_FALSE(plan.focusDestinationFromSnapshotN.has_value());
        const auto sent = f.Dispatch(plan);
        CHECK(sent.focusMerge == UIEventMergeStatus::Merged);
        CHECK(sent.focus.actionMask == 0);
        CHECK(f.Focused() == f.SelectableIdentity(kLeftId));
    }
    CHECK(f.Diagnostics().Total() == 0);
}

TEST_CASE("keyboard and gamepad focus share the pointer focus lifecycle") {
    UIFocusFixture f;
    f.FocusByPointer(kLeftId);
    const auto sent = f.Send(f.DpadRight());
    REQUIRE(sent.focus.resultingFocus.has_value());
    const auto center = f.SelectableIdentity(kCenterId);
    REQUIRE(f.Focused() == center);
    // 방향 입력은 캡처를 만들지 않는다.
    CHECK_FALSE(f.Surface().pointerCapture.has_value());

    SUBCASE("a pointer departure keeps keyboard focus") {
        const auto departure = f.Send(f.PointerDeparture());
        CHECK(departure.focusMerge == UIEventMergeStatus::Merged);
        CHECK(f.Focused() == center);
    }
    SUBCASE("deactivating the focused object clears it on Revalidate") {
        f.Object(kCenterId).SetActive(false);
        f.Focus().Revalidate(f.World(), *f.Snapshot(), f.Diagnostics());
        CHECK_FALSE(f.Focused().has_value());
    }
    SUBCASE("a native window focus loss clears it") {
        f.Focus().OnNativeWindowFocusLost(kSurfaceWindow, f.Diagnostics());
        CHECK_FALSE(f.Focused().has_value());
    }
    SUBCASE("further navigation continues from the moved focus") {
        f.SyncStateFromTable();
        const auto next = f.Send(f.KeyRight());
        CHECK(next.focus.resultingFocus == f.SelectableIdentity(kRightId));
        CHECK(f.Focused() == f.SelectableIdentity(kRightId));
    }
}
