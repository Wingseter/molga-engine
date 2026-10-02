// Task 11.3: 결정적 식별자 키 스크롤 상태.
//
// 이 파일의 모든 값은 프로덕션 입구를 지난다. 스냅샷을 손으로 조립하거나
// 스크롤 표를 직접 채워 검사하면 UILayoutSystem::Build나 UIScrollSystem을
// 통째로 이른 반환으로 바꿔도 스위트가 초록이므로, 여기서 만드는 것은 언제나
// 진짜 World와 진짜 TextLayoutService이고 상태는 언제나 ApplyInput/AdvanceTick이
// 만든다. 단 하나의 예외가 SeedStateForTesting이고, 그것은 합법 구간 **바깥**의
// 시작점 하나 때문이다(탄성 재귀의 요점이 바로 거기서 안으로 돌아오는 한
// 걸음이라 입력 경로로는 그 상태를 만들 수 없다).

#include "doctest.h"

#include "Assets/FontArtifactStore.h"
#include "Common/Fixed26_6.h"
#include "Core/AssetDatabase.h"
#include "Core/World.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UILabel.h"
#include "ECS/Components/UIMask.h"
#include "ECS/Components/UIScrollView.h"
#include "ECS/GameObject.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextShapingService.h"
#include "TextQualificationAssetTree.h"
#include "UI/UIDeterministicTick.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UILayoutSystem.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UIRuntimeInvalidation.h"
#include "UI/UIScrollSystem.h"
#include "UI/UISystem.h"
#include "UI/UITextInputVisualState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using molga::Fixed26_6;
using molga::FixedPoint;
using molga::FixedSize;
using molga::text::TextDiagnosticCode;
using molga::ui::UIDeterministicTick;
using molga::ui::UIRuntimeTargetIdentity;
using molga::ui::UIScrollAxis;
using molga::ui::UIScrollInput;
using molga::ui::UIScrollState;
using molga::ui::UIScrollSystem;
using molga::ui::UISnapshotPtr;

namespace {

constexpr molga::WindowId kSurface = 7;
constexpr std::int32_t kViewportRaw = 640;

FixedSize RawSize(std::int32_t width, std::int32_t height) {
    return FixedSize{Fixed26_6::FromRaw(width), Fixed26_6::FromRaw(height)};
}

float RawToFloat(std::int32_t raw) {
    return static_cast<float>(raw) / 64.0f;
}

// 진단을 코드별로 셀 수 있는 sink. 억제도 중복 제거도 하지 않는다 — 그런
// 필터가 있으면 "정확히 하나"라는 주장이 sink 쪽 억제 덕에 통과할 수 있다.
class CountingDiagnosticSink final : public molga::text::TextDiagnosticSink {
public:
    void Report(molga::text::TextDiagnostic diagnostic) override {
        records_.push_back(std::move(diagnostic));
    }
    std::size_t Count(TextDiagnosticCode code) const {
        std::size_t total = 0;
        for (const auto& record : records_) {
            if (record.code == code) ++total;
        }
        return total;
    }
    std::size_t Total() const noexcept { return records_.size(); }
    const std::vector<molga::text::TextDiagnostic>& Records() const noexcept {
        return records_;
    }
    void Clear() { records_.clear(); }

private:
    std::vector<molga::text::TextDiagnostic> records_;
};

// ── 실물 폰트 위의 실물 배치 서비스 ─────────────────────────────────────────
// Build의 서명이 TextLayoutService를 요구한다. 폰트가 없는 서비스를 쓰면 이
// 파일의 라벨 진단 케이스가 배치 실패 하나로 뭉개져 서로 다른 사실을 세지
// 못한다.
class ScrollTextRuntime {
private:
    QualificationAssetTreeFixture tree_;

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

    ScrollTextRuntime()
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

constexpr const char* kPrimaryFamily = "11111111111111111111111111111111";

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

const molga::ui::UILayoutNodeSnapshot* FindNodeByObject(
    const molga::ui::UISnapshot& snapshot, unsigned int objectId) {
    for (const auto& node : snapshot.nodes) {
        if (node.rectTransform.objectId == objectId) return &node;
    }
    return nullptr;
}

const molga::ui::UIRenderItemSnapshot* FindRenderItem(
    const molga::ui::UISnapshot& snapshot, unsigned int objectId) {
    for (const auto& item : snapshot.renderItems) {
        if (item.canonicalSource.sceneObjectId == objectId) return &item;
    }
    return nullptr;
}

const molga::ui::UIHitTargetSnapshot* FindHitTarget(
    const molga::ui::UISnapshot& snapshot, unsigned int objectId) {
    for (const auto& hit : snapshot.hitTargets) {
        if (hit.canonicalTarget.sceneObjectId == objectId) return &hit;
    }
    return nullptr;
}

// ── 하나의 스크롤 조립 ──────────────────────────────────────────────────────
// viewport는 UIMask를 들고 제자리에 남고, content는 그 자식으로 들어가 런타임
// 오프셋만큼 움직인다. UIScrollView는 viewport 오브젝트 위에 산다.
struct ScrollGroup {
    GameObject* viewport = nullptr;
    GameObject* content = nullptr;
    RectTransform* viewportRect = nullptr;
    RectTransform* contentRect = nullptr;
    UIScrollView* view = nullptr;
};

// 기본값은 일부러 "내용이 뷰포트에 들어가지 않는" 상태다. 들어가는 기본값을
// 쓰면 자르기가 한 번도 걸리지 않아 자르기의 모든 변종이 통과한다.
class UIScrollFixture {
public:
    UIScrollFixture() {
        canvasObject_ = AddObject(world_, 1, nullptr);
        AddOffsetRect(*canvasObject_, 0.0f, 0.0f, RawToFloat(kViewportRaw),
                      RawToFloat(kViewportRaw));
        auto* canvas = canvasObject_->AddComponent<UICanvas>();
        REQUIRE(canvas != nullptr);
        canvas->SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
        canvas->SetSortingOrder(0);
        primary_ = AddScrollGroup(2, 3);
        Rebuild();
    }

    ScrollGroup AddScrollGroup(unsigned int viewportId,
                               unsigned int contentId) {
        ScrollGroup group;
        group.viewport = AddObject(world_, viewportId, canvasObject_);
        group.viewportRect =
            AddOffsetRect(*group.viewport, 0.0f, 0.0f, RawToFloat(kViewportRaw),
                          RawToFloat(kViewportRaw));
        auto* mask = group.viewport->AddComponent<UIMask>();
        REQUIRE(mask != nullptr);
        mask->SetClipsDescendants(true);
        group.content = AddObject(world_, contentId, group.viewport);
        // 내용은 뷰포트의 두 배 높이다 -> 세로 합법 구간은 [-640, 0].
        group.contentRect =
            AddOffsetRect(*group.content, 0.0f, 0.0f, RawToFloat(kViewportRaw),
                          RawToFloat(kViewportRaw * 2));
        group.view = group.viewport->AddComponent<UIScrollView>();
        REQUIRE(group.view != nullptr);
        group.view->SetViewport(SceneObjectRef{viewportId});
        group.view->SetContent(SceneObjectRef{contentId});
        // 가로는 저작에서 꺼 둔다. 켜 두면 "꺼진 축은 변위를 소비하지 않는다"를
        // 시험할 자리가 없다.
        group.view->SetHorizontal(false);
        group.view->SetVertical(true);
        group.view->SetElasticity(RawToFloat(8));
        group.view->SetMovement(UIScrollMovement::Elastic);
        group.view->SetScrollSensitivity(1.0f);
        return group;
    }

    // content 서브트리 안의 자식 하나. 스크롤이 실제로 무엇을 옮기고 무엇을
    // 자르는지는 content 자신이 아니라 그 안의 그려지는 것에서 보인다.
    GameObject* AddContentChild(unsigned int id, float y, float width,
                                float height) {
        GameObject* child = AddObject(world_, id, primary_.content);
        AddOffsetRect(*child, 0.0f, y, width, height);
        return child;
    }

    UILabel* AddContentLabel(unsigned int id, const std::string& text,
                             float y) {
        GameObject* child = AddContentChild(id, y, RawToFloat(kViewportRaw),
                                            RawToFloat(64));
        auto* label = child->AddComponent<UILabel>();
        REQUIRE(label != nullptr);
        label->SetFontFamilyGuid(kPrimaryFamily);
        label->SetFontSizePx(16.0f);
        label->SetWrapMode(molga::text::TextWrapMode::NoWrap);
        label->SetText(text);
        return label;
    }

    World& World() { return world_; }
    UIScrollSystem& System() { return UIScrollSystem::Get(); }
    CountingDiagnosticSink& Diagnostics() { return diagnostics_; }
    molga::ui::UILayoutSystem& Layout() { return layout_; }
    ScrollGroup& Primary() { return primary_; }

    UISnapshotPtr Snapshot() {
        REQUIRE(snapshot_ != nullptr);
        return snapshot_;
    }

    UISnapshotPtr Rebuild() {
        snapshot_ = layout_.Build(
            world_, kSurface, RawSize(kViewportRaw, kViewportRaw),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            runtime_.service, diagnostics_);
        return snapshot_;
    }

    UIRuntimeTargetIdentity ScrollTarget() const {
        return molga::ui::CaptureTarget(world_, *primary_.view);
    }
    UIRuntimeTargetIdentity TargetOf(const ScrollGroup& group) const {
        return molga::ui::CaptureTarget(world_, *group.view);
    }

    // 합법 구간의 두 끝과 뷰포트 길이를 저작된 사각형으로 세운다. 상태를
    // 손으로 채우지 않고 실제 배치가 그 값을 내게 하는 것이 요점이다.
    void SetVerticalExtentRaw(std::int32_t legalMin, std::int32_t legalMax,
                              std::int32_t viewportExtent) {
        REQUIRE(legalMax == 0);
        primary_.viewportRect->SetSizeDelta(
            {primary_.viewportRect->GetSizeDelta().x,
             RawToFloat(viewportExtent)});
        primary_.contentRect->SetSizeDelta(
            {primary_.contentRect->GetSizeDelta().x,
             RawToFloat(viewportExtent - legalMin)});
        Rebuild();
    }

    // 가로 합법 구간. 두 축이 함께 살아 있을 때만 "한 축이 실패하면 다른
    // 축의 반걸음도 발행되지 않는다"를 시험할 수 있다.
    void SetHorizontalExtentRaw(std::int32_t legalMin, std::int32_t legalMax,
                                std::int32_t viewportExtent) {
        REQUIRE(legalMax == 0);
        primary_.view->SetHorizontal(true);
        primary_.viewportRect->SetSizeDelta(
            {RawToFloat(viewportExtent),
             primary_.viewportRect->GetSizeDelta().y});
        primary_.contentRect->SetSizeDelta(
            {RawToFloat(viewportExtent - legalMin),
             primary_.contentRect->GetSizeDelta().y});
        Rebuild();
    }

