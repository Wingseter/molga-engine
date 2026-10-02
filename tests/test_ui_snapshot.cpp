#include "doctest.h"

#include "Common/Fixed26_6.h"
#include "Rendering/GraphicsDevice.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UILayoutTypes.h"
#include "UI/UIRuntimeIdentity.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Step 6: 감지기는 테스트에만 둔다. 프로덕션에 리플렉션을 넣지 않는다.
// UISnapshot은 의미(semantic) 캐시 상태라서 frameIndex 같은 감사용 프레임
// 필드가 하나라도 들어오면 프레임마다 값이 달라져 캐시가 절대 적중하지 않는다.
namespace molga::ui {

template <class...>
using SnapshotVoidT = void;

template <class T, class = void>
struct SnapshotHasFrameIndex : std::false_type {};
template <class T>
struct SnapshotHasFrameIndex<
    T, SnapshotVoidT<decltype(std::declval<const T&>().frameIndex)>>
    : std::true_type {};

} // namespace molga::ui

namespace {

// 감지기가 항상 false를 내면 위 static_assert는 공허하게 통과한다. 실제로
// frameIndex를 가진 타입에서 true가 나오는지 반대 극성도 못 박는다.
struct FrameStampedProbe {
    std::uint64_t frameIndex = 0;
};

} // namespace

static_assert(molga::ui::SnapshotHasFrameIndex<FrameStampedProbe>::value,
              "the detector must actually see a frameIndex member");
static_assert(!molga::ui::SnapshotHasFrameIndex<
                  molga::ui::UILayoutNodeSnapshot>::value,
              "frameIndex belongs to UIFrameInput/UIFrameResult only");

// 스냅샷은 불변이다. 별칭이 non-const 포인티를 가리키게 바뀌면 게시된 스냅샷을
// 나중에 수정할 수 있게 되고, "변경 없는 프레임은 동일한 shared_ptr을 재사용
// 한다"는 계약이 조용히 무너진다.
static_assert(std::is_same<molga::ui::UISnapshotPtr,
                           std::shared_ptr<const molga::ui::UISnapshot>>::value,
              "UISnapshotPtr must alias shared_ptr<const UISnapshot>");
static_assert(std::is_const<molga::ui::UISnapshotPtr::element_type>::value,
              "a published UISnapshot must not be mutable through its handle");

TEST_CASE("UISnapshot is semantic and cacheable") {
    static_assert(!molga::ui::SnapshotHasFrameIndex<
                  molga::ui::UISnapshot>::value,
                  "frameIndex belongs to UIFrameInput/UIFrameResult only");
    molga::ui::UISnapshot snapshot;
    snapshot.worldGeneration = 9;
    snapshot.logicalViewport = {
        molga::Fixed26_6::FromRaw(800 * 64),
        molga::Fixed26_6::FromRaw(600 * 64)};
    const std::string json = molga::ui::StableLayoutSnapshotJson(snapshot);
    CHECK(json.find("frameIndex") == std::string::npos);
    CHECK(json.find("timestamp") == std::string::npos);
    CHECK(json.find("physical") == std::string::npos);
}

// 기본 멤버 초기자를 못 박는다. 아래 픽스처들은 모든 필드를 명시적으로
// 채우므로 기본값이 바뀌어도 나머지 테스트는 전부 통과한다.
//
// interactionEligible의 기본값이 특히 중요하다: 이 플래그가 노드를 hit-test
// 대상으로 만드는데, 기본이 true가 되면 배치 시스템이 플래그를 명시적으로
// 끄지 않은 모든 노드가 조용히 클릭 가능해진다 — 오류 경로가 성공으로 열리는
// 형태다. logicalClip도 같은 모양의 반대편이다: 기본이 engaged면 그 값은 빈
// 사각형이고, "빈 클립 교집합은 렌더/hit 레코드를 모두 제거한다"는 계약에
// 따라 클립을 지정하지 않은 노드가 전부 사라진다. worldGeneration/
// surfaceWindowId의 0은 "설정되지 않음" 센티넬이라 0이 아닌 기본값은 기본
// 생성된 스냅샷이 실제 월드를 사칭하게 만든다.
TEST_CASE("layout records default-construct to the empty, ineligible identity") {
    const molga::ui::UILayoutNodeSnapshot node;
    CHECK(node.layoutRevision == 0);
    CHECK_FALSE(node.interactionEligible);
    CHECK_FALSE(node.logicalClip.has_value());
    CHECK(node.logicalRect == molga::FixedRect{});
    CHECK(node.intrinsicSize == molga::FixedSize{});
    CHECK_FALSE(static_cast<bool>(node.rectTransform));

    const molga::ui::UISnapshot snapshot;
    CHECK(snapshot.surfaceWindowId == 0);
    CHECK(snapshot.worldGeneration == 0);
    CHECK(snapshot.logicalViewport == molga::FixedSize{});
    CHECK(snapshot.nodes.empty());
}

