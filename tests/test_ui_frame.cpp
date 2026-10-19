// Task 12.3 Step 4: UISystem::ProcessFrame — 순서 있는 이벤트 배치를 도는 유일한 루프.
//
// 이 파일의 스냅샷도 진짜 World 위에서 UILayoutSystem::Build가 게시한 값이다.
// 장면은 640x640 캔버스 위의 겹치는 버튼 둘(high/low), 선택 가능한 버튼 B, 스크롤
// 뷰, 버튼 C다. 모든 입력은 ProcessFrame 하나를 지난다 — 라우터/포커스/스크롤을
// 직접 부르는 시험은 test_ui_input / test_ui_focus / test_ui_scroll의 일이다.
//
// 케이스마다 새 픽스처를 만든다. UISystem은 프로세스 싱글턴이므로 생성자가
// InstallLayoutDependencies를 다시 부르고, World를 서비스보다 먼저 파괴한다
// (World 소멸자가 NotifyUIWorldReleased로 UISystem의 슬롯을 비운다).

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
#include "ECS/Components/UIMask.h"
#include "ECS/Components/UIScrollView.h"
#include "ECS/Components/UISelectable.h"
#include "ECS/GameObject.h"
#include "SmokeTestSupport.h"
#include "Systems/Input.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextShapingService.h"
#include "UI/UIDeterministicTick.h"
#include "UI/UIFocusSystem.h"
#include "UI/UIInputEvent.h"
#include "UI/UIInteractionState.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UILayoutSystem.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UIRuntimeInvalidation.h"
#include "UI/UISurfaceCoordinateMapping.h"
#include "UI/UIScrollSystem.h"
#include "UI/UISystem.h"
#include "UI/UITextInputVisualState.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using molga::Fixed26_6;
using molga::FixedPoint;
using molga::FixedSize;
using molga::text::TextDiagnosticCode;
using molga::ui::UIDeterministicTick;
using molga::ui::UIEventAction;
using molga::ui::UIEventActionBit;
using molga::ui::UIFrameInput;
using molga::ui::UIFrameResult;
using molga::ui::UIInputEvent;
using molga::ui::UIInputEventKind;
using molga::ui::UIInteractionStateTable;
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
constexpr unsigned int kLowId = 2;
constexpr unsigned int kHighId = 3;
constexpr unsigned int kBId = 4;
constexpr unsigned int kScrollId = 5;
constexpr unsigned int kScrollContentId = 6;
constexpr unsigned int kCId = 7;

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


bool HasHit(const UISnapshot& snapshot, const UIRuntimeTargetIdentity& target) {
    for (const auto& hit : snapshot.hitTargets) {
        if (hit.target == target) return true;
    }
    return false;
}

// 겹치는 high/low 버튼, 선택 가능한 B, 세로 스크롤 뷰, 버튼 C가 있는 장면.
// 콜백 갈고리(onHigh_/onC_)는 케이스가 세계를 바꾸는 방법이다.
class UIFrameFixture {
public:
    UIFrameFixture() {
        UISystem::Get().InstallLayoutDependencies(
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            runtime_.service);

        GameObject* canvas = AddObject(world_, kCanvasId, nullptr);
        AddOffsetRect(*canvas, 0.0f, 0.0f, kViewportPx, kViewportPx);
        auto* canvasComponent = canvas->AddComponent<UICanvas>();
        REQUIRE(canvasComponent != nullptr);
        canvasComponent->SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
        canvasComponent->SetSortingOrder(0);

        // 겹치는 두 버튼. 나중 형제가 위에 그려지므로 (10,10)의 정답은 high다.
        GameObject* low = AddObject(world_, kLowId, canvas);
        AddOffsetRect(*low, 0.0f, 0.0f, 100.0f, 100.0f);
        low_ = low->AddComponent<UIButton>();
        REQUIRE(low_ != nullptr);
        low_->SetOnClick([this] { ++lowClicks_; });

        GameObject* high = AddObject(world_, kHighId, canvas);
        AddOffsetRect(*high, 0.0f, 0.0f, 100.0f, 100.0f);
        high_ = high->AddComponent<UIButton>();
        REQUIRE(high_ != nullptr);
        high_->SetOnClick([this] {
            ++highClicks_;
            if (onHigh_) onHigh_();
        });

        GameObject* b = AddObject(world_, kBId, canvas);
        AddOffsetRect(*b, 300.0f, 300.0f, 100.0f, 100.0f);
        b_ = b->AddComponent<UIButton>();
        REQUIRE(b_ != nullptr);
        b_->SetOnClick([this] { ++bClicks_; });
        bSelectable_ = b->AddComponent<UISelectable>();
        REQUIRE(bSelectable_ != nullptr);

        // 세로 스크롤 뷰. 마스크 + 배경 이미지라서 뷰 자신이 hit 기록을 가진다.
        GameObject* scroll = AddObject(world_, kScrollId, canvas);
        AddOffsetRect(*scroll, 200.0f, 0.0f, 100.0f, 100.0f);
        auto* mask = scroll->AddComponent<UIMask>();
        REQUIRE(mask != nullptr);
        mask->SetClipsDescendants(true);
        auto* view = scroll->AddComponent<UIScrollView>();
        REQUIRE(view != nullptr);
        view->SetViewport(SceneObjectRef{kScrollId});
        view->SetContent(SceneObjectRef{kScrollContentId});
        view->SetHorizontal(false);
        view->SetVertical(true);
        REQUIRE(scroll->AddComponent<UIImage>() != nullptr);
        scrollView_ = view;
        GameObject* content = AddObject(world_, kScrollContentId, scroll);
        AddOffsetRect(*content, 0.0f, 0.0f, 100.0f, 200.0f);

        GameObject* c = AddObject(world_, kCId, canvas);
        AddOffsetRect(*c, 500.0f, 500.0f, 100.0f, 100.0f);
        c_ = c->AddComponent<UIButton>();
        REQUIRE(c_ != nullptr);
        c_->SetOnClick([this] {
            ++cClicks_;
            if (onC_) onC_();
        });
    }

    ::World& World() { return world_; }
    CountingDiagnosticSink& Diagnostics() { return sink_; }
    GameObject& Object(unsigned int id) {
        GameObject* object = world_.FindById(id);
        REQUIRE(object != nullptr);
        return *object;
    }
    RectTransform& Rect(unsigned int id) {
        auto* rect = Object(id).GetComponent<RectTransform>();
        REQUIRE(rect != nullptr);
        return *rect;
    }
    UIRuntimeTargetIdentity Identity(const Component& component) const {
        return molga::ui::CaptureTarget(world_, component);
    }
    UIRuntimeTargetIdentity HighIdentity() const { return Identity(*high_); }
    UIRuntimeTargetIdentity LowIdentity() const { return Identity(*low_); }
    UIRuntimeTargetIdentity BIdentity() const { return Identity(*b_); }
    UIRuntimeTargetIdentity BSelectableIdentity() const {
        return Identity(*bSelectable_);
    }
    UIRuntimeTargetIdentity CIdentity() const { return Identity(*c_); }
    UIRuntimeTargetIdentity ScrollIdentity() const { return Identity(*scrollView_); }
    UIButton& HighButton() { return *high_; }