    // ── 배율이 1이 아닌 캔버스 ──────────────────────────────────────────────
    // 기본 픽스처는 ConstantPixelSize라 배율이 정확히 1이고, 그러면
    // ScaleSubtree가 통째로 건너뛰어져 "변위는 배율 **뒤**"라는 계약이 아무것도
    // 재지 못한다(그 순서를 뒤바꾼 구현도 같은 값을 낸다). match=0이면 폭이
    // 기준이므로 배율은 뷰포트/참조폭 정확히 그 정수다 — 기하 보간의 부동소수
    // 오차가 끼지 않는다.
    void SetCanvasScaleTwo() {
        auto* canvas = canvasObject_->GetComponent<UICanvas>();
        REQUIRE(canvas != nullptr);
        canvas->SetScaleMode(UICanvasScaleMode::ScaleWithViewport);
        canvas->SetMatchWidthOrHeight(0.0f);
        canvas->SetReferenceResolution(
            {RawToFloat(kViewportRaw) / 2.0f, RawToFloat(kViewportRaw) / 2.0f});
        Rebuild();
    }

    void SetStateRaw(std::int32_t offsetX, std::int32_t velocityX,
                     std::int32_t offsetY, std::int32_t velocityY) {
        UIScrollState state;
        state.offset = FixedPoint{Fixed26_6::FromRaw(offsetX),
                                  Fixed26_6::FromRaw(offsetY)};
        state.velocity = FixedPoint{Fixed26_6::FromRaw(velocityX),
                                    Fixed26_6::FromRaw(velocityY)};
        System().SeedStateForTesting(ScrollTarget(), state);
    }

    void SetVerticalStateRaw(std::int32_t offset, std::int32_t velocity) {
        UIScrollState state;
        state.offset = FixedPoint{Fixed26_6::FromRaw(0),
                                  Fixed26_6::FromRaw(offset)};
        state.velocity = FixedPoint{Fixed26_6::FromRaw(0),
                                    Fixed26_6::FromRaw(velocity)};
        System().SeedStateForTesting(ScrollTarget(), state);
    }

    void SetRatesRaw(std::int32_t elasticity, std::int32_t deceleration) {
        primary_.view->SetElasticity(RawToFloat(elasticity));
        primary_.view->SetDecelerationRate(RawToFloat(deceleration));
        Rebuild();
    }

    molga::ui::UIScrollMutation ApplyInput(const UIScrollInput& input) {
        return System().ApplyInput(world_, *Snapshot(), ScrollTarget(), input,
                                   diagnostics_);
    }
    molga::ui::UIScrollMutation ApplyInputTo(const ScrollGroup& group,
                                             const UIScrollInput& input) {
        return System().ApplyInput(world_, *Snapshot(), TargetOf(group), input,
                                   diagnostics_);
    }

    bool StepRaw(std::int32_t deltaRaw) {
        return AdvanceTick(
            UIDeterministicTick{++nextTick_, Fixed26_6::FromRaw(deltaRaw)});
    }
    bool AdvanceTick(const UIDeterministicTick& tick) {
        nextTick_ = std::max(nextTick_, tick.tickIndex);
        return System()
            .AdvanceTick(world_, *Snapshot(), tick, diagnostics_)
            .changed;
    }
    void AdvanceTicks(const std::vector<UIDeterministicTick>& ticks) {
        for (const auto& tick : ticks) AdvanceTick(tick);
    }

    std::string StableStateJson() const {
        return UIScrollSystem::Get().StableStateJson(world_.Generation());
    }
    // 프로세스 순번이 들어간 완전한 식별자는 두 픽스처 사이에서 절대 같을 수
    // 없다. 방문 **순서**만이 비교 대상이므로 씬 오브젝트 id로 투영한다.
    std::vector<unsigned int> LastVisitedIdentitiesForTesting() const {
        std::vector<unsigned int> ids;
        for (const auto& identity :
             UIScrollSystem::Get().LastVisitedIdentitiesForTesting()) {
            if (identity.worldGeneration != world_.Generation()) continue;
            ids.push_back(identity.objectId);
        }
        return ids;
    }

    void AddThreeStatesInIdentityOrder() {
        auto groups = AddThreeGroups();
        for (auto& group : groups) TouchGroup(group);
    }
    void AddThreeStatesInReverseIdentityOrder() {
        auto groups = AddThreeGroups();
        for (auto it = groups.rbegin(); it != groups.rend(); ++it) {
            TouchGroup(*it);
        }
    }

private:
    std::vector<ScrollGroup> AddThreeGroups() {
        std::vector<ScrollGroup> groups;
        groups.push_back(AddScrollGroup(10, 11));
        groups.push_back(AddScrollGroup(20, 21));
        groups.push_back(AddScrollGroup(30, 31));
        Rebuild();
        return groups;
    }
    void TouchGroup(const ScrollGroup& group) {
        UIScrollInput input;
        input.axisValue = Fixed26_6::FromRaw(-96);
        input.axis = UIScrollAxis::Vertical;
        ApplyInputTo(group, input);
    }