namespace {

molga::FixedRect RectRaw(std::int32_t x, std::int32_t y, std::int32_t width,
                      std::int32_t height) {
    return molga::FixedRect{
        molga::Fixed26_6::FromRaw(x), molga::Fixed26_6::FromRaw(y),
        molga::Fixed26_6::FromRaw(width), molga::Fixed26_6::FromRaw(height)};
}

molga::FixedSize Size(std::int32_t width, std::int32_t height) {
    return molga::FixedSize{molga::Fixed26_6::FromRaw(width),
                            molga::Fixed26_6::FromRaw(height)};
}

molga::ui::UIRuntimeTargetIdentity Identity(unsigned int objectId) {
    molga::ui::UIRuntimeTargetIdentity identity;
    identity.worldGeneration = 4;
    identity.objectId = objectId;
    identity.componentRuntimeTypeId = 6;
    identity.componentInstanceId = 7;
    return identity;
}

// 어떤 두 필드도 값이 같지 않게 고른다. width == height 이거나 x == y 인
// 픽스처는 필드를 뒤바꾼 직렬화기를 절대 잡지 못한다. 항등 사각형(0,0,0,0)도
// 마찬가지로 통째로 빠뜨린 필드를 숨긴다.
molga::ui::UISnapshot MakeSnapshot() {
    molga::ui::UISnapshot snapshot;
    snapshot.surfaceWindowId = 3;
    snapshot.worldGeneration = 5;
    snapshot.logicalViewport = Size(800 * 64, 600 * 64);

    molga::ui::UILayoutNodeSnapshot first;
    first.rectTransform = Identity(11);
    first.logicalRect = RectRaw(64, 128, 192, 256);
    first.intrinsicSize = Size(320, 384);
    first.logicalClip = RectRaw(448, 512, 576, 640);
    first.layoutRevision = 7;
    first.interactionEligible = true;

    molga::ui::UILayoutNodeSnapshot second;
    second.rectTransform = Identity(22);
    second.logicalRect = RectRaw(-64, -128, 704, 768);
    second.intrinsicSize = Size(832, 896);
    second.logicalClip = std::nullopt;
    second.layoutRevision = 8;
    second.interactionEligible = false;

    snapshot.nodes = {first, second};
    return snapshot;
}

constexpr char kExpectedJson[] =
    R"({"logicalViewport":{"width":51200,"height":38400},"nodes":[)"
    R"({"objectId":11,"logicalRect":{"x":64,"y":128,"width":192,"height":256},)"
    R"("intrinsicSize":{"width":320,"height":384},)"
    R"("logicalClip":{"x":448,"y":512,"width":576,"height":640},)"
    R"("interactionEligible":true},)"
    R"({"objectId":22,"logicalRect":{"x":-64,"y":-128,"width":704,"height":768},)"
    R"("intrinsicSize":{"width":832,"height":896},)"
    R"("logicalClip":null,)"
    R"("interactionEligible":false}],)"
    R"("renderItems":[],"hitTargets":[],"textInputLabels":[]})";

} // namespace

