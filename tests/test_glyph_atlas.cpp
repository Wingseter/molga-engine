#include "FontCollectionTestSupport.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/FontFace.h"
#include "Text/TextDiagnostic.h"
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using molga::text::TextDiagnosticCode;

namespace {

// ── The fixture face ────────────────────────────────────────────────────────
// 자격 트리의 커밋된 단일 face 폰트를 face 둘짜리 collection으로 감싼다. 이
// 스위트가 재는 계약의 절반이 "키가 face 정체성을 포함하는가"인데, faceIndex가
// 언제나 0인 픽스처 위에서는 face index를 통째로 0으로 굳혀도 아무 단언도
// 움직이지 않는다. 기법 자체는 Task 5.1/5.2가 이미 쓰던 것을 그대로 쓴다.
fs::path FixtureFontPath() {
    return fs::path(MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT) / "fonts" /
           "NotoSans-Regular.ttf";
}

// 저작된 실제 값과 같은 모양을 쓴다. fontRevision은 FontFamilyResolver가 만드는
// "<artifactSha256>:<faceIndex>" 형식이다.
constexpr const char* kFixtureFontGuid = "44444444444444444444444444444444";
constexpr const char* kFixtureFontRevision =
    "b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5:1";
constexpr const char* kOtherFontGuid = "99999999999999999999999999999999";
constexpr const char* kOtherFontRevision =
    "1f2e3d4c5b6a798807162534435261708f9e0d1c2b3a495867758493a2b1c0d9:1";

// 여덟 필드 중 정확히 하나를 바꾼 키와 그 필드 이름. 이름을 함께 돌려주는
// 이유는 아래 완전성 케이스가 "여덟 개가 전부 열거되었는가"를 못 박기
// 때문이다. 목록에서 한 필드가 조용히 빠지면 그 필드는 키에서 빠져도 어떤
// 단언도 움직이지 않는다.
struct LabeledKey : molga::GlyphAtlasKey {
    std::string label;
};

std::vector<LabeledKey> MutateEachLogicalKeyField(
    const molga::GlyphAtlasKey& key) {
    std::vector<LabeledKey> mutations;

    LabeledKey guid{key, "fontGuid"};
    guid.fontGuid = kOtherFontGuid;
    mutations.push_back(std::move(guid));

    LabeledKey revision{key, "fontRevision"};
    revision.fontRevision = kOtherFontRevision;
    mutations.push_back(std::move(revision));

    // 픽스처 collection은 face가 둘이므로 0 <-> 1을 오간다. 기준 키의
    // faceIndex가 1이라 이 변이는 0을 관찰하고, 기준 쪽이 1을 관찰한다.
    LabeledKey faceIndex{key, "faceIndex"};
    faceIndex.faceIndex = key.faceIndex ^ 1U;
    mutations.push_back(std::move(faceIndex));

    LabeledKey pixelSize{key, "pixelSize"};
    pixelSize.pixelSize = static_cast<std::uint16_t>(key.pixelSize + 1U);
    mutations.push_back(std::move(pixelSize));

    // 64가 1.0인 26.6 배율이므로 96은 1.5배다. 래스터가 실제로 달라지는 값을
    // 골라야 "키에만 있고 그림에는 없는 필드"가 되지 않는다.
    LabeledKey rasterScale{key, "rasterScaleKey"};
    rasterScale.rasterScaleKey = 96U;
    mutations.push_back(std::move(rasterScale));

    LabeledKey variation{key, "variationKey"};
    variation.variationKey = key.variationKey + 1U;
    mutations.push_back(std::move(variation));

    // GlyphRenderMode는 아직 Monochrome 하나뿐이라, 열거자 밖의 값으로만
    // 이 필드를 흔들 수 있다. 고정 underlying type을 가진 scoped enum이므로
    // uint8_t 범위의 값은 전부 적법하다. 지금 렌더 모드가 하나라는 사실이
    // "키에서 빼도 된다"는 뜻은 아니다: 빼는 순간 두 번째 모드가 추가될 때
    // 서로 다른 그림이 같은 캐시 항목을 덮어쓰게 된다.
    LabeledKey renderMode{key, "renderMode"};
    renderMode.renderMode = static_cast<molga::GlyphRenderMode>(1);
    mutations.push_back(std::move(renderMode));

    // 픽스처 폰트에서 42 다음 glyph도 그릴 수 있는 윤곽을 갖고 있다.
    LabeledKey glyphId{key, "glyphId"};
    glyphId.glyphId = key.glyphId + 1U;
    mutations.push_back(std::move(glyphId));

    return mutations;
}

bool HasDiagnostic(const molga::text::VectorTextDiagnosticSink& sink,
                   TextDiagnosticCode code) {
    const auto& diagnostics = sink.Diagnostics();
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const molga::text::TextDiagnostic& diagnostic) {
                           return diagnostic.code == code;
                       });
}

std::size_t CountDiagnostics(
    const molga::text::VectorTextDiagnosticSink& sink,
    TextDiagnosticCode code) {
    const auto& diagnostics = sink.Diagnostics();
    return static_cast<std::size_t>(std::count_if(
        diagnostics.begin(), diagnostics.end(),
        [code](const molga::text::TextDiagnostic& diagnostic) {
            return diagnostic.code == code;
        }));
}

struct GlyphAtlasFixture {
    // 프로덕션 기본 page(1024)를 그대로 쓰면 예산이 걸릴 때까지 수천 개의
    // glyph를 올려야 한다. 픽스처는 page 크기만 줄이고 packing 규칙은 건드리지
    // 않으므로, 여기서 재는 예산/LRU 동작은 프로덕션의 그것과 같은 코드다.
    static constexpr int PageSize = 128;
    static constexpr std::uint64_t PageBytes =
        static_cast<std::uint64_t>(PageSize) *
        static_cast<std::uint64_t>(PageSize) *
        static_cast<std::uint64_t>(molga::GlyphAtlasCache::kBytesPerPixel);

    // 기본 예산 케이스 전용 page. 64 MiB / 33,570,436 = 1.999…이므로 두 번째
    // page가 기본 예산이 반드시 거절해야 하는 첫 요청이다.
    static constexpr int HugePageSize = 2897;
    static constexpr std::uint64_t HugePageBytes =
        static_cast<std::uint64_t>(HugePageSize) *
        static_cast<std::uint64_t>(HugePageSize) *
        static_cast<std::uint64_t>(molga::GlyphAtlasCache::kBytesPerPixel);

    // 예산을 설정하지 않는다: 캐시 자신의 기본값을 관찰하는 생성자다.
    GlyphAtlasFixture() : GlyphAtlasFixture(PageSize, 0U, false, 0U) {}
    explicit GlyphAtlasFixture(std::uint64_t residentBudgetBytes)
        : GlyphAtlasFixture(PageSize, 0U, true, residentBudgetBytes) {}

    // 한 page에 cell 하나만 놓는 결정론적 할당기 + 한 page짜리 예산. 새 glyph
    // 하나마다 새 page 정체성이 필요해지므로 page LRU와 pin 규칙이 glyph 크기에
    // 흔들리지 않는다. 프로덕션 packing은 제한되지 않는다.
    static GlyphAtlasFixture OneGlyphPerPage() {
        return GlyphAtlasFixture(PageSize, 1U, true, PageBytes);
    }

    // 기본 예산을 그대로 두고, page 하나가 예산의 절반을 넘게 만든다.
    static GlyphAtlasFixture HugePageWithDefaultBudget() {
        return GlyphAtlasFixture(HugePageSize, 1U, false, 0U);
    }