    int LowClicks() const noexcept { return lowClicks_; }
    int HighClicks() const noexcept { return highClicks_; }
    int BClicks() const noexcept { return bClicks_; }
    int CClicks() const noexcept { return cClicks_; }
    void OnHigh(std::function<void()> hook) { onHigh_ = std::move(hook); }
    void OnC(std::function<void()> hook) { onC_ = std::move(hook); }

    // ── 표와 호출 ───────────────────────────────────────────────────────────
    static FixedSize Viewport() {
        return RawSize(static_cast<std::int32_t>(kViewportPx * 64.0f),
                       static_cast<std::int32_t>(kViewportPx * 64.0f));
    }
    UISnapshotPtr BuildN(molga::WindowId window = kSurfaceWindow) {
        return UISystem::Get().BuildLayout(world_, window, Viewport(), sink_);
    }
    UISurfaceKey Key(molga::WindowId window = kSurfaceWindow) const {
        return UISurfaceKey{window, world_.Generation()};
    }
    molga::ui::UISurfaceInteractionState Surface(
        molga::WindowId window = kSurfaceWindow) const {
        return UIInteractionStateTable::Get().SurfaceOrEmpty(Key(window));
    }
    std::optional<UIRuntimeTargetIdentity> Focused(
        molga::WindowId window = kSurfaceWindow) const {
        return molga::ui::UIFocusSystem{}.Focused(window, world_.Generation());
    }
    std::uint64_t GeometryBuilds() const {
        return UISystem::Get().LayoutGeometryBuildCountForTesting();
    }

    // ── 입력 ────────────────────────────────────────────────────────────────
    UIFrameInput Frame(std::vector<UIInputEvent> events,
                       std::vector<UIDeterministicTick> ticks = {},
                       std::optional<FixedPoint> seed = std::nullopt,
                       molga::WindowId window = kSurfaceWindow) {
        UIFrameInput input;
        input.frameIndex = ++frame_;
        input.windowId = window;
        input.logicalViewport = Viewport();
        input.orderedEvents = std::move(events);
        input.uiTicks = std::move(ticks);
        input.nativeWindowFocusedAtBatchStart = true;
        if (seed) {
            input.pointerAtBatchStart = *seed;
            input.pointerAtBatchStartValid = true;
        }
        return input;
    }
    UIFrameResult Process(const UIFrameInput& input) {
        return UISystem::Get().ProcessFrame(world_, input, runtime_.service, sink_);
    }
    UIFrameResult Run(std::vector<UIInputEvent> events,
                      std::vector<UIDeterministicTick> ticks = {},
                      std::optional<FixedPoint> seed = std::nullopt) {
        return Process(Frame(std::move(events), std::move(ticks), seed));
    }

    static UIInputEvent Pointer(UIInputEventKind kind, std::uint64_t sequence,
                                FixedPoint point, bool active,
                                molga::WindowId window = kSurfaceWindow) {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = window;
        event.kind = kind;
        event.logicalPoint = point;
        event.logicalPointValid = true;
        event.active = active;
        return event;
    }
    static UIInputEvent Down(std::uint64_t sequence, FixedPoint point,
                             molga::WindowId window = kSurfaceWindow) {
        return Pointer(UIInputEventKind::PointerButton, sequence, point, true, window);
    }
    static UIInputEvent Up(std::uint64_t sequence, FixedPoint point,
                           molga::WindowId window = kSurfaceWindow) {
        return Pointer(UIInputEventKind::PointerButton, sequence, point, false, window);
    }
    static UIInputEvent Move(std::uint64_t sequence, FixedPoint point,
                             molga::WindowId window = kSurfaceWindow) {
        return Pointer(UIInputEventKind::PointerMotion, sequence, point, false, window);
    }
    static UIInputEvent KeyDown(std::uint64_t sequence,
                                Input::KeyCode key = Input::KeyCode::A,
                                molga::WindowId window = kSurfaceWindow) {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = window;
        event.kind = UIInputEventKind::Key;
        event.control = static_cast<std::uint32_t>(key);
        event.active = true;
        return event;
    }
    static UIInputEvent Scroll(std::uint64_t sequence, std::int32_t dxRaw,
                               std::int32_t dyRaw) {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = kSurfaceWindow;
        event.kind = UIInputEventKind::Scroll;
        event.logicalDelta = FixedPoint{Fixed26_6::FromRaw(dxRaw),
                                        Fixed26_6::FromRaw(dyRaw)};
        return event;
    }
    static UIInputEvent WindowFocus(std::uint64_t sequence, bool active,
                                    molga::WindowId window) {
        UIInputEvent event;
        event.sequence = sequence;
        event.traceOrdinal = sequence;
        event.windowId = window;
        event.kind = UIInputEventKind::WindowFocus;
        event.active = active;
        return event;
    }
    static UIDeterministicTick Tick(std::uint64_t index, std::int32_t raw = 1) {
        UIDeterministicTick tick;
        tick.tickIndex = index;
        tick.deltaSeconds = Fixed26_6::FromRaw(raw);
        return tick;
    }

    // 장면의 좌표.
    static FixedPoint OnHigh() { return PxPoint(10.0f, 10.0f); }
    static FixedPoint OnB() { return PxPoint(350.0f, 350.0f); }
    static FixedPoint OnC() { return PxPoint(550.0f, 550.0f); }
    static FixedPoint OnScroll() { return PxPoint(250.0f, 50.0f); }
    static FixedPoint Outside() { return PxPoint(700.0f, 500.0f); }

private:
    CountingDiagnosticSink sink_;
    // 서비스가 World보다 오래 산다: World 소멸자가 NotifyUIWorldReleased로 UISystem이
    // 쥔 슬롯을 비우는 동안 서비스가 아직 있어야 한다.
    FocusTextRuntime runtime_;
    ::World world_;
    UIButton* low_ = nullptr;
    UIButton* high_ = nullptr;
    UIButton* b_ = nullptr;
    UISelectable* bSelectable_ = nullptr;
    UIButton* c_ = nullptr;
    UIScrollView* scrollView_ = nullptr;
    int lowClicks_ = 0;
    int highClicks_ = 0;
    int bClicks_ = 0;
    int cClicks_ = 0;
    std::function<void()> onHigh_;
    std::function<void()> onC_;
    std::uint64_t frame_ = 0;
};