// 바이트를 통째로 못 박는 이유: "필드 하나를 바꾸면 JSON이 달라진다"는 행렬만
// 으로는 x와 y를 서로 바꿔 쓴 직렬화기를 잡지 못한다(바꿔치기해도 여전히
// 달라지기 때문이다). 기대 바이트열은 누락, 뒤바뀜, 키 이름 변경, 필드 순서
// 변경을 한꺼번에 잡는다.
TEST_CASE("StableLayoutSnapshotJson pins canonical bytes") {
    CHECK(molga::ui::StableLayoutSnapshotJson(MakeSnapshot()) == kExpectedJson);

    // 같은 입력은 같은 바이트를 낸다.
    CHECK(molga::ui::StableLayoutSnapshotJson(MakeSnapshot()) ==
          molga::ui::StableLayoutSnapshotJson(MakeSnapshot()));

    // raw 26.6 정수를 그대로 쓴다: 800 논리 단위는 51200이지 800이 아니다.
    const std::string json = molga::ui::StableLayoutSnapshotJson(MakeSnapshot());
    CHECK(json.find("51200") != std::string::npos);
    CHECK(json.find("\"width\":800,") == std::string::npos);
    CHECK(json.find('.') == std::string::npos);
}

TEST_CASE("StableLayoutSnapshotJson reacts to every semantic field") {
    // 아래 람다들은 s.nodes[1]과 s.nodes[0].logicalClip->x 를 그대로 만진다.
    // 픽스처가 회귀해 노드가 줄거나 클립이 비면 그 역참조는 UB이고, 남아 있던
    // 값이 우연히 통과할 수 있다. 픽스처 모양을 먼저 실패로 닫는다.
    REQUIRE(MakeSnapshot().nodes.size() == 2);
    REQUIRE(MakeSnapshot().nodes[0].logicalClip.has_value());
    REQUIRE_FALSE(MakeSnapshot().nodes[1].logicalClip.has_value());

    const std::string base = molga::ui::StableLayoutSnapshotJson(MakeSnapshot());

    auto mutated = [](void (*apply)(molga::ui::UISnapshot&)) {
        molga::ui::UISnapshot snapshot = MakeSnapshot();
        apply(snapshot);
        return molga::ui::StableLayoutSnapshotJson(snapshot);
    };

    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.logicalViewport.width = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.logicalViewport.height = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].rectTransform.objectId = 99;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalRect.x = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalRect.y = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalRect.width = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalRect.height = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].intrinsicSize.width = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].intrinsicSize.height = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalClip->x = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalClip->y = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalClip->width = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalClip->height = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].logicalClip = std::nullopt;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[1].logicalClip = RectRaw(1, 2, 3, 4);
          }));
    // 두 노드의 interactionEligible이 서로 다르므로 어느 쪽을 뒤집어도
    // 달라진다 — 상수로 고정한 구현이 양방향에서 죽는다.
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[0].interactionEligible = false;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.nodes[1].interactionEligible = true;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) { s.nodes.pop_back(); }));
}