    ScrollTextRuntime runtime_;
    ::World world_;
    GameObject* canvasObject_ = nullptr;
    ScrollGroup primary_;
    molga::ui::UILayoutSystem layout_;
    CountingDiagnosticSink diagnostics_;
    UISnapshotPtr snapshot_;
    std::uint64_t nextTick_ = 0;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Step 1a: 부호 있는 변위와 축 선택
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("scroll preserves signed deltas and axis selection") {
    UIScrollFixture f;
    const auto target = f.ScrollTarget();
    f.System().ApplyInput(f.World(), *f.Snapshot(), target,
                          {FixedPoint{Fixed26_6::FromRaw(0),
                                      Fixed26_6::FromRaw(0)},
                           Fixed26_6::FromRaw(-32), UIScrollAxis::Vertical},
                          f.Diagnostics());
    const auto* state = f.System().State(target);
    REQUIRE(state);
    CHECK(state->offset.x.Raw() == 0);    // 가로는 저작에서 꺼져 있다
    CHECK(state->offset.y.Raw() == -32);  // 정확한 부호 있는 축 값

    // 부호가 반대인 축 값은 반대 방향으로 간다. 대칭 입력만 시험하면 부호를
    // 뒤집은 구현도 통과한다.
    f.System().ApplyInput(f.World(), *f.Snapshot(), target,
                          {FixedPoint{Fixed26_6::FromRaw(0),
                                      Fixed26_6::FromRaw(0)},
                           Fixed26_6::FromRaw(8), UIScrollAxis::Vertical},
                          f.Diagnostics());
    CHECK(f.System().State(target)->offset.y.Raw() == -24);

    // 이름 붙은 축이 가로면 세로에는 아무것도 더해지지 않는다. 세로 값은 위의
    // -24 그대로여야 한다.
    f.System().ApplyInput(f.World(), *f.Snapshot(), target,
                          {FixedPoint{Fixed26_6::FromRaw(0),
                                      Fixed26_6::FromRaw(0)},
                           Fixed26_6::FromRaw(-64), UIScrollAxis::Horizontal},
                          f.Diagnostics());
    CHECK(f.System().State(target)->offset.y.Raw() == -24);
    CHECK(f.System().State(target)->offset.x.Raw() == 0);

    // 휠/포인터 변위와 축 값이 둘 다 0이 아니면 더해진다.
    f.System().ApplyInput(f.World(), *f.Snapshot(), target,
                          {FixedPoint{Fixed26_6::FromRaw(0),
                                      Fixed26_6::FromRaw(-8)},
                           Fixed26_6::FromRaw(-16), UIScrollAxis::Vertical},
                          f.Diagnostics());
    CHECK(f.System().State(target)->offset.y.Raw() == -48);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1b: 런타임 상태는 직렬화되지 않는다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("scroll runtime state is not serialized") {
    UIScrollView view;
    nlohmann::json encoded;
    view.Serialize(encoded);
    CHECK_FALSE(encoded.contains("offset"));
    CHECK_FALSE(encoded.contains("velocity"));

    // 저작 payload 전체를 못 박는다. 나중에 필드가 하나 늘어도 offset/velocity가
    // 아닌 것만 늘어야 한다.
    const std::vector<std::string> authored{
        "schemaVersion", "viewport",   "content",
        "horizontal",    "vertical",   "movement",
        "elasticity",    "inertia",    "decelerationRate",
        "scrollSensitivity", "initialNormalizedX", "initialNormalizedY"};
    for (const auto& key : authored) CHECK(encoded.contains(key));
    CHECK(encoded.size() == authored.size());
}

TEST_CASE("scrolling changes no authored byte and no revision") {
    UIScrollFixture f;
    nlohmann::json before;
    f.Primary().view->Serialize(before);
    const std::uint64_t revisionBefore = f.Primary().view->AuthoredRevision();

    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-128);
    REQUIRE(f.ApplyInput(input).changed);
    REQUIRE(f.System().State(f.ScrollTarget())->offset.y.Raw() == -128);
    REQUIRE(f.StepRaw(16));

    nlohmann::json after;
    f.Primary().view->Serialize(after);
    // 바이트 하나도 달라지지 않는다. 오프셋이 저작 상태로 새면 여기서 갈린다.
    CHECK(before.dump() == after.dump());
    // 저작 revision도 움직이지 않는다 — 움직이면 에디터가 dirty를 켜고,
    // 스크롤한 씬이 저장을 요구하게 된다.
    CHECK(f.Primary().view->AuthoredRevision() == revisionBefore);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1c/5f: 정확한 탄성 재귀 (손으로 계산한 픽스처)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("elastic fixed step uses the exact signed 26.6 recurrence") {
    UIScrollFixture f;
    f.SetVerticalExtentRaw(-640, 0, 640);
    f.SetVerticalStateRaw(64, 128);
    f.SetRatesRaw(/* elasticity */ 128, /* deceleration */ 64);
    REQUIRE(f.System()
                .AdvanceTick(f.World(), *f.Snapshot(),
                             UIDeterministicTick{1, Fixed26_6::FromRaw(16)},
                             f.Diagnostics())
                .changed);
    const auto* state = f.System().State(f.ScrollTarget());
    REQUIRE(state);
    // integrated=96, retained=48, nextVelocity=96, overscroll=96,
    // returnStep=32, correction=48, nextOffset=48.
    CHECK(state->offset.y.Raw() == 48);
    CHECK(state->velocity.y.Raw() == 96);

    // 한 걸음만 보면 모든 재귀가 첫 걸음에서 일치한다. 두 번째 걸음을 함께
    // 못 박아야 순서가 어긋난 구현이 드러난다.
    // offset=48, velocity=96: integrated = 48 + Q6(96,16)=24 -> 72
    // decayStep = Q6(64,16) = 16, retained = 48, nextVelocity = Q6(96,48) = 72
    // bounded = 72, legal = 0, overscroll = 72, returnStep = 32,
    // correction = Q6(72,32) = 36, nextOffset = 36.
    REQUIRE(f.System()
                .AdvanceTick(f.World(), *f.Snapshot(),
                             UIDeterministicTick{2, Fixed26_6::FromRaw(16)},
                             f.Diagnostics())
                .changed);
    CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() == 36);
    CHECK(f.System().State(f.ScrollTarget())->velocity.y.Raw() == 72);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1d: 안정된 식별자 순회
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("fixed step visits complete identities in stable order") {
    UIScrollFixture forward;
    UIScrollFixture reverse;
    forward.AddThreeStatesInIdentityOrder();
    reverse.AddThreeStatesInReverseIdentityOrder();
    forward.StepRaw(16);
    reverse.StepRaw(16);
    CHECK(forward.StableStateJson() == reverse.StableStateJson());
    CHECK(forward.LastVisitedIdentitiesForTesting() ==
          reverse.LastVisitedIdentitiesForTesting());
    // 순서 주장이 공허하지 않다는 것을 못 박는다: 실제로 네 대상이 방문된다
    // (기본 그룹 하나 + 셋). 하나뿐이면 어떤 순회 순서든 같은 목록을 낸다.
    const std::vector<unsigned int> expected{2, 10, 20, 30};
    CHECK(forward.LastVisitedIdentitiesForTesting() == expected);
    CHECK(reverse.LastVisitedIdentitiesForTesting() == expected);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 5c: 검증된 Q6 곱
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("MulQ6NearestAway rounds away from zero and refuses to wrap") {
    using molga::ui::MulQ6NearestAway;
    const auto q6 = [](std::int32_t a, std::int32_t b) {
        return MulQ6NearestAway(Fixed26_6::FromRaw(a), Fixed26_6::FromRaw(b));
    };
    CHECK(q6(128, 16)->Raw() == 32);
    CHECK(q6(96, 32)->Raw() == 48);
    // 정확한 절반은 0에서 **먼** 쪽으로 간다. 양쪽 부호를 모두 못 박는다 —
    // 짝수 반올림이나 0 방향 절단은 여기서만 갈린다.
    CHECK(q6(1, 32)->Raw() == 1);
    CHECK(q6(-1, 32)->Raw() == -1);
    CHECK(q6(3, 32)->Raw() == 2);
    CHECK(q6(-3, 32)->Raw() == -2);
    // 절반 아래는 0으로.
    CHECK(q6(1, 31)->Raw() == 0);
    CHECK(q6(-1, 31)->Raw() == 0);
    // 부호는 곱의 부호다.
    CHECK(q6(-128, 16)->Raw() == -32);
    CHECK(q6(128, -16)->Raw() == -32);
    CHECK(q6(-128, -16)->Raw() == 32);
    // int32 범위를 벗어나면 포화가 아니라 실패다.
    CHECK_FALSE(q6(2147483647, 2147483647).has_value());
    // INT32_MIN은 절댓값 연산이 int32에 남아 있으면 UB다. 승격된 구현만
    // 값을 낸다.
    REQUIRE(q6(-2147483647 - 1, 64).has_value());
    CHECK(q6(-2147483647 - 1, 64)->Raw() == -2147483647 - 1);
    CHECK_FALSE(q6(-2147483647 - 1, 65).has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1f: 결정적 tick 값 계약과 정규 리플레이
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("deterministic ticks reject zero, non-increasing, and oversized deltas") {
    using molga::ui::UIDeterministicTickBatchIsValid;
    using molga::ui::UIDeterministicTickIsValid;

    CHECK(UIDeterministicTickIsValid({1, Fixed26_6::FromRaw(1)}));
    CHECK(UIDeterministicTickIsValid({1, Fixed26_6::FromRaw(64)}));
    CHECK_FALSE(UIDeterministicTickIsValid({0, Fixed26_6::FromRaw(16)}));
    CHECK_FALSE(UIDeterministicTickIsValid({1, Fixed26_6::FromRaw(0)}));
    CHECK_FALSE(UIDeterministicTickIsValid({1, Fixed26_6::FromRaw(-16)}));
    CHECK_FALSE(UIDeterministicTickIsValid({1, Fixed26_6::FromRaw(65)}));

    // 묶음은 통째로 거절된다. 나쁜 원소 하나를 건너뛰고 나머지를 밟으면
    // 리플레이와 런타임이 서로 다른 걸음 수를 걷는다.
    CHECK(UIDeterministicTickBatchIsValid(
        {{1, Fixed26_6::FromRaw(16)}, {2, Fixed26_6::FromRaw(8)}}));
    CHECK_FALSE(UIDeterministicTickBatchIsValid(
        {{2, Fixed26_6::FromRaw(16)}, {2, Fixed26_6::FromRaw(8)}}));
    CHECK_FALSE(UIDeterministicTickBatchIsValid(
        {{3, Fixed26_6::FromRaw(16)}, {2, Fixed26_6::FromRaw(8)}}));
    CHECK_FALSE(UIDeterministicTickBatchIsValid(
        {{1, Fixed26_6::FromRaw(16)}, {2, Fixed26_6::FromRaw(65)}}));
    CHECK_FALSE(UIDeterministicTickBatchIsValid({}));
}

TEST_CASE("canonical tick traces carry exact integers and nothing else") {
    using molga::ui::DecodeCanonicalTicks;
    using molga::ui::EncodeCanonicalTicks;

    const std::vector<UIDeterministicTick> ticks{{81, Fixed26_6::FromRaw(16)},
                                                 {82, Fixed26_6::FromRaw(16)},
                                                 {83, Fixed26_6::FromRaw(8)}};
    const std::string encoded = EncodeCanonicalTicks(ticks);
    CHECK(encoded ==
          "[{\"tickIndex\":81,\"deltaSecondsRaw\":16},"
          "{\"tickIndex\":82,\"deltaSecondsRaw\":16},"
          "{\"tickIndex\":83,\"deltaSecondsRaw\":8}]");
    const auto decoded = DecodeCanonicalTicks(encoded);
    REQUIRE(decoded.size() == 3);
    for (std::size_t i = 0; i < decoded.size(); ++i) {
        CHECK(decoded[i] == ticks[i]);
    }
    // 부동소수 지속시간은 받지 않는다.
    CHECK(DecodeCanonicalTicks(
              "[{\"tickIndex\":1,\"deltaSecondsRaw\":16.5}]").empty());
    // 타임스탬프만 있는 항목도 받지 않는다.
    CHECK(DecodeCanonicalTicks("[{\"timestampMs\":17}]").empty());
    // 나쁜 원소 하나가 묶음 전체를 무너뜨린다.
    CHECK(DecodeCanonicalTicks(
              "[{\"tickIndex\":1,\"deltaSecondsRaw\":16},"
              "{\"tickIndex\":0,\"deltaSecondsRaw\":16}]").empty());
    CHECK(DecodeCanonicalTicks("not json").empty());
}

TEST_CASE("scroll consumes exact deterministic UI ticks") {
    UIScrollFixture runtime;
    UIScrollFixture gameView;
    UIScrollFixture replay;
    // 세 픽스처가 같은 자리에서 출발하도록 같은 입력을 먹인다. 출발점이 전부
    // 0이면 감속/탄성이 아무 일도 하지 않아 세 문서가 공허하게 같아진다.
    const UIScrollInput impulse{FixedPoint{Fixed26_6::FromRaw(0),
                                           Fixed26_6::FromRaw(0)},
                                Fixed26_6::FromRaw(-192),
                                UIScrollAxis::Vertical};
    REQUIRE(runtime.ApplyInput(impulse).changed);
    REQUIRE(gameView.ApplyInput(impulse).changed);
    REQUIRE(replay.ApplyInput(impulse).changed);

    const std::vector<UIDeterministicTick> ticks{{81, Fixed26_6::FromRaw(16)},
                                                 {82, Fixed26_6::FromRaw(16)},
                                                 {83, Fixed26_6::FromRaw(8)}};
    runtime.AdvanceTicks(ticks);
    gameView.AdvanceTicks(ticks);
    replay.AdvanceTicks(molga::ui::DecodeCanonicalTicks(
        molga::ui::EncodeCanonicalTicks(ticks)));

    // 독립적으로 만든 세 실행이 같은 문서를 낸다. 한 실행을 자기 자신과
    // 비교하는 것은 어떤 구현에서도 통과하므로 그렇게 쓰지 않는다.
    CHECK(runtime.StableStateJson() == gameView.StableStateJson());
    CHECK(runtime.StableStateJson() == replay.StableStateJson());
    // 그 문서가 실제로 움직인 값을 담는가. 전부 0인 문서끼리는 어떤 구현도
    // 일치한다.
    CHECK(runtime.StableStateJson().find("\"offsetYRaw\":0,") ==
          std::string::npos);
    // 이미 소비한 tick은 다시 소비되지 않는다.
    CHECK_FALSE(runtime.AdvanceTick(ticks.back()));
    CHECK(runtime.StableStateJson() == gameView.StableStateJson());
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1e: 정확한 스크롤 정책 표
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("authored initial normalized position seeds the offset exactly once") {
    UIScrollFixture f;
    f.Primary().view->SetInitialNormalizedY(0.5f);
    f.Rebuild();
    // 발견 pass가 저작된 시작 위치를 실제로 화면 쪽으로 옮긴다.
    // offset = max + RoundNearestAway((min - max) * normalized)
    //        = 0 + RoundNearestAway(-640 * 0.5) = -320.
    f.StepRaw(16);
    const auto* state = f.System().State(f.ScrollTarget());
    REQUIRE(state);
    CHECK(state->offset.y.Raw() == -320);

    // 두 번째 tick이 그 값을 다시 만들지 않는다. 매 프레임 다시 만들면
    // 저작 위치가 사용자의 스크롤을 되돌린다.
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-64);
    REQUIRE(f.ApplyInput(input).changed);
    CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() == -384);
    f.StepRaw(16);
    CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() != -320);
}

TEST_CASE("clamped movement stops at the legal extent and kills outward velocity") {
    UIScrollFixture f;
    f.Primary().view->SetMovement(UIScrollMovement::Clamped);
    f.Rebuild();
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-4096);  // 합법 최솟값보다 훨씬 아래
    REQUIRE(f.ApplyInput(input).changed);
    const auto* state = f.System().State(f.ScrollTarget());
    REQUIRE(state);
    CHECK(state->offset.y.Raw() == -640);   // 정확히 min(0, viewport - content)
    CHECK(state->velocity.y.Raw() == 0);    // 바깥으로 향하는 속도는 죽는다

    // 반대 방향도 같은 규칙이다. 한쪽만 시험하면 부호를 뒤집은 clamp가
    // 통과한다.
    UIScrollInput up;
    up.axisValue = Fixed26_6::FromRaw(4096);
    REQUIRE(f.ApplyInput(up).changed);
    CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() == 0);
    CHECK(f.System().State(f.ScrollTarget())->velocity.y.Raw() == 0);
}

TEST_CASE("elastic movement overshoots the legal extent and returns") {
    UIScrollFixture f;  // 기본이 Elastic이다
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-4096);
    REQUIRE(f.ApplyInput(input).changed);
    // 과주행 한계는 max(64, viewportExtent/2) = 320 이므로 -640 - 320.
    CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() == -960);
    // Clamped였다면 여기서 -640이었을 것이다. 두 모드가 같은 값을 내면
    // movement 분기를 지운 구현이 통과한다.
    CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() != -640);
}

TEST_CASE("authored sensitivity scales the signed delta with checked Q6 math") {
    UIScrollFixture f;
    f.Primary().view->SetScrollSensitivity(0.5f);
    f.Rebuild();
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-32);
    REQUIRE(f.ApplyInput(input).changed);
    CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() == -16);

    // 0.0 감도는 변위를 0으로 만든다(합법이다: 저작자가 그 축을 잠근 것이다).
    UIScrollFixture zero;
    zero.Primary().view->SetScrollSensitivity(0.0f);
    zero.Rebuild();
    CHECK_FALSE(zero.ApplyInput(input).changed);
    CHECK(zero.System().State(zero.ScrollTarget()) != nullptr);
    CHECK(zero.System().State(zero.ScrollTarget())->offset.y.Raw() == 0);
}