// 시험 갈고리: N+1 게시 직후 세계를 바꾼다. 함수 포인터라 상태를 정적 변수로 둔다.
UIFrameFixture* g_observedFixture = nullptr;
int g_observerCalls = 0;
// 세계를 건드리지 않고 호출 횟수만 센다. 관찰자가 설치돼야 정지 검사가 실행된다.
void CountingNoopObserver(World&, const UISnapshot&) { ++g_observerCalls; }
void ToggleCActiveObserver(World&, const UISnapshot&) {
    ++g_observerCalls;
    GameObject& c = g_observedFixture->Object(kCId);
    c.SetActive(!c.IsActive());
}
// 계층 변이가 아니라 세터 하나만: UIComponent::Invalidate가 의미 세대를 올리므로
// 이것도 조용히 미뤄지지 않고 보고된다.
void ResizeBObserver(World&, const UISnapshot&) {
    ++g_observerCalls;
    g_observedFixture->Rect(kBId).SetSizeDelta({90.0f, 90.0f});
}
struct ScopedPublicationObserver {
    explicit ScopedPublicationObserver(UIFrameFixture& fixture) {
        g_observedFixture = &fixture;
        g_observerCalls = 0;
        molga::ui::detail::SetUIFramePublicationObserverForTest(
            &ToggleCActiveObserver);
    }
    ~ScopedPublicationObserver() {
        molga::ui::detail::SetUIFramePublicationObserverForTest(nullptr);
        g_observedFixture = nullptr;
    }
};

}  // namespace

TEST_CASE("ProcessFrame freezes N and publishes at most one N plus one") {
    UIFrameFixture f;
    const UISnapshotPtr n = f.BuildN();
    REQUIRE(n != nullptr);
    f.Diagnostics().Clear();
    // 클릭 콜백이 high를 끄고 low의 사각형을 두 번 바꾼다: 두 변이가 하나의 N+1로 모인다.
    f.OnHigh([&f] {
        f.Object(kHighId).SetActive(false);
        f.Rect(kLowId).SetSizeDelta({300.0f, 100.0f});
        f.Rect(kLowId).SetSizeDelta({400.0f, 100.0f});
    });
    const std::uint64_t buildsBefore = f.GeometryBuilds();
    // 관찰자가 있어야 게시 직후의 정지 검사가 실행된다: 호출 횟수 1, 보고 0.
    g_observerCalls = 0;
    molga::ui::detail::SetUIFramePublicationObserverForTest(&CountingNoopObserver);

    const UIFrameResult result = f.Run(
        {UIFrameFixture::Down(1, UIFrameFixture::OnHigh()),
         UIFrameFixture::Up(2, UIFrameFixture::OnHigh()),
         UIFrameFixture::KeyDown(3)});
    molga::ui::detail::SetUIFramePublicationObserverForTest(nullptr);
    CHECK(g_observerCalls == 1);

    CHECK(result.interactionSnapshot.get() == n.get());
    REQUIRE(result.renderSnapshot != nullptr);
    CHECK(result.renderSnapshot.get() != n.get());
    REQUIRE(result.dispatches.size() == 3);
    CHECK(result.dispatches[0].sequence == 1);
    CHECK(result.dispatches[1].sequence == 2);
    CHECK(result.dispatches[2].sequence == 3);
    CHECK((result.dispatches[1].actionMask &
           UIEventActionBit(UIEventAction::Click)) != 0);
    CHECK(f.HighClicks() == 1);
    CHECK(f.LowClicks() == 0);
    CHECK(f.GeometryBuilds() - buildsBefore == 1);
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReflowDeferred, "ui.frame") == 0);
    // 게시된 N+1은 콜백의 변이를 보여 준다: high는 사라졌다.
    CHECK_FALSE(HasHit(*result.renderSnapshot, f.HighIdentity()));
    CHECK(HasHit(*n, f.HighIdentity()));
    // N+2는 없다: 변경 없이 다시 빌드하면 같은 포인터다.
    CHECK(f.BuildN().get() == result.renderSnapshot.get());
    CHECK(f.GeometryBuilds() - buildsBefore == 1);
}

TEST_CASE("a same-batch press after a callback is judged against N and never against N plus one") {
    UIFrameFixture f;
    const UISnapshotPtr n = f.BuildN();
    REQUIRE(n != nullptr);
    f.Diagnostics().Clear();
    // 콜백이 high를 멀리 옮기고 N+1을 **배치 중간에** 게시한다. 뒤 이벤트를 N+1에 대해
    // 계획하는 구현이라면 (10,10)의 답이 low가 된다.
    f.OnHigh([&f] {
        f.Rect(kHighId).SetAnchoredPosition({400.0f, 400.0f});
        (void)f.BuildN();
    });

    const UIFrameResult result = f.Run(
        {UIFrameFixture::Down(1, UIFrameFixture::OnHigh()),
         UIFrameFixture::Up(2, UIFrameFixture::OnHigh()),
         UIFrameFixture::Down(3, UIFrameFixture::OnHigh()),
         UIFrameFixture::Up(4, UIFrameFixture::OnHigh())});

    CHECK(f.HighClicks() == 2);
    CHECK(f.LowClicks() == 0);
    REQUIRE(result.dispatches.size() == 4);
    CHECK(result.dispatches[3].runtimeTarget == f.HighIdentity());
    CHECK(result.interactionSnapshot.get() == n.get());
    // 다음 프레임은 새 스냅샷이다: 같은 점의 답은 이제 low다.
    const UIFrameResult next = f.Run({UIFrameFixture::Down(5, UIFrameFixture::OnHigh()),
                                      UIFrameFixture::Up(6, UIFrameFixture::OnHigh())});
    CHECK(f.LowClicks() == 1);
    CHECK(f.HighClicks() == 2);
    CHECK(next.interactionSnapshot.get() != n.get());
}

TEST_CASE("a callback that removes its target leaves the rest of the batch on N without retargeting") {
    UIFrameFixture f;
    REQUIRE(f.BuildN() != nullptr);
    f.Diagnostics().Clear();
    f.OnHigh([&f] { f.World().RemoveByIds({kHighId}); });

    const UIFrameResult result = f.Run(
        {UIFrameFixture::Down(1, UIFrameFixture::OnHigh()),
         UIFrameFixture::Up(2, UIFrameFixture::OnHigh()),
         UIFrameFixture::Down(3, UIFrameFixture::OnHigh()),
         UIFrameFixture::Up(4, UIFrameFixture::OnHigh())});

    CHECK(f.HighClicks() == 1);
    CHECK(f.LowClicks() == 0);
    REQUIRE(result.dispatches.size() == 4);
    CHECK_FALSE(result.dispatches[2].delivered);
    CHECK_FALSE(result.dispatches[3].delivered);
    // 사라진 대상을 가리키는 낡은 계획은 눌림과 놓기마다 한 번씩 보고한다.
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.input") == 2);
    CHECK_FALSE(f.Surface().pointerCapture.has_value());
    // 다음 프레임은 새 스냅샷이고 (10,10)은 이제 low다.
    (void)f.Run({UIFrameFixture::Down(5, UIFrameFixture::OnHigh()),
                 UIFrameFixture::Up(6, UIFrameFixture::OnHigh())});
    CHECK(f.LowClicks() == 1);
}