    // 프로덕션 packing 그대로, page 크기만 지정한다. "glyph가 page보다 큰가"의
    // 경계는 page 크기를 glyph 크기에 맞춰야만 양쪽에서 관찰된다.
    static GlyphAtlasFixture WithPageSize(int pageSize) {
        return GlyphAtlasFixture(pageSize, 0U, false, 0U);
    }

    molga::GlyphAtlasKey KeyForGlyph(std::uint32_t glyphId) const {
        molga::GlyphAtlasKey key;
        key.fontGuid = kFixtureFontGuid;
        key.fontRevision = kFixtureFontRevision;
        // 0이 아니다. faceIndex를 0으로 굳혀 놓아도 통과하는 픽스처를 만들지
        // 않는다는 것이 이 값의 전부다.
        key.faceIndex = 1U;
        key.pixelSize = 23U;
        key.rasterScaleKey = 64U;
        key.variationKey = 0U;
        key.renderMode = molga::GlyphRenderMode::Monochrome;
        key.glyphId = glyphId;
        return key;
    }

    // 키가 지목한 face index로 연 픽스처 face. 키마다 별개의 face 객체를
    // 기억해 두는 것이 핵심이다. face를 faceIndex별로만 공유하면, 앞선
    // 반복이 남긴 LastRasterizedGlyphId()가 이번 반복의 단언을 대신
    // 만족시켜서 "이 face에 이 glyph를 물었는가"가 증명되지 않는다.
    molga::FontFace& FaceForKey(const molga::GlyphAtlasKey& key) {
        auto found = faces_.find(key);
        if (found != faces_.end()) return *found->second;
        auto face = std::make_unique<molga::FontFace>();
        std::string error;
        REQUIRE_MESSAGE(face->LoadFromBytes(collectionBytes_, key.faceIndex,
                                            &error),
                        error);
        REQUIRE(face->FaceIndex() == key.faceIndex);
        molga::FontFace& reference = *face;
        faces_.emplace(key, std::move(face));
        return reference;
    }

    // 순번 ordinal번째로 "그릴 수 있는" glyph의 ID. 픽스처 폰트의 glyph 1~3은
    // .notdef/null/space라 비트맵이 비어 있고, 빈 glyph는 page를 하나도 차지
    // 하지 않는다. page 수명을 재는 케이스들은 "새 glyph마다 새 page"를
    // 전제하므로 여기서 걸러 둔다.
    std::uint32_t DrawableGlyphId(std::size_t ordinal) const {
        REQUIRE(ordinal >= 1U);
        REQUIRE(ordinal <= drawableGlyphIds_.size());
        return drawableGlyphIds_[ordinal - 1U];
    }

    molga::GlyphHandle ResolveGlyph(std::size_t ordinal) {
        const molga::GlyphAtlasKey key = KeyForGlyph(DrawableGlyphId(ordinal));
        return cache.GetGlyph(key, FaceForKey(key), sink);
    }

    void SetNextPageIdentityForTest(std::uint64_t nextPageIdentity) {
        molga::detail::SetGlyphAtlasNextPageIdentityForTest(cache,
                                                            nextPageIdentity);
    }

    // 예산이 실제로 걸릴 때까지 서로 다른 glyph를 올린다. 상한까지 올려 두고
    // 여기서 두 가지를 못 박는다: 최고 수위가 정확히 예산에 닿았다는 것과,
    // 그 뒤의 요청이 tofu로 거절되었다는 것. 이 둘이 없으면 Step 1a의 세
    // "<= 예산" 단언은 page 하나만 만들어도 전부 참이 되어 아무것도 재지 못한다.
    void FillWithDistinctGlyphs() {
        for (std::size_t ordinal = 1U; ordinal <= kFillGlyphCount; ++ordinal) {
            if (ResolveGlyph(ordinal).proceduralTofu) ++fillTofuCount;
        }
        REQUIRE(cache.Telemetry().peakResidentBytes == 2U * PageBytes);
        REQUIRE(fillTofuCount > 0U);
    }

    static constexpr std::size_t kFillGlyphCount = 400U;

    molga::GlyphAtlasCache cache;
    molga::text::VectorTextDiagnosticSink sink;
    std::size_t fillTofuCount = 0U;

private:
    GlyphAtlasFixture(int pageSize, std::size_t glyphsPerPage,
                      bool applyBudget, std::uint64_t residentBudgetBytes)
        : cache(pageSize) {
        const std::vector<unsigned char> raw =
            test_support::MakeTwoFaceCollection(FixtureFontPath());
        collectionBytes_ = std::make_shared<const std::vector<std::uint8_t>>(
            raw.begin(), raw.end());
        if (glyphsPerPage != 0U) {
            molga::detail::SetGlyphAtlasGlyphsPerPageForTest(cache,
                                                             glyphsPerPage);
        }
        if (applyBudget) cache.SetResidentBudget(residentBudgetBytes);
        // page 회계가 픽스처 상수와 갈라지면 예산 케이스는 조용히 무의미해진다.
        REQUIRE(cache.PageBytes() ==
                static_cast<std::uint64_t>(pageSize) *
                    static_cast<std::uint64_t>(pageSize) *
                    static_cast<std::uint64_t>(
                        molga::GlyphAtlasCache::kBytesPerPixel));

        molga::FontFace probe;
        std::string error;
        REQUIRE_MESSAGE(probe.LoadFromBytes(collectionBytes_, 1U, &error),
                        error);
        for (std::uint32_t glyphId = 1U;
             drawableGlyphIds_.size() < kFillGlyphCount + 16U &&
             glyphId < 4096U;
             ++glyphId) {
            const molga::FontGlyphBitmap bitmap =
                probe.RasterizeGlyph(glyphId, 23U, 64U);
            if (bitmap.width > 0 && bitmap.height > 0 &&
                !bitmap.coverage.empty()) {
                drawableGlyphIds_.push_back(glyphId);
            }
        }
        REQUIRE(drawableGlyphIds_.size() >= kFillGlyphCount);
    }

    std::shared_ptr<const std::vector<std::uint8_t>> collectionBytes_;
    std::vector<std::uint32_t> drawableGlyphIds_;
    std::unordered_map<molga::GlyphAtlasKey, std::unique_ptr<molga::FontFace>,
                       molga::GlyphAtlasKeyHash>
        faces_;
};

} // namespace

TEST_CASE("every logical glyph field participates in atlas identity") {
    GlyphAtlasFixture f;
    f.cache.BeginFrame(1);
    molga::GlyphAtlasKey key = f.KeyForGlyph(42);
    const auto baseline = f.cache.GetGlyph(key, f.FaceForKey(key), f.sink);
    REQUIRE_FALSE(baseline.proceduralTofu);
    for (const auto& changed : MutateEachLogicalKeyField(key)) {
        CHECK_FALSE(changed == key);
        CHECK(molga::GlyphAtlasKeyHash{}(changed) !=
              molga::GlyphAtlasKeyHash{}(key));
        const auto before = f.cache.Telemetry();
        const auto other =
            f.cache.GetGlyph(changed, f.FaceForKey(changed), f.sink);
        CHECK_FALSE(other.proceduralTofu);
        CHECK(f.cache.Telemetry().misses == before.misses + 1);
        CHECK(f.cache.Telemetry().uploads == before.uploads + 1);
        CHECK(other.glyph.glyphId == changed.glyphId);
        CHECK(f.cache.LastUploadedKeyForTest() == changed);
        CHECK(f.FaceForKey(changed).LastRasterizedGlyphId() == changed.glyphId);
    }
    f.cache.EndCollection(1);
}