TEST_CASE("inertia off leaves velocity at zero and inertia on records the impulse") {
    UIScrollFixture on;
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-96);
    REQUIRE(on.ApplyInput(input).changed);
    // 속도 자극은 변위와 같은 값이다 — 벽시계 간격을 몰래 표본화하지 않는다.
    CHECK(on.System().State(on.ScrollTarget())->velocity.y.Raw() == -96);

    UIScrollFixture off;
    off.Primary().view->SetInertia(false);
    off.Rebuild();
    REQUIRE(off.ApplyInput(input).changed);
    CHECK(off.System().State(off.ScrollTarget())->offset.y.Raw() == -96);
    CHECK(off.System().State(off.ScrollTarget())->velocity.y.Raw() == 0);
    // 관성이 꺼져 있으면 tick이 오프셋을 더 옮기지 않는다.
    const std::int32_t before =
        off.System().State(off.ScrollTarget())->offset.y.Raw();
    off.StepRaw(16);
    CHECK(off.System().State(off.ScrollTarget())->offset.y.Raw() == before);
}

TEST_CASE("a disabled axis consumes no displacement it was given") {
    UIScrollFixture f;  // 가로는 저작에서 꺼져 있고 세로는 켜져 있다
    UIScrollInput input;
    // 꺼진 축에 **실제 변위를 준다**. 0을 주면 "소비하지 않는다"와 "소비하고
    // 버린다"가 같은 값을 낸다.
    input.logicalDelta = FixedPoint{Fixed26_6::FromRaw(-128),
                                    Fixed26_6::FromRaw(-64)};
    input.axisValue = Fixed26_6::FromRaw(-32);
    input.axis = UIScrollAxis::Horizontal;
    REQUIRE(f.ApplyInput(input).changed);
    const auto* state = f.System().State(f.ScrollTarget());
    REQUIRE(state);
    CHECK(state->offset.x.Raw() == 0);
    CHECK(state->velocity.x.Raw() == 0);
    // 같은 사건의 세로 절반은 그대로 적용된다(이름 붙은 축은 가로이므로
    // axisValue는 세로에 들어가지 않는다).
    CHECK(state->offset.y.Raw() == -64);

    // 가로를 켜면 같은 입력이 이제 소비된다 — 축 검사를 지운 구현과 켠
    // 구현이 여기서 갈린다.
    UIScrollFixture both;
    both.Primary().view->SetHorizontal(true);
    both.Rebuild();
    REQUIRE(both.ApplyInput(input).changed);
    CHECK(both.System().State(both.ScrollTarget())->offset.x.Raw() == -160);
}

TEST_CASE("an invalid viewport or content reference disables that scroll target") {
    UIScrollFixture f;
    f.Primary().view->SetContent(SceneObjectRef{});
    f.Rebuild();
    f.Diagnostics().Clear();
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-64);
    CHECK_FALSE(f.ApplyInput(input).changed);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
    // 상태는 만들어지되 어떤 축도 변위를 소비하지 않는다.
    const auto* state = f.System().State(f.ScrollTarget());
    REQUIRE(state);
    CHECK(state->offset.y.Raw() == 0);
    // 진단은 rate-limit된다. 프레임마다 흘리면 로그가 이 하나로 가득 찬다.
    f.ApplyInput(input);
    f.ApplyInput(input);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);

    // 다른 세대의 같은 숫자 id로 재지정되지 않는다: 참조가 이 월드에서
    // 살아 있는 오브젝트를 가리켜야만 축이 살아난다.
    f.Primary().view->SetContent(SceneObjectRef{9999});
    f.Rebuild();
    CHECK_FALSE(f.ApplyInput(input).changed);
}

TEST_CASE("a checked arithmetic failure fails closed and keeps the prior state") {
    UIScrollFixture f;
    f.SetVerticalExtentRaw(-640, 0, 640);
    // 속도를 26.6 범위 끝에 세운다. 한 걸음의 적분이 반드시 넘친다.
    f.SetVerticalStateRaw(2147483000, 2147483647);
    f.SetRatesRaw(/* elasticity */ 8, /* deceleration */ 0);
    f.Diagnostics().Clear();
    const auto target = f.ScrollTarget();
    CHECK_FALSE(f.StepRaw(64));
    const auto* state = f.System().State(target);
    REQUIRE(state);
    // 이전 상태가 한 비트도 바뀌지 않았다 — 부분/감긴 상태는 발행되지 않는다.
    CHECK(state->offset.y.Raw() == 2147483000);
    CHECK(state->velocity.y.Raw() == 2147483647);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);
    CHECK(f.System().IsFailClosed(target));

    // 실패 집합은 저작이 바뀔 때까지 유지된다.
    CHECK_FALSE(f.StepRaw(64));
    CHECK(state->offset.y.Raw() == 2147483000);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);

    // 저작 revision이 움직이면 다시 시도한다 — 영원히 닫으면 값을 고친
    // 저작자에게 남는 것은 죽은 스크롤 뷰뿐이다.
    f.Primary().view->SetScrollSensitivity(0.25f);
    f.SetVerticalStateRaw(-64, 0);
    CHECK(f.StepRaw(16) == false);  // 속도 0이면 값이 변하지 않는다
    CHECK_FALSE(f.System().IsFailClosed(target));
}

TEST_CASE("replacing the scroll component starts fresh and retires the old state") {
    UIScrollFixture f;
    const auto oldTarget = f.ScrollTarget();
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-128);
    REQUIRE(f.ApplyInput(input).changed);
    REQUIRE(f.System().State(oldTarget)->offset.y.Raw() == -128);

    f.Primary().viewport->RemoveComponent<UIScrollView>();
    auto* replacement = f.Primary().viewport->AddComponent<UIScrollView>();
    REQUIRE(replacement != nullptr);
    replacement->SetViewport(SceneObjectRef{2});
    replacement->SetContent(SceneObjectRef{3});
    replacement->SetHorizontal(false);
    replacement->SetVertical(true);
    f.Primary().view = replacement;
    f.Rebuild();

    const auto newTarget = f.ScrollTarget();
    CHECK(newTarget != oldTarget);
    // 오브젝트 id와 컴포넌트 타입은 같다. 그 둘만 보는 키는 죽은 컴포넌트의
    // 오프셋을 새 컴포넌트에 물려준다.
    CHECK(newTarget.objectId == oldTarget.objectId);
    CHECK(newTarget.componentRuntimeTypeId == oldTarget.componentRuntimeTypeId);
    CHECK(newTarget.componentInstanceId != oldTarget.componentInstanceId);
    CHECK(f.System().State(newTarget) == nullptr);

    f.StepRaw(16);
    // 죽은 식별자의 상태는 회수되고, 새 식별자는 저작된 시작 위치에서 출발한다.
    CHECK(f.System().State(oldTarget) == nullptr);
    REQUIRE(f.System().State(newTarget) != nullptr);
    CHECK(f.System().State(newTarget)->offset.y.Raw() == 0);
}

TEST_CASE("retiring a world releases its scroll state through the production notifier") {
    // 프로덕션 배선을 세운다. UISystem::Get()이 처음 만들어질 때
    // molga::ui::SetUIWorldReleaseHandler가 설치되고, World가 은퇴할 때 부르는
    // 이름은 그 핸들러 하나뿐이다. 여기서 재는 것은 erase가 옳은가가 아니라
    // **그 erase에 도달하는가**이다 — Task 10.2에서 옳고 도달하지 않는 함수
    // 하나가 모든 은퇴한 세대를 흘렸다.
    UISystem::Get();
    std::uint64_t generation = 0;
    {
        UIScrollFixture f;
        generation = f.World().Generation();
        UIScrollInput input;
        input.axisValue = Fixed26_6::FromRaw(-64);
        REQUIRE(f.ApplyInput(input).changed);
        REQUIRE(UIScrollSystem::Get().StateCountForWorld(generation) == 1);
        REQUIRE(f.StepRaw(16));
        REQUIRE(UIScrollSystem::Get().LastTickIndexForWorld(generation) == 1);
    }
    CHECK(UIScrollSystem::Get().StateCountForWorld(generation) == 0);
    CHECK(UIScrollSystem::Get().LastTickIndexForWorld(generation) == 0);
}

TEST_CASE("a world generation replaced in place drops the old scroll state") {
    UISystem::Get();
    UIScrollFixture f;
    const std::uint64_t generation = f.World().Generation();
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-64);
    REQUIRE(f.ApplyInput(input).changed);
    REQUIRE(UIScrollSystem::Get().StateCountForWorld(generation) == 1);
    // Clear는 새 세대를 발행하고 옛 세대를 은퇴시킨다.
    f.World().Clear();
    CHECK(f.World().Generation() != generation);
    CHECK(UIScrollSystem::Get().StateCountForWorld(generation) == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6a/6b/6c: 배치·캐시·더럽힘
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("a runtime offset translates the content subtree and not the viewport") {
    UIScrollFixture f;
    const auto before = f.Snapshot();
    const auto* viewportBefore = FindNodeByObject(*before, 2);
    const auto* contentBefore = FindNodeByObject(*before, 3);
    REQUIRE(viewportBefore);
    REQUIRE(contentBefore);
    CHECK(contentBefore->logicalRect.y.Raw() == 0);

    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-128);
    const auto mutation = f.ApplyInput(input);
    // Step 6c: 오프셋이 바뀌면 배치와 렌더는 더러워지고 셰이핑은 그대로다.
    CHECK(mutation.changed);
    CHECK(mutation.arrangementDirty);
    CHECK(mutation.renderDirty);
    CHECK_FALSE(mutation.textShapeDirty);

    const auto after = f.Rebuild();
    REQUIRE(after);
    CHECK(after != before);  // 오프셋 변화는 게시된 스냅샷을 바꾼다
    const auto* viewportAfter = FindNodeByObject(*after, 2);
    const auto* contentAfter = FindNodeByObject(*after, 3);
    REQUIRE(viewportAfter);
    REQUIRE(contentAfter);
    // 내용만 움직인다.
    CHECK(contentAfter->logicalRect.y.Raw() == -128);
    CHECK(contentAfter->logicalRect.x.Raw() == contentBefore->logicalRect.x.Raw());
    CHECK(contentAfter->logicalRect.height.Raw() ==
          contentBefore->logicalRect.height.Raw());
    // 뷰포트는 제자리다 — 함께 움직이면 스크롤해도 아무것도 잘리지 않는다.
    CHECK(viewportAfter->logicalRect.y.Raw() == viewportBefore->logicalRect.y.Raw());
    // 클립은 뷰포트 그대로이고 렌더와 hit이 같은 값을 본다.
    REQUIRE(contentAfter->logicalClip.has_value());
    CHECK(contentAfter->logicalClip->y.Raw() == 0);
    CHECK(contentAfter->logicalClip->height.Raw() == kViewportRaw);
}

