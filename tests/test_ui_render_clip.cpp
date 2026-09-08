// Task 11.1: 구체 렌더/hit 페이로드와 하나의 중첩 클립.
//
// 이 파일의 픽스처는 전부 프로덕션 입구를 지난다. 스냅샷을 손으로 조립해
// 검사하면 UILayoutSystem::Build를 통째로 이른 반환으로 바꿔도 스위트가
// 초록이므로, 여기서 만드는 것은 언제나 진짜 World와 진짜 TextLayoutService다.

#include "doctest.h"

#include "Assets/FontArtifactStore.h"
#include "Common/Fixed26_6.h"
#include "Common/Sha256.h"
#include "Core/AssetDatabase.h"
#include "Core/World.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/Components/UICanvas.h"
#include "ECS/Components/UIImage.h"
#include "ECS/Components/UILabel.h"
#include "ECS/Components/UIMask.h"
#include "ECS/Components/UIScrollView.h"
#include "ECS/Components/UISelectable.h"
#include "ECS/Components/UITextInput.h"
#include "ECS/GameObject.h"
#include "Rendering/GraphicsDevice.h"
#include "Rendering/TextureBindingRegistry.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutCache.h"
#include "Text/TextLayoutService.h"
#include "Text/TextShapingService.h"
#include "TextQualificationAssetTree.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UILayoutSystem.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UIRuntimeInvalidation.h"
#include "UI/UITextInputVisualState.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using molga::Fixed26_6;
using molga::FixedRect;
using molga::FixedSize;
using molga::text::TextDiagnosticCode;
using molga::ui::UISnapshotPtr;