TEST_CASE("the logical key mutation set enumerates every atlas key field") {
    GlyphAtlasFixture f;
    const molga::GlyphAtlasKey key = f.KeyForGlyph(42);

    // 이 픽스처를 조용히 무의미하게 만드는 두 가지를 여기서 막는다. 기준 키가
    // faceIndex 0으로 돌아가면 face 차원은 다시 아무도 보지 않게 되고, glyph ID
    // 42가 codepoint 42의 glyph이기도 하면 키를 codepoint로 되돌려도 통과한다.
    REQUIRE(key.faceIndex != 0U);
    REQUIRE(f.FaceForKey(key).GlyphId(static_cast<char32_t>(key.glyphId)) !=
            key.glyphId);

    std::vector<std::string> labels;
    for (const auto& changed : MutateEachLogicalKeyField(key)) {
        labels.push_back(changed.label);
    }
    std::sort(labels.begin(), labels.end());
    const std::vector<std::string> expected{
        "faceIndex", "fontGuid",      "fontRevision", "glyphId",
        "pixelSize", "rasterScaleKey", "renderMode",  "variationKey"};
    CHECK(labels == expected);

    // 위 변이 케이스의 단언은 전부 "달라야 한다"쪽이다. 같은 키가 같다는
    // 반대편이 없으면 operator==와 해시를 "언제나 다름"으로 굳혀도 통과한다.
    const molga::GlyphAtlasKey same = f.KeyForGlyph(42);
    CHECK(key == same);
    CHECK(molga::GlyphAtlasKeyHash{}(key) == molga::GlyphAtlasKeyHash{}(same));
}

TEST_CASE("atlas never exceeds its configured page budget") {
    GlyphAtlasFixture f(2 * GlyphAtlasFixture::PageBytes);
    f.cache.BeginFrame(1);
    f.FillWithDistinctGlyphs();
    f.cache.EndCollection(1);
    CHECK(f.cache.Telemetry().residentBytes <= 2 * GlyphAtlasFixture::PageBytes);
    CHECK(f.cache.Telemetry().peakResidentBytes <= 2 * GlyphAtlasFixture::PageBytes);
    CHECK(molga::GlyphAtlasCache::DefaultResidentBudgetBytes ==
          64ULL * 1024ULL * 1024ULL);
}

TEST_CASE("zero atlas budget is zero capacity") {
    GlyphAtlasFixture f(0);
    f.cache.BeginFrame(1);
    const auto handle = f.ResolveGlyph(9);
    CHECK(handle.proceduralTofu);
    CHECK(handle.pageIdentity == 0);
    CHECK_FALSE(handle.pageLifetime);
    CHECK(f.cache.Telemetry().uploads == 0);
    CHECK(f.cache.Telemetry().residentBytes == 0);
    CHECK(HasDiagnostic(f.sink,
          molga::text::TextDiagnosticCode::AtlasExhausted));
    f.cache.EndCollection(1);
}

TEST_CASE("cache ownership is not mistaken for an external page pin") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.BeginFrame(1);
    auto first = f.ResolveGlyph(1);
    const auto firstPage = first.pageIdentity;
    f.cache.EndCollection(1);
    first.pageLifetime.reset();
    f.cache.BeginFrame(2);
    f.ResolveGlyph(2);
    CHECK_FALSE(f.cache.IsPageResident(firstPage));
    f.cache.EndCollection(2);
}

TEST_CASE("current collection pin blocks eviction without an external owner") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.BeginFrame(1);
    auto first = f.ResolveGlyph(1);
    const auto firstPage = first.pageIdentity;
    first.pageLifetime.reset();
    CHECK(f.ResolveGlyph(2).proceduralTofu);
    CHECK(f.cache.IsPageResident(firstPage));
    f.cache.EndCollection(1);
}

TEST_CASE("external page owner blocks eviction after collection ends") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.BeginFrame(1);
    auto first = f.ResolveGlyph(1);
    const auto firstPage = first.pageIdentity;
    f.cache.EndCollection(1);
    f.cache.BeginFrame(2);
    CHECK(f.ResolveGlyph(2).proceduralTofu);
    CHECK(f.cache.IsPageResident(firstPage));
    f.cache.EndCollection(2);
}

TEST_CASE("page identity exhaustion fails closed without reuse") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.SetNextPageIdentityForTest(UINT64_MAX);
    f.cache.BeginFrame(1);
    auto last = f.ResolveGlyph(1);
    CHECK(last.pageIdentity == UINT64_MAX);
    f.cache.EndCollection(1);
    last.pageLifetime.reset();
    f.cache.BeginFrame(2);
    const auto exhausted = f.ResolveGlyph(2);
    CHECK(exhausted.proceduralTofu);
    CHECK(exhausted.pageIdentity == 0);
    CHECK(HasDiagnostic(f.sink,
          molga::text::TextDiagnosticCode::AtlasExhausted));
    f.cache.EndCollection(2);
}

// Step 1g. 외부 소유권만이 teardown을 막는다: cache 소유권은 절대 external로
// 세지 않고, 정확히 그 토큰 하나가 풀려야 장치 안전 해제가 열린다.
TEST_CASE("external page pins outlive collection teardown until released") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.BeginFrame(1);
    auto held = f.ResolveGlyph(1);
    REQUIRE_FALSE(held.proceduralTofu);
    REQUIRE(held.pageLifetime);
    const std::uint64_t heldPage = held.pageIdentity;
    CHECK(heldPage != 0U);
    f.cache.EndCollection(1);

    CHECK(f.cache.LiveExternalPagePinCount() == 1U);
    CHECK_FALSE(f.cache.ReleaseAfterGpuIdle());
    // "변이 전에 false를 돌려준다": 거절된 호출은 아무것도 건드리지 않는다.
    CHECK(f.cache.Telemetry().residentBytes == GlyphAtlasFixture::PageBytes);
    CHECK(f.cache.ResidentPageCount() == 1U);
    CHECK(f.cache.IsPageResident(heldPage));

    held.pageLifetime.reset();
    CHECK(f.cache.LiveExternalPagePinCount() == 0U);
    CHECK(f.cache.ReleaseAfterGpuIdle());
    CHECK(f.cache.Telemetry().residentBytes == 0U);
    CHECK(f.cache.ResidentPageCount() == 0U);
    CHECK_FALSE(f.cache.IsPageResident(heldPage));
    // 단조 telemetry는 해제로 지워지지 않는다.
    CHECK(f.cache.Telemetry().uploads == 1U);
    CHECK(f.cache.Telemetry().peakResidentBytes == GlyphAtlasFixture::PageBytes);

    // cache 소유권만 남은 page는 external pin이 아니다.
    f.cache.BeginFrame(2);
    auto owned = f.ResolveGlyph(2);
    REQUIRE_FALSE(owned.proceduralTofu);
    const std::uint64_t ownedPage = owned.pageIdentity;
    // 절대 재사용되지 않는 할당기: 해제가 정체성을 되돌리지 않는다.
    CHECK(ownedPage > heldPage);
    f.cache.EndCollection(2);
    owned.pageLifetime.reset();
    CHECK(f.cache.LiveExternalPagePinCount() == 0U);
    CHECK(f.cache.ResidentPageCount() == 1U);
    CHECK(f.cache.IsPageResident(ownedPage));
    CHECK(f.cache.ReleaseAfterGpuIdle());
    CHECK(f.cache.ResidentPageCount() == 0U);
    CHECK(f.cache.Telemetry().residentBytes == 0U);

    // 수집이 열려 있는 동안에는 pin이 하나도 없어도 거절한다.
    f.cache.BeginFrame(3);
    CHECK(f.cache.LiveExternalPagePinCount() == 0U);
    CHECK_FALSE(f.cache.ReleaseAfterGpuIdle());
    f.cache.EndCollection(3);
    CHECK(f.cache.ReleaseAfterGpuIdle());
}