TEST_CASE("scroll displacement is a collision-checked geometry cache input") {
    UIScrollFixture f;
    UIScrollInput down;
    down.axisValue = Fixed26_6::FromRaw(-64);
    UIScrollInput up;
    up.axisValue = Fixed26_6::FromRaw(64);

    // 상태를 먼저 만든다. 상태가 아예 없는 첫 빌드는 벡터가 비어 있어서
    // "오프셋이 0으로 되돌아왔다"와 구별되지 않고, 그 차이를 지우면 이
    // 케이스가 벡터의 존재만 재게 된다.
    REQUIRE(f.ApplyInput(down).changed);
    REQUIRE(f.Rebuild() != nullptr);
    const auto firstKey = f.Layout().LastGeometryKey();
    REQUIRE(firstKey.has_value());
    // 원래 필드가 키에 남아 있다(해시 하나가 아니다).
    REQUIRE(firstKey->scrollDisplacements.size() == 1);
    CHECK(firstKey->scrollDisplacements[0].offsetYRaw == -64);
    CHECK(firstKey->scrollDisplacements[0].offsetXRaw == 0);
    CHECK(firstKey->scrollDisplacements[0].scrollTarget == f.ScrollTarget());
    CHECK(firstKey->scrollDisplacements[0].viewport.objectId == 2);
    CHECK(firstKey->scrollDisplacements[0].content.objectId == 3);

    REQUIRE(f.ApplyInput(up).changed);
    REQUIRE(f.Rebuild() != nullptr);
    const auto secondKey = f.Layout().LastGeometryKey();
    REQUIRE(secondKey.has_value());
    CHECK(secondKey->scrollDisplacements[0].offsetYRaw == 0);
    // 두 키는 다르다. 같으면 캐시가 스크롤 전의 기하를 돌려준다.
    CHECK(*firstKey != *secondKey);
    // 해시도 갈라진다. 같으면 조회가 선형 탐색이 되고, 그것은 진단 없이
    // 성능으로만 드러난다.
    CHECK(molga::ui::HashUILayoutGeometryCacheKey(*firstKey) !=
          molga::ui::HashUILayoutGeometryCacheKey(*secondKey));

    // 오프셋을 되돌리면 처음의 기하 항목이 그대로 재사용된다.
    REQUIRE(f.ApplyInput(down).changed);
    REQUIRE(f.Rebuild() != nullptr);
    const auto thirdKey = f.Layout().LastGeometryKey();
    REQUIRE(thirdKey.has_value());
    CHECK(*thirdKey == *firstKey);
    CHECK(f.Layout().GeometryCacheContains(*firstKey));
}