TEST_CASE("geometry changes in several callbacks coalesce into one N plus one build") {
    UIFrameFixture f;
    REQUIRE(f.BuildN() != nullptr);
    f.Diagnostics().Clear();
    f.OnHigh([&f] { f.Rect(kLowId).SetSizeDelta({90.0f, 100.0f}); });
    f.OnC([&f] { f.Rect(kCId).SetSizeDelta({90.0f, 90.0f}); });
    const std::uint64_t buildsBefore = f.GeometryBuilds();

    const UIFrameResult result = f.Run(
        {UIFrameFixture::Down(1, UIFrameFixture::OnHigh()),
         UIFrameFixture::Up(2, UIFrameFixture::OnHigh()),
         UIFrameFixture::Down(3, UIFrameFixture::OnC()),
         UIFrameFixture::Up(4, UIFrameFixture::OnC()),
         UIFrameFixture::KeyDown(5)});

    CHECK(f.HighClicks() == 1);
    CHECK(f.CClicks() == 1);
    REQUIRE(result.dispatches.size() == 5);
    CHECK(f.GeometryBuilds() - buildsBefore == 1);
    CHECK(result.renderSnapshot != nullptr);
}

TEST_CASE("ProcessFrame rejects a native batch replay before dispatch") {
    const auto clickPair = [](std::uint64_t first, std::uint64_t second) {
        return std::vector<UIInputEvent>{
            UIFrameFixture::Down(first, UIFrameFixture::OnHigh()),
            UIFrameFixture::Up(second, UIFrameFixture::OnHigh())};
    };
    {
        UIFrameFixture f;
        const UIFrameResult accepted = f.Run(clickPair(40, 41));
        CHECK(accepted.dispatches.size() == 2);
        CHECK(f.HighClicks() == 1);
        CHECK(UISystem::Get().LastCompletedNativeSequenceForTesting(f.Key()) == 41);

        // 같은 열을 다시 먹이면 발송 전에 거절된다. 틱과 게시는 계속 돈다.
        f.Diagnostics().Clear();
        const UIFrameResult replay = f.Run(clickPair(40, 41), {UIFrameFixture::Tick(1)});
        CHECK(replay.dispatches.empty());
        CHECK(f.HighClicks() == 1);
        CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.frame") == 1);
        CHECK(replay.renderSnapshot != nullptr);
        CHECK(UISystem::Get().LastCompletedTickForTesting(f.Key()) == 1);
        CHECK(UISystem::Get().LastCompletedNativeSequenceForTesting(f.Key()) == 41);

        // 분리된 창은 자기 장부를 가진다: 같은 번호가 그 표면에서는 새 값이다.
        REQUIRE(f.BuildN(kDetachedWindow) != nullptr);
        f.Diagnostics().Clear();
        const UIFrameResult detached = f.Process(f.Frame(
            {UIFrameFixture::Down(40, UIFrameFixture::OnHigh(), kDetachedWindow),
             UIFrameFixture::Up(41, UIFrameFixture::OnHigh(), kDetachedWindow)},
            {}, std::nullopt, kDetachedWindow));
        CHECK(detached.dispatches.size() == 2);
        CHECK(f.HighClicks() == 2);
        CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.frame") == 0);
        CHECK(UISystem::Get().LastCompletedNativeSequenceForTesting(f.Key(kDetachedWindow)) == 41);
        const UIFrameResult detachedReplay = f.Process(f.Frame(
            {UIFrameFixture::Down(40, UIFrameFixture::OnHigh(), kDetachedWindow),
             UIFrameFixture::Up(41, UIFrameFixture::OnHigh(), kDetachedWindow)},
            {}, std::nullopt, kDetachedWindow));
        CHECK(detachedReplay.dispatches.empty());
        CHECK(f.HighClicks() == 2);
        CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.frame") == 1);
    }

    // 새 픽스처마다 한 가지 나쁜 열: 같은 번호 반복, 역행, 0.
    const std::vector<std::pair<std::uint64_t, std::uint64_t>> bad{
        {42, 42}, {44, 43}, {0, 1}};
    for (const auto& pair : bad) {
        UIFrameFixture f;
        const UIFrameResult result = f.Run(clickPair(pair.first, pair.second));
        CHECK(result.dispatches.empty());
        CHECK(f.HighClicks() == 0);
        CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.frame") == 1);
        CHECK(UISystem::Get().LastCompletedNativeSequenceForTesting(f.Key()) == 0);
    }

    // 창 0은 표면이 아니다: 계획도 발송도 게시도 없다.
    UIFrameFixture f;
    const UIFrameResult noWindow = f.Process(f.Frame(clickPair(50, 51), {}, std::nullopt, 0));
    CHECK(noWindow.interactionSnapshot == nullptr);
    CHECK(noWindow.renderSnapshot == nullptr);
    CHECK(noWindow.dispatches.empty());
    CHECK(f.HighClicks() == 0);
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.frame") == 1);
}

TEST_CASE("ProcessFrame echoes the frame index and an empty frame is silent") {
    UIFrameFixture f;
    UIFrameInput input = f.Frame({});
    input.frameIndex = 4242;
    const UIFrameResult result = f.Process(input);
    CHECK(result.frameIndex == 4242);
    CHECK(result.dispatches.empty());
    CHECK(result.interactionSnapshot != nullptr);
    CHECK(result.renderSnapshot.get() == result.interactionSnapshot.get());
    CHECK(f.Diagnostics().Total() == 0);
}