// BeginFrame도 같은 pin 집합을 비우므로, 두 호출 사이의 창에서 보지 않으면
// "EndCollection이 자기 집합을 비운다"는 계약에는 증인이 없다 — 비우기를 통째로
// 지워도 다음 BeginFrame이 뒷정리를 해 준다. 수집 밖 조회가 그 창이다.
TEST_CASE("ending a collection releases its claim before the next frame") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.BeginFrame(1);
    auto first = f.ResolveGlyph(1);
    REQUIRE_FALSE(first.proceduralTofu);
    const std::uint64_t firstPage = first.pageIdentity;
    f.cache.EndCollection(1);
    first.pageLifetime.reset();

    auto outside = f.ResolveGlyph(2);
    CHECK_FALSE(outside.proceduralTofu);
    CHECK_FALSE(f.cache.IsPageResident(firstPage));
    CHECK(f.cache.Telemetry().evictions == 1U);

    // 수집 밖 조회는 어떤 수집의 것도 아니므로 pin을 남기지 않는다. 남긴다면
    // 그 pin은 비워 줄 EndCollection이 없어 page를 영원히 붙든다. 대신 외부
    // 토큰은 준다: 이 handle이 page를 살려 두는 유일한 소유자다.
    const std::uint64_t outsidePage = outside.pageIdentity;
    REQUIRE(outside.pageLifetime);
    CHECK(f.cache.LiveExternalPagePinCount() == 1U);
    CHECK_FALSE(f.cache.ReleaseAfterGpuIdle());

    outside.pageLifetime.reset();
    const auto third = f.ResolveGlyph(3);
    CHECK_FALSE(third.proceduralTofu);
    CHECK_FALSE(f.cache.IsPageResident(outsidePage));
    CHECK(f.cache.Telemetry().evictions == 2U);
}

TEST_CASE("a drawable glyph carries its page slot and a normalized uv rect") {
    GlyphAtlasFixture f;
    f.cache.BeginFrame(1);
    const molga::GlyphAtlasKey key = f.KeyForGlyph(42);
    molga::FontFace& face = f.FaceForKey(key);
    const molga::FontGlyphBitmap bitmap = face.RasterizeGlyph(42U, 23U, 64U);
    REQUIRE(bitmap.width > 0);
    REQUIRE(bitmap.height > 0);

    const auto handle = f.cache.GetGlyph(key, face, f.sink);
    REQUIRE(handle.glyph.drawable);
    CHECK(handle.glyph.pageIndex >= 0);
    CHECK(handle.glyph.width == bitmap.width);
    CHECK(handle.glyph.height == bitmap.height);
    CHECK(handle.glyph.xOffset == bitmap.xOffset);
    CHECK(handle.glyph.yOffset == bitmap.yOffset);

    // uv는 page 크기로 정규화된 사각형이다. 크기가 어긋나면 화면에는 이웃
    // glyph의 일부가 섞여 나올 뿐 어떤 진단도 나오지 않는다.
    const float extent = static_cast<float>(GlyphAtlasFixture::PageSize);
    CHECK(handle.glyph.u0 >= 0.0f);
    CHECK(handle.glyph.v0 >= 0.0f);
    CHECK(handle.glyph.u1 <= 1.0f);
    CHECK(handle.glyph.v1 <= 1.0f);
    CHECK(handle.glyph.u1 - handle.glyph.u0 ==
          doctest::Approx(static_cast<float>(bitmap.width) / extent));
    CHECK(handle.glyph.v1 - handle.glyph.v0 ==
          doctest::Approx(static_cast<float>(bitmap.height) / extent));
    f.cache.EndCollection(1);
}

// packing 자체를 재는 유일한 케이스다. page 수명/LRU 케이스는 전부
// OneGlyphPerPage()로 "새 glyph마다 새 page"를 강제하므로, 여기가 없으면 열린
// page 재사용을 통째로 꺼도(=glyph 하나에 page 하나) 스위트가 전부 통과한다.
// 프로덕션 1024 page에서 그것은 4 MiB짜리 page에 glyph 하나이므로, 64 MiB
// 예산이 glyph 16개에서 포화하는 총체적 실패다.
//
// 같은 page 위의 두 번째 glyph여야 uv도 재진다. 첫 glyph는 page 원점(1,1)에
// 놓여 x == y이므로 u/v를 뒤바꾼 구현이 첫 glyph만으로는 구분되지 않는다.
TEST_CASE("two glyphs share one page and their uv rects follow their slots") {
    GlyphAtlasFixture f;
    f.cache.BeginFrame(1);
    const auto first = f.ResolveGlyph(1);
    const auto second = f.ResolveGlyph(2);
    REQUIRE(first.glyph.drawable);
    REQUIRE(second.glyph.drawable);

    CHECK(second.pageIdentity == first.pageIdentity);
    CHECK(second.glyph.pageIndex == first.glyph.pageIndex);
    CHECK(f.cache.ResidentPageCount() == 1U);
    CHECK(f.cache.Telemetry().residentBytes == GlyphAtlasFixture::PageBytes);

    constexpr float kExtent = static_cast<float>(GlyphAtlasFixture::PageSize);
    constexpr float kOnePixel = 1.0f / kExtent;
    // 첫 glyph는 원점(1,1), 둘째는 같은 선반의 오른쪽이다. v0는 그대로 한
    // 픽셀이고 u0만 커진다 — u/v를 뒤바꾸면 이 두 단언이 서로 자리를 바꾼다.
    CHECK(first.glyph.u0 == doctest::Approx(kOnePixel));
    CHECK(first.glyph.v0 == doctest::Approx(kOnePixel));
    CHECK(second.glyph.v0 == doctest::Approx(kOnePixel));
    CHECK(second.glyph.u0 > second.glyph.v0);
    CHECK(second.glyph.u0 == doctest::Approx(first.glyph.u1 + kOnePixel));
    f.cache.EndCollection(1);
}

// atlas의 유일한 책임은 래스터 이미지를 담는 것인데, 그 바이트가 실제로 page에
// 닿았는지는 어떤 단언도 보지 않았다. 헤드리스 테스트에는 GraphicsDevice가 없어
// GlyphInfo::texture가 언제나 nullptr이므로 GPU 쪽은 볼 수 없지만, page의 CPU
// 원본은 볼 수 있다 — 업로드는 그 원본을 거쳐서만 일어난다.
TEST_CASE("uploaded coverage lands in the page where the uv rect says it does") {
    GlyphAtlasFixture f;
    f.cache.BeginFrame(1);
    // 두 번째 glyph여야 x != y다. page 원점(1,1)에 놓이는 첫 glyph만 보면
    // 좌표를 뒤바꾼 복사도, 뒤바뀐 uv도 구분되지 않는다.
    const auto first = f.ResolveGlyph(1);
    const auto second = f.ResolveGlyph(2);
    REQUIRE(first.glyph.drawable);
    REQUIRE(second.glyph.drawable);
    REQUIRE(second.pageIdentity == first.pageIdentity);

    const molga::GlyphAtlasKey key = f.KeyForGlyph(f.DrawableGlyphId(2));
    const molga::FontGlyphBitmap bitmap = f.FaceForKey(key).RasterizeGlyph(
        key.glyphId, key.pixelSize, key.rasterScaleKey);
    REQUIRE(bitmap.width == second.glyph.width);
    REQUIRE(bitmap.height == second.glyph.height);

    constexpr float kExtent = static_cast<float>(GlyphAtlasFixture::PageSize);
    const int x = static_cast<int>(std::lround(second.glyph.u0 * kExtent));
    const int y = static_cast<int>(std::lround(second.glyph.v0 * kExtent));
    // 픽스처가 조용히 대칭이 되면 이 케이스는 아무것도 잡지 못한다.
    REQUIRE(x != y);

    std::size_t mismatched = 0U;
    std::size_t inked = 0U;
    for (int row = 0; row < bitmap.height; ++row) {
        for (int column = 0; column < bitmap.width; ++column) {
            const unsigned char expected =
                bitmap.coverage[static_cast<std::size_t>(row) *
                                    static_cast<std::size_t>(bitmap.width) +
                                static_cast<std::size_t>(column)];
            if (expected != 0U) ++inked;
            if (f.cache.PageCoverageForTest(second.pageIdentity, x + column,
                                            y + row) != expected) {
                ++mismatched;
            }
        }
    }
    CHECK(mismatched == 0U);
    // 반대편: coverage가 전부 0이면 복사를 통째로 지워도 위 단언이 통과한다.
    CHECK(inked > 0U);

    // 사각형 밖은 건드리지 않았고, 상주하지 않는 page는 0이다.
    CHECK(f.cache.PageCoverageForTest(second.pageIdentity, 0, 0) == 0U);
    CHECK(f.cache.PageCoverageForTest(second.pageIdentity,
                                      GlyphAtlasFixture::PageSize - 1,
                                      GlyphAtlasFixture::PageSize - 1) == 0U);
    CHECK(f.cache.PageCoverageForTest(0U, x, y) == 0U);
    CHECK(f.cache.PageCoverageForTest(second.pageIdentity, -1, -1) == 0U);
    f.cache.EndCollection(1);
}