TEST_CASE("an unchanged offset returns the identical snapshot") {
    UIScrollFixture f;
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-64);
    REQUIRE(f.ApplyInput(input).changed);
    const auto warm = f.Rebuild();
    REQUIRE(warm);
    // 아무것도 바뀌지 않은 프레임은 같은 인스턴스를 돌려준다.
    CHECK(f.Rebuild() == warm);
    CHECK(f.Rebuild() == warm);
    // 값이 같은 입력은 오프셋을 움직이지 않으므로 스냅샷도 그대로다.
    UIScrollInput zero;
    CHECK_FALSE(f.ApplyInput(zero).changed);
    CHECK(f.Rebuild() == warm);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.2 Step 2e의 남은 절반(스크롤). blink는 Task 14의 것이다.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("4096 scroll semantic misses grow neither full-snapshot nor geometry storage") {
    UIScrollFixture f;
    const auto clock = molga::ui::UIRuntimeInvalidationClock::Current();
    molga::ui::UISnapshotWorldDeviceSlotKey slot;
    slot.worldGeneration = f.World().Generation();
    slot.deviceGeneration = clock.deviceGeneration;

    UIScrollInput down;
    down.axisValue = Fixed26_6::FromRaw(-1);
    UIScrollInput up;
    up.axisValue = Fixed26_6::FromRaw(1);
    std::size_t settledFullSlots = 0;
    std::size_t settledGeometry = 0;
    UISnapshotPtr previous = f.Snapshot();
    for (int i = 0; i < 4096; ++i) {
        // 매 반복이 서로 다른 오프셋이므로 매번 의미 미스다. 같은 값을 반복
        // 발행하면 빠른 경로가 잡아 이 루프가 아무것도 재지 못한다.
        REQUIRE(f.ApplyInput((i % 2 == 0) ? down : up).changed);
        const auto built = f.Rebuild();
        REQUIRE(built != nullptr);
        // 매 반복이 정말 새 스냅샷을 낳는가. 같은 인스턴스가 돌아온다면 이
        // 루프는 4096번의 미스가 아니라 4096번의 빠른 경로 적중이고, 그러면
        // 상한 주장이 아무것도 재지 않는다.
        REQUIRE(built != previous);
        previous = built;
        if (i == 1) {
            settledFullSlots =
                f.Layout().FullSnapshotCacheEntryCountForWorldDevice(slot);
            settledGeometry =
                f.Layout().GeometryCacheEntryCountForWorld(slot.worldGeneration);
        }
    }
    // 이 루프가 만든 **증가분**을 잰다. 절대 수를 재면 이 프로세스가 여기까지
    // 오면서 남긴 역사를 재게 된다(Task 11.2가 정확히 그 함정을 밟았다).
    CHECK(f.Layout().FullSnapshotCacheEntryCountForWorldDevice(slot) ==
          settledFullSlots);
    CHECK(settledFullSlots == 1);
    CHECK(f.Layout().GeometryCacheEntryCountForWorld(slot.worldGeneration) <=
          molga::ui::kUILayoutGeometryEntriesPerWorld);
    CHECK(f.Layout().GeometryCacheEntryCountForWorld(slot.worldGeneration) >=
          settledGeometry);
    // 스크롤 상태 자체도 자라지 않는다: 식별자 하나에 항목 하나.
    CHECK(UIScrollSystem::Get().StateCountForWorld(slot.worldGeneration) == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6c: 중첩 마스크 parity — 렌더와 hit이 같은 클립을 본다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("scrolled content keeps render and hit clip parity under a nested mask") {
    UIScrollFixture f;
    // content 안쪽 아래에 놓인 버튼. 스크롤 전에는 뷰포트 밖이고, 위로
    // 스크롤하면 안으로 들어온다 — 그 두 상태가 실제로 다르지 않으면
    // 클립 계약이 아무것도 재지 않는다.
    GameObject* deep = f.AddContentChild(40, RawToFloat(kViewportRaw + 64),
                                         RawToFloat(kViewportRaw),
                                         RawToFloat(64));
    auto* button = deep->AddComponent<UIButton>();
    REQUIRE(button != nullptr);
    const auto before = f.Rebuild();
    REQUIRE(before);
    // 뷰포트 아래에 있으므로 렌더도 hit도 없다.
    CHECK(FindRenderItem(*before, 40) == nullptr);
    CHECK(FindHitTarget(*before, 40) == nullptr);

    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-(kViewportRaw + 64));
    REQUIRE(f.ApplyInput(input).changed);
    const auto after = f.Rebuild();
    REQUIRE(after);
    const auto* item = FindRenderItem(*after, 40);
    const auto* hit = FindHitTarget(*after, 40);
    REQUIRE(item);
    REQUIRE(hit);
    // 같은 사각형, 같은 클립. 렌더와 hit이 갈리면 보이지 않는 버튼이 눌린다.
    CHECK(item->logicalRect == hit->logicalRect);
    REQUIRE(item->logicalClip.has_value());
    REQUIRE(hit->logicalClip.has_value());
    CHECK(*item->logicalClip == *hit->logicalClip);
    // 그리고 실제로 뷰포트 안으로 들어왔다.
    CHECK(item->logicalRect.y.Raw() == 0);
    CHECK(item->logicalClip->height.Raw() == kViewportRaw);
    // hit 레코드는 조상 스크롤 대상을 안쪽에서 바깥쪽 순서로 얼려 둔다.
    REQUIRE(hit->scrollTargets.size() == 1);
    CHECK(hit->scrollTargets[0].canonicalTarget.sceneObjectId == 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.2 M33: RateLimitedPayloadSink의 절반
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("the label payload sink reports only what it can remember") {
    UIScrollFixture f;
    // 256칸 상한보다 많은 **서로 다른** 라벨 진단을 만든다. 라벨마다 오브젝트
    // id가 다르므로 rate-limit 키도 전부 다르다. 유효하지 않은 UTF-8은 배치를
    // 성공시키면서 진단을 내는 실패라서, 이 사실들이 정말 sink를 지난다.
    constexpr int kLabels = 300;
    for (int i = 0; i < kLabels; ++i) {
        auto* label = f.AddContentLabel(static_cast<unsigned int>(1000 + i),
                                        "scroll", 0.0f);
        // 라벨마다 존재하지 않는 **서로 다른** family를 지목한다. 배치는
        // 절차적 두부로 살아남고 진단 하나를 낸다. 오브젝트 id가 전부 다르므로
        // rate-limit 키도 전부 다르다.
        label->SetFontFamilyGuid("missing-family-" + std::to_string(i));
    }
    f.Diagnostics().Clear();
    REQUIRE(f.Rebuild() != nullptr);
    // 라벨 하나가 두 사실을 낸다(해석 실패와 그 결과의 두부). 세는 것은
    // 코드별 수가 아니라 **sink를 지난 서로 다른 사실의 수**다 — 그것이
    // limiter가 세는 것과 같은 수이기 때문이다.
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::FontFamilyInvalid) > 0);
    // 상한이 진단을 **삼키는** 순간에 rate limit이 사라지면 안 된다: 기억할
    // 수 없는 것은 보고하지도 않는다. 정확히 256이다 — 600이면 기억하지 않고
    // 보고하는 구현이고, 그 구현은 다음 프레임부터 344개를 영원히 흘린다.
    CHECK(f.Diagnostics().Total() == 256);

    f.Diagnostics().Clear();
    // 두 번째 빌드는 반드시 기하를 다시 짓는다(뷰포트 높이를 실제로 바꾼다).
    // warm 사실도 같은 sink를 지나므로 이 0은 캐시가 아니라 limiter가 만든
    // 값이다.
    f.Primary().viewportRect->SetSizeDelta(
        {RawToFloat(kViewportRaw), RawToFloat(kViewportRaw - 64)});
    REQUIRE(f.Rebuild() != nullptr);
    CHECK(f.Diagnostics().Total() == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.2 인계 3: ToLogicalPoint는 검증된 유리수다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("ToLogicalPoint scales with checked 26.6 arithmetic") {
    molga::ui::UIPhysicalTransform transform;
    // 논리 3 : 물리 1. 정확한 유리수가 아니면 축마다 다른 값이 나온다.
    transform.logicalViewport = molga::FixedRect{
        Fixed26_6::FromRaw(0), Fixed26_6::FromRaw(0),
        Fixed26_6::FromRaw(300 * 64), Fixed26_6::FromRaw(150 * 64)};
    transform.physicalViewport = molga::PixelRectU32{0, 0, 100, 50};
    transform.deviceGeneration = 2;

    // 기대값은 구현을 부르지 않고 손으로 세운 정수 유리수다.
    // pixel 37 -> 37 * (300*64) / 100 = 7104 raw.
    const auto point = transform.ToLogicalPoint(37.0, 21.0);
    REQUIRE(point.has_value());
    CHECK(point->x.Raw() == 7104);
    CHECK(point->y.Raw() == 21 * (150 * 64) / 50);

    // 0에서 먼 쪽 반올림. pixel 1/128 은 raw 0.5 -> 1 로 간다.
    molga::ui::UIPhysicalTransform unit;
    unit.logicalViewport = molga::FixedRect{
        Fixed26_6::FromRaw(0), Fixed26_6::FromRaw(0),
        Fixed26_6::FromRaw(100 * 64), Fixed26_6::FromRaw(50 * 64)};
    unit.physicalViewport = molga::PixelRectU32{0, 0, 100, 50};
    unit.deviceGeneration = 2;
    const auto half = unit.ToLogicalPoint(1.0 / 128.0, 1.0 / 128.0);
    REQUIRE(half.has_value());
    CHECK(half->x.Raw() == 1);
    CHECK(half->y.Raw() == 1);

    // 반열린 경계는 그대로다 — 이 변경은 안쪽 점의 반올림 경로만 바꾼다.
    const auto first = unit.ToLogicalPoint(0.0, 0.0);
    REQUIRE(first.has_value());
    CHECK(first->x.Raw() == 0);
    const auto last = unit.ToLogicalPoint(99.0, 49.0);
    REQUIRE(last.has_value());
    CHECK(last->x.Raw() == 99 * 64);
    CHECK(last->y.Raw() == 49 * 64);
    CHECK_FALSE(unit.ToLogicalPoint(100.0, 25.0));
    CHECK_FALSE(unit.ToLogicalPoint(25.0, 50.0));

    // 1/64 픽셀보다 미세한 입력도 살아남는다. 분자를 26.6으로 잡았다면
    // 여기서 0이 되어, 검증된 산술로 바꾸는 대가로 정밀도를 잃었을 것이다.
    molga::ui::UIPhysicalTransform dense;
    dense.logicalViewport = molga::FixedRect{
        Fixed26_6::FromRaw(0), Fixed26_6::FromRaw(0),
        Fixed26_6::FromRaw(300 * 64), Fixed26_6::FromRaw(300 * 64)};
    dense.physicalViewport = molga::PixelRectU32{0, 0, 100, 100};
    dense.deviceGeneration = 2;
    // 픽셀당 192 raw이므로 0.005픽셀은 0.96 raw -> 0에서 먼 쪽으로 1이다.
    const auto tiny = dense.ToLogicalPoint(0.005, 0.005);
    REQUIRE(tiny.has_value());
    CHECK(tiny->x.Raw() == 1);
    CHECK(tiny->y.Raw() == 1);

    // 검증된 분자에 들어가지 않는 물리 뷰포트는 값을 지어내는 대신 실패를
    // 보고한다. double 곱셈은 여기서 조용히 값을 냈다. hit-test에서 이 실패는
    // "그 점은 이 표면 밖"이므로 fail-closed다.
    molga::ui::UIPhysicalTransform enormous;
    enormous.logicalViewport = molga::FixedRect{
        Fixed26_6::FromRaw(0), Fixed26_6::FromRaw(0),
        Fixed26_6::FromRaw(100 * 64), Fixed26_6::FromRaw(100 * 64)};
    enormous.physicalViewport = molga::PixelRectU32{0, 0, 65536, 65536};
    enormous.deviceGeneration = 2;
    // 32768픽셀까지는 그대로 답한다.
    CHECK(enormous.ToLogicalPoint(1000.0, 1000.0).has_value());
    CHECK_FALSE(enormous.ToLogicalPoint(40000.0, 40000.0));
}

TEST_CASE("a failing axis never publishes the other axis's half step") {
    UIScrollFixture f;
    // 두 축이 모두 살아 있어야 부분 발행이 보인다. 한 축만 도는 픽스처에서는
    // "전부 되돌린다"와 "실패한 축만 그대로 둔다"가 같은 값을 낸다.
    f.SetVerticalExtentRaw(-640, 0, 640);
    f.SetHorizontalExtentRaw(-640, 0, 640);
    f.SetRatesRaw(/* elasticity */ 8, /* deceleration */ 0);
    // 가로는 정상적으로 -32 -> 28로 움직이고, 세로는 적분에서 넘친다.
    f.SetStateRaw(-32, 64, 2147483000, 2147483647);
    f.Diagnostics().Clear();
    const auto target = f.ScrollTarget();
    CHECK_FALSE(f.StepRaw(64));
    const auto* state = f.System().State(target);
    REQUIRE(state);
    CHECK(state->offset.x.Raw() == -32);
    CHECK(state->velocity.x.Raw() == 64);
    CHECK(state->offset.y.Raw() == 2147483000);
    CHECK(state->velocity.y.Raw() == 2147483647);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);
    CHECK(f.System().IsFailClosed(target));
}

TEST_CASE("a tick-driven offset change republishes the snapshot") {
    UIScrollFixture f;
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-256);
    REQUIRE(f.ApplyInput(input).changed);
    const auto before = f.Rebuild();
    REQUIRE(before);
    const auto* contentBefore = FindNodeByObject(*before, 3);
    REQUIRE(contentBefore);
    CHECK(contentBefore->logicalRect.y.Raw() == -256);

    // 관성이 tick에서 오프셋을 더 옮긴다. 그 변화도 반드시 새 스냅샷이 된다 —
    // 스크롤 변위 세대를 올리지 않으면 빠른 경로의 도장이 그대로라서 옛
    // 스냅샷이 그대로 돌아오고, 화면은 tick 하나만큼 뒤처진 채로 굳는다.
    const auto step = f.System().AdvanceTick(
        f.World(), *before, UIDeterministicTick{1, Fixed26_6::FromRaw(16)},
        f.Diagnostics());
    CHECK(step.changed);
    CHECK(step.arrangementDirty);
    CHECK(step.renderDirty);
    CHECK_FALSE(step.textShapeDirty);
    const auto* state = f.System().State(f.ScrollTarget());
    REQUIRE(state);
    // 감속 0.875/s, delta 0.25s -> retained 50/64, elasticity는 합법 구간
    // 안이라 걸리지 않는다.
    CHECK(state->offset.y.Raw() == -320);
    CHECK(state->velocity.y.Raw() == -200);

    const auto after = f.Rebuild();
    REQUIRE(after);
    CHECK(after != before);
    const auto* contentAfter = FindNodeByObject(*after, 3);
    REQUIRE(contentAfter);
    CHECK(contentAfter->logicalRect.y.Raw() == -320);
}

TEST_CASE("scroll diagnostics report only what they can remember") {
    UIScrollFixture f;
    // 상한(256)보다 많은 **서로 다른** 스크롤 사실. 스크롤 뷰마다 오브젝트
    // id가 다르므로 rate-limit 키도 전부 다르다.
    constexpr unsigned int kBroken = 300;
    for (unsigned int i = 0; i < kBroken; ++i) {
        ScrollGroup broken = f.AddScrollGroup(2000 + i * 2, 2001 + i * 2);
        broken.view->SetContent(SceneObjectRef{});
    }
    REQUIRE(f.Rebuild() != nullptr);
    f.Diagnostics().Clear();
    f.StepRaw(16);
    // 300이면 상한이 기억만 멈추고 보고는 멈추지 않은 것이고, 그 구현은
    // 다음 프레임부터 44개를 영원히 흘린다.
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 256);
    // 억제 사실 자체는 한 번 알린다.
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);

    f.Diagnostics().Clear();
    f.StepRaw(16);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 0);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6b: 소진된 스크롤 변위 세대는 이전 오프셋을 그대로 남긴다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("an exhausted scroll displacement generation keeps the prior offset") {
    UIScrollFixture f;
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-64);
    REQUIRE(f.ApplyInput(input).changed);
    REQUIRE(f.System().State(f.ScrollTarget())->offset.y.Raw() == -64);
    f.Diagnostics().Clear();
    {
        molga::ui::ScopedUIRuntimeGenerationForTesting exhausted(
            molga::ui::UIRuntimeGenerationKind::ScrollDisplacement,
            std::numeric_limits<std::uint64_t>::max());
        UIScrollInput more;
        more.axisValue = Fixed26_6::FromRaw(-64);
        // 발행 전에 세대를 못 올렸으면 발행하지 않는다. 올리지 않고 발행하면
        // warm 빠른 경로의 도장이 새 오프셋에 옛 이름을 붙인다.
        CHECK_FALSE(f.ApplyInput(more).changed);
        CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() == -64);
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);
        bool blocker = false;
        for (const auto& record : f.Diagnostics().Records()) {
            if (record.severity == molga::text::TextSeverity::Blocker) {
                blocker = true;
            }
        }
        CHECK(blocker);
        // 같은 사실은 프레임마다 다시 흐르지 않는다.
        f.ApplyInput(more);
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);
    }
    // 소진이 풀리면 다시 발행된다 — 영구 정지가 아니다.
    UIScrollInput again;
    again.axisValue = Fixed26_6::FromRaw(-64);
    CHECK(f.ApplyInput(again).changed);
    CHECK(f.System().State(f.ScrollTarget())->offset.y.Raw() == -128);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.3 인계: "이 스냅샷에 없다"는 "참조가 무너졌다"가 아니다
//
// FindNode가 빗나가는 세 이유를 하나로 뭉갠 결함 하나에서 세 증상이 나왔다.
// 아래 세 케이스가 그 셋을 각각 못 박는다.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("elastic overscroll never strands the content outside its own clip") {
    UIScrollFixture f;
    // 내용이 뷰포트의 **절반보다 짧다**. 그러면 과주행 한계(max(64, 640/2)=320)가
    // 내용 길이(256)보다 커서, 한 번의 플링이 내용을 뷰포트 사각형 **밖으로**
    // 통째로 밀어낼 수 있다. 픽스처의 기본값(내용이 뷰포트의 2배)에서는 이
    // 상태가 구조적으로 도달 불가능하고, 그 도달 불가능성이 이 결함을 가렸다.
    f.Primary().contentRect->SetSizeDelta(
        {RawToFloat(kViewportRaw), RawToFloat(256)});
    // 관성은 끈다. 저작된 감속(0.875/s)의 감쇠는 |velocity| == 2에서 고정점을
    // 가지므로(MulQ6가 0에서 먼 쪽으로 반올림한다) 속도가 남아 있으면 이
    // 재귀는 0이 아니라 그 속도가 만드는 평형점에 선다. 그것은 승인된 재귀
    // 자체의 성질이고 이 케이스가 재려는 것이 아니다.
    f.Primary().view->SetInertia(false);
    // 탄성 복귀율을 1.0/s로 둔다. 저작 기본값(0.125/s)에서는 한 걸음의 보정이
    // |offset| < 16 에서 0으로 반올림되어 재귀가 합법 구간 밖 15 raw에 선다 —
    // 이 역시 승인된 재귀의 성질이지 이 케이스의 대상이 아니다.
    f.Primary().view->SetElasticity(1.0f);
    f.Rebuild();

    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-4096);
    REQUIRE(f.ApplyInput(input).changed);
    const auto target = f.ScrollTarget();
    // 합법 구간은 [0,0]이고 과주행 한계가 320이므로 정확히 -320이다.
    REQUIRE(f.System().State(target)->offset.y.Raw() == -320);

    f.Diagnostics().Clear();
    const auto stranded = f.Rebuild();
    REQUIRE(stranded);
    // 빈 클립 교집합은 렌더와 hit 레코드를 **함께** 제거한다. 그것이 제약이고
    // 옳다 — 그래서 스크롤은 그 사실에서 스스로 회복할 수 있어야 한다.
    CHECK(FindNodeByObject(*stranded, 3) == nullptr);

    f.StepRaw(16);
    // 온전한 참조를 깨진 참조로 보고하지 않는다. 이 진단은 저작자가 고칠 수
    // 있는 것이 없는 사실이고, 진단 예산 한 칸을 그 월드 내내 먹는다.
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 0);
    // 그리고 축이 얼지 않았다. 얼면 오프셋이 다시는 바뀌지 않고, 바뀌지 않으면
    // 노드가 다시 나타나지 않는다 — 흡수 상태다.
    CHECK(f.System().State(target)->offset.y.Raw() > -320);

    for (int i = 0; i < 200; ++i) f.StepRaw(16);
    CHECK(f.System().State(target)->offset.y.Raw() == 0);
    const auto recovered = f.Rebuild();
    REQUIRE(recovered);
    const auto* content = FindNodeByObject(*recovered, 3);
    REQUIRE(content);  // 내용이 클립 안으로 돌아와 다시 발행된다
    CHECK(content->logicalRect.y.Raw() == 0);
}