TEST_CASE("ticks are validated as one batch and consumed once") {
    UIFrameFixture f;
    auto& scroll = molga::ui::UIScrollSystem::Get();
    const std::uint64_t generation = f.World().Generation();

    // 시작: 스크롤 뷰 위에서 휠.
    const UIFrameResult wheel = f.Run({UIFrameFixture::Scroll(1, 0, -640)}, {},
                                      UIFrameFixture::OnScroll());
    REQUIRE(wheel.dispatches.size() == 1);
    CHECK(wheel.dispatches[0].runtimeTarget == f.ScrollIdentity());

    f.Diagnostics().Clear();
    const UIFrameResult ticked = f.Run(
        {}, {UIFrameFixture::Tick(1), UIFrameFixture::Tick(2), UIFrameFixture::Tick(3)});
    CHECK(scroll.LastTickIndexForWorld(generation) == 3);
    CHECK(UISystem::Get().LastCompletedTickForTesting(f.Key()) == 3);
    CHECK(ticked.renderSnapshot != nullptr);
    CHECK(f.Diagnostics().Total() == 0);

    // 빈 tick은 시간이 흐르지 않은 것이다: 진단도 소비도 없다.
    (void)f.Run({}, {});
    CHECK(f.Diagnostics().Total() == 0);
    CHECK(scroll.LastTickIndexForWorld(generation) == 3);

    // 묶음 전체가 거절되는 네 가지: 재생, 같은 번호 반복, 0, 한 걸음 1초 초과.
    const std::vector<std::vector<UIDeterministicTick>> bad{
        {UIFrameFixture::Tick(3), UIFrameFixture::Tick(4)},
        {UIFrameFixture::Tick(5), UIFrameFixture::Tick(5)},
        {UIFrameFixture::Tick(0)},
        {UIFrameFixture::Tick(6, 65)},
        {UIFrameFixture::Tick(6), UIFrameFixture::Tick(7), UIFrameFixture::Tick(7)}};
    std::uint64_t sequence = 10;
    for (const auto& ticks : bad) {
        f.Diagnostics().Clear();
        const UIFrameResult result = f.Run(
            {UIFrameFixture::KeyDown(sequence++)}, ticks);
        CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::LayoutInvalid, "ui.frame") == 1);
        CHECK(scroll.LastTickIndexForWorld(generation) == 3);
        CHECK(UISystem::Get().LastCompletedTickForTesting(f.Key()) == 3);
        // 같은 프레임의 이벤트는 그대로 발송된다.
        CHECK(result.dispatches.size() == 1);
    }
    // 유효한 다음 묶음은 여전히 받아들여진다.
    f.Diagnostics().Clear();
    (void)f.Run({}, {UIFrameFixture::Tick(4), UIFrameFixture::Tick(5)});
    CHECK(f.Diagnostics().Total() == 0);
    CHECK(scroll.LastTickIndexForWorld(generation) == 5);
    CHECK(UISystem::Get().LastCompletedTickForTesting(f.Key()) == 5);
}

TEST_CASE("a post-publication mutation is deferred with one ReflowDeferred") {
    UIFrameFixture f;
    const UISnapshotPtr n = f.BuildN();
    REQUIRE(n != nullptr);
    f.Diagnostics().Clear();
    const std::uint64_t buildsBefore = f.GeometryBuilds();

    UIFrameResult first;
    {
        // N+1 게시 **뒤**에 C를 끈다. 이번 프레임에는 보이지 않는다.
        ScopedPublicationObserver observer(f);
        first = f.Run({});
        CHECK(g_observerCalls == 1);
    }
    CHECK(first.renderSnapshot.get() == n.get());
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReflowDeferred, "ui.frame") == 1);
    CHECK(f.GeometryBuilds() == buildsBefore);
    CHECK(HasHit(*first.renderSnapshot, f.CIdentity()));

    // 다음 프레임이 그 변이를 게시한다: C가 사라진다. 기하 빌드는 정확히 한 번이다.
    const UIFrameResult second = f.Run({});
    CHECK(f.GeometryBuilds() - buildsBefore == 1);
    CHECK_FALSE(HasHit(*second.renderSnapshot, f.CIdentity()));
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReflowDeferred, "ui.frame") == 1);

    // 세 번째 프레임에서 다시 변이해도 월드 세대당 한 번만 보고한다.
    {
        ScopedPublicationObserver observer(f);
        (void)f.Run({});
        CHECK(g_observerCalls == 1);
    }
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReflowDeferred, "ui.frame") == 1);
}

TEST_CASE("a setter-only mutation after publication is reported too and never rebuilt in the frame") {
    UIFrameFixture f;
    const UISnapshotPtr n = f.BuildN();
    REQUIRE(n != nullptr);
    f.Diagnostics().Clear();
    const std::uint64_t buildsBefore = f.GeometryBuilds();
    g_observedFixture = &f;
    g_observerCalls = 0;
    molga::ui::detail::SetUIFramePublicationObserverForTest(&ResizeBObserver);
    const UIFrameResult result = f.Run({});
    molga::ui::detail::SetUIFramePublicationObserverForTest(nullptr);
    g_observedFixture = nullptr;
    CHECK(g_observerCalls == 1);
    CHECK(result.renderSnapshot.get() == n.get());
    CHECK(f.GeometryBuilds() == buildsBefore);
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReflowDeferred, "ui.frame") == 1);
    // 다음 프레임에 게시된다.
    (void)f.Run({});
    CHECK(f.GeometryBuilds() - buildsBefore == 1);
}

TEST_CASE("a callback that replaces the world leaves the rest of the batch undelivered and the ledgers released") {
    UIFrameFixture f;
    REQUIRE(f.BuildN() != nullptr);
    f.Diagnostics().Clear();
    const std::uint64_t retired = f.World().Generation();
    f.OnHigh([&f] { f.World().Clear(); });

    const UIFrameResult result = f.Run(
        {UIFrameFixture::Down(1, UIFrameFixture::OnHigh()),
         UIFrameFixture::Up(2, UIFrameFixture::OnHigh()),
         UIFrameFixture::Down(3, UIFrameFixture::OnHigh()),
         UIFrameFixture::Up(4, UIFrameFixture::OnHigh())},
        {UIFrameFixture::Tick(1)});

    CHECK(f.HighClicks() == 1);
    CHECK(f.LowClicks() == 0);
    // 보고는 정확히 한 번: 프레임이 한 줄, 입력 쪽(누산기)은 없다.
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.frame") == 1);
    CHECK(f.Diagnostics().CountFrom(TextDiagnosticCode::ReferenceInvalid, "ui.input") == 0);
    CHECK(f.Diagnostics().Total() == 1);
    // 감사는 이벤트마다 한 줄이지만 클릭 뒤의 둘은 발송되지 않았다.
    REQUIRE(result.dispatches.size() == 4);
    CHECK(result.dispatches[1].delivered);
    CHECK_FALSE(result.dispatches[2].delivered);
    CHECK_FALSE(result.dispatches[3].delivered);
    CHECK(f.World().Generation() != retired);
    // 은퇴한 세대의 장부는 남지 않고, 그 세대의 tick은 소비되지 않았다.
    const UISurfaceKey old{kSurfaceWindow, retired};
    CHECK(UISystem::Get().LastCompletedNativeSequenceForTesting(old) == 0);
    CHECK(UISystem::Get().LastCompletedTickForTesting(old) == 0);
    CHECK(UIInteractionStateTable::Get().SurfaceCountForWorld(retired) == 0);
    // 새 세대의 N+1은 게시된다(빈 월드).
    CHECK(result.renderSnapshot != nullptr);
}