// page당 외부 토큰은 정확히 하나다. glyph마다 새 토큰을 만들면, 같은 page의
// 다른 glyph 토큰을 아직 들고 있는데도 LiveExternalPagePinCount가 0을 보고하고
// ReleaseAfterGpuIdle이 제출된 프레임 밑에서 page 소유권을 지워 버린다.
TEST_CASE("one page hands out exactly one external lifetime token") {
    GlyphAtlasFixture f;
    f.cache.BeginFrame(1);
    auto first = f.ResolveGlyph(1);
    auto second = f.ResolveGlyph(2);
    REQUIRE(first.glyph.drawable);
    REQUIRE(second.glyph.drawable);
    REQUIRE(second.pageIdentity == first.pageIdentity);

    REQUIRE(first.pageLifetime);
    CHECK(second.pageLifetime == first.pageLifetime);
    CHECK(f.cache.LiveExternalPagePinCount() == 1U);
    f.cache.EndCollection(1);

    // 둘째 handle만 놓아도 page는 여전히 밖에서 붙들려 있다.
    second.pageLifetime.reset();
    CHECK(f.cache.LiveExternalPagePinCount() == 1U);
    CHECK_FALSE(f.cache.ReleaseAfterGpuIdle());
    CHECK(f.cache.IsPageResident(first.pageIdentity));

    first.pageLifetime.reset();
    CHECK(f.cache.LiveExternalPagePinCount() == 0U);
    CHECK(f.cache.ReleaseAfterGpuIdle());
}

TEST_CASE("a repeated glyph is a hit that rasterizes and uploads nothing") {
    GlyphAtlasFixture f;
    f.cache.BeginFrame(1);
    const molga::GlyphAtlasKey key = f.KeyForGlyph(42);
    const auto first = f.cache.GetGlyph(key, f.FaceForKey(key), f.sink);
    REQUIRE_FALSE(first.proceduralTofu);
    REQUIRE(first.glyph.drawable);
    const auto after = f.cache.Telemetry();
    CHECK(after.misses == 1U);
    CHECK(after.hits == 0U);
    CHECK(after.uploads == 1U);

    const auto second = f.cache.GetGlyph(key, f.FaceForKey(key), f.sink);
    CHECK_FALSE(second.proceduralTofu);
    // hit도 page 지분을 준다. 주지 않으면 캐시에서 나온 glyph를 그린 명령이
    // 아무 소유자 없는 텍스처를 가리키게 된다.
    REQUIRE(second.pageLifetime);
    CHECK(second.pageIdentity == first.pageIdentity);
    CHECK(second.glyph.glyphId == first.glyph.glyphId);
    CHECK(second.glyph.width == first.glyph.width);
    CHECK(f.cache.Telemetry().hits == 1U);
    CHECK(f.cache.Telemetry().misses == after.misses);
    CHECK(f.cache.Telemetry().uploads == after.uploads);
    CHECK(f.cache.Telemetry().residentBytes == after.residentBytes);
    f.cache.EndCollection(1);
}

// hit은 upload가 아니지만 소유권 면에서는 upload와 똑같이 취급되어야 한다.
// 캐시에서 나왔다는 이유로 pin을 건너뛰면, 두 번째 프레임부터 그리는 glyph가
// 자기 page를 붙들지 못하고 같은 프레임 안에서 그 page가 축출된다.
TEST_CASE("a cache hit pins its page exactly as an upload does") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.BeginFrame(1);
    auto first = f.ResolveGlyph(1);
    REQUIRE_FALSE(first.proceduralTofu);
    const std::uint64_t firstPage = first.pageIdentity;
    f.cache.EndCollection(1);
    first.pageLifetime.reset();

    f.cache.BeginFrame(2);
    auto hit = f.ResolveGlyph(1);
    REQUIRE(f.cache.Telemetry().hits == 1U);
    REQUIRE_FALSE(hit.proceduralTofu);
    CHECK(hit.pageIdentity == firstPage);
    // 이번 수집의 pin: 한 page짜리 예산에서 다음 glyph는 자리를 못 찾는다.
    CHECK(f.ResolveGlyph(2).proceduralTofu);
    CHECK(f.cache.IsPageResident(firstPage));
    f.cache.EndCollection(2);

    // 외부 토큰: 수집이 끝난 뒤에도 이 handle 하나가 page를 붙든다.
    CHECK(f.cache.LiveExternalPagePinCount() == 1U);
    f.cache.BeginFrame(3);
    CHECK(f.ResolveGlyph(2).proceduralTofu);
    CHECK(f.cache.IsPageResident(firstPage));
    f.cache.EndCollection(3);
    hit.pageLifetime.reset();
    CHECK(f.cache.LiveExternalPagePinCount() == 0U);
}

// 축출 순서는 "가장 오래 쓰이지 않은" page이고, hit도 쓴 것이다. hit이 순서를
// 갱신하지 않으면 매 프레임 그려지는 page가 한 번도 다시 업로드되지 않았다는
// 이유로 먼저 버려지고, 다음 프레임이 그것을 다시 올린다.
TEST_CASE("a cache hit refreshes the page's eviction order") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.SetResidentBudget(2U * GlyphAtlasFixture::PageBytes);

    f.cache.BeginFrame(1);
    auto older = f.ResolveGlyph(1);
    auto newer = f.ResolveGlyph(2);
    REQUIRE_FALSE(older.proceduralTofu);
    REQUIRE_FALSE(newer.proceduralTofu);
    const std::uint64_t olderPage = older.pageIdentity;
    const std::uint64_t newerPage = newer.pageIdentity;
    f.cache.EndCollection(1);
    older.pageLifetime.reset();
    newer.pageLifetime.reset();

    // 올린 순서만 보면 olderPage가 먼저 나가야 한다. 이 프레임의 hit이 그
    // 순서를 뒤집는다.
    f.cache.BeginFrame(2);
    auto hit = f.ResolveGlyph(1);
    REQUIRE(f.cache.Telemetry().hits == 1U);
    REQUIRE(hit.pageIdentity == olderPage);
    f.cache.EndCollection(2);
    hit.pageLifetime.reset();

    f.cache.BeginFrame(3);
    const auto admitted = f.ResolveGlyph(3);
    CHECK_FALSE(admitted.proceduralTofu);
    CHECK(f.cache.Telemetry().evictions == 1U);
    CHECK(f.cache.IsPageResident(olderPage));
    CHECK_FALSE(f.cache.IsPageResident(newerPage));
    f.cache.EndCollection(3);
}