// 정규 JSON은 프로세스마다, 그리고 편집 이력에 따라 달라지는 값을 담지 않는다.
// worldGeneration과 componentInstanceId는 이 프로세스가 지금까지 몇 개를
// 할당했는지에 달린 순번이고, componentRuntimeTypeId는 타입을 처음 만난
// 순서이며, surfaceWindowId는 에디터/런타임의 창 할당이고, layoutRevision은
// 재계산/편집 횟수다. 하나라도 들어가면 같은 authored 입력이 프로세스에 따라,
// 또는 cold 빌드와 편집을 거친 warm 빌드 사이에서 다른 바이트를 내서 parity
// 비교가 무너진다. 이 목록은 07-parity-qualification의 canonical 금지 키
// 목록과 같은 계약이다.
TEST_CASE("StableLayoutSnapshotJson omits process-local and edit-history values") {
    const std::string base = molga::ui::StableLayoutSnapshotJson(MakeSnapshot());

    molga::ui::UISnapshot otherWindow = MakeSnapshot();
    otherWindow.surfaceWindowId = 4242;
    CHECK(molga::ui::StableLayoutSnapshotJson(otherWindow) == base);

    molga::ui::UISnapshot otherGeneration = MakeSnapshot();
    otherGeneration.worldGeneration = 999;
    CHECK(molga::ui::StableLayoutSnapshotJson(otherGeneration) == base);

    molga::ui::UISnapshot otherInstances = MakeSnapshot();
    for (auto& node : otherInstances.nodes) {
        node.rectTransform.worldGeneration = 999;
        node.rectTransform.componentRuntimeTypeId = 888;
        node.rectTransform.componentInstanceId = 777;
    }
    CHECK(molga::ui::StableLayoutSnapshotJson(otherInstances) == base);

    // 같은 authored 기하를 cold 빌드(낮은 revision)와 여러 번 편집/되돌린 warm
    // 빌드(높은 revision)에서 만들어도 바이트가 같아야 한다.
    molga::ui::UISnapshot editedHistory = MakeSnapshot();
    for (auto& node : editedHistory.nodes) node.layoutRevision += 41;
    CHECK(molga::ui::StableLayoutSnapshotJson(editedHistory) == base);

    // 위 검사들이 공허하지 않다는 증인: 씬에 저장되는 objectId는 정규 JSON에
    // 들어가므로 바뀌면 바이트가 달라진다.
    molga::ui::UISnapshot otherObject = MakeSnapshot();
    otherObject.nodes[0].rectTransform.objectId = 33;
    CHECK(molga::ui::StableLayoutSnapshotJson(otherObject) != base);
}

// 값이 우연히 겹쳐 위 동등성 검사를 통과하는 일이 없도록, 금지된 키 이름이
// 바이트열에 아예 나타나지 않는 것도 함께 못 박는다. 목록은
// 07-parity-qualification.md의 "canonical bytes contain no runtime or raster
// keys" 와 같다(이 단계에 아직 존재하지 않는 텍스트/래스터 키는 제외).
TEST_CASE("StableLayoutSnapshotJson contains no runtime or raster keys") {
    const std::string json = molga::ui::StableLayoutSnapshotJson(MakeSnapshot());
    for (const char* forbidden :
         {"frameIndex", "worldGeneration", "componentRuntimeTypeId",
          "componentInstanceId", "layoutRevision", "surfaceWindowId", "atlas",
          "uv", "gpu", "timestamp", "physical"}) {
        CAPTURE(forbidden);
        CHECK(json.find(forbidden) == std::string::npos);
    }

    // 검사가 공허하지 않다는 증인: 실제로 내보내는 키는 찾을 수 있어야 한다.
    CHECK(json.find("objectId") != std::string::npos);
    CHECK(json.find("interactionEligible") != std::string::npos);
}

namespace {

struct KeyedNode {
    molga::ui::UIDrawOrderKey key;
    molga::ui::UILayoutNodeSnapshot node;
};

KeyedNode MakeKeyedNode(std::int32_t canvasSortingOrder,
                        std::vector<std::uint32_t> siblingPath,
                        std::int32_t componentSortingOrder,
                        std::uint64_t stableSubmissionIndex,
                        unsigned int objectId) {
    KeyedNode keyed;
    keyed.key.canvasSortingOrder = canvasSortingOrder;
    keyed.key.siblingPath = std::move(siblingPath);
    keyed.key.componentSortingOrder = componentSortingOrder;
    keyed.key.stableSubmissionIndex = stableSubmissionIndex;
    keyed.node.rectTransform = Identity(objectId);
    // 노드마다 내용이 달라야 순열이 JSON에 드러난다. 내용이 같으면 정렬이
    // 틀려도 바이트가 같아 검사가 공허해진다.
    keyed.node.logicalRect =
        RectRaw(static_cast<std::int32_t>(objectId) * 64, 128, 192, 256);
    keyed.node.intrinsicSize = Size(320, 384);
    keyed.node.layoutRevision = objectId;
    keyed.node.interactionEligible = (objectId % 2u) == 1u;
    return keyed;
}

molga::ui::UISnapshot SnapshotOf(const std::vector<KeyedNode>& ordered) {
    molga::ui::UISnapshot snapshot;
    snapshot.surfaceWindowId = 3;
    snapshot.worldGeneration = 5;
    snapshot.logicalViewport = Size(800 * 64, 600 * 64);
    for (const auto& keyed : ordered) snapshot.nodes.push_back(keyed.node);
    return snapshot;
}

std::vector<KeyedNode> SortedByDrawOrder(std::vector<KeyedNode> nodes) {
    std::sort(nodes.begin(), nodes.end(),
              [](const KeyedNode& a, const KeyedNode& b) {
                  return a.key < b.key;
              });
    return nodes;
}

} // namespace