TEST_CASE("the planning state is seeded from the surface table") {
    UIFrameFixture f;
    auto& table = UIInteractionStateTable::Get();

    // 누름과 놓기가 서로 다른 프레임에 와도 클릭이다: 캡처는 표에서 온다.
    (void)f.Run({UIFrameFixture::Down(1, UIFrameFixture::OnHigh())});
    CHECK(f.Surface().pointerCapture == f.HighIdentity());
    CHECK(f.HighClicks() == 0);
    (void)f.Run({UIFrameFixture::Up(2, UIFrameFixture::OnHigh())}, {},
                UIFrameFixture::OnHigh());
    CHECK(f.HighClicks() == 1);
    CHECK_FALSE(f.Surface().pointerCapture.has_value());

    // 누른 채 밖으로 가서 놓으면 클릭이 아니고 캡처가 풀린다. 잡힌 움직임이 밖에 있으면
    // 표의 hover는 비고, 다음 프레임의 움직임도 그것을 되살리지 않는다.
    (void)f.Run({UIFrameFixture::Down(3, UIFrameFixture::OnHigh())});
    CHECK(f.Surface().hovered == f.HighIdentity());
    (void)f.Run({UIFrameFixture::Move(4, UIFrameFixture::Outside())}, {},
                UIFrameFixture::OnHigh());
    CHECK(f.Surface().pointerCapture == f.HighIdentity());
    CHECK_FALSE(f.Surface().hovered.has_value());
    (void)f.Run({UIFrameFixture::Up(5, UIFrameFixture::Outside())}, {},
                UIFrameFixture::Outside());
    CHECK(f.HighClicks() == 1);
    CHECK_FALSE(f.Surface().pointerCapture.has_value());
    (void)f.Run({UIFrameFixture::Move(6, UIFrameFixture::Outside())}, {},
                UIFrameFixture::Outside());
    CHECK_FALSE(f.Surface().hovered.has_value());
    (void)table;

    // 포인터 씨앗: 배치의 첫 휠이 씨앗 위치의 스크롤 뷰로 간다. 씨앗이 없으면 대상이 없다.
    const UIFrameResult seeded = f.Run({UIFrameFixture::Scroll(7, 0, -64)}, {},
                                       UIFrameFixture::OnScroll());
    REQUIRE(seeded.dispatches.size() == 1);
    CHECK(seeded.dispatches[0].runtimeTarget == f.ScrollIdentity());
    const UIFrameResult unseeded = f.Run({UIFrameFixture::Scroll(8, 0, -64)});
    REQUIRE(unseeded.dispatches.size() == 1);
    CHECK_FALSE(unseeded.dispatches[0].runtimeTarget.has_value());
    // 씨앗이 없어도 같은 배치 안의 움직임이 위치를 세운다.
    const UIFrameResult moved = f.Run(
        {UIFrameFixture::Move(9, UIFrameFixture::OnScroll()),
         UIFrameFixture::Scroll(10, 0, -64)});
    REQUIRE(moved.dispatches.size() == 2);
    CHECK(moved.dispatches[1].runtimeTarget == f.ScrollIdentity());

    // 포커스도 표에서 온다: 누르고 놓아 B에 포커스를 둔 뒤 다음 프레임의 키가 B로 간다.
    (void)f.Run({UIFrameFixture::Down(11, UIFrameFixture::OnB()),
                 UIFrameFixture::Up(12, UIFrameFixture::OnB())});
    CHECK(f.BClicks() == 1);
    CHECK(f.Focused() == f.BSelectableIdentity());
    const UIFrameResult key = f.Run({UIFrameFixture::KeyDown(13, Input::KeyCode::Right)});
    REQUIRE(key.dispatches.size() == 1);
    CHECK(key.dispatches[0].runtimeTarget == f.BIdentity());
}

TEST_CASE("clickedThisFrame is cleared at the next frame start without a visible change") {
    UIFrameFixture f;
    (void)f.Run({UIFrameFixture::Down(1, UIFrameFixture::OnHigh()),
                 UIFrameFixture::Up(2, UIFrameFixture::OnHigh())});
    CHECK(f.HighButton().WasClickedThisFrame());

    const std::uint64_t semanticBefore =
        molga::ui::UIRuntimeInvalidationClock::Current().semanticDirtyGeneration;
    (void)f.Run({});
    CHECK_FALSE(f.HighButton().WasClickedThisFrame());
    // 클릭 가장자리는 보이는 상태가 아니다: 의미 세대가 오르지 않았다.
    CHECK(molga::ui::UIRuntimeInvalidationClock::Current().semanticDirtyGeneration ==
          semanticBefore);
}

TEST_CASE("Revalidate runs at frame start and after each callback") {
    {
        UIFrameFixture f;
        (void)f.Run({UIFrameFixture::Down(1, UIFrameFixture::OnB()),
                     UIFrameFixture::Up(2, UIFrameFixture::OnB())});
        REQUIRE(f.Focused().has_value());
        // 월드가 돌지 않으므로 OnDisable이 없다. 프레임 시작의 Revalidate만이 정리한다.
        f.Object(kBId).SetActive(false);
        (void)f.Run({});
        CHECK_FALSE(f.Focused().has_value());
    }
    {
        UIFrameFixture f;
        f.OnHigh([&f] { f.Object(kBId).SetActive(false); });
        (void)f.Run({UIFrameFixture::Down(1, UIFrameFixture::OnB()),
                     UIFrameFixture::Up(2, UIFrameFixture::OnB()),
                     UIFrameFixture::Down(3, UIFrameFixture::OnHigh()),
                     UIFrameFixture::Up(4, UIFrameFixture::OnHigh()),
                     UIFrameFixture::KeyDown(5, Input::KeyCode::Right)});
        CHECK(f.HighClicks() == 1);
        // 콜백이 B를 껐다. 콜백 뒤의 Revalidate가 포커스를 놓았고, 뒤따르는 키가 되살리지 않았다.
        CHECK_FALSE(f.Focused().has_value());
    }
}

TEST_CASE("foreign-window events stay audited and touch nothing") {
    UIFrameFixture f;
    const UIFrameResult result = f.Run(
        {UIFrameFixture::Down(1, UIFrameFixture::OnB()),
         UIFrameFixture::WindowFocus(2, false, kDetachedWindow),
         UIFrameFixture::Up(3, UIFrameFixture::OnB())});
    REQUIRE(result.dispatches.size() == 3);
    CHECK(result.dispatches[1].sequence == 2);
    CHECK_FALSE(result.dispatches[1].runtimeTarget.has_value());
    CHECK_FALSE(result.dispatches[1].consumed);
    CHECK_FALSE(result.dispatches[1].delivered);
    CHECK(result.dispatches[2].delivered);
    CHECK(f.BClicks() == 1);
    // 눌림이 세운 포커스는 다른 창의 포커스 상실에 지워지지 않는다.
    CHECK(f.Focused() == f.BSelectableIdentity());

    // 대조군: 같은 창의 포커스 상실은 포커스를 놓는다.
    (void)f.Run({UIFrameFixture::WindowFocus(4, false, kSurfaceWindow)});
    CHECK_FALSE(f.Focused().has_value());
}