namespace {

FixedSize RawSize(std::int32_t width, std::int32_t height) {
    return FixedSize{Fixed26_6::FromRaw(width), Fixed26_6::FromRaw(height)};
}

FixedRect RawRect(std::int32_t x, std::int32_t y, std::int32_t width,
                  std::int32_t height) {
    return FixedRect{Fixed26_6::FromRaw(x), Fixed26_6::FromRaw(y),
                     Fixed26_6::FromRaw(width), Fixed26_6::FromRaw(height)};
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
// 자격 트리를 그대로 쓴다. 폰트가 없는 서비스를 쓰면 모든 라벨이 배치에
// 실패해 텍스트 렌더 항목이 하나도 나오지 않고, 그러면 "텍스트는 자기 기록
// 수만큼 예약한다"는 주장이 공허하게 통과한다.
class RenderClipTextRuntime {
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

    RenderClipTextRuntime()
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

// ── 씬 조립 ─────────────────────────────────────────────────────────────────
// 앵커/피벗을 (0,0)으로 고정하면 확정된 사각형이 정확히 "부모 원점 + 저작
// 위치"라서, 기대값을 손으로 계산할 수 있다. 앵커까지 섞으면 실패한 검사가
// 클립 때문인지 앵커 산술 때문인지 구분되지 않는다.
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

UICanvas* AddConstantCanvas(GameObject& object) {
    auto* canvas = object.AddComponent<UICanvas>();
    REQUIRE(canvas != nullptr);
    canvas->SetScaleMode(UICanvasScaleMode::ConstantPixelSize);
    canvas->SetSortingOrder(0);
    return canvas;
}

// ── Task 11.1 F1: 배율이 1이 아닌 기본 모드 캔버스 ──────────────────────────
// UICanvas::scaleMode_의 기본값은 ScaleWithViewport다. 이 파일의 모든 픽스처가
// ConstantPixelSize를 쓰는 바람에 배율이 1로 고정되었고, 그래서 "스냅샷 경로와
// 레거시 경로가 일치한다"는 시험이 두 경로가 아니라 배율 1만 재고 있었다.
//
// match=0이면 폭이 기준이므로 배율은 viewport.x / referenceWidth 정확히
// 그것이다 — 기하 보간의 부동소수 오차 없이 정수 배율을 고를 수 있다.
UICanvas* AddScaledCanvas(GameObject& object, float referenceWidth,
                          float referenceHeight) {
    auto* canvas = object.AddComponent<UICanvas>();
    REQUIRE(canvas != nullptr);
    canvas->SetScaleMode(UICanvasScaleMode::ScaleWithViewport);
    canvas->SetReferenceResolution({referenceWidth, referenceHeight});
    canvas->SetMatchWidthOrHeight(0.0f);
    canvas->SetSortingOrder(0);
    return canvas;
}

UILabel* AddLabel(GameObject& object, const std::string& text) {
    auto* label = object.AddComponent<UILabel>();
    REQUIRE(label != nullptr);
    label->SetFontFamilyGuid(kPrimaryFamily);
    label->SetText(text);
    label->SetFontSizePx(16.0f);
    label->SetWrapMode(molga::text::TextWrapMode::NoWrap);
    return label;
}

const molga::ui::UIRenderItemSnapshot* FindRenderItem(
    const molga::ui::UISnapshot& snapshot, unsigned int objectId) {
    for (const auto& item : snapshot.renderItems) {
        if (item.canonicalSource.sceneObjectId == objectId) return &item;
    }
    return nullptr;
}

std::size_t CountRenderItems(const molga::ui::UISnapshot& snapshot,
                             unsigned int objectId,
                             const char* componentTypeName) {
    std::size_t total = 0;
    for (const auto& item : snapshot.renderItems) {
        if (item.canonicalSource.sceneObjectId == objectId &&
            item.canonicalSource.componentTypeName == componentTypeName) {
            ++total;
        }
    }
    return total;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Step 4a: 검증된 사각형 교집합
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("IntersectFixedRects returns nullopt for empty and overflowed input") {
    using molga::ui::IntersectFixedRects;

    const auto overlap =
        IntersectFixedRects(RawRect(0, 0, 64, 64), RawRect(32, 16, 64, 64));
    REQUIRE(overlap.has_value());
    // 네 필드를 전부 못 박는다. x/y 또는 width/height를 뒤바꾼 구현이 통과할
    // 수 없도록 어떤 두 값도 같지 않게 골랐다.
    CHECK(overlap->x.Raw() == 32);
    CHECK(overlap->y.Raw() == 16);
    CHECK(overlap->width.Raw() == 32);
    CHECK(overlap->height.Raw() == 48);

    // 반열린 사각형이라 변이 맞닿는 것은 빈 교집합이다. 폭 0짜리 사각형을
    // 돌려주면 "빈 교집합은 레코드를 제거한다"는 계약이 호출부마다 다시
    // 구현된다.
    CHECK_FALSE(
        IntersectFixedRects(RawRect(0, 0, 64, 64), RawRect(64, 0, 8, 8)));
    CHECK_FALSE(
        IntersectFixedRects(RawRect(0, 0, 64, 64), RawRect(0, 64, 8, 8)));
    // 완전히 떨어진 두 사각형.
    CHECK_FALSE(
        IntersectFixedRects(RawRect(0, 0, 64, 64), RawRect(128, 0, 64, 64)));

    // 음수 원점에서도 min/max가 뒤바뀌지 않는다.
    const auto negative =
        IntersectFixedRects(RawRect(-32, -16, 64, 64), RawRect(-33, 0, 2, 2));
    REQUIRE(negative.has_value());
    CHECK(negative->x.Raw() == -32);
    CHECK(negative->y.Raw() == 0);
    CHECK(negative->width.Raw() == 1);
    CHECK(negative->height.Raw() == 2);

    // 최대 변이 26.6 범위를 벗어나면 실패다. 포화시키면 잘리지 않아야 할
    // 것이 조용히 잘린다.
    const auto overflowed = IntersectFixedRects(
        RawRect(2147483000, 0, 1000, 64), RawRect(0, 0, 2147483000, 64));
    CHECK_FALSE(overflowed.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1j: C++17 값 동등성 행렬
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("payload identities compare field-wise with one-field differences") {
    using molga::ui::TextureRuntimeBindingIdentity;
    using molga::ui::UIFrozenTarget;
    using molga::ui::UIRuntimeBindingCacheIdentity;
    using molga::ui::UIStableComponentKey;

    UIStableComponentKey key;
    key.sceneObjectId = 11;
    key.componentTypeName = "UIImage";
    key.componentSchemaVersion = 3;
    CHECK(key == key);
    {
        UIStableComponentKey other = key;
        other.sceneObjectId = 12;
        CHECK(key != other);
    }
    {
        UIStableComponentKey other = key;
        other.componentTypeName = "UILabel";
        CHECK(key != other);
    }
    {
        UIStableComponentKey other = key;
        other.componentSchemaVersion = 4;
        CHECK(key != other);
    }

    molga::ui::UIRuntimeTargetIdentity runtime;
    runtime.worldGeneration = 5;
    runtime.objectId = 11;
    runtime.componentRuntimeTypeId = 7;
    runtime.componentInstanceId = 9;
    UIFrozenTarget frozen;
    frozen.runtimeTarget = runtime;
    frozen.canonicalTarget = key;
    CHECK(static_cast<bool>(frozen));
    CHECK(frozen == frozen);
    {
        UIFrozenTarget other = frozen;
        other.runtimeTarget.componentInstanceId = 10;
        CHECK(frozen != other);
    }
    {
        UIFrozenTarget other = frozen;
        other.canonicalTarget.sceneObjectId = 12;
        CHECK(frozen != other);
    }
    {
        UIFrozenTarget empty;
        CHECK_FALSE(static_cast<bool>(empty));
    }

    TextureRuntimeBindingIdentity binding;
    binding.deviceGeneration = 11;
    binding.uploadGeneration = 3;
    binding.texture = molga::detail::MakeTextureHandleForTest(4, 1);
    binding.sampler = molga::detail::MakeSamplerHandleForTest(5, 1);
    binding.lifetimeIdentity = 101;
    CHECK(binding == binding);
    {
        TextureRuntimeBindingIdentity other = binding;
        other.deviceGeneration = 12;
        CHECK(binding != other);
    }
    {
        TextureRuntimeBindingIdentity other = binding;
        other.uploadGeneration = 4;
        CHECK(binding != other);
    }
    {
        // 핸들 비교는 resource index와 handle generation을 모두 본다. index만
        // 보는 구현은 파괴 후 재사용된 슬롯을 같은 핸들로 읽는다.
        TextureRuntimeBindingIdentity sameIndexNewGeneration = binding;
        sameIndexNewGeneration.texture =
            molga::detail::MakeTextureHandleForTest(4, 2);
        CHECK(binding != sameIndexNewGeneration);
        TextureRuntimeBindingIdentity newIndexSameGeneration = binding;
        newIndexSameGeneration.texture =
            molga::detail::MakeTextureHandleForTest(6, 1);
        CHECK(binding != newIndexSameGeneration);
    }
    {
        TextureRuntimeBindingIdentity other = binding;
        other.sampler = molga::detail::MakeSamplerHandleForTest(5, 2);
        CHECK(binding != other);
    }
    {
        TextureRuntimeBindingIdentity other = binding;
        other.lifetimeIdentity = 202;
        CHECK(binding != other);
    }

    UIRuntimeBindingCacheIdentity cacheIdentity;
    cacheIdentity.sceneObjectId = 11;
    cacheIdentity.componentTypeName = "UIImage";
    cacheIdentity.componentSchemaVersion = 1;
    cacheIdentity.binding = binding;
    CHECK(cacheIdentity == cacheIdentity);
    {
        UIRuntimeBindingCacheIdentity other = cacheIdentity;
        other.sceneObjectId = 12;
        CHECK(cacheIdentity != other);
    }
    {
        UIRuntimeBindingCacheIdentity other = cacheIdentity;
        other.componentTypeName = "UILabel";
        CHECK(cacheIdentity != other);
    }
    {
        UIRuntimeBindingCacheIdentity other = cacheIdentity;
        other.componentSchemaVersion = 2;
        CHECK(cacheIdentity != other);
    }
    {
        UIRuntimeBindingCacheIdentity other = cacheIdentity;
        other.binding.lifetimeIdentity = 999;
        CHECK(cacheIdentity != other);
    }

    molga::PixelRectU32 pixels{3, 5, 7, 11};
    CHECK(pixels == pixels);
    CHECK(pixels != molga::PixelRectU32{4, 5, 7, 11});
    CHECK(pixels != molga::PixelRectU32{3, 6, 7, 11});
    CHECK(pixels != molga::PixelRectU32{3, 5, 8, 11});
    CHECK(pixels != molga::PixelRectU32{3, 5, 7, 12});
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6: 유일한 물리 변환
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("UIPhysicalTransform opens outward and inverts on the half-open edge") {
    molga::ui::UIPhysicalTransform transform;
    // 1배. 논리 100x50 단위(raw 6400x3200)를 물리 100x50 픽셀로.
    transform.logicalViewport = RawRect(0, 0, 100 * 64, 50 * 64);
    transform.physicalViewport = molga::PixelRectU32{0, 0, 100, 50};
    transform.deviceGeneration = 7;

    // 정확히 격자에 맞는 사각형은 그대로다.
    const auto exact = transform.ToPhysicalOutward(RawRect(64, 128, 640, 320));
    REQUIRE(exact.has_value());
    CHECK(*exact == molga::PixelRectU32{1, 2, 10, 5});

    // 반 픽셀 걸친 사각형은 바깥으로 열린다: 최소 변 floor, 최대 변 ceil.
    // 반올림이면 여기서 {1,2,10,5}가 나온다.
    const auto fractional =
        transform.ToPhysicalOutward(RawRect(96, 160, 640, 320));
    REQUIRE(fractional.has_value());
    CHECK(*fractional == molga::PixelRectU32{1, 2, 11, 6});

    // 1픽셀보다 얇아도 사라지지 않는다.
    const auto sliver = transform.ToPhysicalOutward(RawRect(80, 80, 8, 8));
    REQUIRE(sliver.has_value());
    CHECK(*sliver == molga::PixelRectU32{1, 1, 1, 1});

    // 빈 사각형과 뷰포트 밖은 nullopt다.
    CHECK_FALSE(transform.ToPhysicalOutward(RawRect(0, 0, 0, 64)));
    CHECK_FALSE(transform.ToPhysicalOutward(RawRect(100 * 64, 0, 64, 64)));

    // 뷰포트를 넘는 사각형은 물리 뷰포트로 잘린다.
    const auto clamped =
        transform.ToPhysicalOutward(RawRect(-640, -640, 200 * 64, 200 * 64));
    REQUIRE(clamped.has_value());
    CHECK(*clamped == molga::PixelRectU32{0, 0, 100, 50});

    // 역변환은 반열린 구간이다. 왼쪽 위 픽셀은 안, 오른쪽 아래 가장자리는 밖.
    const auto inside = transform.ToLogicalPoint(0.0, 0.0);
    REQUIRE(inside.has_value());
    CHECK(inside->x.Raw() == 0);
    CHECK(inside->y.Raw() == 0);
    const auto lastPixel = transform.ToLogicalPoint(99.0, 49.0);
    REQUIRE(lastPixel.has_value());
    CHECK(lastPixel->x.Raw() == 99 * 64);
    CHECK(lastPixel->y.Raw() == 49 * 64);
    CHECK_FALSE(transform.ToLogicalPoint(100.0, 25.0));
    CHECK_FALSE(transform.ToLogicalPoint(25.0, 50.0));
    CHECK_FALSE(transform.ToLogicalPoint(-0.5, 25.0));
    CHECK_FALSE(transform.ToLogicalPoint(
        std::numeric_limits<double>::quiet_NaN(), 25.0));

    // 왕복. 물리 픽셀 -> 논리 -> 물리는 그 픽셀을 다시 덮어야 한다.
    const auto roundTrip = transform.ToLogicalPoint(37.0, 21.0);
    REQUIRE(roundTrip.has_value());
    const auto back = transform.ToPhysicalOutward(
        FixedRect{roundTrip->x, roundTrip->y, Fixed26_6::FromRaw(64),
                  Fixed26_6::FromRaw(64)});
    REQUIRE(back.has_value());
    CHECK(back->x == 37);
    CHECK(back->y == 21);
}

TEST_CASE("UIPhysicalTransform affine covers both backing scales and origins") {
    molga::ui::UIPhysicalTransform unit;
    unit.logicalViewport = RawRect(0, 0, 100 * 64, 50 * 64);
    unit.physicalViewport = molga::PixelRectU32{0, 0, 100, 50};
    unit.deviceGeneration = 3;

    const auto identity =
        unit.LayoutToOutputAffine(molga::FixedPoint{Fixed26_6::FromRaw(0),
                                                    Fixed26_6::FromRaw(0)});
    REQUIRE(identity.has_value());
    CHECK(identity->m00 == doctest::Approx(1.0f));
    CHECK(identity->m11 == doctest::Approx(1.0f));
    CHECK(identity->m01 == doctest::Approx(0.0f));
    CHECK(identity->m10 == doctest::Approx(0.0f));
    CHECK(identity->tx == doctest::Approx(0.0f));
    CHECK(identity->ty == doctest::Approx(0.0f));

    // 논리 원점이 (3, 7)인 항목.
    const auto translated = unit.LayoutToOutputAffine(
        molga::FixedPoint{Fixed26_6::FromRaw(3 * 64),
                          Fixed26_6::FromRaw(7 * 64)});
    REQUIRE(translated.has_value());
    CHECK(translated->tx == doctest::Approx(3.0f));
    CHECK(translated->ty == doctest::Approx(7.0f));

    // 2배 backing scale에 0이 아닌 물리 뷰포트 원점.
    molga::ui::UIPhysicalTransform retina;
    retina.logicalViewport = RawRect(0, 0, 100 * 64, 50 * 64);
    retina.physicalViewport = molga::PixelRectU32{40, 24, 200, 100};
    retina.deviceGeneration = 4;
    const auto scaled = retina.LayoutToOutputAffine(
        molga::FixedPoint{Fixed26_6::FromRaw(3 * 64),
                          Fixed26_6::FromRaw(7 * 64)});
    REQUIRE(scaled.has_value());
    CHECK(scaled->m00 == doctest::Approx(2.0f));
    CHECK(scaled->m11 == doctest::Approx(2.0f));
    CHECK(scaled->m01 == doctest::Approx(0.0f));
    CHECK(scaled->m10 == doctest::Approx(0.0f));
    // physicalViewport.x + (logicalOrigin.x - logicalViewport.x) * scaleX
    CHECK(scaled->tx == doctest::Approx(40.0f + 3.0f * 2.0f));
    CHECK(scaled->ty == doctest::Approx(24.0f + 7.0f * 2.0f));

    // 0이 아닌 논리 뷰포트 원점도 함께 뺀다.
    molga::ui::UIPhysicalTransform shifted;
    shifted.logicalViewport = RawRect(10 * 64, 4 * 64, 100 * 64, 50 * 64);
    shifted.physicalViewport = molga::PixelRectU32{40, 24, 200, 100};
    shifted.deviceGeneration = 5;
    const auto shiftedAffine = shifted.LayoutToOutputAffine(
        molga::FixedPoint{Fixed26_6::FromRaw(13 * 64),
                          Fixed26_6::FromRaw(11 * 64)});
    REQUIRE(shiftedAffine.has_value());
    CHECK(shiftedAffine->tx == doctest::Approx(40.0f + 3.0f * 2.0f));
    CHECK(shiftedAffine->ty == doctest::Approx(24.0f + 7.0f * 2.0f));

    // 물리 원점을 옮겨도 반열린 역변환은 그 원점 기준이다.
    const auto atOrigin = shifted.ToLogicalPoint(40.0, 24.0);
    REQUIRE(atOrigin.has_value());
    CHECK(atOrigin->x.Raw() == 10 * 64);
    CHECK(atOrigin->y.Raw() == 4 * 64);
    CHECK_FALSE(shifted.ToLogicalPoint(39.0, 24.0));
    CHECK_FALSE(shifted.ToLogicalPoint(240.0, 50.0));

    // 래스터 배율은 affine의 float이 아니라 검증된 정수 규칙에서 온다.
    CountingDiagnosticSink sink;
    const auto policy = retina.RasterPolicy(sink);
    REQUIRE(policy.has_value());
    CHECK(policy->rasterScaleKey == 128);  // Q10.6에서 2배
    const auto unitPolicy = unit.RasterPolicy(sink);
    REQUIRE(unitPolicy.has_value());
    CHECK(unitPolicy->rasterScaleKey == 64);

    molga::ui::UIPhysicalTransform degenerate;
    degenerate.logicalViewport = RawRect(0, 0, 0, 0);
    degenerate.physicalViewport = molga::PixelRectU32{0, 0, 10, 10};
    CHECK_FALSE(degenerate.LayoutToOutputAffine(molga::FixedPoint{}));
    CHECK_FALSE(degenerate.ToPhysicalOutward(RawRect(0, 0, 64, 64)));
    CHECK_FALSE(degenerate.ToLogicalPoint(1.0, 1.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1a/1b: 중첩 클립과 그 가장자리 정책
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// 하나의 Canvas 아래에 마스크를 겹쳐 쌓고 잎에 시각 컴포넌트를 붙이는 픽스처.
// 앵커/피벗이 (0,0)이므로 확정된 사각형은 언제나 "부모 원점 + 저작 위치"다.
class UIClipFixture {
public:
    explicit UIClipFixture(FixedSize viewport = RawSize(200 * 64, 200 * 64))
        : viewport_(viewport) {
        root_ = AddObject(world_, 1, nullptr);
        AddConstantCanvas(*root_);
        AddOffsetRect(*root_, 0.0f, 0.0f, 200.0f, 200.0f);
        parent_ = root_;
    }

    // 마스크 하나를 현재 부모 아래에 쌓는다.
    GameObject& PushMask(float x, float y, float width, float height,
                         bool clips = true, bool enabled = true,
                         bool active = true) {
        GameObject* masker = AddObject(world_, nextId_++, parent_);
        AddOffsetRect(*masker, x, y, width, height);
        auto* mask = masker->AddComponent<UIMask>();
        REQUIRE(mask != nullptr);
        mask->SetClipsDescendants(clips);
        if (!enabled) mask->SetEnabled(false);
        if (!active) masker->SetActive(false);
        parent_ = masker;
        return *masker;
    }

    GameObject& AddImageLeaf(float x, float y, float width, float height) {
        GameObject* leaf = AddObject(world_, nextId_++, parent_);
        AddOffsetRect(*leaf, x, y, width, height);
        auto* image = leaf->AddComponent<UIImage>();
        REQUIRE(image != nullptr);
        image->SetTint(Color{0.25f, 0.5f, 0.75f, 1.0f});
        image->SetSortingOrder(0);
        return *leaf;
    }

    GameObject& AddButtonWithImageAndLabel(float x, float y, float width,
                                          float height) {
        GameObject& leaf = AddImageLeaf(x, y, width, height);
        auto* button = leaf.AddComponent<UIButton>();
        REQUIRE(button != nullptr);
        AddLabel(leaf, "abc");
        return leaf;
    }

    UISnapshotPtr Build() {
        return system_.Build(
            world_, molga::WindowId{7}, viewport_,
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            text_.service, sink_);
    }

    World& GetWorld() noexcept { return world_; }
    molga::ui::UILayoutSystem& System() noexcept { return system_; }
    CountingDiagnosticSink& Diagnostics() noexcept { return sink_; }
    unsigned int NextId() const noexcept { return nextId_; }

private:
    RenderClipTextRuntime text_;
    World world_;
    molga::ui::UILayoutSystem system_;
    CountingDiagnosticSink sink_;
    FixedSize viewport_;
    GameObject* root_ = nullptr;
    GameObject* parent_ = nullptr;
    unsigned int nextId_ = 2;
};

} // namespace

TEST_CASE("empty nested clip removes render and hit records together") {
    UIClipFixture f;
    f.PushMask(0.0f, 0.0f, 64.0f, 64.0f);
    // 두 번째 마스크는 첫 번째 바깥에 있다. 교집합이 비므로 그 아래 전부가
    // 렌더에서도 hit에서도 사라져야 한다 — 한쪽만 사라지면 보이지 않는
    // 버튼이 생긴다.
    f.PushMask(128.0f, 0.0f, 64.0f, 64.0f);
    f.AddButtonWithImageAndLabel(1.0f, 1.0f, 8.0f, 8.0f);
    const auto snapshot = f.Build();
    REQUIRE(snapshot);
    CHECK(snapshot->renderItems.empty());
    CHECK(snapshot->hitTargets.empty());

    // 이 검사가 공허하지 않다는 증인: 같은 잎을 겹치는 두 마스크 아래 두면
    // 세 컴포넌트가 전부 나온다.
    UIClipFixture visible;
    visible.PushMask(0.0f, 0.0f, 64.0f, 64.0f);
    visible.PushMask(8.0f, 8.0f, 32.0f, 32.0f);
    const GameObject& leaf =
        visible.AddButtonWithImageAndLabel(1.0f, 1.0f, 8.0f, 8.0f);
    const auto visibleSnapshot = visible.Build();
    REQUIRE(visibleSnapshot);
    CHECK(CountRenderItems(*visibleSnapshot, leaf.GetID(), "UIImage") == 1);
    CHECK(CountRenderItems(*visibleSnapshot, leaf.GetID(), "UIButton") == 1);
    CHECK(CountRenderItems(*visibleSnapshot, leaf.GetID(), "UILabel") == 1);
    CHECK(visibleSnapshot->hitTargets.size() == 1);
}

TEST_CASE("render and hit records share the exact clip and order") {
    UIClipFixture f;
    f.PushMask(0.0f, 0.0f, 64.0f, 64.0f);
    f.PushMask(8.0f, 8.0f, 32.0f, 32.0f);
    // 시각 컴포넌트가 하나뿐인 잎이라 렌더 항목도 hit 대상도 정확히 하나다.
    const GameObject& leaf = f.AddImageLeaf(1.0f, 1.0f, 8.0f, 8.0f);
    const auto snapshot = f.Build();
    REQUIRE(snapshot);
    REQUIRE(snapshot->renderItems.size() == 1);
    REQUIRE(snapshot->hitTargets.size() == 1);
    CHECK(snapshot->renderItems[0].logicalClip ==
          snapshot->hitTargets[0].logicalClip);
    CHECK(snapshot->renderItems[0].order == snapshot->hitTargets[0].order);
    CHECK(snapshot->renderItems[0].logicalRect ==
          snapshot->hitTargets[0].logicalRect);

    // 클립은 조상 둘의 교집합이지 가장 가까운 마스크 하나가 아니다.
    // 바깥 (0,0,64,64) 과 안쪽 (8,8,32,32) 의 교집합은 안쪽 자신이다.
    REQUIRE(snapshot->renderItems[0].logicalClip.has_value());
    CHECK(*snapshot->renderItems[0].logicalClip ==
          RawRect(8 * 64, 8 * 64, 32 * 64, 32 * 64));
    // 그리고 그 잎의 사각형은 안쪽 마스크 원점 기준이다.
    CHECK(snapshot->renderItems[0].logicalRect ==
          RawRect(9 * 64, 9 * 64, 8 * 64, 8 * 64));
    CHECK(snapshot->renderItems[0].canonicalSource.sceneObjectId ==
          leaf.GetID());
    CHECK(snapshot->hitTargets[0].canonicalTarget.sceneObjectId ==
          leaf.GetID());
}

namespace {

struct ClipCase {
    const char* name;
    // 마스크가 아예 없으면 nullopt. 있으면 그 사각형과 정책 플래그.
    std::optional<FixedRect> mask;
    bool maskClips = true;
    bool maskEnabled = true;
    bool ancestorActive = true;
    FixedRect child{};
    bool renders = false;
    bool hits = false;
};

float Units(std::int32_t raw) { return static_cast<float>(raw) / 64.0f; }

} // namespace

TEST_CASE("nested clip edge policy table") {
    const std::vector<ClipCase> cases{
        // 마스크가 없으면 넘친 부분도 그대로 보인다.
        {"unmasked-overflow", std::nullopt, true, true, true,
         RawRect(-10 * 64, 0, 80 * 64, 20 * 64), true, true},
        // 비활성 조상 아래는 아무것도 나오지 않는다.
        {"inactive-ancestor", RawRect(0, 0, 64 * 64, 64 * 64), true, true,
         false, RawRect(1 * 64, 1 * 64, 8 * 64, 8 * 64), false, false},
        // clipsDescendants=false 인 마스크는 자르지 않는다.
        {"disabled-mask", RawRect(0, 0, 64 * 64, 64 * 64), false, true, true,
         RawRect(70 * 64, 0, 8 * 64, 8 * 64), true, true},
        // 컴포넌트 자체를 끈 마스크도 자르지 않는다.
        {"mask-component-disabled", RawRect(0, 0, 64 * 64, 64 * 64), true,
         false, true, RawRect(70 * 64, 0, 8 * 64, 8 * 64), true, true},
        // 음수 원점에서도 min/max가 뒤바뀌지 않는다.
        {"negative-origin", RawRect(-32 * 64, -16 * 64, 64 * 64, 64 * 64), true,
         true, true, RawRect(-1 * 64, 0, 2 * 64, 2 * 64), true, true},
        // 오른쪽 변은 배타적이다. 맞닿기만 하면 빈 교집합이다.
        {"right-edge-exclusive", RawRect(0, 0, 64 * 64, 64 * 64), true, true,
         true, RawRect(64 * 64, 1 * 64, 1 * 64, 1 * 64), false, false},
        // 한 칸만 걸쳐도 남는다. 위 행의 반대편 증인이다.
        {"right-edge-inclusive-by-one", RawRect(0, 0, 64 * 64, 64 * 64), true,
         true, true, RawRect(63 * 64, 1 * 64, 8 * 64, 1 * 64), true, true},
    };

    for (const auto& c : cases) {
        CAPTURE(c.name);
        UIClipFixture f;
        if (c.mask) {
            f.PushMask(Units(c.mask->x.Raw()), Units(c.mask->y.Raw()),
                       Units(c.mask->width.Raw()), Units(c.mask->height.Raw()),
                       c.maskClips, c.maskEnabled, c.ancestorActive);
        }
        f.AddImageLeaf(Units(c.child.x.Raw()), Units(c.child.y.Raw()),
                       Units(c.child.width.Raw()), Units(c.child.height.Raw()));
        const auto snapshot = f.Build();
        REQUIRE(snapshot);
        CHECK((!snapshot->renderItems.empty()) == c.renders);
        CHECK((!snapshot->hitTargets.empty()) == c.hits);
        // 두 벡터는 언제나 같은 자격을 갖는다. 한쪽만 살아남는 구현이
        // 위 두 검사만으로는 통과할 수 있으므로 명시적으로 못 박는다.
        CHECK(snapshot->renderItems.empty() == snapshot->hitTargets.empty());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1c-1f: 내용 정체성, 런타임 바인딩, 그리고 그 소진 경계
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// AssetDatabase가 쓰는 것과 같은 유도 규칙. 내용에서 나온 값이라야 cold
// 빌드와 warm 빌드가 같은 바이트를 낸다.
std::uint64_t StableIdFromSha256(const std::string& sha256Hex) {
    std::uint64_t id = 0;
    for (std::size_t index = 0; index < 16 && index < sha256Hex.size();
         ++index) {
        const char digit = sha256Hex[index];
        std::uint64_t value = 0;
        if (digit >= '0' && digit <= '9') {
            value = static_cast<std::uint64_t>(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            value = static_cast<std::uint64_t>(digit - 'a') + 10U;
        } else {
            return 0;
        }
        id = (id << 4) | value;
    }
    return id == 0 ? 1U : id;
}

std::string Sha256Of(const std::string& bytes) {
    return molga::Sha256String(bytes);
}

struct DeviceBinding {
    std::uint64_t deviceGeneration = 0;
    std::uint64_t uploadGeneration = 0;
    molga::TextureHandle texture;
    molga::SamplerHandle sampler;
    std::uint64_t lifetimeIdentity = 0;
};

// 하나의 이미지/라벨/버튼/선택 가능 요소를 마스크 아래에 둔 픽스처.
// 텍스처 GUID는 인스턴스마다 다르다 — 두 등록부가 프로세스 전역이라
// 고정 GUID를 쓰면 케이스가 서로의 발행을 물려받는다.
class UISnapshotPayloadFixture {
public:
    UISnapshotPayloadFixture() {
        static unsigned int counter = 0;
        guid_ = "payload-texture-" + std::to_string(++counter);

        GameObject* root = AddObject(world_, 1, nullptr);
        AddConstantCanvas(*root);
        AddOffsetRect(*root, 0.0f, 0.0f, 200.0f, 200.0f);

        masker_ = AddObject(world_, 2, root);
        AddOffsetRect(*masker_, 0.0f, 0.0f, 64.0f, 64.0f);
        mask_ = masker_->AddComponent<UIMask>();
        REQUIRE(mask_ != nullptr);
        mask_->SetClipsDescendants(true);

        leaf_ = AddObject(world_, 3, masker_);
        AddOffsetRect(*leaf_, 1.0f, 2.0f, 20.0f, 10.0f);
        image_ = leaf_->AddComponent<UIImage>();
        REQUIRE(image_ != nullptr);
        image_->SetTextureGuid(guid_);
        image_->SetTint(Color{0.25f, 0.5f, 0.75f, 1.0f});
        image_->SetSortingOrder(0);
        label_ = AddLabel(*leaf_, "abc");
        label_->SetSortingOrder(1);
        button_ = leaf_->AddComponent<UIButton>();
        REQUIRE(button_ != nullptr);
        button_->SetSortingOrder(2);
        selectable_ = leaf_->AddComponent<UISelectable>();
        REQUIRE(selectable_ != nullptr);

        // 명시 방향 대상 하나. 형제로 두어 마스크 안에 남게 한다.
        neighbour_ = AddObject(world_, 4, masker_);
        AddOffsetRect(*neighbour_, 1.0f, 20.0f, 20.0f, 10.0f);
        neighbourSelectable_ = neighbour_->AddComponent<UISelectable>();
        REQUIRE(neighbourSelectable_ != nullptr);
    }

    const std::string& Guid() const noexcept { return guid_; }

    void SetTextureContent(const std::string& sha256) {
        molga::ui::UITextureContentIdentity identity;
        identity.contentSha256 = sha256;
        identity.contentStableId = StableIdFromSha256(sha256);
        molga::ui::UITextureContentRegistry::Get().Publish(guid_, identity);
    }

    molga::ui::UITextureBindingPublishResult BindTexture(
        const DeviceBinding& request) {
        molga::ui::TextureRuntimeBindingIdentity identity;
        identity.deviceGeneration = request.deviceGeneration;
        identity.uploadGeneration = request.uploadGeneration;
        identity.texture = request.texture;
        identity.sampler = request.sampler;
        identity.lifetimeIdentity = request.lifetimeIdentity;
        auto token = molga::TextureBindingRegistry::Get().Publish(identity);
        const auto result =
            molga::ui::UITextureBindingRegistry::Get().Publish(guid_, token);
        if (result == molga::ui::UITextureBindingPublishResult::Published) {
            retained_.push_back(token);
        }
        return result;
    }

    // 장치를 다시 만든다. 프로덕션에서 GraphicsDevice::Create가 하는 그
    // 취득이다.
    std::uint64_t RecreateDevice() {
        const auto generation = molga::ui::UIRuntimeInvalidationClock::Advance(
            molga::ui::UIRuntimeGenerationKind::Device);
        REQUIRE(generation.has_value());
        return *generation;
    }

    std::uint64_t CurrentDeviceGeneration() const {
        return molga::ui::UIRuntimeInvalidationClock::Current().deviceGeneration;
    }

    UISnapshotPtr Build() {
        return system_.Build(
            world_, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            text_.service, sink_);
    }

    std::uint64_t GeometryBuildCount() const noexcept {
        return system_.GeometryBuildCount();
    }

    const molga::ui::UISpriteSnapshot& Sprite(
        const molga::ui::UISnapshot& snapshot) const {
        const auto* item = FindRenderItem(snapshot, leaf_->GetID());
        REQUIRE(item != nullptr);
        REQUIRE(std::holds_alternative<molga::ui::UISpriteSnapshot>(
            item->payload));
        return std::get<molga::ui::UISpriteSnapshot>(item->payload);
    }

    const molga::ui::UITextSnapshot& Text(
        const molga::ui::UISnapshot& snapshot) const {
        for (const auto& item : snapshot.renderItems) {
            if (item.canonicalSource.componentTypeName != "UILabel") continue;
            REQUIRE(std::holds_alternative<molga::ui::UITextSnapshot>(
                item.payload));
            return std::get<molga::ui::UITextSnapshot>(item.payload);
        }
        FAIL("the snapshot has no UILabel render item");
        static const molga::ui::UITextSnapshot unreachable;
        return unreachable;
    }

    const molga::ui::UIHitTargetSnapshot& Hit(
        const molga::ui::UISnapshot& snapshot) const {
        for (const auto& hit : snapshot.hitTargets) {
            if (hit.canonicalTarget.sceneObjectId == leaf_->GetID()) return hit;
        }
        FAIL("the snapshot has no hit target for the leaf");
        static const molga::ui::UIHitTargetSnapshot unreachable;
        return unreachable;
    }

    bool LifetimeAlive(
        const std::shared_ptr<const molga::TextureBindingLifetime>& token)
        const {
        if (!token) return false;
        // 등록부는 약한 기록만 든다. 이 토큰이 만료되지 않았다면 등록부가
        // 그 장치 세대에 대해 살아 있는 소유자를 실제로 센다.
        return molga::TextureBindingRegistry::Get().LiveRetainedBindingCount(
                   token->Identity().deviceGeneration) > 0;
    }

    molga::TextureHandle TextureHandleA() const {
        return molga::detail::MakeTextureHandleForTest(11, 1);
    }
    molga::TextureHandle TextureHandleB() const {
        return molga::detail::MakeTextureHandleForTest(12, 1);
    }
    molga::SamplerHandle SamplerHandleA() const {
        return molga::detail::MakeSamplerHandleForTest(21, 1);
    }
    molga::SamplerHandle SamplerHandleB() const {
        return molga::detail::MakeSamplerHandleForTest(22, 1);
    }

    UIImage& Image() noexcept { return *image_; }
    UILabel& Label() noexcept { return *label_; }
    UIButton& Button() noexcept { return *button_; }
    UISelectable& Selectable() noexcept { return *selectable_; }
    UISelectable& NeighbourSelectable() noexcept {
        return *neighbourSelectable_;
    }
    UIMask& Mask() noexcept { return *mask_; }
    GameObject& Leaf() noexcept { return *leaf_; }
    GameObject& Neighbour() noexcept { return *neighbour_; }
    World& GetWorld() noexcept { return world_; }
    molga::ui::UILayoutSystem& System() noexcept { return system_; }
    CountingDiagnosticSink& Diagnostics() noexcept { return sink_; }

private:
    RenderClipTextRuntime text_;
    World world_;
    molga::ui::UILayoutSystem system_;
    CountingDiagnosticSink sink_;
    std::string guid_;
    GameObject* masker_ = nullptr;
    GameObject* leaf_ = nullptr;
    GameObject* neighbour_ = nullptr;
    UIMask* mask_ = nullptr;
    UIImage* image_ = nullptr;
    UILabel* label_ = nullptr;
    UIButton* button_ = nullptr;
    UISelectable* selectable_ = nullptr;
    UISelectable* neighbourSelectable_ = nullptr;
    std::vector<std::shared_ptr<const molga::TextureBindingLifetime>> retained_;
};

DeviceBinding FirstBinding(const UISnapshotPayloadFixture& f,
                           std::uint64_t deviceGeneration) {
    DeviceBinding binding;
    binding.deviceGeneration = deviceGeneration;
    binding.uploadGeneration = 3;
    binding.texture = f.TextureHandleA();
    binding.sampler = f.SamplerHandleA();
    binding.lifetimeIdentity = 101;
    return binding;
}

} // namespace

TEST_CASE("same texture GUID with new content SHA republishes sprite payload") {
    UISnapshotPayloadFixture f;
    f.SetTextureContent(Sha256Of("pixels-a"));
    f.BindTexture(FirstBinding(f, f.CurrentDeviceGeneration()));
    const auto first = f.Build();
    REQUIRE(first);
    const auto geometryBuilds = f.GeometryBuildCount();
    REQUIRE(std::holds_alternative<molga::ui::UISpriteSnapshot>(
        FindRenderItem(*first, f.Leaf().GetID())->payload));
    CHECK(f.Sprite(*first).textureContentSha256 == Sha256Of("pixels-a"));

    f.SetTextureContent(Sha256Of("pixels-b"));
    const auto second = f.Build();
    REQUIRE(second);
    CHECK(first.get() != second.get());
    // 내용이 바뀌어도 기하는 그대로다. 재측정하면 이 값이 오른다.
    CHECK(f.GeometryBuildCount() == geometryBuilds);
    CHECK(f.Sprite(*second).textureContentSha256 == Sha256Of("pixels-b"));
    CHECK(f.Sprite(*second).textureContentStableId !=
          f.Sprite(*first).textureContentStableId);
    CHECK(f.Sprite(*second).textureGuid == f.Guid());
    // 그리고 처음 게시된 스냅샷은 그대로 옛 값을 본다.
    CHECK(f.Sprite(*first).textureContentSha256 == Sha256Of("pixels-a"));
}

TEST_CASE("every payload-bearing edit republishes its concrete value") {
    // 포인터 부등만 보면 "매번 새로 만든다"는 구현도 통과한다. 각 행은
    // 두 번째 스냅샷의 구체 값을 직접 못 박는다.
    SUBCASE("label text") {
        UISnapshotPayloadFixture f;
        const auto before = f.Build();
        REQUIRE(before);
        const auto geometryBuilds = f.GeometryBuildCount();
        const std::size_t beforeGlyphs =
            f.Text(*before).layout ? f.Text(*before).layout->caretStops.size()
                                   : 0;
        f.Label().SetText("abcdef");
        const auto after = f.Build();
        REQUIRE(after);
        CHECK(before.get() != after.get());
        // Task 11.1 F3: 글이 바뀌면 제약을 벗긴 고유 크기도 바뀐다. Build가
        // 그 값을 게시하므로 intrinsicGenerations가 움직이고 기하는 반드시
        // 다시 지어진다 — 이것은 페이로드만 바뀐 편집이 아니다. 이 자리가
        // "재사용"이었던 것은 게시하는 사람이 아무도 없어서였다.
        CHECK(f.GeometryBuildCount() > geometryBuilds);
        REQUIRE(f.Text(*after).layout);
        CHECK(f.Text(*after).layout->caretStops.size() != beforeGlyphs);
        CHECK(f.Text(*before).layout != f.Text(*after).layout);
    }
    SUBCASE("label color") {
        UISnapshotPayloadFixture f;
        const auto before = f.Build();
        REQUIRE(before);
        f.Label().SetColor(Color{0.1f, 0.2f, 0.3f, 0.4f});
        const auto after = f.Build();
        REQUIRE(after);
        CHECK(f.Text(*after).color.r == doctest::Approx(0.1f));
        CHECK(f.Text(*after).color.g == doctest::Approx(0.2f));
        CHECK(f.Text(*after).color.b == doctest::Approx(0.3f));
        CHECK(f.Text(*after).color.a == doctest::Approx(0.4f));
        CHECK(f.Text(*before).color.r != doctest::Approx(0.1f));
    }
    SUBCASE("image tint") {
        UISnapshotPayloadFixture f;
        const auto before = f.Build();
        REQUIRE(before);
        f.Image().SetTint(Color{0.9f, 0.8f, 0.7f, 0.6f});
        const auto after = f.Build();
        REQUIRE(after);
        CHECK(f.Sprite(*after).tint.r == doctest::Approx(0.9f));
        CHECK(f.Sprite(*after).tint.a == doctest::Approx(0.6f));
        CHECK(f.Sprite(*before).tint.r == doctest::Approx(0.25f));
    }
    SUBCASE("interactability") {
        UISnapshotPayloadFixture f;
        const auto before = f.Build();
        REQUIRE(before);
        CHECK(f.Hit(*before).canonicalTarget.componentTypeName == "UIButton");
        REQUIRE(f.Hit(*before).focusTarget.has_value());
        CHECK(f.Hit(*before).focusable);
        f.Selectable().SetInteractable(false);
        f.Button().SetInteractable(false);
        const auto after = f.Build();
        REQUIRE(after);
        // 버튼이 상호작용 불가가 되면 동작 대상이 이미지로 내려간다.
        CHECK(f.Hit(*after).canonicalTarget.componentTypeName == "UIImage");
        CHECK_FALSE(f.Hit(*after).focusTarget.has_value());
        CHECK_FALSE(f.Hit(*after).focusable);
    }
    SUBCASE("explicit navigation") {
        UISnapshotPayloadFixture f;
        const auto before = f.Build();
        REQUIRE(before);
        CHECK(f.Hit(*before).navigation.mode == UINavigationMode::Auto);
        CHECK_FALSE(f.Hit(*before).navigation.canonicalTargets[0].has_value());
        f.Selectable().SetNavigationMode(UINavigationMode::Explicit);
        SceneObjectRef up;
        up.targetId = f.Neighbour().GetID();
        f.Selectable().SetNavigateUp(up);
        const auto after = f.Build();
        REQUIRE(after);
        CHECK(f.Hit(*after).navigation.mode == UINavigationMode::Explicit);
        REQUIRE(f.Hit(*after).navigation.canonicalTargets[0].has_value());
        CHECK(f.Hit(*after).navigation.canonicalTargets[0]->sceneObjectId ==
              f.Neighbour().GetID());
        REQUIRE(f.Hit(*after).navigation.explicitTargets[0].has_value());
        CHECK(*f.Hit(*after).navigation.explicitTargets[0] ==
              molga::ui::CaptureTarget(f.GetWorld(), f.NeighbourSelectable()));
        // 나머지 세 축은 여전히 비어 있다.
        CHECK_FALSE(f.Hit(*after).navigation.canonicalTargets[1].has_value());
    }
    SUBCASE("mask enablement") {
        UISnapshotPayloadFixture f;
        const auto before = f.Build();
        REQUIRE(before);
        REQUIRE(f.Hit(*before).logicalClip.has_value());
        CHECK(*f.Hit(*before).logicalClip == RawRect(0, 0, 64 * 64, 64 * 64));
        f.Mask().SetClipsDescendants(false);
        const auto after = f.Build();
        REQUIRE(after);
        CHECK_FALSE(f.Hit(*after).logicalClip.has_value());
        CHECK_FALSE(FindRenderItem(*after, f.Leaf().GetID())
                        ->logicalClip.has_value());
    }
}

TEST_CASE("same texture content republishes bindings after device recreation") {
    UISnapshotPayloadFixture f;
    f.SetTextureContent(Sha256Of("same-pixels"));
    const std::uint64_t firstDevice = f.CurrentDeviceGeneration();
    DeviceBinding first;
    first.deviceGeneration = firstDevice;
    first.uploadGeneration = 3;
    first.texture = f.TextureHandleA();
    first.sampler = f.SamplerHandleA();
    first.lifetimeIdentity = 101;
    REQUIRE(f.BindTexture(first) ==
            molga::ui::UITextureBindingPublishResult::Published);

    const auto oldSnapshot = f.Build();
    REQUIRE(oldSnapshot);
    const auto oldJson = molga::ui::StableLayoutSnapshotJson(*oldSnapshot);
    const auto geometryBuilds = f.GeometryBuildCount();
    const auto oldLifetime = f.Sprite(*oldSnapshot).resourceLifetime;
    REQUIRE(oldLifetime != nullptr);

    // 장치를 다시 만든다. 내용은 그대로이므로 정규 바이트는 달라지면 안 되고,
    // 핸들과 수명은 반드시 새것이어야 한다.
    const std::uint64_t secondDevice = f.RecreateDevice();
    CHECK(secondDevice > firstDevice);
    DeviceBinding second;
    second.deviceGeneration = secondDevice;
    second.uploadGeneration = 1;
    second.texture = f.TextureHandleB();
    second.sampler = f.SamplerHandleB();
    second.lifetimeIdentity = 202;
    REQUIRE(f.BindTexture(second) ==
            molga::ui::UITextureBindingPublishResult::Published);

    const auto newSnapshot = f.Build();
    REQUIRE(newSnapshot);
    CHECK(oldSnapshot.get() != newSnapshot.get());
    CHECK(f.GeometryBuildCount() == geometryBuilds);
    CHECK(f.Sprite(*newSnapshot).binding.texture == f.TextureHandleB());
    CHECK(f.Sprite(*newSnapshot).binding.sampler == f.SamplerHandleB());
    CHECK(f.Sprite(*newSnapshot).binding.deviceGeneration == secondDevice);
    CHECK(f.Sprite(*newSnapshot).binding.uploadGeneration == 1);
    CHECK(f.Sprite(*newSnapshot).binding.lifetimeIdentity == 202);
    CHECK(f.Sprite(*newSnapshot).resourceLifetime != oldLifetime);
    // 옛 스냅샷은 여전히 자기 핸들을 본다. 갱신되었다면 이 검사가 죽는다.
    CHECK(f.Sprite(*oldSnapshot).binding.texture == f.TextureHandleA());
    CHECK(f.LifetimeAlive(oldLifetime));
    // 게시된 수명 객체의 값은 바인딩과 필드 하나까지 같아야 한다.
    CHECK(f.Sprite(*newSnapshot).resourceLifetime->Identity() ==
          f.Sprite(*newSnapshot).binding);
    CHECK(molga::ui::StableLayoutSnapshotJson(*newSnapshot) == oldJson);
    // 그리고 그 등식이 공허하지 않다는 증인: 내용이 바뀌면 바이트가 달라진다.
    f.SetTextureContent(Sha256Of("other-pixels"));
    const auto changed = f.Build();
    REQUIRE(changed);
    CHECK(molga::ui::StableLayoutSnapshotJson(*changed) != oldJson);
}

TEST_CASE("one runtime binding field at a time misses the full cache only") {
    struct BindingRow {
        const char* name;
        bool recreateDevice = false;
        std::uint64_t uploadGeneration = 3;
        bool newTexture = false;
        bool newSampler = false;
        std::uint64_t lifetimeIdentity = 101;
    };
    const std::vector<BindingRow> rows{
        {"device", true, 3, false, false, 101},
        {"upload", false, 4, false, false, 101},
        {"texture", false, 3, true, false, 101},
        {"sampler", false, 3, false, true, 101},
        {"lifetime", false, 3, false, false, 202},
    };

    for (const auto& row : rows) {
        CAPTURE(row.name);
        UISnapshotPayloadFixture f;
        f.SetTextureContent(Sha256Of("unchanged-pixels"));
        const std::uint64_t baseDevice = f.CurrentDeviceGeneration();
        REQUIRE(f.BindTexture(FirstBinding(f, baseDevice)) ==
                molga::ui::UITextureBindingPublishResult::Published);
        const auto warm = f.Build();
        REQUIRE(warm);
        const auto warmJson = molga::ui::StableLayoutSnapshotJson(*warm);
        const auto geometryBuilds = f.GeometryBuildCount();
        const auto clockBefore =
            molga::ui::UIRuntimeInvalidationClock::Current();

        DeviceBinding next;
        next.deviceGeneration =
            row.recreateDevice ? f.RecreateDevice() : baseDevice;
        next.uploadGeneration = row.uploadGeneration;
        next.texture = row.newTexture ? f.TextureHandleB() : f.TextureHandleA();
        next.sampler = row.newSampler ? f.SamplerHandleB() : f.SamplerHandleA();
        next.lifetimeIdentity = row.lifetimeIdentity;
        REQUIRE(f.BindTexture(next) ==
                molga::ui::UITextureBindingPublishResult::Published);

        const auto clockAfter =
            molga::ui::UIRuntimeInvalidationClock::Current();
        if (row.recreateDevice) {
            CHECK(clockAfter.deviceGeneration >
                  clockBefore.deviceGeneration);
        } else {
            CHECK(clockAfter.deviceGeneration ==
                  clockBefore.deviceGeneration);
        }
        // 바인딩 축은 언제나 앞으로 간다. 장치 행에서도 그렇다 —
        // UITextureBindingRegistry가 실제 변경 하나에 한 번 올린다.
        CHECK(clockAfter.textureBindingGeneration >
              clockBefore.textureBindingGeneration);

        const auto rebound = f.Build();
        REQUIRE(rebound);
        CHECK(rebound.get() != warm.get());
        CHECK(f.GeometryBuildCount() == geometryBuilds);
        // 정확히 그 필드가 바뀌었고 나머지는 그대로다. 해시만 비교하는
        // 구현은 여기서 index/generation이 다른 핸들을 같다고 읽는다.
        const auto& sprite = f.Sprite(*rebound);
        CHECK(sprite.binding.deviceGeneration == next.deviceGeneration);
        CHECK(sprite.binding.uploadGeneration == next.uploadGeneration);
        CHECK(sprite.binding.texture == next.texture);
        CHECK(sprite.binding.sampler == next.sampler);
        CHECK(sprite.binding.lifetimeIdentity == next.lifetimeIdentity);
        CHECK(sprite.resourceLifetime->Identity() == sprite.binding);
        // 내용은 그대로이므로 정규 바이트도 그대로다.
        CHECK(molga::ui::StableLayoutSnapshotJson(*rebound) == warmJson);
    }
}

TEST_CASE("an exhausted binding clock publishes nothing and blocks caching") {
    UISnapshotPayloadFixture f;
    f.SetTextureContent(Sha256Of("exhaustion-pixels"));
    const std::uint64_t device = f.CurrentDeviceGeneration();
    REQUIRE(f.BindTexture(FirstBinding(f, device)) ==
            molga::ui::UITextureBindingPublishResult::Published);
    const auto before = f.Build();
    REQUIRE(before);
    const auto beforeLifetime = f.Sprite(*before).resourceLifetime;
    REQUIRE(beforeLifetime != nullptr);
    molga::ui::UISnapshotWorldDeviceSlotKey warmSlot;
    warmSlot.worldGeneration = f.GetWorld().Generation();
    warmSlot.deviceGeneration =
        molga::ui::UIRuntimeInvalidationClock::Current().deviceGeneration;
    const std::size_t slotsBeforeExhaustion =
        f.System().FullSnapshotCacheEntryCountForWorldDevice(warmSlot);
    REQUIRE(slotsBeforeExhaustion == 1);
    f.Diagnostics().Clear();

    molga::ui::ScopedUIRuntimeGenerationForTesting exhausted(
        molga::ui::UIRuntimeGenerationKind::TextureBinding,
        std::numeric_limits<std::uint64_t>::max());

    DeviceBinding reupload;
    reupload.deviceGeneration = device;
    reupload.uploadGeneration = 9;
    reupload.texture = f.TextureHandleB();
    reupload.sampler = f.SamplerHandleB();
    reupload.lifetimeIdentity = 999;
    CHECK(f.BindTexture(reupload) ==
          molga::ui::UITextureBindingPublishResult::Exhausted);

    // 아무것도 게시되지 않았다. 옛 바인딩이 그대로 남아 있다.
    const auto stillBound =
        molga::ui::UITextureBindingRegistry::Get().Find(f.Guid());
    REQUIRE(stillBound.has_value());
    CHECK(stillBound->binding.texture == f.TextureHandleA());
    CHECK(stillBound->binding.lifetimeIdentity == 101);
    CHECK(stillBound->lifetime == beforeLifetime);
    // 0도 재사용된 값도 발행되지 않았다.
    CHECK(molga::ui::UIRuntimeInvalidationClock::Current()
              .textureBindingGeneration ==
          std::numeric_limits<std::uint64_t>::max());
    CHECK_FALSE(molga::ui::UIRuntimeInvalidationClock::Current().cacheable);

    const auto during = f.Build();
    REQUIRE(during);
    // 차단 진단은 정확히 하나다. 프레임마다 내면 로그가 이것 하나로 찬다.
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);
    bool sawBlocker = false;
    for (const auto& record : f.Diagnostics().Records()) {
        if (record.severity == molga::text::TextSeverity::Blocker) {
            sawBlocker = true;
        }
    }
    CHECK(sawBlocker);
    const auto again = f.Build();
    REQUIRE(again);
    CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 1);
    // 조회도 삽입도 하지 않는다: 같은 상태를 두 번 지어도 같은 객체가 아니고,
    // 소진 이전에 들어 있던 슬롯 하나가 늘지도 줄지도 않는다. (그 슬롯은
    // 남아 있되 다시는 조회되지 않는다 — 지워 버리면 시계가 회복될 수 없는
    // 이 경계에서 아무 차이도 없고, 조회를 건너뛴다는 사실만이 계약이다.)
    CHECK(during.get() != again.get());
    CHECK(during.get() != before.get());
    molga::ui::UISnapshotWorldDeviceSlotKey slot;
    slot.worldGeneration = f.GetWorld().Generation();
    slot.deviceGeneration =
        molga::ui::UIRuntimeInvalidationClock::Current().deviceGeneration;
    CHECK(f.System().FullSnapshotCacheEntryCountForWorldDevice(slot) ==
          slotsBeforeExhaustion);
    // 그리고 직전 스냅샷과 그 수명은 손대지 않은 채 남아 있다.
    CHECK(f.Sprite(*before).binding.texture == f.TextureHandleA());
    CHECK(f.LifetimeAlive(beforeLifetime));
    CHECK(f.Sprite(*during).binding.texture == f.TextureHandleA());
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1g: 하나의 hit이 얼리는 동작/포커스/텍스트/스크롤 대상
// ─────────────────────────────────────────────────────────────────────────────
namespace {

class UIMultiTargetFixture {
public:
    UIMultiTargetFixture() {
        GameObject* root = AddObject(world_, 1, nullptr);
        AddConstantCanvas(*root);
        AddOffsetRect(*root, 0.0f, 0.0f, 200.0f, 200.0f);

        outer_ = AddObject(world_, 2, root);
        AddOffsetRect(*outer_, 0.0f, 0.0f, 120.0f, 120.0f);
        outerScroll_ = outer_->AddComponent<UIScrollView>();
        REQUIRE(outerScroll_ != nullptr);

        inner_ = AddObject(world_, 3, outer_);
        AddOffsetRect(*inner_, 0.0f, 0.0f, 80.0f, 80.0f);
        innerScroll_ = inner_->AddComponent<UIScrollView>();
        REQUIRE(innerScroll_ != nullptr);

        target_ = AddObject(world_, 4, inner_);
        AddOffsetRect(*target_, 1.0f, 1.0f, 40.0f, 20.0f);
        button_ = target_->AddComponent<UIButton>();
        REQUIRE(button_ != nullptr);
        selectable_ = target_->AddComponent<UISelectable>();
        REQUIRE(selectable_ != nullptr);
        input_ = target_->AddComponent<UITextInput>();
        REQUIRE(input_ != nullptr);

        rendered_ = AddObject(world_, 5, target_);
        AddOffsetRect(*rendered_, 0.0f, 0.0f, 40.0f, 20.0f);
        renderedLabel_ = AddLabel(*rendered_, "abc");
        SceneObjectRef ref;
        ref.targetId = rendered_->GetID();
        input_->SetRenderedLabel(ref);
    }

    UISnapshotPtr Build() {
        return system_.Build(
            world_, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            text_.service, sink_);
    }

    molga::ui::UIRuntimeTargetIdentity ButtonActionIdentity() {
        return molga::ui::CaptureTarget(world_, *button_);
    }
    molga::ui::UIRuntimeTargetIdentity SelectableIdentity() {
        return molga::ui::CaptureTarget(world_, *selectable_);
    }
    molga::ui::UIRuntimeTargetIdentity TextInputIdentity() {
        return molga::ui::CaptureTarget(world_, *input_);
    }
    molga::ui::UIRuntimeTargetIdentity InnerScrollIdentity() {
        return molga::ui::CaptureTarget(world_, *innerScroll_);
    }
    molga::ui::UIRuntimeTargetIdentity OuterScrollIdentity() {
        return molga::ui::CaptureTarget(world_, *outerScroll_);
    }

    UISelectable& Selectable() noexcept { return *selectable_; }
    UITextInput& Input() noexcept { return *input_; }
    UIScrollView& InnerScroll() noexcept { return *innerScroll_; }
    GameObject& Inner() noexcept { return *inner_; }
    GameObject& Outer() noexcept { return *outer_; }
    GameObject& Target() noexcept { return *target_; }
    World& GetWorld() noexcept { return world_; }
    CountingDiagnosticSink& Diagnostics() noexcept { return sink_; }

private:
    RenderClipTextRuntime text_;
    World world_;
    molga::ui::UILayoutSystem system_;
    CountingDiagnosticSink sink_;
    GameObject* outer_ = nullptr;
    GameObject* inner_ = nullptr;
    GameObject* target_ = nullptr;
    GameObject* rendered_ = nullptr;
    UIScrollView* outerScroll_ = nullptr;
    UIScrollView* innerScroll_ = nullptr;
    UIButton* button_ = nullptr;
    UISelectable* selectable_ = nullptr;
    UITextInput* input_ = nullptr;
    UILabel* renderedLabel_ = nullptr;
};

} // namespace

TEST_CASE("one hit freezes action focus text and inner-to-outer scroll targets") {
    UIMultiTargetFixture f;
    const auto snapshot = f.Build();
    REQUIRE(snapshot);
    // 입력창이 소유한 라벨은 자기 hit 레코드를 만들지 않는다. 만들면 그 라벨이
    // 입력창보다 위에 있으므로 텍스트를 누를 때 포커스가 사라진다.
    REQUIRE(snapshot->hitTargets.size() == 1);
    const auto& hit = snapshot->hitTargets.front();
    CHECK(hit.target == f.ButtonActionIdentity());
    CHECK(hit.canonicalTarget.componentTypeName == "UIButton");
    CHECK(hit.canonicalTarget.sceneObjectId == f.Target().GetID());
    REQUIRE(hit.focusTarget.has_value());
    CHECK(hit.focusTarget->runtimeTarget == f.SelectableIdentity());
    CHECK(hit.focusTarget->canonicalTarget.componentTypeName == "UISelectable");
    REQUIRE(hit.textInputTarget.has_value());
    CHECK(hit.textInputTarget->runtimeTarget == f.TextInputIdentity());
    CHECK(hit.textInputTarget->canonicalTarget.componentTypeName ==
          "UITextInput");
    REQUIRE(hit.scrollTargets.size() == 2);
    // 안쪽에서 바깥쪽. 뒤집힌 구현은 여기서 죽는다.
    CHECK(hit.scrollTargets[0].runtimeTarget == f.InnerScrollIdentity());
    CHECK(hit.scrollTargets[1].runtimeTarget == f.OuterScrollIdentity());
    CHECK(hit.scrollTargets[0].canonicalTarget.sceneObjectId ==
          f.Inner().GetID());
    CHECK(hit.scrollTargets[1].canonicalTarget.sceneObjectId ==
          f.Outer().GetID());
    CHECK(hit.focusable);
    CHECK(hit.acceptsTextInput);
    CHECK(hit.interactable);
    // 얼어붙은 쌍은 언제나 런타임과 정규 정체성을 모두 담는다.
    CHECK(static_cast<bool>(*hit.focusTarget));
    CHECK(hit.focusTarget->canonicalTarget.componentSchemaVersion ==
          UISelectable::CurrentSchemaVersion);
}

TEST_CASE("disabled stage components drop exactly their own frozen target") {
    SUBCASE("disabled selectable removes the focus target only") {
        UIMultiTargetFixture f;
        f.Selectable().SetEnabled(false);
        const auto snapshot = f.Build();
        REQUIRE(snapshot);
        REQUIRE(snapshot->hitTargets.size() == 1);
        const auto& hit = snapshot->hitTargets.front();
        CHECK_FALSE(hit.focusTarget.has_value());
        CHECK_FALSE(hit.focusable);
        CHECK(hit.textInputTarget.has_value());
        CHECK(hit.scrollTargets.size() == 2);
        CHECK(hit.target == f.ButtonActionIdentity());
    }
    SUBCASE("non-interactable selectable removes the focus target only") {
        UIMultiTargetFixture f;
        f.Selectable().SetInteractable(false);
        const auto snapshot = f.Build();
        REQUIRE(snapshot);
        REQUIRE(snapshot->hitTargets.size() == 1);
        CHECK_FALSE(snapshot->hitTargets.front().focusTarget.has_value());
        CHECK(snapshot->hitTargets.front().textInputTarget.has_value());
    }
    SUBCASE("disabled text input removes the text target only") {
        UIMultiTargetFixture f;
        f.Input().SetEnabled(false);
        const auto snapshot = f.Build();
        REQUIRE(snapshot);
        // 입력창이 아무 주장도 하지 않으므로 라벨이 평범한 가시성으로 돌아온다:
        // 그 라벨 오브젝트가 자기 hit 레코드를 갖는다.
        REQUIRE(snapshot->hitTargets.size() == 2);
        const auto& hit = snapshot->hitTargets.front();
        CHECK(hit.target == f.ButtonActionIdentity());
        CHECK_FALSE(hit.textInputTarget.has_value());
        CHECK_FALSE(hit.acceptsTextInput);
        CHECK(hit.focusTarget.has_value());
    }
    SUBCASE("read-only text input removes the text target only") {
        UIMultiTargetFixture f;
        f.Input().SetReadOnly(true);
        const auto snapshot = f.Build();
        REQUIRE(snapshot);
        REQUIRE_FALSE(snapshot->hitTargets.empty());
        const auto& hit = snapshot->hitTargets.front();
        CHECK_FALSE(hit.textInputTarget.has_value());
        CHECK(hit.focusTarget.has_value());
    }
    SUBCASE("a disabled ancestor scroll is omitted, never replaced") {
        UIMultiTargetFixture f;
        const auto outerIdentity = f.OuterScrollIdentity();
        f.InnerScroll().SetEnabled(false);
        const auto snapshot = f.Build();
        REQUIRE(snapshot);
        REQUIRE(snapshot->hitTargets.size() == 1);
        const auto& hit = snapshot->hitTargets.front();
        REQUIRE(hit.scrollTargets.size() == 1);
        // 남은 하나는 바깥 스크롤 자신이다. 안쪽 오브젝트 id가 바깥 컴포넌트를
        // 달고 남아 있으면 나중 라우팅이 존재하지 않는 영역을 스크롤한다.
        CHECK(hit.scrollTargets[0].runtimeTarget == outerIdentity);
        CHECK(hit.scrollTargets[0].canonicalTarget.sceneObjectId ==
              f.Outer().GetID());
    }
    SUBCASE("a replaced ancestor scroll never matches the frozen identity") {
        UIMultiTargetFixture f;
        const auto snapshotBefore = f.Build();
        REQUIRE(snapshotBefore);
        REQUIRE(snapshotBefore->hitTargets.size() == 1);
        const auto frozenInner =
            snapshotBefore->hitTargets.front().scrollTargets[0];

        f.Inner().RemoveComponent<UIScrollView>();
        auto* replacement = f.Inner().AddComponent<UIScrollView>();
        REQUIRE(replacement != nullptr);

        const auto snapshotAfter = f.Build();
        REQUIRE(snapshotAfter);
        REQUIRE(snapshotAfter->hitTargets.size() == 1);
        const auto& scrolls = snapshotAfter->hitTargets.front().scrollTargets;
        REQUIRE(scrolls.size() == 2);
        // 같은 오브젝트 id, 같은 컴포넌트 타입 — 그런데 다른 인스턴스다.
        CHECK(scrolls[0].canonicalTarget == frozenInner.canonicalTarget);
        CHECK(scrolls[0].runtimeTarget != frozenInner.runtimeTarget);
        CHECK(scrolls[0].runtimeTarget ==
              molga::ui::CaptureTarget(f.GetWorld(), *replacement));
    }
}

TEST_CASE("an undecorated selectable and input shell is still interactive") {
    // 자격 입력창 껍데기의 모양: RectTransform, 켜진 UIMask, 켜지고 상호작용
    // 가능한 UISelectable, 켜지고 쓸 수 있는 UITextInput. UIButton도 UIImage도
    // 이 오브젝트의 UILabel도 없다.
    RenderClipTextRuntime text;
    World world;
    molga::ui::UILayoutSystem system;
    CountingDiagnosticSink sink;

    GameObject* root = AddObject(world, 1, nullptr);
    AddConstantCanvas(*root);
    AddOffsetRect(*root, 0.0f, 0.0f, 200.0f, 200.0f);

    GameObject* shell = AddObject(world, 2, root);
    AddOffsetRect(*shell, 4.0f, 6.0f, 40.0f, 20.0f);
    auto* mask = shell->AddComponent<UIMask>();
    REQUIRE(mask != nullptr);
    auto* selectable = shell->AddComponent<UISelectable>();
    REQUIRE(selectable != nullptr);
    auto* input = shell->AddComponent<UITextInput>();
    REQUIRE(input != nullptr);

    GameObject* rendered = AddObject(world, 3, shell);
    AddOffsetRect(*rendered, 0.0f, 0.0f, 40.0f, 20.0f);
    AddLabel(*rendered, "abc");
    SceneObjectRef ref;
    ref.targetId = rendered->GetID();
    input->SetRenderedLabel(ref);

    const auto snapshot = system.Build(
        world, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
        molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
        text.service, sink);
    REQUIRE(snapshot);
    REQUIRE(snapshot->hitTargets.size() == 1);
    const auto& hit = snapshot->hitTargets.front();
    // 장식이 하나도 없어도 포인터 다운이 포커스/텍스트 소유자 획득에 닿는다.
    CHECK(hit.target == molga::ui::CaptureTarget(world, *selectable));
    CHECK(hit.canonicalTarget.componentTypeName == "UISelectable");
    REQUIRE(hit.focusTarget.has_value());
    CHECK(hit.focusTarget->runtimeTarget ==
          molga::ui::CaptureTarget(world, *selectable));
    REQUIRE(hit.textInputTarget.has_value());
    CHECK(hit.textInputTarget->runtimeTarget ==
          molga::ui::CaptureTarget(world, *input));
    CHECK(hit.logicalRect == RawRect(4 * 64, 6 * 64, 40 * 64, 20 * 64));
    // 껍데기 자신은 아무 렌더 항목도 내지 않는다. 소유된 라벨도 마찬가지다.
    CHECK(snapshot->renderItems.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1h: 입력창이 소유한 라벨의 실패 표
// ─────────────────────────────────────────────────────────────────────────────
namespace {

constexpr const char* kInputFamily = "22222222222222222222222222222222";

// 라벨을 입력창의 자식이 아니라 형제로 둔다. 자식이면 입력창을 비활성으로
// 만드는 행이 라벨까지 함께 숨겨, "주장하지 않는 입력창의 라벨은 평범한
// 가시성을 따른다"는 주장이 공허해진다.
class UITextInputLabelFixture {
public:
    UITextInputLabelFixture() {
        GameObject* root = AddObject(world_, 1, nullptr);
        AddConstantCanvas(*root);
        AddOffsetRect(*root, 0.0f, 0.0f, 200.0f, 200.0f);

        inputObject_ = AddObject(world_, 2, root);
        AddOffsetRect(*inputObject_, 0.0f, 0.0f, 60.0f, 20.0f);
        input_ = inputObject_->AddComponent<UITextInput>();
        REQUIRE(input_ != nullptr);
        input_->SetFontFamilyGuid(kInputFamily);
        UIAuthoredParagraphStyle style;
        style.fontSizePx = 31.0f;
        style.lineSpacing = 1.75f;
        style.color = Color{0.11f, 0.22f, 0.33f, 0.44f};
        style.locale = "ko";
        style.baseDirection = molga::text::BaseDirection::RightToLeft;
        style.wrap = molga::text::TextWrapMode::Grapheme;
        style.overflow = molga::text::TextOverflowMode::Ellipsis;
        style.maxLines = 7;
        style.horizontal = molga::text::TextHorizontalAlignment::Right;
        style.vertical = molga::text::TextVerticalAlignment::Bottom;
        input_->SetParagraphStyle(style);
        input_->SetInitialText("typed");

        renderedObject_ = AddObject(world_, 3, root);
        AddOffsetRect(*renderedObject_, 0.0f, 0.0f, 60.0f, 20.0f);
        rendered_ = AddLabel(*renderedObject_, "typed");
        // 라벨의 스타일은 입력창과 일부러 전부 다르게 둔다. 유효 요청이
        // 라벨에서 가져오는 것은 뷰포트 제약/색/정체성/진단 출처뿐이다.
        rendered_->SetFontSizePx(13.0f);
        rendered_->SetLocale("he");
        rendered_->SetWrapMode(molga::text::TextWrapMode::Word);
        rendered_->SetHorizontalAlignment(UILabel::HorizontalAlignment::Left);
        rendered_->SetVerticalAlignment(UILabel::VerticalAlignment::Top);
        rendered_->SetColor(Color{0.5f, 0.6f, 0.7f, 0.8f});

        placeholderObject_ = AddObject(world_, 4, root);
        AddOffsetRect(*placeholderObject_, 0.0f, 0.0f, 60.0f, 20.0f);
        placeholder_ = AddLabel(*placeholderObject_, "hint");

        otherInputObject_ = AddObject(world_, 5, root);
        AddOffsetRect(*otherInputObject_, 0.0f, 40.0f, 60.0f, 20.0f);
        otherInput_ = otherInputObject_->AddComponent<UITextInput>();
        REQUIRE(otherInput_ != nullptr);

        notALabel_ = AddObject(world_, 6, root);
        AddOffsetRect(*notALabel_, 0.0f, 80.0f, 10.0f, 10.0f);

        inactiveHolder_ = AddObject(world_, 7, root);
        AddOffsetRect(*inactiveHolder_, 0.0f, 100.0f, 60.0f, 20.0f);
        inactiveLabelObject_ = AddObject(world_, 8, inactiveHolder_);
        AddOffsetRect(*inactiveLabelObject_, 0.0f, 0.0f, 60.0f, 20.0f);
        inactiveLabel_ = AddLabel(*inactiveLabelObject_, "hidden");
        inactiveHolder_->SetActive(false);

        SetRenderedRef(renderedObject_->GetID());
        SetPlaceholderRef(placeholderObject_->GetID());
    }

    void SetRenderedRef(unsigned int targetId) {
        SceneObjectRef ref;
        ref.targetId = targetId;
        input_->SetRenderedLabel(ref);
    }
    void SetPlaceholderRef(unsigned int targetId) {
        SceneObjectRef ref;
        ref.targetId = targetId;
        input_->SetPlaceholderLabel(ref);
    }
    void SetOtherRenderedRef(unsigned int targetId) {
        SceneObjectRef ref;
        ref.targetId = targetId;
        otherInput_->SetRenderedLabel(ref);
    }

    UISnapshotPtr BuildActiveInput() {
        return system_.Build(
            world_, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
            provider_ ? *provider_
                      : static_cast<
                            const molga::ui::UITextInputVisualStateProvider&>(
                            molga::ui::EmptyUITextInputVisualStateProvider::
                                Instance()),
            text_.service, sink_);
    }

    void UseProvider(const molga::ui::UITextInputVisualStateProvider* provider) {
        provider_ = provider;
    }

    std::size_t OrdinaryLabelItemCount(const molga::ui::UISnapshot& snapshot,
                                       unsigned int labelObjectId) const {
        return CountRenderItems(snapshot, labelObjectId, "UILabel");
    }

    unsigned int RenderedLabel() const { return renderedObject_->GetID(); }
    unsigned int PlaceholderLabel() const {
        return placeholderObject_->GetID();
    }
    unsigned int NotALabel() const { return notALabel_->GetID(); }
    unsigned int InactiveLabel() const {
        return inactiveLabelObject_->GetID();
    }
    molga::ui::UIRuntimeTargetIdentity RenderedLabelIdentity() {
        return molga::ui::CaptureTarget(world_, *rendered_);
    }
    molga::ui::UIRuntimeTargetIdentity InputIdentity() {
        return molga::ui::CaptureTarget(world_, *input_);
    }

    UITextInput& Input() noexcept { return *input_; }
    UITextInput& OtherInput() noexcept { return *otherInput_; }
    molga::ui::UILayoutSystem& System() noexcept { return system_; }
    UILabel& Rendered() noexcept { return *rendered_; }
    UILabel& Placeholder() noexcept { return *placeholder_; }
    GameObject& InputObject() noexcept { return *inputObject_; }
    World& GetWorld() noexcept { return world_; }
    CountingDiagnosticSink& Diagnostics() noexcept { return sink_; }

private:
    RenderClipTextRuntime text_;
    World world_;
    molga::ui::UILayoutSystem system_;
    CountingDiagnosticSink sink_;
    const molga::ui::UITextInputVisualStateProvider* provider_ = nullptr;
    GameObject* inputObject_ = nullptr;
    GameObject* renderedObject_ = nullptr;
    GameObject* placeholderObject_ = nullptr;
    GameObject* otherInputObject_ = nullptr;
    GameObject* notALabel_ = nullptr;
    GameObject* inactiveHolder_ = nullptr;
    GameObject* inactiveLabelObject_ = nullptr;
    UITextInput* input_ = nullptr;
    UITextInput* otherInput_ = nullptr;
    UILabel* rendered_ = nullptr;
    UILabel* placeholder_ = nullptr;
    UILabel* inactiveLabel_ = nullptr;
};

const molga::ui::UITextInputLabelSnapshot* FindOwnedLabels(
    const molga::ui::UISnapshot& snapshot, unsigned int inputObjectId) {
    for (const auto& owned : snapshot.textInputLabels) {
        if (owned.input.canonicalTarget.sceneObjectId == inputObjectId) {
            return &owned;
        }
    }
    return nullptr;
}

bool HasTextInputTarget(const molga::ui::UISnapshot& snapshot,
                        unsigned int inputObjectId) {
    for (const auto& hit : snapshot.hitTargets) {
        if (hit.canonicalTarget.sceneObjectId != inputObjectId) continue;
        if (hit.textInputTarget.has_value()) return true;
    }
    return false;
}

} // namespace

TEST_CASE("UITextInput-owned labels never also render as ordinary labels") {
    UITextInputLabelFixture f;
    const auto snapshot = f.BuildActiveInput();
    REQUIRE(snapshot);
    CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 0);
    CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 0);
    REQUIRE(snapshot->textInputLabels.size() == 1);
    CHECK(snapshot->textInputLabels[0].renderedLabel.label.runtimeTarget ==
          f.RenderedLabelIdentity());
    CHECK(snapshot->textInputLabels[0].input.runtimeTarget == f.InputIdentity());
    REQUIRE(snapshot->textInputLabels[0].placeholderLabel.has_value());
    CHECK(snapshot->textInputLabels[0]
              .placeholderLabel->label.canonicalTarget.sceneObjectId ==
          f.PlaceholderLabel());
    CHECK(HasTextInputTarget(*snapshot, f.InputObject().GetID()));

    // 이 검사가 공허하지 않다는 증인: 주장을 걷으면 두 라벨이 평범하게 그려진다.
    UITextInputLabelFixture free;
    free.SetRenderedRef(0);
    free.SetPlaceholderRef(0);
    const auto freeSnapshot = free.BuildActiveInput();
    REQUIRE(freeSnapshot);
    CHECK(free.OrdinaryLabelItemCount(*freeSnapshot, free.RenderedLabel()) == 1);
    CHECK(free.OrdinaryLabelItemCount(*freeSnapshot, free.PlaceholderLabel()) ==
          1);
    CHECK(freeSnapshot->textInputLabels.empty());
}

TEST_CASE("input label ownership follows the fail-closed table exactly") {
    SUBCASE("two inputs claiming one rendered label fail both") {
        UITextInputLabelFixture f;
        f.SetOtherRenderedRef(f.RenderedLabel());
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        // 충돌한 라벨은 평범한 출력에서도 사라진다(fail-closed).
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 0);
        CHECK(snapshot->textInputLabels.empty());
        CHECK_FALSE(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) >= 1);
        // placeholder는 첫 입력창만 붙들었으므로 그것도 함께 억눌린다.
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 0);
    }
    SUBCASE("one input using one label as both roles fails") {
        UITextInputLabelFixture f;
        f.SetPlaceholderRef(f.RenderedLabel());
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 0);
        CHECK(snapshot->textInputLabels.empty());
        CHECK_FALSE(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
        // 진짜 placeholder는 아무도 붙들지 않았으므로 평범하게 그려진다.
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 1);
    }
    SUBCASE("an unset rendered ref suppresses the claimed placeholder silently") {
        UITextInputLabelFixture f;
        f.SetRenderedRef(0);
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        CHECK(snapshot->textInputLabels.empty());
        CHECK_FALSE(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        // 아무도 붙들지 않은 rendered 라벨은 평범하게 그려진다.
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 1);
        // placeholder는 여전히 이 입력창이 붙들고 있다. 놓아 주면 그 라벨이
        // 자기 저작 문구를 영원히 그린다 — 죽은 입력창 위의 유령 문구다.
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 0);
        // 참조를 아예 적지 않은 것은 오작성이 아니라 미완성이므로 조용하다.
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 0);
    }
    SUBCASE("a wrong-type rendered ref disables the input visual") {
        UITextInputLabelFixture f;
        f.SetRenderedRef(f.NotALabel());
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        CHECK(snapshot->textInputLabels.empty());
        CHECK_FALSE(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        // 오작성 하나에 진단 하나다. 억제 사실은 그 진단 안에 함께 적힌다.
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
        // 아무도 붙들지 않은 라벨은 평범하게 그려진다.
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 1);
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 0);
    }
    SUBCASE("an out-of-world rendered ref disables the input visual") {
        UITextInputLabelFixture f;
        f.SetRenderedRef(9999);
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        CHECK(snapshot->textInputLabels.empty());
        CHECK_FALSE(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 0);
    }
    SUBCASE("a disabled rendered label disables the input visual") {
        UITextInputLabelFixture f;
        f.Rendered().SetEnabled(false);
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        CHECK(snapshot->textInputLabels.empty());
        CHECK_FALSE(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 0);
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
        // 이 행에서 rendered가 0인 것은 IsEnabled(false) 때문이므로 억제
        // 정책을 재지 못한다. placeholder는 켜져 있으니 정책 그 자체다.
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 0);
    }
    SUBCASE("an inactive label ancestor disables the input visual") {
        UITextInputLabelFixture f;
        f.SetRenderedRef(f.InactiveLabel());
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        CHECK(snapshot->textInputLabels.empty());
        CHECK_FALSE(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.InactiveLabel()) == 0);
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
        // 비활성 조상 아래 라벨은 애초에 노드가 아니라 위 검사가 정책을 재지
        // 못한다. 켜져 있는 placeholder가 그 정책을 잰다.
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 0);
    }
    SUBCASE("an unset placeholder disables only the placeholder output") {
        UITextInputLabelFixture f;
        f.SetPlaceholderRef(0);
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        REQUIRE(snapshot->textInputLabels.size() == 1);
        CHECK_FALSE(snapshot->textInputLabels[0].placeholderLabel.has_value());
        CHECK(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 0);
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 1);
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 0);
    }
    SUBCASE("an invalid placeholder disables only the placeholder output") {
        UITextInputLabelFixture f;
        f.SetPlaceholderRef(f.NotALabel());
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        REQUIRE(snapshot->textInputLabels.size() == 1);
        CHECK_FALSE(snapshot->textInputLabels[0].placeholderLabel.has_value());
        CHECK(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
        // 소유권과 억제는 함께 움직여야 한다. 여기가 갈리면 소유된 라벨이
        // 입력창의 글과 자기 저작 문자열을 겹쳐 두 번 그린다.
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 0);
    }
    SUBCASE("a disabled placeholder disables only the placeholder output") {
        UITextInputLabelFixture f;
        f.Placeholder().SetEnabled(false);
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        REQUIRE(snapshot->textInputLabels.size() == 1);
        CHECK_FALSE(snapshot->textInputLabels[0].placeholderLabel.has_value());
        CHECK(HasTextInputTarget(*snapshot, f.InputObject().GetID()));
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 1);
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 0);
    }
    SUBCASE("a disabled input makes no claim at all") {
        UITextInputLabelFixture f;
        f.Input().SetEnabled(false);
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        CHECK(snapshot->textInputLabels.empty());
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 1);
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 1);
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::ReferenceInvalid) == 0);
    }
    SUBCASE("an inactive input object makes no claim at all") {
        UITextInputLabelFixture f;
        f.InputObject().SetActive(false);
        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        CHECK(snapshot->textInputLabels.empty());
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.RenderedLabel()) == 1);
        CHECK(f.OrdinaryLabelItemCount(*snapshot, f.PlaceholderLabel()) == 1);
    }
}