TEST_CASE("an authored start position survives a tick taken before its subtree is built") {
    UIScrollFixture f;
    f.Primary().view->SetInitialNormalizedY(0.5f);
    // 패널이 꺼진 프레임에 orchestrator의 첫 tick이 온다. GatherSubtree는
    // 비활성 오브젝트를 건너뛰지만 SceneObjectRef::Resolve는 그대로 돌려주므로
    // 두 식별자는 유효하고 두 FindNode는 모두 빗나간다.
    f.Primary().viewport->SetActive(false);
    f.Rebuild();
    f.StepRaw(16);
    const auto target = f.ScrollTarget();
    // 잴 수 없는 프레임에는 상태를 만들지 않는다. 만들면 그 한 번뿐인 seed가
    // 0으로 소진되고, 다음부터는 "이미 있는 항목"이라 저작된 시작 위치가
    // 다시는 적용되지 않는다.
    CHECK(f.System().State(target) == nullptr);

    f.Primary().viewport->SetActive(true);
    f.Rebuild();
    f.StepRaw(16);
    REQUIRE(f.System().State(target) != nullptr);
    // offset = max + RoundNearestAway((min - max) * normalized)
    //        = 0 + RoundNearestAway(-640 * 0.5) = -320.
    CHECK(f.System().State(target)->offset.y.Raw() == -320);
}

TEST_CASE("a hidden scroll view is not a broken reference") {
    UIScrollFixture f;
    f.Primary().viewport->SetActive(false);
    f.Rebuild();
    f.Diagnostics().Clear();
    f.StepRaw(16);
    // 숨긴 탭/풀링된 패널은 저작이 고른 정상 상태다. Error 하나를 내면 그
    // 사실이 월드 세대 내내 256칸 예산의 한 칸을 붙들고, 숨긴 패널이 256개면
    // 진짜로 깨진 참조는 억제 통지 하나로만 보인다.
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 0);
    CHECK(f.Diagnostics().Total() == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 5f: 살아 있는 대상의 상태는 회수되지 않는다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("a live scroll view keeps its offset when it is disabled or misauthored") {
    UIScrollFixture f;
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-128);
    REQUIRE(f.ApplyInput(input).changed);
    const auto target = f.ScrollTarget();
    REQUIRE(f.System().State(target)->offset.y.Raw() == -128);

    // (a) 교체가 아니라 잠깐 끈 것이다. 컴포넌트는 그 자리에 살아 있다.
    f.Primary().view->SetEnabled(false);
    f.Diagnostics().Clear();
    f.StepRaw(16);
    REQUIRE(f.System().State(target) != nullptr);
    CHECK(f.System().State(target)->offset.y.Raw() == -128);
    // 꺼 둔 것 자체는 보고할 사실이 아니다 — 보고하면 예산 한 칸이 영구히 간다.
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 0);

    f.Primary().view->SetEnabled(true);
    f.StepRaw(16);
    // 다시 켠 프레임에 목록이 맨 위로 튀지 않는다. 상태를 지웠다면 발견 pass가
    // initialNormalizedY(기본 0)에서 다시 만들어 정확히 0이 되었을 것이다.
    REQUIRE(f.System().State(target) != nullptr);
    CHECK(f.System().State(target)->offset.y.Raw() == -160);

    // (b) 살아 있는 컴포넌트의 저작된 rate가 26.6에 담기지 않는다. 저작 API는
    // 유한하고 음이 아닌 값만 요구하므로 이 값은 **저작 가능하다**. Step 5f는
    // 검증 실패가 "이전 상태를 그대로 둔다"고 말한다 — 지우는 것은 그 반대다.
    f.Primary().view->SetScrollSensitivity(1e30f);
    f.StepRaw(16);
    REQUIRE(f.System().State(target) != nullptr);
    CHECK(f.System().State(target)->offset.y.Raw() == -160);
    CHECK(f.System().IsFailClosed(target));

    // 저작을 고치면 사용자의 위치에서 이어서 간다. 지웠다면 여기서 저작된
    // 시작 위치(0)로 되돌아갔을 것이다.
    f.Primary().view->SetScrollSensitivity(1.0f);
    f.StepRaw(16);
    REQUIRE(f.System().State(target) != nullptr);
    CHECK_FALSE(f.System().IsFailClosed(target));
    CHECK(f.System().State(target)->offset.y.Raw() == -185);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6b: 변위는 자기 **완전한 식별자**가 이름하는 서브트리만 옮긴다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("a scroll displacement never moves a subtree its identity does not name") {
    UIScrollFixture f;
    // 씬 JSON은 id 0을 그대로 발급할 수 있다(SceneSerializer가 objJson["id"]를
    // 검사 없이 적용한다). 그 id는 "빈 식별자"의 표식과 같은 숫자다.
    GameObject* zero = AddObject(f.World(), 0, f.Primary().viewport);
    AddOffsetRect(*zero, 0.0f, 0.0f, RawToFloat(64), RawToFloat(64));
    REQUIRE(f.Rebuild() != nullptr);

    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-128);
    REQUIRE(f.ApplyInput(input).changed);
    // 오프셋은 살아 있고 참조만 끊긴다. 다음 해석이 entry.content를 빈
    // 식별자로 갱신하지만 오프셋은 그대로 발행된다(두 축이 모두 죽었으므로
    // applyAxis가 이른 반환을 한다). 그 사이에 Rebuild를 끼우지 않는다 —
    // 끼우면 그 빌드가 아직 유효한 식별자로 이미 -128을 발행해 버린다.
    f.Primary().view->SetContent(SceneObjectRef{});
    f.ApplyInput(input);
    REQUIRE(f.System().State(f.ScrollTarget())->offset.y.Raw() == -128);

    const auto after = f.Rebuild();
    REQUIRE(after);
    const auto* zeroNode = FindNodeByObject(*after, 0);
    REQUIRE(zeroNode);
    // 빈 식별자는 아무것도 이름하지 않는다. 숫자 id 하나만 맞춰 보면 이
    // 무관한 서브트리가 살아 있는 오프셋만큼 밀린다.
    CHECK(zeroNode->logicalRect.y.Raw() == 0);
    const auto* contentNode = FindNodeByObject(*after, 3);
    REQUIRE(contentNode);
    CHECK(contentNode->logicalRect.y.Raw() == 0);
}

TEST_CASE("a replaced content RectTransform does not inherit the old displacement") {
    UIScrollFixture f;
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-128);
    REQUIRE(f.ApplyInput(input).changed);
    REQUIRE(f.System().State(f.ScrollTarget())->offset.y.Raw() == -128);

    // 같은 오브젝트 위에서 RectTransform을 갈아 끼운다. 오브젝트 id는 그대로고
    // 컴포넌트 인스턴스 id만 달라진다 — 숫자 id만 보는 비교는 이 둘을 구별하지
    // 못한다.
    f.Primary().content->RemoveComponent<RectTransform>();
    f.Primary().contentRect =
        AddOffsetRect(*f.Primary().content, 0.0f, 0.0f,
                      RawToFloat(kViewportRaw), RawToFloat(kViewportRaw * 2));
    const auto after = f.Rebuild();
    REQUIRE(after);
    const auto* content = FindNodeByObject(*after, 3);
    REQUIRE(content);
    // 죽은 컴포넌트의 이름으로 발행된 변위는 새 컴포넌트를 움직이지 않는다.
    CHECK(content->logicalRect.y.Raw() == 0);

    // 그리고 영구 정지가 아니다: 다음 걸음이 entry의 content 식별자를 갱신하면
    // 그 변위는 다시 살아 있는 컴포넌트를 이름한다. (스냅샷이 아니라 발행되는
    // 변위 자체를 본다 — 기하 캐시가 도장이 같은 프레임에서 옛 노드를
    // 돌려줄 수 있고, 그것은 이 케이스가 재는 사실이 아니다.)
    f.StepRaw(16);
    const auto live =
        molga::ui::CaptureTarget(f.World(), *f.Primary().contentRect);
    const auto displacements =
        f.System().DisplacementsForWorld(f.World().Generation());
    REQUIRE(displacements.size() == 1);
    CHECK(displacements[0].content == live);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6c: 저작된 시작 위치를 실현한 tick은 자기가 더럽힌 것을 보고한다