TEST_CASE("releasing a world drops the frame ledgers") {
    UIFrameFixture f;
    (void)f.Run({UIFrameFixture::KeyDown(7)}, {UIFrameFixture::Tick(1)});
    const UISurfaceKey key = f.Key();
    REQUIRE(UISystem::Get().LastCompletedNativeSequenceForTesting(key) == 7);
    REQUIRE(UISystem::Get().LastCompletedTickForTesting(key) == 1);

    UISystem::Get().OnWorldReleased(f.World().Generation());
    CHECK(UISystem::Get().LastCompletedNativeSequenceForTesting(key) == 0);
    CHECK(UISystem::Get().LastCompletedTickForTesting(key) == 0);
}

TEST_CASE("a surface pointer departure keeps keyboard focus") {
    UIFrameFixture f;
    (void)f.Run({UIFrameFixture::Down(1, UIFrameFixture::OnB()),
                 UIFrameFixture::Up(2, UIFrameFixture::OnB()),
                 UIFrameFixture::Down(3, UIFrameFixture::OnHigh())});
    REQUIRE(f.Focused() == f.BSelectableIdentity());
    REQUIRE(f.Surface().pointerCapture == f.HighIdentity());

    UISystem::Get().OnSurfacePointerDeparted(kSurfaceWindow, f.World().Generation(),
                                             f.Diagnostics());
    CHECK_FALSE(f.Surface().pointerCapture.has_value());
    CHECK_FALSE(f.Surface().hovered.has_value());
    CHECK(f.Focused() == f.BSelectableIdentity());
}

// 라우터는 실제 표면 {창, 세대}에 hover/눌림을 쓰고, 레이아웃 빌드는 **그 스냅샷의 표면**
// 만 읽는다(정확한 표면 읽기). 게시된 N+1의 버튼 색이 그것을 보여 줘야 하고, 다른 표면의
// 스냅샷은 같은 월드 세대의 같은 버튼이라도 기본 색이어야 한다 — 그래야 잘못된 표면에
// 쓰는 라우터(또는 any-surface로 읽는 빌드)가 실패한다.
namespace {
const Color* SolidColorOf(const UISnapshot& snapshot,
                          const UIRuntimeTargetIdentity& source) {
    for (const auto& item : snapshot.renderItems) {
        if (!(item.source == source)) continue;
        if (const auto* solid = std::get_if<molga::ui::UISolidRectSnapshot>(&item.payload)) {
            return &solid->color;
        }
    }
    return nullptr;
}

// 다른 표면(분리 창)의 스냅샷에서 high의 색. 매번 새로 짓는다.
Color HighColorOnSurface(UIFrameFixture& f, molga::WindowId window) {
    const UISnapshotPtr snapshot = f.BuildN(window);
    REQUIRE(snapshot != nullptr);
    const Color* color = SolidColorOf(*snapshot, f.HighIdentity());
    REQUIRE(color != nullptr);
    return *color;
}
}  // namespace

TEST_CASE("the published render snapshot shows the router's hover and press colours") {
    UIFrameFixture f;
    UIButton& high = f.HighButton();
    high.SetNormalColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
    high.SetHoverColor(Color(0.2f, 0.6f, 0.2f, 1.0f));
    high.SetPressedColor(Color(0.7f, 0.2f, 0.2f, 1.0f));
    const UISnapshotPtr cold = f.BuildN();
    REQUIRE(cold != nullptr);
    REQUIRE(SolidColorOf(*cold, f.HighIdentity()) != nullptr);
    CHECK(*SolidColorOf(*cold, f.HighIdentity()) == high.GetNormalColor());
    CHECK(HighColorOnSurface(f, kDetachedWindow) == high.GetNormalColor());

    // 모션만: hover 색.
    const UIFrameResult hovered = f.Run({UIFrameFixture::Move(1, UIFrameFixture::OnHigh())});
    REQUIRE(hovered.renderSnapshot != nullptr);
    REQUIRE(SolidColorOf(*hovered.renderSnapshot, f.HighIdentity()) != nullptr);
    CHECK(*SolidColorOf(*hovered.renderSnapshot, f.HighIdentity()) == high.GetHoverColor());
    // 같은 월드 세대의 다른 표면은 hover를 모른다. any-surface getter만 본다.
    CHECK(HighColorOnSurface(f, kDetachedWindow) == high.GetNormalColor());
    CHECK(high.CurrentColor() == high.GetHoverColor());
    CHECK(high.IsHovered());

    // 누름: pressed 색.
    const UIFrameResult pressed = f.Run({UIFrameFixture::Down(2, UIFrameFixture::OnHigh())});
    REQUIRE(pressed.renderSnapshot != nullptr);
    REQUIRE(SolidColorOf(*pressed.renderSnapshot, f.HighIdentity()) != nullptr);
    CHECK(*SolidColorOf(*pressed.renderSnapshot, f.HighIdentity()) == high.GetPressedColor());
    CHECK(HighColorOnSurface(f, kDetachedWindow) == high.GetNormalColor());
    CHECK(high.CurrentColor() == high.GetPressedColor());

    // 놓기: hover 색으로 돌아온다(포인터는 아직 위에 있다).
    const UIFrameResult released = f.Run({UIFrameFixture::Up(3, UIFrameFixture::OnHigh())});
    REQUIRE(released.renderSnapshot != nullptr);
    REQUIRE(SolidColorOf(*released.renderSnapshot, f.HighIdentity()) != nullptr);
    CHECK(*SolidColorOf(*released.renderSnapshot, f.HighIdentity()) == high.GetHoverColor());
    CHECK(HighColorOnSurface(f, kDetachedWindow) == high.GetNormalColor());

    // 표면 이탈 뒤 다시 지으면 기본 색이다.
    UISystem::Get().OnSurfacePointerDeparted(kSurfaceWindow, f.World().Generation(),
                                             f.Diagnostics());
    const UISnapshotPtr after = f.BuildN();
    REQUIRE(after != nullptr);
    REQUIRE(SolidColorOf(*after, f.HighIdentity()) != nullptr);
    CHECK(*SolidColorOf(*after, f.HighIdentity()) == high.GetNormalColor());
    CHECK(high.CurrentColor() == high.GetNormalColor());
}