TEST_CASE("the effective input request takes family and style from the input") {
    UITextInputLabelFixture f;
    const auto snapshot = f.BuildActiveInput();
    REQUIRE(snapshot);
    REQUIRE(snapshot->textInputLabels.size() == 1);
    const auto& owned = snapshot->textInputLabels[0];
    const auto& effective = owned.effectiveInputRequestTemplate;

    // 최상위 UITextInput::fontFamilyGuid 하나가 family 권한이다. 라벨의
    // family를 쓰는 구현은 여기서 죽는다.
    CHECK(effective.style.fontFamilyGuid == kInputFamily);
    CHECK(effective.style.legacyFontGuid.empty());
    // 그리고 문단 스타일의 모든 필드가 입력창에서 온다.
    CHECK(effective.style.shape.fontSize.Raw() == 31 * 64);
    CHECK(effective.style.lineSpacing.Raw() ==
          Fixed26_6::FromFloat(1.75f)->Raw());
    CHECK(effective.style.analysis.locale == "ko");
    CHECK(effective.style.analysis.baseDirection ==
          molga::text::BaseDirection::RightToLeft);
    CHECK(effective.style.wrap == molga::text::TextWrapMode::Grapheme);
    CHECK(effective.style.overflow == molga::text::TextOverflowMode::Ellipsis);
    CHECK(effective.style.maxLines == 7);
    CHECK(effective.style.horizontal ==
          molga::text::TextHorizontalAlignment::Right);
    CHECK(effective.style.vertical ==
          molga::text::TextVerticalAlignment::Bottom);

    // 라벨에서 가져오는 것은 넷뿐이다: 뷰포트 제약, 색, 라벨 정체성, 진단 출처.
    REQUIRE(effective.constraints.width.has_value());
    CHECK(effective.constraints.width->Raw() == 200 * 64);
    REQUIRE(effective.constraints.height.has_value());
    CHECK(effective.constraints.height->Raw() == 200 * 64);
    CHECK(effective.diagnosticContext.sceneObjectId == f.RenderedLabel());
    CHECK(effective.diagnosticContext.componentType == "UILabel");
    CHECK(owned.renderedLabel.color.r == doctest::Approx(0.5f));
    CHECK(owned.renderedLabel.color.a == doctest::Approx(0.8f));
    CHECK(owned.renderedLabel.label.canonicalTarget.sceneObjectId ==
          f.RenderedLabel());

    // 라벨의 자기 요청 템플릿은 출처로 그대로 남는다. 입력창의 스타일로
    // 덮어써 버리면 나중 진단이 저작자가 보는 라벨과 다른 값을 가리킨다.
    CHECK(owned.renderedLabel.requestTemplate.style.fontFamilyGuid ==
          kPrimaryFamily);
    CHECK(owned.renderedLabel.requestTemplate.style.shape.fontSize.Raw() ==
          13 * 64);
    CHECK(owned.renderedLabel.requestTemplate.style.analysis.locale == "he");
    CHECK(owned.renderedLabel.requestTemplate.style.wrap ==
          molga::text::TextWrapMode::Word);

    // 그리고 IME 기하가 그 입력창에 대해 하나 게시된다.
    REQUIRE(snapshot->textInputImeGeometry.size() == 1);
    CHECK(snapshot->textInputImeGeometry[0].input.runtimeTarget ==
          f.InputIdentity());
    CHECK(snapshot->textInputImeGeometry[0].logicalInputArea ==
          RawRect(0, 0, 60 * 64, 20 * 64));
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1i: 값 제공자 경계
// ─────────────────────────────────────────────────────────────────────────────
namespace {

class RecordingUITextInputVisualStateProvider final
    : public molga::ui::UITextInputVisualStateProvider {
public:
    struct Lookup {
        molga::WindowId window = 0;
        molga::ui::UIRuntimeTargetIdentity input;
    };

    std::optional<molga::ui::UITextInputVisualState> GetVisualState(
        molga::WindowId window,
        const molga::ui::UIRuntimeTargetIdentity& input) const override {
        lookups.push_back(Lookup{window, input});
        if (!published) return std::nullopt;
        molga::ui::UITextInputVisualState copy = *published;
        copy.surfaceWindowId = window;
        copy.input = input;
        return copy;
    }

    // 진짜 편집 시스템이 하는 일을 그대로 한다: 게시된 값이 달라지면 의미
    // 세대를 올린다. 올리지 않으면 스칼라 빠른 경로가 그대로 맞아 편집이
    // 화면에 영원히 나타나지 않는다 — 제공자를 직접 고치는 아래 케이스가
    // 그 사실을 일부러 이용한다.
    void Publish(const molga::ui::UITextInputVisualState& state) {
        published = state;
        molga::ui::NotifyUISemanticMutation();
    }

    mutable std::vector<Lookup> lookups;
    std::optional<molga::ui::UITextInputVisualState> published;
};

} // namespace

TEST_CASE("the visual state provider is asked once per valid active input") {
    UITextInputLabelFixture f;
    RecordingUITextInputVisualStateProvider provider;
    molga::ui::UITextInputVisualState state;
    state.committedUtf8.clear();
    state.focused = false;
    state.caretVisible = false;
    provider.published = state;
    f.UseProvider(&provider);

    const auto first = f.BuildActiveInput();
    REQUIRE(first);
    // 유효한 활성 입력창은 하나뿐이다. 두 번 물으면 편집 상태가 조회 사이에
    // 바뀌었을 때 같은 프레임이 두 값을 보게 된다.
    REQUIRE(provider.lookups.size() == 1);
    CHECK(provider.lookups[0].window == 7);
    CHECK(provider.lookups[0].input == f.InputIdentity());

    // 스칼라 빠른 경로가 맞으면 아예 묻지 않는다.
    const auto second = f.BuildActiveInput();
    REQUIRE(second);
    CHECK(second.get() == first.get());
    CHECK(provider.lookups.size() == 1);

    // 게시된 값은 사본이다. 제공자를 나중에 고쳐도 이미 게시된 스냅샷은
    // 달라지지 않는다.
    REQUIRE(first->textInputImeGeometry.size() == 1);
    CHECK_FALSE(first->textInputImeGeometry[0].focused);
    REQUIRE(first->textInputLabels.size() == 1);
    REQUIRE(first->textInputLabels[0].placeholderLabel.has_value());
    CHECK(first->textInputLabels[0].placeholderLabel->enabledAndVisible);
    provider.published->focused = true;
    provider.published->committedUtf8 = "mutated";
    CHECK_FALSE(first->textInputImeGeometry[0].focused);
    CHECK(first->textInputLabels[0].placeholderLabel->enabledAndVisible);
    CHECK(first->textInputLabels[0].effectiveInputRequestTemplate.utf8.empty());
}

TEST_CASE("placeholder visibility follows empty-and-unfocused exactly") {
    struct Row {
        const char* name;
        std::string committed;
        bool focused;
        bool visible;
    };
    const std::vector<Row> rows{
        {"empty-unfocused", "", false, true},
        {"empty-focused", "", true, false},
        {"nonempty-unfocused", "typed", false, false},
        {"nonempty-focused", "typed", true, false},
    };
    for (const auto& row : rows) {
        CAPTURE(row.name);
        UITextInputLabelFixture f;
        RecordingUITextInputVisualStateProvider provider;
        molga::ui::UITextInputVisualState state;
        state.committedUtf8 = row.committed;
        state.focused = row.focused;
        provider.published = state;
        f.UseProvider(&provider);

        const auto snapshot = f.BuildActiveInput();
        REQUIRE(snapshot);
        REQUIRE(snapshot->textInputLabels.size() == 1);
        REQUIRE(snapshot->textInputLabels[0].placeholderLabel.has_value());
        CHECK(snapshot->textInputLabels[0].placeholderLabel->enabledAndVisible ==
              row.visible);
        // 본문 라벨은 언제나 보인다. 이 행이 없으면 두 값을 함께 끈 구현이
        // 위 검사만으로 통과한다.
        CHECK(snapshot->textInputLabels[0].renderedLabel.enabledAndVisible);
        // 유효 요청의 UTF-8은 제공자가 준 그 값이다.
        CHECK(snapshot->textInputLabels[0].effectiveInputRequestTemplate.utf8 ==
              row.committed);
        // IME 기하는 caret 가시성이 아니라 포커스를 그대로 싣는다.
        REQUIRE(snapshot->textInputImeGeometry.size() == 1);
        CHECK(snapshot->textInputImeGeometry[0].focused == row.focused);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1j: 예약된 명령 구간
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::uint64_t PositionedGlyphCount(const molga::text::TextLayout& layout) {
    std::uint64_t total = 0;
    for (const auto& line : layout.lines) {
        for (const auto& run : line.visualRuns) total += run.glyphs.size();
    }
    return total;
}

// 한 오브젝트에 라벨(정렬 0)과 버튼(정렬 1)을 함께 둔다. 그러면 draw order가
// 라벨 -> 솔리드로 정해지고, 솔리드의 번호가 라벨의 예약 구간 뒤에서 시작해야
// 한다.
class CommandSpanFixture {
public:
    explicit CommandSpanFixture(const std::string& text) {
        GameObject* root = AddObject(world_, 1, nullptr);
        AddConstantCanvas(*root);
        AddOffsetRect(*root, 0.0f, 0.0f, 200.0f, 200.0f);
        leaf_ = AddObject(world_, 2, root);
        AddOffsetRect(*leaf_, 0.0f, 0.0f, 120.0f, 40.0f);
        label_ = AddLabel(*leaf_, text);
        label_->SetSortingOrder(0);
        button_ = leaf_->AddComponent<UIButton>();
        REQUIRE(button_ != nullptr);
        button_->SetSortingOrder(1);
    }

    UISnapshotPtr Build() {
        return system_.Build(
            world_, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            text_.service, sink_);
    }

    CountingDiagnosticSink& Diagnostics() noexcept { return sink_; }

private:
    RenderClipTextRuntime text_;
    World world_;
    molga::ui::UILayoutSystem system_;
    CountingDiagnosticSink sink_;
    GameObject* leaf_ = nullptr;
    UILabel* label_ = nullptr;
    UIButton* button_ = nullptr;
};

} // namespace

TEST_CASE("ordinary text reserves its complete positioned-record span") {
    struct Row {
        const char* name;
        std::string text;
        std::uint64_t expectedSpan;
    };
    const std::vector<Row> rows{
        {"three-drawable-glyphs", "abc", 3},
        {"six-drawable-glyphs", "abcdef", 6},
        // 공백은 명령을 내지 않지만 예약 서수 하나를 소비한다. 그 뒤의 솔리드는
        // 여전히 완전한 기록 구간 뒤에서 시작한다.
        {"space-between-drawables", "a c", 3},
    };
    for (const auto& row : rows) {
        CAPTURE(row.name);
        CommandSpanFixture f(row.text);
        const auto snapshot = f.Build();
        REQUIRE(snapshot);
        REQUIRE(snapshot->renderItems.size() == 2);
        const auto& textItem = snapshot->renderItems[0];
        const auto& solidItem = snapshot->renderItems[1];
        REQUIRE(std::holds_alternative<molga::ui::UITextSnapshot>(
            textItem.payload));
        REQUIRE(std::holds_alternative<molga::ui::UISolidRectSnapshot>(
            solidItem.payload));
        const auto& layout =
            std::get<molga::ui::UITextSnapshot>(textItem.payload).layout;
        REQUIRE(layout != nullptr);
        // 픽스처가 실제로 그 수의 기록을 냈다는 것을 먼저 못 박는다. 아니면
        // 아래 등식이 어떤 구현에서도 성립하는 공허한 주장이 된다.
        REQUIRE(PositionedGlyphCount(*layout) == row.expectedSpan);
        CHECK(textItem.reservedCommandSpan == row.expectedSpan);
        CHECK(solidItem.reservedCommandSpan == 1);
        // 다음 항목의 번호는 앞선 구간을 전부 더한 뒤에 배정된다. 텍스트도
        // 1만 예약하는 구현은 여기서 죽는다.
        CHECK(solidItem.order.stableSubmissionIndex ==
              textItem.order.stableSubmissionIndex + row.expectedSpan);
        CHECK(textItem.order.componentSortingOrder <
              solidItem.order.componentSortingOrder);
        CHECK(textItem.order < solidItem.order);
        CHECK(f.Diagnostics().Count(TextDiagnosticCode::LayoutInvalid) == 0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 10.2가 이 태스크에 넘긴 의무 (1): 컴포넌트를 붙이고 떼는 것도 게시다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("attaching and detaching a UI component republishes the surface") {
    RenderClipTextRuntime text;
    World world;
    molga::ui::UILayoutSystem system;
    CountingDiagnosticSink sink;

    GameObject* root = AddObject(world, 1, nullptr);
    AddConstantCanvas(*root);
    AddOffsetRect(*root, 0.0f, 0.0f, 200.0f, 200.0f);
    GameObject* plain = AddObject(world, 2, root);
    AddOffsetRect(*plain, 4.0f, 6.0f, 20.0f, 10.0f);

    const auto build = [&] {
        return system.Build(
            world, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            text.service, sink);
    };

    const auto before = build();
    REQUIRE(before);
    REQUIRE(before->nodes.size() == 2);
    CHECK(before->renderItems.empty());
    // 같은 상태를 두 번 지으면 같은 스냅샷이다. 아래 검사가 무엇을 재는지
    // 못 박는다 — 빠른 경로가 애초에 맞지 않으면 그 뒤의 부등은 공허하다.
    CHECK(build().get() == before.get());

    // 인스펙터의 "Add Component"가 정확히 이 경로다. 세터를 하나도 부르지
    // 않으므로, 붙이는 것 자체가 게시하지 않으면 새 UIImage는 무기한 화면에
    // 나오지 않는다.
    auto* image = plain->AddComponent<UIImage>();
    REQUIRE(image != nullptr);

    const auto attached = build();
    REQUIRE(attached);
    CHECK(attached.get() != before.get());
    REQUIRE(attached->renderItems.size() == 1);
    CHECK(attached->renderItems[0].canonicalSource.sceneObjectId ==
          plain->GetID());
    CHECK(attached->renderItems[0].canonicalSource.componentTypeName ==
          "UIImage");
    REQUIRE(attached->hitTargets.size() == 1);

    // 떼는 것도 같은 사실이다. RemoveComponentById는 오브젝트가 파괴 중일
    // 때만 OnDestroy를 부르므로 OnDetach가 그 자리다.
    CHECK(build().get() == attached.get());
    plain->RemoveComponent<UIImage>();
    const auto detached = build();
    REQUIRE(detached);
    CHECK(detached.get() != attached.get());
    CHECK(detached->renderItems.empty());
    CHECK(detached->hitTargets.empty());

    // 캔버스를 나중에 붙이는 경우도 같다: 붙이기 전에는 UI가 하나도 없다.
    World second;
    molga::ui::UILayoutSystem secondSystem;
    GameObject* late = AddObject(second, 1, nullptr);
    AddOffsetRect(*late, 0.0f, 0.0f, 200.0f, 200.0f);
    const auto buildSecond = [&] {
        return secondSystem.Build(
            second, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            text.service, sink);
    };
    const auto withoutCanvas = buildSecond();
    REQUIRE(withoutCanvas);
    CHECK(withoutCanvas->nodes.empty());
    CHECK(buildSecond().get() == withoutCanvas.get());
    AddConstantCanvas(*late);
    const auto withCanvas = buildSecond();
    REQUIRE(withCanvas);
    CHECK(withCanvas.get() != withoutCanvas.get());
    CHECK(withCanvas->nodes.size() == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 10.2가 이 태스크에 넘긴 의무 (2): 레거시 즉시 경로와 스냅샷 경로의 일치
// ─────────────────────────────────────────────────────────────────────────────
// UISystem::CollectRender/HitTest는 RectTransform::ResolveLogical(float, 노드별
// 재귀)로 사각형을 정하고, Build는 검증된 26.6과 한 번의 DFS로 정한다. 지금은
// 레거시 경로만 화면에 나오므로 두 경로가 어긋나도 드러나지 않는다. 이 케이스가
// 그 어긋남을 지금 잡는다.
//
// 범위를 분명히 한다: 이 등식은 저작된 RectTransform만으로 이루어진 트리에
// 대한 것이다. UILayoutGroup/UIContentSizeFitter/꺼진 RectTransform은 레거시
// 경로가 애초에 구현한 적이 없으므로 두 경로가 의도적으로 다르며, 그 차이는
// 스냅샷 경로가 옳다.
TEST_CASE("the legacy float rect path and the snapshot path agree exactly") {
    // ── Task 11.1 F1 ────────────────────────────────────────────────────────
    // 두 행을 돈다. 배율 1 행만 있으면 이 등식은 두 경로가 아니라 배율 1을
    // 재고, 스냅샷이 캔버스 배율을 통째로 빠뜨려도 통과한다 — 실제로 그랬다.
    // 배율 2 행이 기본 모드(ScaleWithViewport)의 그 차이를 잡는다.
    float referenceWidth = 0.0f;
    float referenceHeight = 0.0f;
    float expectedScale = 1.0f;
    SUBCASE("constant pixel canvas") {
        referenceWidth = 0.0f;
        expectedScale = 1.0f;
    }
    SUBCASE("scale-with-viewport canvas at factor two") {
        referenceWidth = 100.0f;
        referenceHeight = 60.0f;
        expectedScale = 2.0f;
    }

    RenderClipTextRuntime text;
    World world;
    molga::ui::UILayoutSystem system;
    CountingDiagnosticSink sink;
    const Vector2 viewport{200.0f, 120.0f};

    GameObject* root = AddObject(world, 1, nullptr);
    if (referenceWidth > 0.0f) {
        AddScaledCanvas(*root, referenceWidth, referenceHeight);
    } else {
        AddConstantCanvas(*root);
    }
    auto* rootRect = root->AddComponent<RectTransform>();
    REQUIRE(rootRect != nullptr);
    rootRect->SetAnchorMin({0.0f, 0.0f});
    rootRect->SetAnchorMax({1.0f, 1.0f});
    rootRect->SetPivot({0.0f, 0.0f});
    rootRect->SetAnchoredPosition({0.0f, 0.0f});
    rootRect->SetSizeDelta({0.0f, 0.0f});

    // 앵커/피벗/음수 위치를 섞는다. 전부 (0,0) 앵커인 트리는 두 구현이
    // 사실상 같은 식을 쓰므로 어긋남을 드러내지 못한다.
    GameObject* stretched = AddObject(world, 2, root);
    auto* stretchedRect = stretched->AddComponent<RectTransform>();
    stretchedRect->SetAnchorMin({0.25f, 0.0f});
    stretchedRect->SetAnchorMax({0.75f, 0.5f});
    stretchedRect->SetPivot({0.5f, 0.5f});
    stretchedRect->SetAnchoredPosition({-8.0f, 12.0f});
    stretchedRect->SetSizeDelta({16.0f, -4.0f});
    stretched->AddComponent<UIImage>();

    GameObject* nested = AddObject(world, 3, stretched);
    auto* nestedRect = nested->AddComponent<RectTransform>();
    nestedRect->SetAnchorMin({1.0f, 1.0f});
    nestedRect->SetAnchorMax({1.0f, 1.0f});
    nestedRect->SetPivot({1.0f, 0.25f});
    nestedRect->SetAnchoredPosition({-3.0f, -7.0f});
    nestedRect->SetSizeDelta({24.0f, 10.0f});
    nested->AddComponent<UIImage>();

    // RectTransform이 없는 중간 오브젝트. 두 경로 모두 이것을 건너뛰고 가장
    // 가까운 RectTransform 조상을 부모로 삼아야 한다.
    GameObject* passthrough = AddObject(world, 4, nested);
    GameObject* leaf = AddObject(world, 5, passthrough);
    auto* leafRect = leaf->AddComponent<RectTransform>();
    leafRect->SetAnchorMin({0.5f, 0.5f});
    leafRect->SetAnchorMax({0.5f, 0.5f});
    leafRect->SetPivot({0.0f, 1.0f});
    leafRect->SetAnchoredPosition({2.0f, -6.0f});
    leafRect->SetSizeDelta({12.0f, 8.0f});
    leaf->AddComponent<UIImage>();

    const auto snapshot = system.Build(
        world, molga::WindowId{7},
        RawSize(static_cast<std::int32_t>(viewport.x) * 64,
                static_cast<std::int32_t>(viewport.y) * 64),
        molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
        text.service, sink);
    REQUIRE(snapshot);
    REQUIRE(snapshot->nodes.size() == 4);
    // 이 행이 실제로 어떤 배율을 재는지 못 박는다. 배율이 1로 굳어 버리면
    // 아래 등식은 두 경로가 아니라 항등을 재게 된다.
    CHECK(root->GetComponent<UICanvas>()->ScaleFactor(viewport) ==
          doctest::Approx(expectedScale));

    const auto rawOf = [](float value) {
        const auto fixed = Fixed26_6::FromFloat(value);
        REQUIRE(fixed.has_value());
        return fixed->Raw();
    };

    for (const auto& node : snapshot->nodes) {
        GameObject* object = world.FindById(node.rectTransform.objectId);
        REQUIRE(object != nullptr);
        auto* rect = object->GetComponent<RectTransform>();
        REQUIRE(rect != nullptr);
        CAPTURE(node.rectTransform.objectId);
        // UISystem::CollectRender와 HitTest가 실제로 부르는 그 함수다.
        // 두 경로가 같은 저작 입력에서 같은 사각형을 내야 한다.
        const AABB legacy = rect->GetScreenRect(viewport);
        CHECK(node.logicalRect.x.Raw() == rawOf(legacy.x));
        CHECK(node.logicalRect.y.Raw() == rawOf(legacy.y));
        CHECK(node.logicalRect.width.Raw() == rawOf(legacy.width));
        CHECK(node.logicalRect.height.Raw() == rawOf(legacy.height));
        // 그리고 렌더 항목은 그 노드 사각형을 그대로 싣는다.
        const auto* item = FindRenderItem(*snapshot, object->GetID());
        if (item) CHECK(item->logicalRect == node.logicalRect);
    }

    // 이 등식이 공허하지 않다는 증인: 앵커 하나를 바꾸면 두 값이 함께 움직이고
    // 여전히 같아야 한다. (양쪽 다 상수를 돌려주는 구현이면 위 검사가 통과한다.)
    const AABB beforeLegacy = leafRect->GetScreenRect(viewport);
    leafRect->SetAnchoredPosition({40.0f, -6.0f});
    const auto moved = system.Build(
        world, molga::WindowId{7},
        RawSize(static_cast<std::int32_t>(viewport.x) * 64,
                static_cast<std::int32_t>(viewport.y) * 64),
        molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
        text.service, sink);
    REQUIRE(moved);
    const AABB afterLegacy = leafRect->GetScreenRect(viewport);
    CHECK(afterLegacy.x != beforeLegacy.x);
    for (const auto& node : moved->nodes) {
        if (node.rectTransform.objectId != leaf->GetID()) continue;
        CHECK(node.logicalRect.x.Raw() == rawOf(afterLegacy.x));
    }
}

// Step 3e: 보이는 글의 변화는 기하를 다시 짓게 하고, caret/포커스/깜빡임만의
// 변화는 기하를 그대로 재사용하면서 전체 스냅샷만 다시 짓는다. 두 방향을 함께
// 못 박지 않으면 "전부 기하 키에 넣기"와 "전부 빼기"가 각각 한쪽 검사만으로
// 통과한다.
TEST_CASE("visible input text reshapes geometry while caret-only edits reuse it") {
    UITextInputLabelFixture f;
    RecordingUITextInputVisualStateProvider provider;
    molga::ui::UITextInputVisualState state;
    state.committedUtf8 = "typed";
    state.focused = false;
    state.caretVisible = false;
    provider.published = state;
    f.UseProvider(&provider);

    const auto warm = f.BuildActiveInput();
    REQUIRE(warm);
    REQUIRE(warm->textInputLabels.size() == 1);
    CHECK(warm->textInputLabels[0].effectiveInputRequestTemplate.utf8 ==
          "typed");
    const auto geometryBuilds = f.System().GeometryBuildCount();

    // caret/affinity/선택 끝점/포커스/깜빡임/표면 revision은 전체 키에만 있다.
    molga::ui::UITextInputVisualState blinkState = *provider.published;
    blinkState.caret.boundary = 3;
    blinkState.caret.affinity = molga::text::CaretAffinity::Upstream;
    blinkState.selection = molga::text::GraphemeRange{1, 3};
    blinkState.focused = true;
    blinkState.caretVisible = true;
    blinkState.surfaceRevision = 9;
    provider.Publish(blinkState);
    const auto blinked = f.BuildActiveInput();
    REQUIRE(blinked);
    CHECK(blinked.get() != warm.get());
    CHECK(f.System().GeometryBuildCount() == geometryBuilds);
    REQUIRE(blinked->textInputImeGeometry.size() == 1);
    CHECK(blinked->textInputImeGeometry[0].focused);

    // 보이는 글이 바뀌면 편집 이전의 고유/확정 기하를 재사용할 수 없다.
    molga::ui::UITextInputVisualState editState = *provider.published;
    editState.committedUtf8 = "typed more";
    provider.Publish(editState);
    const auto edited = f.BuildActiveInput();
    REQUIRE(edited);
    CHECK(edited.get() != blinked.get());
    CHECK(f.System().GeometryBuildCount() == geometryBuilds + 1);
    CHECK(edited->textInputLabels[0].effectiveInputRequestTemplate.utf8 ==
          "typed more");

    // 조합 중인 글도 보이는 글이다.
    const auto afterEdit = f.System().GeometryBuildCount();
    molga::ui::UITextInputVisualState composeState = *provider.published;
    composeState.compositionUtf8 = "ing";
    provider.Publish(composeState);
    const auto composing = f.BuildActiveInput();
    REQUIRE(composing);
    CHECK(f.System().GeometryBuildCount() == afterEdit + 1);
    CHECK(composing->textInputLabels[0].effectiveInputRequestTemplate.utf8 ==
          "typed moreing");
}

// Step 3b/3e: 넓은 스칼라가 모든 편집에서 함께 움직이므로, 키의 *내용*은
// 포인터 동일성으로 관찰할 수 없다. Task 11.2가 렌더 페이로드를 이 키에
// 의존하게 만들기 전에 그 내용이 실제로 실려 있는지 여기서 못 박는다.
TEST_CASE("the full snapshot key retains every runtime binding field") {
    UISnapshotPayloadFixture f;
    f.SetTextureContent(Sha256Of("keyed-pixels"));
    const std::uint64_t device = f.CurrentDeviceGeneration();
    REQUIRE(f.BindTexture(FirstBinding(f, device)) ==
            molga::ui::UITextureBindingPublishResult::Published);
    REQUIRE(f.Build());

    const auto key = f.System().LastSnapshotKey();
    REQUIRE(key.has_value());
    REQUIRE(key->runtimeBindings.size() == 1);
    const auto& entry = key->runtimeBindings[0];
    CHECK(entry.sceneObjectId == f.Leaf().GetID());
    CHECK(entry.componentTypeName == "UIImage");
    CHECK(entry.componentSchemaVersion == UIImage::CurrentSchemaVersion);
    CHECK(entry.binding.deviceGeneration == device);
    CHECK(entry.binding.uploadGeneration == 3);
    CHECK(entry.binding.texture == f.TextureHandleA());
    CHECK(entry.binding.sampler == f.SamplerHandleA());
    CHECK(entry.binding.lifetimeIdentity == 101);

    // 그리고 그 벡터가 실제로 비교에 참여한다: 필드 하나만 다른 두 키는
    // 서로 다르다. 넓은 스칼라가 같아도 그렇다.
    molga::ui::UISnapshotCacheKey other = *key;
    other.runtimeBindings[0].binding.lifetimeIdentity = 999;
    CHECK(*key != other);
    molga::ui::UISnapshotCacheKey handleOnly = *key;
    handleOnly.runtimeBindings[0].binding.texture =
        molga::detail::MakeTextureHandleForTest(11, 2);
    CHECK(*key != handleOnly);
    molga::ui::UISnapshotCacheKey dropped = *key;
    dropped.runtimeBindings.clear();
    CHECK(*key != dropped);

    // 재바인딩하면 키의 내용이 실제로 따라 움직인다.
    DeviceBinding next = FirstBinding(f, device);
    next.texture = f.TextureHandleB();
    REQUIRE(f.BindTexture(next) ==
            molga::ui::UITextureBindingPublishResult::Published);
    REQUIRE(f.Build());
    const auto rebound = f.System().LastSnapshotKey();
    REQUIRE(rebound.has_value());
    REQUIRE(rebound->runtimeBindings.size() == 1);
    CHECK(rebound->runtimeBindings[0].binding.texture == f.TextureHandleB());
    CHECK(*rebound != *key);
}

TEST_CASE("the full snapshot key retains every input visual state field") {
    UITextInputLabelFixture f;
    RecordingUITextInputVisualStateProvider provider;
    molga::ui::UITextInputVisualState state;
    state.committedUtf8 = "typed";
    state.compositionUtf8 = "ing";
    state.caret.boundary = 3;
    state.caret.affinity = molga::text::CaretAffinity::Upstream;
    state.selection = molga::text::GraphemeRange{1, 3};
    state.compositionSelection = molga::text::GraphemeRange{0, 2};
    state.focused = true;
    state.caretVisible = true;
    state.editRevision = 17;
    state.surfaceRevision = 23;
    provider.Publish(state);
    f.UseProvider(&provider);
    REQUIRE(f.BuildActiveInput());

    const auto key = f.System().LastSnapshotKey();
    REQUIRE(key.has_value());
    REQUIRE(key->inputVisualStates.size() == 1);
    const auto& published = key->inputVisualStates[0];
    CHECK(published.surfaceWindowId == 7);
    CHECK(published.input == f.InputIdentity());
    CHECK(published.committedUtf8 == "typed");
    CHECK(published.compositionUtf8 == "ing");
    CHECK(published.caret.boundary == 3);
    CHECK(published.caret.affinity == molga::text::CaretAffinity::Upstream);
    CHECK(published.selection == molga::text::GraphemeRange{1, 3});
    CHECK(published.compositionSelection == molga::text::GraphemeRange{0, 2});
    CHECK(published.focused);
    CHECK(published.caretVisible);
    CHECK(published.editRevision == 17);
    CHECK(published.surfaceRevision == 23);

    // caret/선택/포커스/깜빡임/표면 revision은 전체 키에만 있으므로, 그 값들이
    // 실제로 비교에 참여하는지도 함께 못 박는다.
    for (const auto& mutate :
         std::vector<std::function<void(molga::ui::UITextInputVisualState&)>>{
             [](auto& s) { s.caret.boundary = 4; },
             [](auto& s) {
                 s.caret.affinity = molga::text::CaretAffinity::Downstream;
             },
             [](auto& s) { s.selection = molga::text::GraphemeRange{0, 1}; },
             [](auto& s) { s.focused = false; },
             [](auto& s) { s.caretVisible = false; },
             [](auto& s) { s.editRevision = 18; },
             [](auto& s) { s.surfaceRevision = 24; },
             [](auto& s) { s.committedUtf8 = "other"; }}) {
        molga::ui::UISnapshotCacheKey other = *key;
        mutate(other.inputVisualStates[0]);
        CHECK(*key != other);
    }
    molga::ui::UISnapshotCacheKey dropped = *key;
    dropped.inputVisualStates.clear();
    CHECK(*key != dropped);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3c: 약한 기록만 남기는 바인딩 등록부
// ─────────────────────────────────────────────────────────────────────────────
// 만료되지 않은 토큰이 곧 진짜 외부 소유자다. shared_ptr::use_count()의
// 임계값이었다면 등록부 자신의 지분까지 세게 되어, 소유자가 하나 늘어난 날
// 그 임계값이 조용히 틀린다.
TEST_CASE("retained binding lifetimes block teardown until the last owner drops") {
    auto& registry = molga::TextureBindingRegistry::Get();
    // 이 케이스만의 장치 세대를 쓴다. 등록부는 프로세스 전역이라 고정 값을
    // 쓰면 다른 케이스가 발행한 기록을 함께 세게 된다.
    static std::uint64_t deviceSeed = 900000;
    const std::uint64_t device = ++deviceSeed;

    molga::ui::TextureRuntimeBindingIdentity identity;
    identity.deviceGeneration = device;
    identity.uploadGeneration = 1;
    identity.texture = molga::detail::MakeTextureHandleForTest(31, 1);
    identity.sampler = molga::detail::MakeSamplerHandleForTest(41, 1);
    identity.lifetimeIdentity = 5150;

    CHECK(registry.LiveRetainedBindingCount(device) == 0);
    auto token = registry.Publish(identity);
    REQUIRE(token != nullptr);
    // 게시된 토큰의 값은 발행된 정체성과 필드 하나까지 같다.
    CHECK(token->Identity() == identity);
    CHECK(registry.LiveRetainedBindingCount(device) == 1);

    // 같은 정체성을 다시 게시하면 서로 다른 토큰이 나온다. 두 번째 게시가 첫
    // 번째의 소유자를 대신 늘리면 첫 게시자가 놓은 뒤에도 핸들이 살아남는다.
    auto second = registry.Publish(identity);
    REQUIRE(second != nullptr);
    CHECK(second != token);
    CHECK(registry.LiveRetainedBindingCount(device) == 2);

    std::string error;
    CHECK_FALSE(registry.DestroyRetiredBindings(device, error));
    CHECK_FALSE(error.empty());
    // 거절했으므로 기록은 그대로다.
    CHECK(registry.LiveRetainedBindingCount(device) == 2);

    second.reset();
    CHECK(registry.LiveRetainedBindingCount(device) == 1);
    CHECK_FALSE(registry.DestroyRetiredBindings(device, error));

    // 마지막 외부 소유자가 사라져야 비로소 놓는다.
    token.reset();
    CHECK(registry.LiveRetainedBindingCount(device) == 0);
    CHECK(registry.DestroyRetiredBindings(device, error));
    CHECK(error.empty());
    CHECK(registry.LiveRetainedBindingCount(device) == 0);

    // 다른 장치 세대의 기록은 건드리지 않는다.
    const std::uint64_t otherDevice = ++deviceSeed;
    molga::ui::TextureRuntimeBindingIdentity otherIdentity = identity;
    otherIdentity.deviceGeneration = otherDevice;
    auto otherToken = registry.Publish(otherIdentity);
    REQUIRE(otherToken != nullptr);
    CHECK(registry.DestroyRetiredBindings(device, error));
    CHECK(registry.LiveRetainedBindingCount(otherDevice) == 1);
    CHECK_FALSE(registry.DestroyRetiredBindings(otherDevice, error));
}

// 해시가 같아도 원래의 순서 있는 필드를 전부 다시 본다. 서로 다른 키만 넣어
// 보는 시험은 해시만 믿는 구현에서도 전부 통과하므로, 실제로 충돌하는 쌍을
// 두 벌 만들어 넣는다 — 하나는 입력 정체성만, 하나는 유효 요청만 다르다.
//
// FNV-1a의 한 걸음은 (state ^ value) * prime이고 곱하는 상수가 홀수라 가역이다.
// 어느 값 하나를 바꿔 상태가 갈라져도, 바로 다음 값을 x ^ hA ^ hB 로 고르면
// 상태가 다시 합쳐지고 이후 스트림 전체가 동일해진다.
TEST_CASE("the geometry cache rejects a colliding but different input key") {
    using namespace molga::ui;
    UITextInputLabelFixture f;
    RecordingUITextInputVisualStateProvider provider;
    UITextInputVisualState state;
    state.committedUtf8 = "typed";
    provider.Publish(state);
    f.UseProvider(&provider);
    REQUIRE(f.BuildActiveInput());

    const auto cached = f.System().LastGeometryKey();
    REQUIRE(cached.has_value());
    REQUIRE(f.System().GeometryCacheContains(*cached));
    REQUIRE(cached->inputGeometry.size() == 1);

    // 접기 스트림을 입력 항목 직전까지 그대로 다시 만든다.
    std::size_t state0 = kUILayoutCacheHashSeed;
    state0 = FoldUILayoutCacheHash(state0, cached->worldGeneration);
    state0 = FoldUILayoutCacheHash(
        state0, static_cast<std::uint64_t>(cached->viewport.width.Raw()));
    state0 = FoldUILayoutCacheHash(
        state0, static_cast<std::uint64_t>(cached->viewport.height.Raw()));
    state0 = FoldUILayoutCacheHash(state0, cached->viewportGeneration);
    for (const auto* vector :
         {&cached->canvasScaleRevisions, &cached->hierarchyAndSiblingRevisions,
          &cached->rectAndLayoutRevisions, &cached->intrinsicGenerations}) {
        state0 = FoldUILayoutCacheHash(state0, vector->size());
        for (const auto value : *vector) {
            state0 = FoldUILayoutCacheHash(state0, value);
        }
    }
    state0 = FoldUILayoutCacheHash(state0, cached->inputGeometry.size());

    const auto& entry = cached->inputGeometry[0];

    SUBCASE("only the input identity differs") {
        const std::uint64_t originalObject = entry.input.objectId;
        const std::uint64_t alteredObject = originalObject ^ 0x5A5AULL;
        const std::size_t afterOriginal =
            FoldUILayoutCacheHash(state0, originalObject);
        const std::size_t afterAltered =
            FoldUILayoutCacheHash(state0, alteredObject);

        UILayoutGeometryCacheKey colliding = *cached;
        colliding.inputGeometry[0].input.objectId =
            static_cast<unsigned int>(alteredObject);
        colliding.inputGeometry[0].input.componentInstanceId =
            entry.input.componentInstanceId ^
            static_cast<std::uint64_t>(afterOriginal) ^
            static_cast<std::uint64_t>(afterAltered);

        REQUIRE(colliding != *cached);
        REQUIRE(HashUILayoutGeometryCacheKey(colliding) ==
                HashUILayoutGeometryCacheKey(*cached));
        CHECK_FALSE(f.System().GeometryCacheContains(colliding));
    }

    SUBCASE("only the effective request differs") {
        std::size_t stream = FoldUILayoutCacheHash(state0, entry.input.objectId);
        stream = FoldUILayoutCacheHash(stream, entry.input.componentInstanceId);
        stream = FoldUILayoutCacheHash(
            stream, molga::text::CacheBytesHash(
                        entry.effectiveRequest.style.fontFamilyGuid));
        stream = FoldUILayoutCacheHash(
            stream, static_cast<std::uint64_t>(
                        entry.effectiveRequest.style.shape.fontSize.Raw()));

        const std::string alteredUtf8 = entry.effectiveRequest.utf8 + "!";
        const std::size_t afterOriginal = FoldUILayoutCacheHash(
            stream, molga::text::CacheBytesHash(entry.effectiveRequest.utf8));
        const std::size_t afterAltered = FoldUILayoutCacheHash(
            stream, molga::text::CacheBytesHash(alteredUtf8));

        UILayoutGeometryCacheKey colliding = *cached;
        colliding.inputGeometry[0].effectiveRequest.utf8 = alteredUtf8;
        colliding.inputGeometry[0].effectiveRequest.visualRevision =
            entry.effectiveRequest.visualRevision ^
            static_cast<std::uint64_t>(afterOriginal) ^
            static_cast<std::uint64_t>(afterAltered);

        // 정체성은 글자 하나까지 같다. 두 키를 가르는 것은 유효 요청뿐이다.
        REQUIRE(colliding.inputGeometry[0].input == entry.input);
        REQUIRE(colliding != *cached);
        REQUIRE(HashUILayoutGeometryCacheKey(colliding) ==
                HashUILayoutGeometryCacheKey(*cached));
        CHECK_FALSE(f.System().GeometryCacheContains(colliding));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.1 마감 (F3): 고유 크기의 생산자는 Build 안에 있다
// ─────────────────────────────────────────────────────────────────────────────
// Task 10.2는 이 생산자를 UISystem::CollectRender에 두었고, 그 근거는 "그때
// Build에는 TextLayoutService가 없었다" 하나였다. Step 3i가 그 인자를 넣었으므로
// 근거가 사라졌다. 생산자가 레거시 경로에만 있으면, 스냅샷만으로 그리는 표면
// (Task 11.2가 만드는 바로 그것)에서 모든 라벨이 다시 0으로 측정된다.
TEST_CASE("Build publishes the label intrinsic measured with constraints stripped") {
    UISnapshotPayloadFixture f;
    // NoWrap이면 폭 제약이 아무것도 바꾸지 않아, 제약을 실은 구현도 이 검사를
    // 통과한다. Task 10.2가 같은 자리에서 배운 함정이라 같은 대비를 둔다.
    f.Label().SetWrapMode(molga::text::TextWrapMode::Word);
    f.Label().SetText("intrinsic size probe with several separate words");
    // 두 폭 모두 가장 긴 낱말보다 넓어야 한다. 둘 다 낱말보다 좁으면 줄바꿈
    // 결과가 양쪽에서 "가장 긴 낱말"로 같아져, 제약을 실은 구현도 통과한다.
    auto* rect = f.Leaf().GetComponent<RectTransform>();
    REQUIRE(rect != nullptr);
    rect->SetSizeDelta({400.0f, 40.0f});
    REQUIRE(f.Build());

    const auto target = molga::ui::CaptureTarget(f.GetWorld(), f.Label());
    REQUIRE(target);
    const auto first =
        molga::ui::UIIntrinsicLayoutRegistry::Get().Find(target);
    REQUIRE(first.has_value());
    CHECK(first->intrinsicSize.width.Raw() > 0);
    CHECK(first->intrinsicSize.height.Raw() > 0);

    // 그리고 그 측정은 제약을 보지 않는다. 라벨 사각형을 좁혀도 게시된 값은
    // 그대로여야 한다 — 렌더 요청의 intrinsicSize를 그대로 게시하면 여기서
    // 값이 흔들리고, 그 흔들림이 fitter → rect → 제약 → fitter의 무한 순환이
    // 된다(10.2가 비싸게 배운 규칙).
    rect->SetSizeDelta({120.0f, 40.0f});
    REQUIRE(f.Build());
    const auto second =
        molga::ui::UIIntrinsicLayoutRegistry::Get().Find(target);
    REQUIRE(second.has_value());
    CHECK(second->intrinsicSize.width.Raw() == first->intrinsicSize.width.Raw());
    CHECK(second->intrinsicSize.height.Raw() ==
          first->intrinsicSize.height.Raw());
    CHECK(second->contentIdentity == first->contentIdentity);
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.1 마감 (F2/F4/Q1): 바인딩 핸들의 주인은 등록부 하나다
// ─────────────────────────────────────────────────────────────────────────────
// 만료된 기록을 그냥 버리면 아직 반납되지 않은 핸들을 아는 마지막 지식이
// 사라진다. 그것을 관찰할 수 있는 자리는 ReleasedBindingCount 하나뿐이다 —
// LiveRetainedBindingCount는 만료된 기록을 정의상 건너뛰므로, 조용히 버리는
// 구현과 제대로 반납하는 구현이 그 계수기에서 똑같이 보인다.
TEST_CASE("the binding registry releases retired records and refuses live owners") {
    auto& registry = molga::TextureBindingRegistry::Get();
    // 앞선 케이스가 남긴 만료 기록을 먼저 비운다. 그러지 않으면 아래 델타가
    // 이 케이스의 것이 아니다.
    registry.SweepRetiredBindings();
    const std::uint64_t releasedBefore = registry.ReleasedBindingCount();

    molga::ui::TextureRuntimeBindingIdentity identity;
    identity.deviceGeneration = 910011;
    identity.uploadGeneration = 1;
    identity.texture = molga::detail::MakeTextureHandleForTest(31, 1);
    identity.sampler = molga::detail::MakeSamplerHandleForTest(41, 1);
    identity.lifetimeIdentity = 9001;

    const std::size_t recordsBefore = registry.RecordCount();
    auto retained = registry.Publish(identity);
    REQUIRE(retained != nullptr);
    CHECK(registry.RecordCount() == recordsBefore + 1);
    CHECK(registry.LiveRetainedBindingCount(identity.deviceGeneration) == 1);

    // 살아 있는 토큰은 진짜 외부 소유자다. 쓸어 내기도 teardown도 그 기록을
    // 건드리지 않는다. 여기서 파괴하면 그 소유자가 다음 프레임에 죽은 핸들을
    // 제출한다.
    CHECK(registry.SweepRetiredBindings() == 0);
    std::string error;
    CHECK_FALSE(
        registry.DestroyRetiredBindings(identity.deviceGeneration, error));
    CHECK_FALSE(error.empty());
    CHECK(registry.ReleasedBindingCount() == releasedBefore);

    // 마지막 외부 소유자가 사라지면 그때 놓아 준다.
    retained.reset();
    CHECK(registry.SweepRetiredBindings() == 1);
    CHECK(registry.ReleasedBindingCount() == releasedBefore + 1);
    CHECK(registry.LiveRetainedBindingCount(identity.deviceGeneration) == 0);
    // 기록 자체도 사라진다. 남겨 두면 같은 텍스처를 계속 다시 올리는 세션에서
    // 상한 없이 쌓인다.
    CHECK(registry.RecordCount() == recordsBefore);

    // ── 게시가 하는 그 걷어내기 자체를 잰다 ────────────────────────────────
    // 위의 검사들은 SweepRetiredBindings를 직접 부르므로, Publish 안의
    // 걷어내기가 원래대로 "만료된 기록을 반납 없이 지우기"로 돌아가도 전부
    // 통과한다. 그것이 이 결함의 원래 모습이었다: 기록은 사라지는데 그 기록이
    // 들고 있던 핸들은 아무도 반납하지 않는다. 여기서는 쓸어내기를 부르지
    // 않고, 다음 게시 하나만으로 은퇴한 기록이 *반납되었는지*를 본다.
    molga::ui::TextureRuntimeBindingIdentity retiredIdentity = identity;
    retiredIdentity.lifetimeIdentity = 9002;
    retiredIdentity.texture = molga::detail::MakeTextureHandleForTest(32, 1);
    retiredIdentity.sampler = molga::detail::MakeSamplerHandleForTest(42, 1);
    auto dropped = registry.Publish(retiredIdentity);
    REQUIRE(dropped != nullptr);
    const std::uint64_t releasedAfterRetire = registry.ReleasedBindingCount();
    const std::size_t recordsAfterRetire = registry.RecordCount();
    dropped.reset();
    // 아직 아무도 걷어내지 않았다: 은퇴했을 뿐 반납은 일어나지 않았다.
    CHECK(registry.ReleasedBindingCount() == releasedAfterRetire);
    CHECK(registry.RecordCount() == recordsAfterRetire);

    molga::ui::TextureRuntimeBindingIdentity nextIdentity = identity;
    nextIdentity.lifetimeIdentity = 9003;
    nextIdentity.texture = molga::detail::MakeTextureHandleForTest(33, 1);
    nextIdentity.sampler = molga::detail::MakeSamplerHandleForTest(43, 1);
    auto next = registry.Publish(nextIdentity);
    REQUIRE(next != nullptr);
    // 게시 하나가 은퇴한 기록을 반납했다. 기록 수는 그대로다(하나 나가고 하나
    // 들어왔다) — 그래서 기록 수만 보는 시험은 조용히 버리는 구현도 통과한다.
    CHECK(registry.ReleasedBindingCount() == releasedAfterRetire + 1);
    CHECK(registry.RecordCount() == recordsAfterRetire);
    next.reset();
    registry.SweepRetiredBindings();
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.1 마감 (Q4): 입력 묶음은 자기 칸을 예약한다
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("the frozen input group starts after the shell's own render items") {
    UITextInputLabelFixture f;
    // 입력창 껍데기의 배경. 예약이 없으면 baseOrder가 이 스프라이트의 키와
    // 네 필드 전부 같아진다 — 같은 캔버스 순서, 같은 형제 경로, 같은
    // componentSortingOrder 0, 같은 stableSubmissionIndex.
    auto* background = f.InputObject().AddComponent<UIImage>();
    REQUIRE(background != nullptr);
    background->SetSortingOrder(0);

    const auto snapshot = f.BuildActiveInput();
    REQUIRE(snapshot);
    REQUIRE(snapshot->textInputLabels.size() == 1);
    const auto& owned = snapshot->textInputLabels[0];
    const auto* sprite = FindRenderItem(*snapshot, f.InputObject().GetID());
    REQUIRE(sprite != nullptr);

    CHECK_FALSE(owned.baseOrder == sprite->order);
    CHECK(sprite->order < owned.baseOrder);
    CHECK(owned.baseOrder.stableSubmissionIndex ==
          sprite->order.stableSubmissionIndex + sprite->reservedCommandSpan);
    // 그리고 그 묶음이 예약한 칸을 다른 오브젝트가 가져가지 않는다.
    CHECK(owned.reservedCommandSpan > 0);
    for (const auto& item : snapshot->renderItems) {
        if (item.canonicalSource.sceneObjectId == f.InputObject().GetID()) {
            continue;
        }
        if (item.order.stableSubmissionIndex <
            owned.baseOrder.stableSubmissionIndex) {
            continue;
        }
        CHECK(item.order.stableSubmissionIndex >=
              owned.baseOrder.stableSubmissionIndex + owned.reservedCommandSpan);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Task 11.1 마감 (Q5): 페이로드 진단의 기억은 월드마다다
// ─────────────────────────────────────────────────────────────────────────────
// 씬을 다시 열면 오브젝트 id는 낮은 값부터 다시 쓰인다. 키에 월드 세대가
// 없으면 새 월드 7번의 진짜 첫 진단이 이웃 월드 7번이 이미 보고한 사실 때문에
// 조용히 사라진다.
TEST_CASE("payload diagnostics are remembered per world generation") {
    RenderClipTextRuntime text;
    CountingDiagnosticSink sink;
    molga::ui::UILayoutSystem system;

    const auto buildWorld = [&](World& world) {
        GameObject* root = AddObject(world, 1, nullptr);
        AddConstantCanvas(*root);
        AddOffsetRect(*root, 0.0f, 0.0f, 200.0f, 200.0f);
        GameObject* leaf = AddObject(world, 7, root);
        AddOffsetRect(*leaf, 0.0f, 0.0f, 20.0f, 10.0f);
        auto* image = leaf->AddComponent<UIImage>();
        REQUIRE(image != nullptr);
        image->SetTextureGuid("never-published-texture-guid");
        return system.Build(
            world, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
            molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
            text.service, sink);
    };

    World first;
    REQUIRE(buildWorld(first));
    CHECK(sink.Count(TextDiagnosticCode::ReferenceInvalid) == 1);
    // 같은 표면에서 다시 지어도 같은 사실은 한 번뿐이다(M35의 상한).
    REQUIRE(system.Build(
        first, molga::WindowId{7}, RawSize(200 * 64, 200 * 64),
        molga::ui::EmptyUITextInputVisualStateProvider::Instance(),
        text.service, sink));
    CHECK(sink.Count(TextDiagnosticCode::ReferenceInvalid) == 1);

    // 같은 오브젝트 id, 같은 guid, 그러나 다른 월드다. 두 월드가 동시에 살아
    // 있으므로 은퇴 회수로는 이 행이 통과할 수 없다 — 키의 월드 세대만이
    // 통과시킨다.
    World second;
    REQUIRE(buildWorld(second));
    CHECK(sink.Count(TextDiagnosticCode::ReferenceInvalid) == 2);
}