// 축출은 page만 버리는 것이 아니다. 그 page 위에 있던 항목까지 함께 지우지
// 않으면, 다음 조회가 hit으로 판정되어 이미 사라진 page를 찾아간다.
TEST_CASE("an evicted page takes its glyph entries with it") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.BeginFrame(1);
    auto first = f.ResolveGlyph(1);
    REQUIRE_FALSE(first.proceduralTofu);
    const std::uint64_t firstPage = first.pageIdentity;
    f.cache.EndCollection(1);
    first.pageLifetime.reset();

    f.cache.BeginFrame(2);
    REQUIRE_FALSE(f.ResolveGlyph(2).proceduralTofu);
    REQUIRE_FALSE(f.cache.IsPageResident(firstPage));
    f.cache.EndCollection(2);

    const auto before = f.cache.Telemetry();
    f.cache.BeginFrame(3);
    const auto again = f.ResolveGlyph(1);
    CHECK_FALSE(again.proceduralTofu);
    CHECK(again.pageIdentity != firstPage);
    CHECK(f.cache.IsPageResident(again.pageIdentity));
    CHECK(f.cache.Telemetry().misses == before.misses + 1U);
    CHECK(f.cache.Telemetry().uploads == before.uploads + 1U);
    CHECK(f.cache.Telemetry().hits == before.hits);
    f.cache.EndCollection(3);
}

TEST_CASE("a blank glyph is cached without a page or an upload") {
    GlyphAtlasFixture f;
    f.cache.BeginFrame(1);
    // 픽스처 폰트의 glyph 3은 space이고 윤곽이 없다.
    const molga::GlyphAtlasKey key = f.KeyForGlyph(3);
    molga::FontFace& face = f.FaceForKey(key);
    const molga::FontGlyphBitmap blank = face.RasterizeGlyph(3U, 23U, 64U);
    REQUIRE(blank.width == 0);
    REQUIRE(blank.height == 0);

    const auto handle = f.cache.GetGlyph(key, face, f.sink);
    CHECK_FALSE(handle.proceduralTofu);
    CHECK_FALSE(handle.glyph.drawable);
    CHECK(handle.glyph.glyphId == 3U);
    CHECK(handle.glyph.pageIndex == -1);
    CHECK(handle.pageIdentity == 0U);
    CHECK_FALSE(handle.pageLifetime);
    CHECK(f.cache.Telemetry().uploads == 0U);
    CHECK(f.cache.ResidentPageCount() == 0U);
    CHECK(f.cache.Telemetry().residentBytes == 0U);
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::AtlasExhausted));

    // 두 번째 요청은 hit이고, 다시 래스터하지 않는다.
    const auto again = f.cache.GetGlyph(key, face, f.sink);
    CHECK_FALSE(again.glyph.drawable);
    CHECK(f.cache.Telemetry().hits == 1U);
    f.cache.EndCollection(1);
}

TEST_CASE("the resident budget admits exactly the pages it pays for") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.SetResidentBudget(3U * GlyphAtlasFixture::PageBytes);

    f.cache.BeginFrame(1);
    std::vector<molga::GlyphHandle> handles;
    for (std::size_t ordinal = 1U; ordinal <= 3U; ++ordinal) {
        handles.push_back(f.ResolveGlyph(ordinal));
        REQUIRE_FALSE(handles.back().proceduralTofu);
    }
    const std::uint64_t oldestPage = handles.front().pageIdentity;
    CHECK(f.cache.IsPageResident(oldestPage));
    CHECK(f.cache.ResidentPageCount() == 3U);
    CHECK(f.cache.Telemetry().residentBytes ==
          3U * GlyphAtlasFixture::PageBytes);
    // 세 page 전부 이번 수집이 pin하고 있으므로 네 번째는 tofu다.
    CHECK(f.ResolveGlyph(4).proceduralTofu);
    CHECK(f.cache.Telemetry().residentBytes ==
          3U * GlyphAtlasFixture::PageBytes);
    CHECK(f.cache.Telemetry().evictions == 0U);
    f.cache.EndCollection(1);

    // 다음 수집에서 pin이 풀리면 같은 요청이 LRU 축출로 성공한다.
    handles.clear();
    f.cache.BeginFrame(2);
    const auto admitted = f.ResolveGlyph(4);
    CHECK_FALSE(admitted.proceduralTofu);
    CHECK(f.cache.Telemetry().evictions == 1U);
    CHECK(f.cache.ResidentPageCount() == 3U);
    CHECK(f.cache.Telemetry().residentBytes ==
          3U * GlyphAtlasFixture::PageBytes);
    // 가장 오래 쓰이지 않은 page가 나갔다.
    CHECK_FALSE(f.cache.IsPageResident(oldestPage));
    f.cache.EndCollection(2);
}

// pageIndex는 조밀한 slot 번호이고 축출된 자리는 재활용된다. 자리를 반납하지
// 않으면 slot 벡터와 pageIndex 값이 프로세스 수명 동안 만들어진 page 수만큼
// 자라난다 — 존재 이유가 단단한 상한 하나뿐인 하위 시스템에서 무한 증가다.
TEST_CASE("page slots are distinct while live and recycled after eviction") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.SetResidentBudget(2U * GlyphAtlasFixture::PageBytes);

    f.cache.BeginFrame(1);
    auto older = f.ResolveGlyph(1);
    auto newer = f.ResolveGlyph(2);
    REQUIRE_FALSE(older.proceduralTofu);
    REQUIRE_FALSE(newer.proceduralTofu);
    CHECK(older.glyph.pageIndex >= 0);
    CHECK(newer.glyph.pageIndex >= 0);
    // 동시에 살아 있는 두 page가 같은 자리를 가리키면 텍스처 배열 바인딩이
    // 서로를 덮어쓴다.
    CHECK(newer.glyph.pageIndex != older.glyph.pageIndex);
    const int recycledSlot = older.glyph.pageIndex;
    f.cache.EndCollection(1);
    older.pageLifetime.reset();
    newer.pageLifetime.reset();

    f.cache.BeginFrame(2);
    const auto admitted = f.ResolveGlyph(3);
    CHECK_FALSE(admitted.proceduralTofu);
    CHECK(f.cache.Telemetry().evictions == 1U);
    CHECK_FALSE(f.cache.IsPageResident(older.pageIdentity));
    CHECK(f.cache.ResidentPageCount() == 2U);
    CHECK(admitted.glyph.pageIndex == recycledSlot);
    // 자리는 재활용되어도 정체성은 재활용되지 않는다. 이 둘을 혼동하면 늦게
    // 도착한 fence가 같은 자리에 앉은 다른 page를 반납한다.
    CHECK(admitted.pageIdentity > newer.pageIdentity);
    f.cache.EndCollection(2);
}