TEST_CASE("a detached surface's hover and press colours stay on that surface") {
    UIFrameFixture f;
    UIButton& high = f.HighButton();
    high.SetNormalColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
    high.SetHoverColor(Color(0.2f, 0.6f, 0.2f, 1.0f));
    high.SetPressedColor(Color(0.7f, 0.2f, 0.2f, 1.0f));
    REQUIRE(f.BuildN(kDetachedWindow) != nullptr);

    // 분리 창에서 누른다: 그 창의 스냅샷만 pressed, 내장 창 스냅샷은 기본 색.
    const UIFrameResult pressed = f.Process(f.Frame(
        {UIFrameFixture::Down(40, UIFrameFixture::OnHigh(), kDetachedWindow)}, {},
        std::nullopt, kDetachedWindow));
    REQUIRE(pressed.renderSnapshot != nullptr);
    REQUIRE(SolidColorOf(*pressed.renderSnapshot, f.HighIdentity()) != nullptr);
    CHECK(*SolidColorOf(*pressed.renderSnapshot, f.HighIdentity()) == high.GetPressedColor());
    CHECK(HighColorOnSurface(f, kSurfaceWindow) == high.GetNormalColor());
    CHECK(HighColorOnSurface(f, kDetachedWindow) == high.GetPressedColor());
    CHECK(high.CurrentColor() == high.GetPressedColor());
}

// ── 레거시 ProcessInput 시험에서 옮긴 의미 ──────────────────────────────────
// (맨 위 캡처, 같은 hover의 세대, 밖에서 놓기, 콜백이 월드를 비움은 위의 라우터/프레임
// 케이스가 이미 덮는다.)
TEST_CASE("disabled canvases and inactive ancestors absorb no press") {
    UIFrameFixture f;
    UIButton& high = f.HighButton();

    f.Object(kCanvasId).GetComponent<UICanvas>()->SetEnabled(false);
    (void)f.Run({UIFrameFixture::Down(1, UIFrameFixture::OnHigh())});
    CHECK_FALSE(high.IsPressed());
    CHECK_FALSE(f.Surface().pointerCapture.has_value());

    f.Object(kCanvasId).GetComponent<UICanvas>()->SetEnabled(true);
    f.Object(kCanvasId).SetActive(false);
    (void)f.Run({UIFrameFixture::Down(2, UIFrameFixture::OnHigh())});
    CHECK_FALSE(high.IsPressed());
    CHECK_FALSE(f.Surface().pointerCapture.has_value());

    // 대조: 되살리면 같은 점에서 눌린다.
    f.Object(kCanvasId).SetActive(true);
    (void)f.Run({UIFrameFixture::Down(3, UIFrameFixture::OnHigh())});
    CHECK(high.IsPressed());
    CHECK(f.Surface().pointerCapture == f.HighIdentity());
}

TEST_CASE("disabling a pressed button clears its transient pointer state") {
    UIFrameFixture f;
    UIButton& high = f.HighButton();
    (void)f.Run({UIFrameFixture::Down(1, UIFrameFixture::OnHigh())});
    REQUIRE(high.IsPressed());
    REQUIRE(f.Surface().pointerCapture == f.HighIdentity());

    high.SetInteractable(false);
    CHECK_FALSE(high.IsPressed());
    CHECK_FALSE(high.IsHovered());
    CHECK_FALSE(high.WasClickedThisFrame());
    CHECK_FALSE(f.Surface().pointerCapture.has_value());
}

TEST_CASE("a pointer that leaves the surface releases capture so a later release never clicks") {
    UIFrameFixture f;
    (void)f.Run({UIFrameFixture::Down(1, UIFrameFixture::OnHigh())});
    REQUIRE(f.HighButton().IsPressed());

    // 표면 밖(유효하지 않은 논리점)으로 나간 움직임이 캡처를 푼다.
    UIInputEvent gone = UIFrameFixture::Move(2, UIFrameFixture::OnHigh());
    gone.logicalPointValid = false;
    (void)f.Run({gone});
    CHECK_FALSE(f.HighButton().IsPressed());
    CHECK_FALSE(f.Surface().pointerCapture.has_value());

    (void)f.Run({UIFrameFixture::Up(3, UIFrameFixture::OnHigh())}, {},
                UIFrameFixture::OnHigh());
    CHECK(f.HighClicks() == 0);
}

// Task 12.3 Step 7: 런타임 스모크는 호스트 배치 대신 합성 클릭으로 같은 ProcessFrame을
// 구동한다. 점은 게시된 스냅샷의 hit 기록 중심이고, 버튼은 항상 주 버튼(Left)이다.
TEST_CASE("smoke click synthesis drives a primary click through ProcessFrame") {
    UIFrameFixture f;
    const UISnapshotPtr n = f.BuildN();
    REQUIRE(n != nullptr);

    const auto point = molga::ui::SmokeClickPointForObject(*n, kHighId);
    REQUIRE(point.has_value());
    // high는 (0,0) 100x100: 중심은 (50,50)이고 맨 위 기록은 high다.
    CHECK(point->x.Raw() == 50 * 64);
    CHECK(point->y.Raw() == 50 * 64);

    const UIInputEvent press = molga::ui::MakeSyntheticPrimaryButtonEvent(
        1, kSurfaceWindow, *point, true);
    const UIInputEvent release = molga::ui::MakeSyntheticPrimaryButtonEvent(
        2, kSurfaceWindow, *point, false);
    CHECK(press.kind == UIInputEventKind::PointerButton);
    CHECK(press.control == static_cast<std::uint32_t>(Input::MouseButton::Left));
    CHECK(press.active);
    CHECK_FALSE(release.active);
    CHECK(press.logicalPointValid);

    // 스모크처럼 눌림과 놓음은 서로 다른 프레임이다.
    (void)f.Run({press});
    CHECK(f.Surface().pointerCapture == f.HighIdentity());
    CHECK(f.HighClicks() == 0);
    (void)f.Run({release});
    CHECK(f.HighClicks() == 1);
    CHECK(f.LowClicks() == 0);

    // 가려진 버튼(low)과 기록 없는 오브젝트에는 클릭을 지어내지 않는다.
    CHECK_FALSE(molga::ui::SmokeClickPointForObject(*n, kLowId).has_value());
    CHECK_FALSE(molga::ui::SmokeClickPointForObject(*n, 987654u).has_value());

    // 주 버튼이 아닌 합성 이벤트는 클릭을 만들지 않는다(대조).
    UIInputEvent rightPress = press;
    rightPress.sequence = 3;
    rightPress.control = static_cast<std::uint32_t>(Input::MouseButton::Right);
    UIInputEvent rightRelease = release;
    rightRelease.sequence = 4;
    rightRelease.control = rightPress.control;
    (void)f.Run({rightPress, rightRelease});
    CHECK(f.HighClicks() == 1);
}