// (B5-d의 clock acquire seam도 같은 자리다 — 아래 Rebuild가 그것을 관찰한다)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("the tick that seeds an authored start position reports what it dirtied") {
    UIScrollFixture f;
    f.Primary().view->SetInitialNormalizedY(0.5f);
    const auto before = f.Rebuild();
    REQUIRE(before);
    REQUIRE(FindNodeByObject(*before, 3)->logicalRect.y.Raw() == 0);

    const auto mutation = f.System().AdvanceTick(
        f.World(), *before, UIDeterministicTick{1, Fixed26_6::FromRaw(16)},
        f.Diagnostics());
    // 이 tick이 content 서브트리를 640 raw의 절반만큼 옮겼다. "아무것도
    // 더럽히지 않았다"고 보고하면 arrangementDirty로 재빌드를 거르는
    // orchestrator가 이 프레임을 통째로 건너뛴다.
    CHECK(mutation.changed);
    CHECK(mutation.arrangementDirty);
    CHECK(mutation.renderDirty);
    CHECK_FALSE(mutation.textShapeDirty);
    REQUIRE(f.System().State(f.ScrollTarget())->offset.y.Raw() == -320);

    // 그리고 그 seed가 실제로 스냅샷을 다시 발행한다 — seed의 clock acquire를
    // 지우면 도장이 그대로라 옛 스냅샷이 그대로 돌아오고, 스크롤 표와 화면이
    // 서로 다른 값을 말한 채로 굳는다.
    const auto after = f.Rebuild();
    REQUIRE(after);
    CHECK(after != before);
    const auto* content = FindNodeByObject(*after, 3);
    REQUIRE(content);
    CHECK(content->logicalRect.y.Raw() == -320);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 5c: tick 값 계약은 **소비자**를 통해 재진다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("AdvanceTick refuses an invalid tick value at the consumer") {
    UIScrollFixture f;
    f.SetVerticalStateRaw(/* offset */ -96, /* velocity */ -128);
    const auto json = f.System().StableStateJson(f.World().Generation());
    f.Diagnostics().Clear();

    // 음수 delta는 관성 스크롤을 거꾸로 돌린다(travelled = Q6(v, -16)).
    CHECK_FALSE(f.System()
                    .AdvanceTick(f.World(), *f.Snapshot(),
                                 UIDeterministicTick{5, Fixed26_6::FromRaw(-16)},
                                 f.Diagnostics())
                    .changed);
    CHECK(f.System().StableStateJson(f.World().Generation()) == json);

    // 상한을 넘는 delta는 decayStep을 64로 포화시켜 retained를 0으로 만들고,
    // 한 걸음에 속도를 100배로 적분한다.
    CHECK_FALSE(f.System()
                    .AdvanceTick(f.World(), *f.Snapshot(),
                                 UIDeterministicTick{6, Fixed26_6::FromRaw(6400)},
                                 f.Diagnostics())
                    .changed);
    CHECK(f.System().StableStateJson(f.World().Generation()) == json);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) >= 1);
    // 나쁜 tick은 커서도 소비하지 않는다. 소비하면 그 뒤의 정상 tick이 조용히
    // 버려진다.
    CHECK(f.System().LastTickIndexForWorld(f.World().Generation()) == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6a: 가로 변위도 배치에 닿는다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("a horizontal runtime offset translates the content subtree") {
    UIScrollFixture f;
    f.SetHorizontalExtentRaw(-640, 0, 640);
    UIScrollInput input;
    input.axis = UIScrollAxis::Horizontal;
    input.axisValue = Fixed26_6::FromRaw(-160);
    REQUIRE(f.ApplyInput(input).changed);
    REQUIRE(f.System().State(f.ScrollTarget())->offset.x.Raw() == -160);

    const auto after = f.Rebuild();
    REQUIRE(after);
    const auto* content = FindNodeByObject(*after, 3);
    REQUIRE(content);
    // x 절반이 실제로 배치까지 간다. 여기까지 오는 케이스가 없으면 Step 6a의
    // 가로 절반은 통째로 재지지 않는다.
    CHECK(content->logicalRect.x.Raw() == -160);
    CHECK(content->logicalRect.y.Raw() == 0);
    const auto* viewport = FindNodeByObject(*after, 2);
    REQUIRE(viewport);
    CHECK(viewport->logicalRect.x.Raw() == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6a: 변위는 배율 **뒤**다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("a runtime offset is applied after the canvas scale") {
    UIScrollFixture f;
    // 내용을 캔버스 공간 32 raw 위에 저작한다. 배율 2가 걸리면 스냅샷의 그
    // 자리는 64 raw다.
    f.Primary().contentRect->SetAnchoredPosition({0.0f, RawToFloat(32)});
    f.SetCanvasScaleTwo();
    const auto before = f.Snapshot();
    const auto* scaled = FindNodeByObject(*before, 3);
    REQUIRE(scaled);
    // 배율이 정말 1이 아니다. 이 줄이 없으면 아래 값이 배율 1에서도 같아진다.
    REQUIRE(scaled->logicalRect.y.Raw() == 64);

    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-128);
    REQUIRE(f.ApplyInput(input).changed);
    REQUIRE(f.System().State(f.ScrollTarget())->offset.y.Raw() == -128);

    const auto after = f.Rebuild();
    REQUIRE(after);
    const auto* content = FindNodeByObject(*after, 3);
    REQUIRE(content);
    // 배율 뒤: 32*2 + (-128) = -64. 배율 앞이었다면 (32-128)*2 = -192이다.
    // 스크롤 시스템이 읽는 오프셋은 이미 배율을 받은 스냅샷의 논리 단위이므로,
    // 배율 앞에서 더하면 같은 수가 두 공간을 오간다.
    CHECK(content->logicalRect.y.Raw() == -64);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 5e: 마지막 1 raw를 닫는 settle 절
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("the elastic settle clause terminates a recurrence that cannot close itself") {
    UIScrollFixture f;
    f.SetVerticalExtentRaw(-640, 0, 640);
    f.SetRatesRaw(/* elasticity */ 8, /* deceleration */ 8);
    // 합법 구간 밖으로 정확히 1 raw, 속도도 1 raw. MulQ6가 0에서 **먼** 쪽으로
    // 반올림하므로 이 둘은 각자의 재귀에서 고정점이다: Q6(1, 62) = 1 이라 속도가
    // 줄지 않고, Q6(1, 2) = 0 이라 보정이 마지막 한 칸을 닫지 못한다.
    f.SetVerticalStateRaw(/* offset */ 1, /* velocity */ 1);
    f.StepRaw(16);
    const auto* state = f.System().State(f.ScrollTarget());
    REQUIRE(state);
    CHECK(state->offset.y.Raw() == 0);
    CHECK(state->velocity.y.Raw() == 0);
    // 그리고 그 자리에 머문다 — 정지한 스크롤이 프레임마다 "변했다"를 보고하면
    // 스냅샷이 영원히 다시 발행된다.
    CHECK_FALSE(f.StepRaw(16));
    CHECK(f.System().State(f.ScrollTarget())->velocity.y.Raw() == 0);
}

TEST_CASE("an authored start position waits for the first snapshot that can measure the axis") {
    UIScrollFixture f;
    f.Primary().view->SetInitialNormalizedY(0.5f);
    // 내용을 뷰포트 **아래**에 저작한다. 계층은 활성이지만 마스크와의 교집합이
    // 비어 이 스냅샷에서 제거된다 — 참조는 온전하고 다만 잴 수 없을 뿐이다.
    f.Primary().contentRect->SetAnchoredPosition({0.0f, RawToFloat(700)});
    const auto hidden = f.Rebuild();
    REQUIRE(hidden);
    REQUIRE(FindNodeByObject(*hidden, 3) == nullptr);
    f.StepRaw(16);
    const auto target = f.ScrollTarget();
    REQUIRE(f.System().State(target) != nullptr);
    // 잴 수 없는 축에서 유도한 시작 위치는 언제나 0이다. 그 0을 seed로 세면
    // 한 번뿐인 기회가 소진되고 저작된 위치는 다시는 적용되지 않는다.
    CHECK(f.System().State(target)->offset.y.Raw() == 0);

    f.Primary().contentRect->SetAnchoredPosition({0.0f, 0.0f});
    const auto measured = f.Rebuild();
    REQUIRE(measured);
    REQUIRE(FindNodeByObject(*measured, 3) != nullptr);
    f.StepRaw(16);
    // 그 축을 처음 잰 스냅샷에서 심는다.
    CHECK(f.System().State(target)->offset.y.Raw() == -320);
}

TEST_CASE("the input that seeds an authored start position reports what it dirtied") {
    UIScrollFixture f;
    f.Primary().view->SetInitialNormalizedY(0.5f);
    // 이 입력 **자신의** 변위는 0으로 스케일된다. 그래도 이 호출은 저작된
    // 시작 위치를 실현하면서 content 서브트리를 320 raw 옮긴다 — 두 사실을
    // 하나의 offsetChanged로 재면 옮긴 프레임이 "아무것도 안 했다"가 된다.
    f.Primary().view->SetScrollSensitivity(0.0f);
    const auto before = f.Rebuild();
    REQUIRE(before);
    REQUIRE(FindNodeByObject(*before, 3)->logicalRect.y.Raw() == 0);

    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-64);
    const auto mutation = f.ApplyInput(input);
    CHECK(mutation.changed);
    CHECK(mutation.arrangementDirty);
    CHECK(mutation.renderDirty);
    CHECK_FALSE(mutation.textShapeDirty);
    REQUIRE(f.System().State(f.ScrollTarget())->offset.y.Raw() == -320);

    const auto after = f.Rebuild();
    REQUIRE(after);
    CHECK(after != before);
    const auto* content = FindNodeByObject(*after, 3);
    REQUIRE(content);
    CHECK(content->logicalRect.y.Raw() == -320);
}

TEST_CASE("a snapshot that does not name the live content component cannot measure it") {
    UIScrollFixture f;
    f.Primary().view->SetInitialNormalizedY(0.5f);
    // 스냅샷은 **지금의** RectTransform을 이름한다. 그 컴포넌트를 갈아 끼우면
    // 오브젝트 id와 타입은 그대로고 인스턴스 id만 달라진다 — 숫자 id만 맞춰
    // 보는 조회는 죽은 컴포넌트의 사각형을 살아 있는 것으로 읽고, 그 위에서
    // 잰 합법 구간으로 저작된 시작 위치를 심어 버린다.
    f.Primary().content->RemoveComponent<RectTransform>();
    f.Primary().contentRect =
        AddOffsetRect(*f.Primary().content, 0.0f, 0.0f,
                      RawToFloat(kViewportRaw), RawToFloat(kViewportRaw * 2));
    f.StepRaw(16);  // 아직 다시 짓지 않은 스냅샷 위에서 한 걸음
    const auto target = f.ScrollTarget();
    REQUIRE(f.System().State(target) != nullptr);
    CHECK(f.System().State(target)->offset.y.Raw() == 0);

    // 그리고 영구 정지가 아니다: 스냅샷이 살아 있는 컴포넌트를 이름하게 되면
    // 그때 잰다.
    //
    // 저작을 한 번 더 건드려 기하 키를 실제로 움직인다. 기하 캐시 키는
    // 컴포넌트 인스턴스가 아니라 컴포넌트별 revision **번호**만 들고 있어서,
    // 같은 횟수만큼 저작된 교체 컴포넌트는 키를 움직이지 못하고 캐시가 옛
    // 노드(옛 식별자)를 그대로 돌려준다. 그것은 이 스크롤 seam의 결함이
    // 아니라 기하 캐시 키의 것이다.
    f.Primary().contentRect->SetPivot({0.5f, 0.5f});
    f.Primary().contentRect->SetPivot({0.0f, 0.0f});
    REQUIRE(f.Rebuild() != nullptr);
    f.StepRaw(16);
    CHECK(f.System().State(target)->offset.y.Raw() == -320);
}

TEST_CASE("a displacement whose content is not in this tree stays out of the geometry key") {
    UIScrollFixture f;
    UIScrollInput input;
    input.axisValue = Fixed26_6::FromRaw(-64);
    REQUIRE(f.ApplyInput(input).changed);
    REQUIRE(f.Rebuild() != nullptr);
    auto key = f.Layout().LastGeometryKey();
    REQUIRE(key.has_value());
    REQUIRE(key->scrollDisplacements.size() == 1);  // 이 표면이 실제로 옮긴다

    // 패널을 끈다. 그 서브트리는 이 표면이 짓는 노드 집합에 아예 없으므로 그
    // 변위는 어떤 사각형도 옮기지 않는다 — 그래도 키에 남겨 두면 이 표면이
    // 짓지 않는 서브트리의 스크롤이 이 표면의 기하를 무효화한다.
    f.Primary().viewport->SetActive(false);
    REQUIRE(f.Rebuild() != nullptr);
    key = f.Layout().LastGeometryKey();
    REQUIRE(key.has_value());
    CHECK(key->scrollDisplacements.empty());
}