// 예산을 낮추면 상주량이 예산을 넘긴 채 남는다(수집 중에 눈앞에서 page를
// 없애지 않으므로). 그 상태에서 부족분은 page 하나가 아니라 "상주량 + 새 page -
// 예산"이다. 남은 여유에서 빼면 부족분을 한 page로 과소평가해서, 결국 tofu로
// 끝나는 요청이 살아 있던 page들을 먼저 버리고 만다.
TEST_CASE("a shrunken budget refuses a page without discarding live ones") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.SetResidentBudget(3U * GlyphAtlasFixture::PageBytes);

    f.cache.BeginFrame(1);
    std::vector<molga::GlyphHandle> handles;
    for (std::size_t ordinal = 1U; ordinal <= 3U; ++ordinal) {
        handles.push_back(f.ResolveGlyph(ordinal));
        REQUIRE_FALSE(handles.back().proceduralTofu);
    }
    REQUIRE(f.cache.ResidentPageCount() == 3U);
    f.cache.EndCollection(1);

    // 하나만 밖에서 붙들어 둔다: 나머지 둘은 축출 가능하지만, 둘을 다 버려도
    // 새 page가 들어갈 자리는 나오지 않는다.
    handles[1].pageLifetime.reset();
    handles[2].pageLifetime.reset();
    REQUIRE(f.cache.LiveExternalPagePinCount() == 1U);
    f.cache.SetResidentBudget(GlyphAtlasFixture::PageBytes);

    f.cache.BeginFrame(2);
    const auto refused = f.ResolveGlyph(4);
    CHECK(refused.proceduralTofu);
    CHECK(refused.pageIdentity == 0U);
    CHECK(HasDiagnostic(f.sink, TextDiagnosticCode::AtlasExhausted));
    // 아무것도 버리지 않았다. 버렸다면 다음 프레임이 그 전부를 다시 올린다.
    CHECK(f.cache.Telemetry().evictions == 0U);
    CHECK(f.cache.ResidentPageCount() == 3U);
    CHECK(f.cache.Telemetry().residentBytes ==
          3U * GlyphAtlasFixture::PageBytes);
    for (const auto& handle : handles) {
        CHECK(f.cache.IsPageResident(handle.pageIdentity));
    }
    f.cache.EndCollection(2);

    // 반대편: 낮아진 예산이 실제로 담을 수 있는 요청은 통과하고, 그때는
    // 축출이 일어난다. 그러지 않으면 위 단언은 "언제나 거절"로도 통과한다.
    handles.front().pageLifetime.reset();
    f.cache.BeginFrame(3);
    const auto admitted = f.ResolveGlyph(4);
    CHECK_FALSE(admitted.proceduralTofu);
    CHECK(f.cache.Telemetry().evictions == 3U);
    CHECK(f.cache.ResidentPageCount() == 1U);
    CHECK(f.cache.Telemetry().residentBytes == GlyphAtlasFixture::PageBytes);
    f.cache.EndCollection(3);
}

TEST_CASE("the default resident budget admits exactly 64 MiB of pages") {
    // 기본값이 32 MiB였다면 첫 page도 못 들어오고, 128 MiB였다면 둘째 page가
    // 들어온다. 두 단언 중 하나가 반드시 깨진다.
    static_assert(GlyphAtlasFixture::HugePageBytes <=
                      molga::GlyphAtlasCache::DefaultResidentBudgetBytes,
                  "one huge page must fit inside the default budget");
    static_assert(2U * GlyphAtlasFixture::HugePageBytes >
                      molga::GlyphAtlasCache::DefaultResidentBudgetBytes,
                  "two huge pages must not fit inside the default budget");

    GlyphAtlasFixture f = GlyphAtlasFixture::HugePageWithDefaultBudget();
    f.cache.BeginFrame(1);
    const auto first = f.ResolveGlyph(1);
    CHECK_FALSE(first.proceduralTofu);
    CHECK(f.cache.Telemetry().residentBytes ==
          GlyphAtlasFixture::HugePageBytes);
    CHECK(f.ResolveGlyph(2).proceduralTofu);
    CHECK(f.cache.Telemetry().residentBytes ==
          GlyphAtlasFixture::HugePageBytes);
    CHECK(f.cache.ResidentPageCount() == 1U);
    f.cache.EndCollection(1);
}

// page보다 큰 glyph는 page를 만들기 전에 거절해야 한다. 그 경계가 한 픽셀
// 어긋나면 정확히 들어맞는 glyph가 영원히 tofu가 되고, 진단만 남는다.
TEST_CASE("the oversized glyph guard closes exactly one pixel past the page") {
    constexpr int kPagePadding = 1;
    GlyphAtlasFixture measure;
    molga::GlyphAtlasKey key = measure.KeyForGlyph(42);
    // 최소 page 크기(16)에서 충분히 떨어진 비트맵이라야 양쪽 page를 다 만들 수
    // 있다.
    key.pixelSize = 64U;
    const molga::FontGlyphBitmap bitmap = measure.FaceForKey(key).RasterizeGlyph(
        key.glyphId, key.pixelSize, key.rasterScaleKey);
    REQUIRE(bitmap.width > 0);
    REQUIRE(bitmap.height > 0);
    const int exact = std::max(bitmap.width, bitmap.height) + kPagePadding * 2;
    REQUIRE(exact > 17);

    {
        GlyphAtlasFixture fits = GlyphAtlasFixture::WithPageSize(exact);
        fits.cache.BeginFrame(1);
        const auto handle = fits.cache.GetGlyph(key, fits.FaceForKey(key),
                                                fits.sink);
        CHECK_FALSE(handle.proceduralTofu);
        CHECK(handle.glyph.drawable);
        CHECK(fits.cache.ResidentPageCount() == 1U);
        CHECK_FALSE(HasDiagnostic(fits.sink, TextDiagnosticCode::AtlasExhausted));
        fits.cache.EndCollection(1);
    }
    {
        GlyphAtlasFixture tight = GlyphAtlasFixture::WithPageSize(exact - 1);
        tight.cache.BeginFrame(1);
        const auto handle = tight.cache.GetGlyph(key, tight.FaceForKey(key),
                                                 tight.sink);
        CHECK(handle.proceduralTofu);
        CHECK(handle.pageIdentity == 0U);
        // 들어가지 못할 것을 알고 거절했으므로 page를 만들지 않았다.
        CHECK(tight.cache.ResidentPageCount() == 0U);
        CHECK(tight.cache.Telemetry().residentBytes == 0U);
        CHECK(HasDiagnostic(tight.sink, TextDiagnosticCode::AtlasExhausted));
        tight.cache.EndCollection(1);
    }
}

// 위 변이 집합의 정수 섭동은 전부 낮은 바이트만 흔든다. 그래서 상위 56비트를
// 통째로 버리는 해시도, 문자열 길이를 접지 않아 경계가 사라진 해시도 그 집합
// 만으로는 구분되지 않는다.
TEST_CASE("the atlas key hash folds every byte of every field") {
    GlyphAtlasFixture f;
    const molga::GlyphAtlasKey key = f.KeyForGlyph(0x11223344U);
    const std::size_t base = molga::GlyphAtlasKeyHash{}(key);

    molga::GlyphAtlasKey glyphId = key;
    glyphId.glyphId = 0x22223344U; // 낮은 세 바이트가 같다
    CHECK_FALSE(glyphId == key);
    CHECK(molga::GlyphAtlasKeyHash{}(glyphId) != base);

    molga::GlyphAtlasKey variation = key;
    variation.variationKey = key.variationKey + 0x0100000000000000ULL;
    CHECK_FALSE(variation == key);
    CHECK(molga::GlyphAtlasKeyHash{}(variation) != base);

    molga::GlyphAtlasKey faceIndex = key;
    faceIndex.faceIndex = key.faceIndex + 0x01000000U;
    CHECK_FALSE(faceIndex == key);
    CHECK(molga::GlyphAtlasKeyHash{}(faceIndex) != base);

    molga::GlyphAtlasKey pixelSize = key;
    pixelSize.pixelSize = static_cast<std::uint16_t>(key.pixelSize + 0x0100U);
    CHECK_FALSE(pixelSize == key);
    CHECK(molga::GlyphAtlasKeyHash{}(pixelSize) != base);

    molga::GlyphAtlasKey rasterScale = key;
    rasterScale.rasterScaleKey =
        static_cast<std::uint16_t>(key.rasterScaleKey + 0x0100U);
    CHECK_FALSE(rasterScale == key);
    CHECK(molga::GlyphAtlasKeyHash{}(rasterScale) != base);

    // 두 문자열의 경계. 길이를 먼저 접지 않으면 ("ab","c")와 ("a","bc")가 같은
    // 값이 되고, GUID 하나가 개정판 문자열의 첫 글자를 삼켜도 같은 항목이 된다.
    molga::GlyphAtlasKey left = key;
    left.fontGuid = "ab";
    left.fontRevision = "c";
    molga::GlyphAtlasKey right = key;
    right.fontGuid = "a";
    right.fontRevision = "bc";
    CHECK_FALSE(left == right);
    CHECK(molga::GlyphAtlasKeyHash{}(left) != molga::GlyphAtlasKeyHash{}(right));
}

