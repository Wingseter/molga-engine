#include "doctest.h"

#include "Common/Fixed26_6.h"
#include "UI/UILayoutTypes.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

TEST_CASE("fixed conversion rejects invalid input and normalizes zero") {
    CHECK_FALSE(molga::Fixed26_6::FromFloat(
        std::numeric_limits<float>::infinity()));
    CHECK_FALSE(molga::Fixed26_6::FromFloat(
        std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(molga::Fixed26_6::FromFloat(-0.0f));
    CHECK(molga::Fixed26_6::FromFloat(-0.0f)->Raw() == 0);
    CHECK(molga::Fixed26_6::FromFloat(1.0f / 128.0f)->Raw() == 1);
    CHECK(molga::Fixed26_6::FromFloat(-1.0f / 128.0f)->Raw() == -1);
}

TEST_CASE("checked fixed division exposes explicit floor and ceil") {
    CHECK(molga::CheckedFloorDiv(-65, 64) == -2);
    CHECK(molga::CheckedCeilDiv(-65, 64) == -1);
    CHECK(molga::CheckedFloorDiv(65, 64) == 1);
    CHECK(molga::CheckedCeilDiv(65, 64) == 2);
    CHECK_FALSE(molga::CheckedFloorDiv(INT64_MIN, -1));
    CHECK_FALSE(molga::CheckedCeilDiv(INT64_MIN, -1));
    CHECK_FALSE(molga::CheckedFloorDiv(1, 0));
}

namespace {

// CheckedFloorDiv/CheckedCeilDiv의 인자는 raw 26.6 정수다. 이 프로젝트는
// -Wconversion 없이 빌드되므로 부동소수를 넘기면 경고 없이 잘려(예: 1.9 -> 1)
// 반올림 방향 자체가 사라진다. 삭제된 오버로드는 런타임 흔적이 없어서 테스트로
// 고정하지 않으면 조용히 제거될 수 있다. 삭제된 함수를 이름 부르는 것은
// immediate context의 오류이므로 SFINAE로 검출된다. 양쪽 극성을 모두 못 박아
// 검출기가 공허하게 통과하지 못하게 한다.
template <class...>
using VoidT = void;

template <class TNumerator, class TDenominator, class = void>
struct FloorDivCallable : std::false_type {};
template <class TNumerator, class TDenominator>
struct FloorDivCallable<
    TNumerator, TDenominator,
    VoidT<decltype(molga::CheckedFloorDiv(std::declval<TNumerator>(),
                                          std::declval<TDenominator>()))>>
    : std::true_type {};

template <class TNumerator, class TDenominator, class = void>
struct CeilDivCallable : std::false_type {};
template <class TNumerator, class TDenominator>
struct CeilDivCallable<
    TNumerator, TDenominator,
    VoidT<decltype(molga::CheckedCeilDiv(std::declval<TNumerator>(),
                                         std::declval<TDenominator>()))>>
    : std::true_type {};

static_assert(!FloorDivCallable<double, int>::value,
              "CheckedFloorDiv with a floating-point numerator must be "
              "rejected");
static_assert(!FloorDivCallable<int, float>::value,
              "CheckedFloorDiv with a floating-point denominator must be "
              "rejected");
static_assert(!FloorDivCallable<double, double>::value,
              "CheckedFloorDiv with two floating-point arguments must be "
              "rejected");
static_assert(FloorDivCallable<int, int>::value,
              "CheckedFloorDiv with integer arguments must stay callable");
static_assert(FloorDivCallable<std::int64_t, std::int64_t>::value,
              "CheckedFloorDiv with 64-bit integers must stay callable");
static_assert(!CeilDivCallable<double, int>::value,
              "CheckedCeilDiv with a floating-point numerator must be "
              "rejected");
static_assert(!CeilDivCallable<int, float>::value,
              "CheckedCeilDiv with a floating-point denominator must be "
              "rejected");
static_assert(!CeilDivCallable<double, double>::value,
              "CheckedCeilDiv with two floating-point arguments must be "
              "rejected");
static_assert(CeilDivCallable<int, int>::value,
              "CheckedCeilDiv with integer arguments must stay callable");
static_assert(CeilDivCallable<std::int64_t, std::int64_t>::value,
              "CheckedCeilDiv with 64-bit integers must stay callable");

molga::ui::UIDrawOrderKey MakeKey() {
    // 네 필드가 서로 다른 값을 갖고, 어느 둘도 서로 바꿔치기해도 같은 키가
    // 되지 않도록 고른다. 같은 값을 두 필드에 넣으면 필드 뒤바뀜을 잡지 못한다.
    molga::ui::UIDrawOrderKey key;
    key.canvasSortingOrder = 3;
    key.siblingPath = {7, 11};
    key.componentSortingOrder = 5;
    key.stableSubmissionIndex = 9;
    return key;
}

} // namespace

// 위 케이스가 못 박지 못하는 나머지 정의역을 닫는다. floor/ceil은 물리 뷰포트/
// 시저 엣지를 만드는 마지막 변환이라 "min은 floor, max는 ceil"이 어긋나면
// 한 픽셀이 조용히 잘린다.
//
// 값 비교는 optional을 역참조하지 않고 optional 자체와 기대값을 비교한다.
// 역참조(`*opt == v`)는 회귀가 nullopt를 낼 때 UB가 되고 남아 있던 값이
// 우연히 기대값과 같아 통과할 수 있다. `opt == v`는 비어 있으면 항상 false라
// 실패로 닫힌다. 여기서는 센티넬 방식을 쓸 수 없다: INT64_MIN도 정당한
// 몫(INT64_MIN / 1)이라 어떤 정수도 "값 없음"을 대신할 수 없다.
TEST_CASE("checked fixed division covers signs, exactness and boundaries") {
    // 정확히 나누어떨어지면 floor와 ceil이 같은 몫을 낸다. 나머지가 0인데도
    // 한 쪽으로 밀어버리는 구현을 잡는다.
    CHECK(molga::CheckedFloorDiv(-128, 64) == -2);
    CHECK(molga::CheckedCeilDiv(-128, 64) == -2);
    CHECK(molga::CheckedFloorDiv(128, 64) == 2);
    CHECK(molga::CheckedCeilDiv(128, 64) == 2);
    CHECK(molga::CheckedFloorDiv(0, 64) == 0);
    CHECK(molga::CheckedCeilDiv(0, 64) == 0);

    // 음수 제수: C++의 / 는 0 방향으로 자르므로 부호 조합 네 가지를 모두
    // 확인해야 한다. 나머지 부호 판정을 잘못 접으면 여기서 갈린다.
    CHECK(molga::CheckedFloorDiv(-65, -64) == 1);
    CHECK(molga::CheckedCeilDiv(-65, -64) == 2);
    CHECK(molga::CheckedFloorDiv(65, -64) == -2);
    CHECK(molga::CheckedCeilDiv(65, -64) == -1);

    // 0으로 나누기는 두 함수 모두 거부한다(Step 1은 floor만 못 박는다).
    CHECK_FALSE(molga::CheckedCeilDiv(1, 0));
    CHECK_FALSE(molga::CheckedFloorDiv(0, 0));

    // INT64_MIN / -1만 오버플로다. 인접한 INT64_MIN / 1과 (INT64_MIN + 1) / -1은
    // 정상값이어야 한다 — 가드를 너무 넓게 잡은 구현을 잡는다.
    CHECK(molga::CheckedFloorDiv(INT64_MIN, 1) == INT64_MIN);
    CHECK(molga::CheckedCeilDiv(INT64_MIN, 1) == INT64_MIN);
    CHECK(molga::CheckedFloorDiv(INT64_MIN + 1, -1) == INT64_MAX);
    CHECK(molga::CheckedCeilDiv(INT64_MIN + 1, -1) == INT64_MAX);
    CHECK(molga::CheckedFloorDiv(INT64_MAX, -1) == -INT64_MAX);
    CHECK(molga::CheckedCeilDiv(INT64_MAX, -1) == -INT64_MAX);
}

TEST_CASE("fixed UI values provide symmetric C++17 equality") {
    const molga::Fixed26_6 one = molga::Fixed26_6::FromRaw(64);
    const molga::Fixed26_6 two = molga::Fixed26_6::FromRaw(128);
    CHECK(one == one);
    CHECK(one != two);
    CHECK(molga::FixedPoint{one, two} == molga::FixedPoint{one, two});
    CHECK(molga::FixedSize{one, two} != molga::FixedSize{two, one});
    CHECK(molga::FixedRect{one, one, two, two} !=
          molga::FixedRect{one, two, two, two});
}

TEST_CASE("fixed UI values differ in every single field") {
    const molga::Fixed26_6 a = molga::Fixed26_6::FromRaw(64);
    const molga::Fixed26_6 b = molga::Fixed26_6::FromRaw(128);

    // 한 필드만 다른 행렬. 어떤 필드를 비교에서 빼먹어도 그 행이 죽는다.
    CHECK(molga::FixedPoint{a, b} != molga::FixedPoint{b, b});
    CHECK(molga::FixedPoint{a, b} != molga::FixedPoint{a, a});
    CHECK(molga::FixedSize{a, b} != molga::FixedSize{b, b});
    CHECK(molga::FixedSize{a, b} != molga::FixedSize{a, a});
    CHECK(molga::FixedRect{a, b, a, b} != molga::FixedRect{b, b, a, b});
    CHECK(molga::FixedRect{a, b, a, b} != molga::FixedRect{a, a, a, b});
    CHECK(molga::FixedRect{a, b, a, b} != molga::FixedRect{a, b, b, b});
    CHECK(molga::FixedRect{a, b, a, b} != molga::FixedRect{a, b, a, a});

    // 같은 값끼리는 반드시 같다 — != 를 항상 true로 만든 구현을 잡는다.
    CHECK(molga::FixedSize{a, b} == molga::FixedSize{a, b});
    CHECK(molga::FixedRect{a, b, a, b} == molga::FixedRect{a, b, a, b});
    CHECK_FALSE(molga::FixedRect{a, b, a, b} != molga::FixedRect{a, b, a, b});
}

// 기본 멤버 초기자를 못 박는다. MakeKey()는 네 필드를 전부 명시적으로
// 채우므로 기본값이 바뀌어도 아래 케이스들은 전부 통과한다. 세 정수 필드의
// 기본 0은 "정렬 기준을 아직 주지 않았다"는 뜻이고, 0이 아닌 기본값은
// 지정하지 않은 키를 다른 캔버스/컴포넌트 순서로 밀어 넣는다.
TEST_CASE("UIDrawOrderKey default-constructs to the zero identity") {
    const molga::ui::UIDrawOrderKey key;
    CHECK(key.canvasSortingOrder == 0);
    CHECK(key.siblingPath.empty());
    CHECK(key.componentSortingOrder == 0);
    CHECK(key.stableSubmissionIndex == 0);

    molga::ui::UIDrawOrderKey explicitZero;
    explicitZero.canvasSortingOrder = 0;
    explicitZero.componentSortingOrder = 0;
    explicitZero.stableSubmissionIndex = 0;
    CHECK(key == explicitZero);
}

TEST_CASE("UIDrawOrderKey equality differs in every single field") {
    const molga::ui::UIDrawOrderKey base = MakeKey();
    CHECK(base == MakeKey());
    CHECK_FALSE(base != MakeKey());

    molga::ui::UIDrawOrderKey canvas = MakeKey();
    canvas.canvasSortingOrder = 4;
    CHECK(base != canvas);
    CHECK_FALSE(base == canvas);

    molga::ui::UIDrawOrderKey path = MakeKey();
    path.siblingPath = {7, 12};
    CHECK(base != path);

    // 길이만 다른 경로도 다른 키다. vector의 크기를 무시하고 앞쪽만 비교하는
    // 구현을 잡는다.
    molga::ui::UIDrawOrderKey shorterPath = MakeKey();
    shorterPath.siblingPath = {7};
    CHECK(base != shorterPath);

    molga::ui::UIDrawOrderKey longerPath = MakeKey();
    longerPath.siblingPath = {7, 11, 0};
    CHECK(base != longerPath);

    molga::ui::UIDrawOrderKey component = MakeKey();
    component.componentSortingOrder = 6;
    CHECK(base != component);

    molga::ui::UIDrawOrderKey submission = MakeKey();
    submission.stableSubmissionIndex = 10;
    CHECK(base != submission);
}

TEST_CASE("UIDrawOrderKey orders by canvas, path, component, then submission") {
    const molga::ui::UIDrawOrderKey base = MakeKey();

    // 엄격 약순서의 기본: 자기 자신보다 작지 않다.
    CHECK_FALSE(base < base);

    // 우선순위 검증은 반드시 "낮은 우선순위 필드가 반대 방향을 가리키는" 쌍으로
    // 해야 한다. 그러지 않으면 필드 순서를 뒤바꾼 구현도 같은 답을 낸다.
    molga::ui::UIDrawOrderKey higherCanvas = MakeKey();
    higherCanvas.canvasSortingOrder = 4;
    higherCanvas.siblingPath = {0};
    higherCanvas.componentSortingOrder = -100;
    higherCanvas.stableSubmissionIndex = 0;
    CHECK(base < higherCanvas);
    CHECK_FALSE(higherCanvas < base);

    // canvasSortingOrder는 부호 있는 값이다. 부호 없이 비교하는 구현을 잡는다.
    molga::ui::UIDrawOrderKey negativeCanvas = MakeKey();
    negativeCanvas.canvasSortingOrder = -1;
    CHECK(negativeCanvas < base);
    CHECK_FALSE(base < negativeCanvas);

    molga::ui::UIDrawOrderKey laterSibling = MakeKey();
    laterSibling.siblingPath = {7, 12};
    laterSibling.componentSortingOrder = -100;
    laterSibling.stableSubmissionIndex = 0;
    CHECK(base < laterSibling);
    CHECK_FALSE(laterSibling < base);

    // 접두사인 경로가 먼저다: 조상이 자손보다 앞선다.
    molga::ui::UIDrawOrderKey ancestor = MakeKey();
    ancestor.siblingPath = {7};
    ancestor.componentSortingOrder = 1000;
    ancestor.stableSubmissionIndex = 1000;
    CHECK(ancestor < base);
    CHECK_FALSE(base < ancestor);

    // 접두사가 아닌, 길이가 다른 경로. 길이를 먼저 보는 비교자("짧은 쪽이
    // 먼저")는 오직 이런 쌍에서만 사전식과 갈린다. base는 {7,11}이므로
    // 사전식으로는 {7,11} < {8} 이지만 길이 우선이면 {8} < {7,11} 이다.
    // 루트 자식 7 아래의 잎과 루트 자식 8은 어떤 계층에도 나오는 조합이라,
    // 이 쌍이 없으면 깊은 서브트리를 얕은 형제보다 먼저 그리는 구현이 통과한다.
    molga::ui::UIDrawOrderKey shallowLaterSubtree = MakeKey();
    shallowLaterSubtree.siblingPath = {8};
    shallowLaterSubtree.componentSortingOrder = -100;
    shallowLaterSubtree.stableSubmissionIndex = 0;
    CHECK(base < shallowLaterSubtree);
    CHECK_FALSE(shallowLaterSubtree < base);

    // 반대 방향도 닫는다: 더 긴 경로가 사전식으로 앞서는 경우.
    molga::ui::UIDrawOrderKey deeperEarlierSubtree = MakeKey();
    deeperEarlierSubtree.siblingPath = {6, 99, 99};
    deeperEarlierSubtree.componentSortingOrder = 1000;
    deeperEarlierSubtree.stableSubmissionIndex = 1000;
    CHECK(deeperEarlierSubtree < base);
    CHECK_FALSE(base < deeperEarlierSubtree);

    molga::ui::UIDrawOrderKey higherComponent = MakeKey();
    higherComponent.componentSortingOrder = 6;
    higherComponent.stableSubmissionIndex = 0;
    CHECK(base < higherComponent);
    CHECK_FALSE(higherComponent < base);

    // componentSortingOrder도 부호 있는 값이다.
    molga::ui::UIDrawOrderKey negativeComponent = MakeKey();
    negativeComponent.componentSortingOrder = -1;
    negativeComponent.stableSubmissionIndex = 1000;
    CHECK(negativeComponent < base);
    CHECK_FALSE(base < negativeComponent);

    molga::ui::UIDrawOrderKey laterSubmission = MakeKey();
    laterSubmission.stableSubmissionIndex = 10;
    CHECK(base < laterSubmission);
    CHECK_FALSE(laterSubmission < base);

    // 모든 필드가 같으면 어느 쪽도 앞서지 않는다(동치).
    CHECK_FALSE(base < MakeKey());
    CHECK_FALSE(MakeKey() < base);
}