// Step 6. 삽입 순서가 다른 두 목록을 UIDrawOrderKey로 정렬하면 같은 바이트가
// 나와야 한다. 이 검사만으로는 직렬화기가 스스로 정렬해 버려도 통과하므로,
// "정렬하지 않은 목록은 다른 바이트를 낸다"는 반대편도 함께 못 박는다.
// UILayoutNodeSnapshot이 draw order key를 담지 않는 것은 의도된 설계라서
// (Step 5c의 verbatim 레코드) 직렬화기는 스스로 그 순서를 만들 수 없다.
TEST_CASE("draw order sorting makes snapshot bytes insertion-order stable") {
    // 낮은 우선순위 필드가 전부 반대 방향을 가리키도록 고른다: 정렬 결과는
    // first < second < third 인데, componentSortingOrder만 보면 second가,
    // stableSubmissionIndex만 보면 second가, siblingPath만 보면 third가
    // 앞선다. 그래서 필드 우선순위를 뒤바꾼 비교자는 여기서 다른 순열을 낸다.
    const KeyedNode first = MakeKeyedNode(0, {1}, 5, 9, 1);
    const KeyedNode second = MakeKeyedNode(0, {2}, -5, 0, 2);
    const KeyedNode third = MakeKeyedNode(1, {0}, -100, 0, 3);

    const std::vector<KeyedNode> insertionA = {third, first, second};
    const std::vector<KeyedNode> insertionB = {second, third, first};

    const std::string sortedA = molga::ui::StableLayoutSnapshotJson(
        SnapshotOf(SortedByDrawOrder(insertionA)));
    const std::string sortedB = molga::ui::StableLayoutSnapshotJson(
        SnapshotOf(SortedByDrawOrder(insertionB)));
    CHECK(sortedA == sortedB);

    // 정렬 결과가 실제로 오름차순인지 못 박는다. 두 목록이 같은 순열로만
    // 수렴하면 방향이 뒤집혀도 위 검사는 통과한다.
    const std::string expected = molga::ui::StableLayoutSnapshotJson(
        SnapshotOf({first, second, third}));
    CHECK(sortedA == expected);

    // 직렬화기가 순서에 민감하다는 증인. 이게 없으면 위 등식은 공허하다.
    CHECK(molga::ui::StableLayoutSnapshotJson(SnapshotOf(insertionA)) !=
          expected);
    CHECK(molga::ui::StableLayoutSnapshotJson(SnapshotOf(insertionA)) !=
          molga::ui::StableLayoutSnapshotJson(SnapshotOf(insertionB)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.1 Step 7: 정규 JSON은 의미 페이로드 필드만 담는다
// ─────────────────────────────────────────────────────────────────────────────
namespace {

molga::ui::UIStableComponentKey StableKey(unsigned int objectId,
                                          const char* typeName,
                                          std::uint32_t schemaVersion) {
    molga::ui::UIStableComponentKey key;
    key.sceneObjectId = objectId;
    key.componentTypeName = typeName;
    key.componentSchemaVersion = schemaVersion;
    return key;
}

molga::ui::UIFrozenTarget FrozenTarget(unsigned int objectId,
                                       const char* typeName,
                                       std::uint32_t schemaVersion) {
    molga::ui::UIFrozenTarget target;
    target.runtimeTarget = Identity(objectId);
    target.canonicalTarget = StableKey(objectId, typeName, schemaVersion);
    return target;
}

molga::ui::UIDrawOrderKey Order(std::int32_t canvasSortingOrder,
                                std::vector<std::uint32_t> siblingPath,
                                std::int32_t componentSortingOrder,
                                std::uint64_t stableSubmissionIndex) {
    molga::ui::UIDrawOrderKey order;
    order.canvasSortingOrder = canvasSortingOrder;
    order.siblingPath = std::move(siblingPath);
    order.componentSortingOrder = componentSortingOrder;
    order.stableSubmissionIndex = stableSubmissionIndex;
    return order;
}

// 런타임 바인딩을 일부러 전부 0이 아닌 값으로 채운다. 정규 바이트에 하나라도
// 새어 나오면 아래 금지 키 검사와 "장치를 다시 만들어도 바이트가 같다"는
// 검사가 함께 죽는다.
molga::ui::TextureRuntimeBindingIdentity RuntimeBinding(
    std::uint64_t deviceGeneration, std::uint64_t uploadGeneration,
    std::uint64_t lifetimeIdentity) {
    molga::ui::TextureRuntimeBindingIdentity binding;
    binding.deviceGeneration = deviceGeneration;
    binding.uploadGeneration = uploadGeneration;
    binding.texture = molga::detail::MakeTextureHandleForTest(
        static_cast<std::uint32_t>(deviceGeneration), 1);
    binding.sampler = molga::detail::MakeSamplerHandleForTest(
        static_cast<std::uint32_t>(uploadGeneration), 1);
    binding.lifetimeIdentity = lifetimeIdentity;
    return binding;
}

molga::ui::UISnapshot MakePayloadSnapshot() {
    molga::ui::UISnapshot snapshot;
    snapshot.surfaceWindowId = 3;
    snapshot.worldGeneration = 5;
    snapshot.logicalViewport = Size(800 * 64, 600 * 64);

    molga::ui::UIRenderItemSnapshot sprite;
    sprite.source = Identity(11);
    sprite.canonicalSource = StableKey(11, "UIImage", 1);
    sprite.order = Order(2, {0, 1}, 5, 9);
    sprite.logicalRect = RectRaw(64, 128, 192, 256);
    sprite.logicalClip = RectRaw(448, 512, 576, 640);
    sprite.reservedCommandSpan = 1;
    molga::ui::UISpriteSnapshot spritePayload;
    spritePayload.textureGuid = "texture-guid";
    spritePayload.textureContentSha256 = "abc123";
    spritePayload.textureContentStableId = 7788;
    spritePayload.binding = RuntimeBinding(11, 3, 101);
    spritePayload.tint = Color{0.25f, 0.5f, 0.75f, 1.0f};
    sprite.payload = spritePayload;

    molga::ui::UIRenderItemSnapshot solid;
    solid.source = Identity(22);
    solid.canonicalSource = StableKey(22, "UIButton", 1);
    solid.order = Order(2, {0, 1}, 6, 10);
    solid.logicalRect = RectRaw(-64, -128, 704, 768);
    solid.logicalClip = std::nullopt;
    solid.reservedCommandSpan = 4;
    molga::ui::UISolidRectSnapshot solidPayload;
    solidPayload.color = Color{0.1f, 0.2f, 0.3f, 0.4f};
    solid.payload = solidPayload;

    snapshot.renderItems = {sprite, solid};

    molga::ui::UIHitTargetSnapshot hit;
    hit.target = Identity(11);
    hit.canonicalTarget = StableKey(11, "UIImage", 1);
    hit.focusTarget = FrozenTarget(11, "UISelectable", 1);
    hit.textInputTarget = FrozenTarget(11, "UITextInput", 1);
    hit.scrollTargets = {FrozenTarget(33, "UIScrollView", 1),
                         FrozenTarget(44, "UIScrollView", 1)};
    hit.order = Order(2, {0, 1}, 5, 9);
    hit.logicalRect = RectRaw(64, 128, 192, 256);
    hit.logicalClip = RectRaw(448, 512, 576, 640);
    hit.interactable = true;
    // 직렬화기 시험이다. 게시는 상호작용 가능한 기록의 주인을 비워 두지만,
    // 여기서는 필드가 어떻게 나가는지만 본다.
    hit.pointerOwner = FrozenTarget(22, "UIButton", 1);
    hit.focusable = true;
    hit.acceptsTextInput = false;
    hit.navigation.mode = UINavigationMode::Explicit;
    hit.navigation.explicitTargets[0] = Identity(55);
    hit.navigation.canonicalTargets[0] = StableKey(55, "UISelectable", 1);
    snapshot.hitTargets = {hit};

    molga::ui::UITextInputLabelSnapshot owned;
    owned.input = FrozenTarget(11, "UITextInput", 1);
    owned.renderedLabel.label = FrozenTarget(66, "UILabel", 2);
    owned.renderedLabel.color = Color{0.5f, 0.6f, 0.7f, 0.8f};
    owned.renderedLabel.enabledAndVisible = true;
    molga::ui::UIFrozenInputLabelVisual placeholder;
    placeholder.label = FrozenTarget(77, "UILabel", 2);
    placeholder.color = Color{0.9f, 0.8f, 0.7f, 0.6f};
    placeholder.enabledAndVisible = false;
    owned.placeholderLabel = placeholder;
    owned.logicalViewport = RectRaw(1, 2, 3, 4);
    owned.logicalClip = std::nullopt;
    owned.baseOrder = Order(2, {0, 1}, 0, 9);
    snapshot.textInputLabels = {owned};

    // IME 기하는 런타임 편집 상태(focused)를 싣고 있으므로 정규 바이트에
    // 들어가지 않는다. 값을 채워 두는 것은 그 배제가 실제로 일어나는지를
    // 아래에서 확인하기 위해서다.
    molga::ui::UITextInputImeGeometrySnapshot ime;
    ime.input = FrozenTarget(11, "UITextInput", 1);
    ime.logicalInputArea = RectRaw(5, 6, 7, 8);
    ime.focused = true;
    snapshot.textInputImeGeometry = {ime};
    return snapshot;
}

} // namespace

TEST_CASE("payload JSON carries canonical keys and no runtime binding") {
    const std::string json =
        molga::ui::StableLayoutSnapshotJson(MakePayloadSnapshot());

    // 같은 입력은 같은 바이트를 낸다.
    CHECK(json == molga::ui::StableLayoutSnapshotJson(MakePayloadSnapshot()));

    // 실제로 내보내는 의미 필드들.
    for (const char* present :
         {"canonicalSource", "componentTypeName", "componentSchemaVersion",
          "reservedCommandSpan", "stableSubmissionIndex", "siblingPath",
          "textureGuid", "textureContentSha256", "textureContentStableId",
          "focusTarget", "textInputTarget", "scrollTargets", "pointerOwner",
          "navigation",
          "renderedLabel", "placeholderLabel", "Explicit", "sprite", "solid"}) {
        CAPTURE(present);
        CHECK(json.find(present) != std::string::npos);
    }

    // 그리고 절대 나오면 안 되는 것들. 런타임 정체성, 표면 창, 프로세스 지역
    // 세대, 네이티브 핸들, 수명 토큰, atlas/UV, 물리 픽셀.
    for (const char* forbidden :
         {"frameIndex", "worldGeneration", "componentRuntimeTypeId",
          "componentInstanceId", "layoutRevision", "surfaceWindowId",
          "deviceGeneration", "uploadGeneration", "lifetimeIdentity",
          "binding", "resourceLifetime", "runtimeTarget", "atlas", "uv", "gpu",
          "timestamp", "physical", "focused", "caretVisible", "editRevision",
          "enabledAndVisible", "textInputImeGeometry"}) {
        CAPTURE(forbidden);
        CHECK(json.find(forbidden) == std::string::npos);
    }
}

TEST_CASE("payload JSON is byte-identical across binding and edit history") {
    const std::string base =
        molga::ui::StableLayoutSnapshotJson(MakePayloadSnapshot());

    // 장치를 다시 만들어 모든 런타임 바인딩 필드가 달라져도 바이트는 같다.
    molga::ui::UISnapshot rebound = MakePayloadSnapshot();
    auto& sprite = std::get<molga::ui::UISpriteSnapshot>(
        rebound.renderItems[0].payload);
    sprite.binding = RuntimeBinding(99, 77, 555);
    CHECK(molga::ui::StableLayoutSnapshotJson(rebound) == base);

    // 런타임 라우팅 정체성도 마찬가지다.
    molga::ui::UISnapshot rerouted = MakePayloadSnapshot();
    rerouted.surfaceWindowId = 4242;
    rerouted.renderItems[0].source = Identity(999);
    rerouted.hitTargets[0].target = Identity(999);
    rerouted.hitTargets[0].focusTarget->runtimeTarget = Identity(999);
    rerouted.hitTargets[0].pointerOwner->runtimeTarget = Identity(999);
    rerouted.hitTargets[0].navigation.explicitTargets[0] = Identity(999);
    rerouted.textInputImeGeometry[0].focused = false;
    // ── Task 11.1 Q3 ────────────────────────────────────────────────────────
    // enabledAndVisible은 "비어 있고 포커스가 없는가"를 그대로 담은 런타임
    // 편집 상태다. IME 기하를 focused 때문에 통째로 뺀 것과 같은 사실이므로
    // 같은 규칙이어야 한다: 사용자가 입력창을 한 번 누르는 것만으로 같은 저작
    // 씬의 정규 바이트가 달라지면, "편집 이력이 다른 빌드가 바이트 동일한
    // 의미 스냅샷을 낸다"는 Exit Contract가 그 자리에서 깨진다.
    rerouted.textInputLabels[0].renderedLabel.enabledAndVisible = false;
    rerouted.textInputLabels[0].placeholderLabel->enabledAndVisible = true;
    CHECK(molga::ui::StableLayoutSnapshotJson(rerouted) == base);

    // 위 등식이 공허하지 않다는 증인: 의미 필드는 전부 바이트를 바꾼다.
    auto mutated = [](void (*apply)(molga::ui::UISnapshot&)) {
        molga::ui::UISnapshot snapshot = MakePayloadSnapshot();
        apply(snapshot);
        return molga::ui::StableLayoutSnapshotJson(snapshot);
    };
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].canonicalSource.sceneObjectId = 999;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].canonicalSource.componentTypeName = "UILabel";
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].canonicalSource.componentSchemaVersion = 9;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].order.stableSubmissionIndex = 999;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].order.siblingPath = {9, 9};
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].order.canvasSortingOrder = 99;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].order.componentSortingOrder = 99;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].logicalRect.x = molga::Fixed26_6::FromRaw(1);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[0].logicalClip = std::nullopt;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems[1].reservedCommandSpan = 5;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              std::get<molga::ui::UISpriteSnapshot>(s.renderItems[0].payload)
                  .textureGuid = "other";
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              std::get<molga::ui::UISpriteSnapshot>(s.renderItems[0].payload)
                  .textureContentSha256 = "other";
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              std::get<molga::ui::UISpriteSnapshot>(s.renderItems[0].payload)
                  .textureContentStableId = 1;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              std::get<molga::ui::UISpriteSnapshot>(s.renderItems[0].payload)
                  .tint = Color{1.0f, 1.0f, 1.0f, 1.0f};
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              std::get<molga::ui::UISolidRectSnapshot>(s.renderItems[1].payload)
                  .color = Color{1.0f, 1.0f, 1.0f, 1.0f};
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].focusTarget = std::nullopt;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].textInputTarget = std::nullopt;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              // 안쪽에서 바깥쪽 순서 자체가 계약이다.
              std::swap(s.hitTargets[0].scrollTargets[0],
                        s.hitTargets[0].scrollTargets[1]);
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].interactable = false;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].pointerOwner = std::nullopt;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].pointerOwner->canonicalTarget.sceneObjectId = 999;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].focusable = false;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].acceptsTextInput = true;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].navigation.mode = UINavigationMode::None;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets[0].navigation.canonicalTargets[0] = std::nullopt;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.textInputLabels[0].placeholderLabel = std::nullopt;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.textInputLabels[0].reservedCommandSpan = 5;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.textInputLabels[0].renderedLabel.label.canonicalTarget
                  .sceneObjectId = 999;
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.renderItems.pop_back();
          }));
    CHECK(base != mutated([](molga::ui::UISnapshot& s) {
              s.hitTargets.clear();
          }));
}