// 상시 금지된 "항목마다 하나씩 나가는 진단"을 막는 상한. 권한 있는 기록은
// 진단이 아니라 돌려주는 handle의 proceduralTofu이고 그쪽에는 상한이 없다.
TEST_CASE("atlas exhaustion diagnostics are bounded per collection") {
    // 아래 단언은 상수에서 기댓값을 유도하므로 상한이 1,000,000이어도 스스로
    // 맞춰 통과한다. 그런 상한은 이름만 상한이다. 크기 자체를 이웃
    // (kMaxFamilyDiagnosticsPerResolve, kMaxShapingDiagnosticsPerItem = 8)과
    // 같은 자릿수로 못 박아 둔다.
    static_assert(molga::kMaxAtlasDiagnosticsPerCollection >= 1U &&
                      molga::kMaxAtlasDiagnosticsPerCollection <= 32U,
                  "the atlas diagnostic cap must stay a small constant");

    GlyphAtlasFixture f(0);
    constexpr std::size_t kRequests =
        molga::kMaxAtlasDiagnosticsPerCollection * 4U;

    f.cache.BeginFrame(1);
    std::size_t tofu = 0U;
    for (std::size_t ordinal = 1U; ordinal <= kRequests; ++ordinal) {
        if (f.ResolveGlyph(ordinal).proceduralTofu) ++tofu;
    }
    f.cache.EndCollection(1);
    CHECK(tofu == kRequests);
    CHECK(CountDiagnostics(f.sink, TextDiagnosticCode::AtlasExhausted) ==
          molga::kMaxAtlasDiagnosticsPerCollection);

    // 상한은 수집마다 새로 열린다: 한 프레임의 포화가 다음 프레임의 진단을
    // 영구히 침묵시키지는 않는다.
    f.cache.BeginFrame(2);
    CHECK(f.ResolveGlyph(1).proceduralTofu);
    f.cache.EndCollection(2);
    CHECK(CountDiagnostics(f.sink, TextDiagnosticCode::AtlasExhausted) ==
          molga::kMaxAtlasDiagnosticsPerCollection + 1U);
}

// ── FontFace::RasterizeGlyph ────────────────────────────────────────────────
TEST_CASE("glyph rasterization is addressed by glyph id, not by codepoint") {
    GlyphAtlasFixture f;
    molga::FontFace& face = f.FaceForKey(f.KeyForGlyph(1));

    const std::uint32_t glyphForA = face.GlyphId(U'A');
    REQUIRE(glyphForA != 0U);
    // 이 픽스처가 무의미해지는 유일한 경우를 못 박는다: glyph ID가 codepoint와
    // 우연히 같으면 키를 codepoint로 되돌려도 아무 단언이 움직이지 않는다.
    REQUIRE(glyphForA != static_cast<std::uint32_t>(U'A'));

    const molga::FontGlyphBitmap byGlyphId =
        face.RasterizeGlyph(glyphForA, 23U, 64U);
    REQUIRE(byGlyphId.width > 0);
    REQUIRE(byGlyphId.height > 0);
    REQUIRE_FALSE(byGlyphId.coverage.empty());

    // 같은 그림이 legacy codepoint 경로에서도 나온다: glyph ID 경로는 cmap을
    // 건너뛰었을 뿐 다른 래스터라이저가 아니다.
    const molga::FontGlyphBitmap legacy = face.Rasterize(U'A', 23.0f);
    CHECK(byGlyphId.width == legacy.width);
    CHECK(byGlyphId.height == legacy.height);
    CHECK(byGlyphId.xOffset == legacy.xOffset);
    CHECK(byGlyphId.yOffset == legacy.yOffset);
    CHECK(byGlyphId.coverage == legacy.coverage);

    // 그리고 codepoint 숫자를 glyph ID로 쓴 것과는 다른 그림이다.
    const molga::FontGlyphBitmap byCodepointNumber =
        face.RasterizeGlyph(static_cast<std::uint32_t>(U'A'), 23U, 64U);
    CHECK(byCodepointNumber.coverage != byGlyphId.coverage);

    // 논리 advance는 HarfBuzz의 몫이다. 이 함수는 비트맵 경계/bearing/coverage만
    // 돌려준다.
    CHECK(byGlyphId.xAdvance == 0.0f);
    CHECK(legacy.xAdvance > 0.0f);
}

TEST_CASE("the raster scale key scales the rasterized image") {
    GlyphAtlasFixture f;
    molga::FontFace& face = f.FaceForKey(f.KeyForGlyph(1));
    const molga::FontGlyphBitmap unit = face.RasterizeGlyph(42U, 23U, 64U);
    const molga::FontGlyphBitmap doubled = face.RasterizeGlyph(42U, 23U, 128U);
    REQUIRE(unit.width > 0);
    CHECK(doubled.width > unit.width);
    CHECK(doubled.height > unit.height);
    CHECK(doubled.coverage.size() > unit.coverage.size());
}

TEST_CASE("the face records the glyph id it was asked to rasterize") {
    GlyphAtlasFixture f;
    molga::FontFace& face = f.FaceForKey(f.KeyForGlyph(7));
    // 0은 "아직 아무것도 묻지 않았다"이다.
    CHECK(face.LastRasterizedGlyphId() == 0U);
    face.RasterizeGlyph(42U, 23U, 64U);
    CHECK(face.LastRasterizedGlyphId() == 42U);

    // 범위 밖 glyph ID는 빈 비트맵으로 닫히지만, 물어본 사실은 남는다.
    const molga::FontGlyphBitmap missing =
        face.RasterizeGlyph(4000000U, 23U, 64U);
    CHECK(missing.width == 0);
    CHECK(missing.height == 0);
    CHECK(missing.coverage.empty());
    CHECK(face.LastRasterizedGlyphId() == 4000000U);

    // 적재되지 않은 face에 물어도 물었다는 사실은 남는다. 세고 싶은 것은 "몇 번
    // 답을 얻었는가"가 아니라 "누가 무엇을 물었는가"이고, 성공한 요청만 남기면
    // "엉뚱한 face에 물어서 빈 비트맵이 나왔다"가 흔적 없이 사라진다.
    molga::FontFace unloaded;
    CHECK(unloaded.LastRasterizedGlyphId() == 0U);
    const molga::FontGlyphBitmap nothing = unloaded.RasterizeGlyph(77U, 23U, 64U);
    CHECK(nothing.width == 0);
    CHECK(nothing.coverage.empty());
    CHECK(unloaded.LastRasterizedGlyphId() == 77U);
}
