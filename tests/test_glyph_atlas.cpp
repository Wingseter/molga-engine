#include "Assets/FontArtifactStore.h"
#include "Core/AssetDatabase.h"
#include "FontCollectionTestSupport.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/FontFace.h"
#include "Rendering/TextRenderer.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextShapingService.h"
#include "Text/UnicodeAnalysis.h"
#include "Text/UnicodeTextBuffer.h"
#include "TextQualificationAssetTree.h"
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
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
        return ResolveGlyphInto(cache, ordinal);
    }

    // 같은 키/face로 다른 캐시를 채운다. Task 6.3의 TextRenderer는 자기 캐시를
    // 소유하므로, 이 픽스처가 가진 face 목록을 그쪽으로도 흘려보낼 수 있어야
    // 한다.
    molga::GlyphHandle ResolveGlyphInto(molga::GlyphAtlasCache& target,
                                        std::size_t ordinal) {
        const molga::GlyphAtlasKey key = KeyForGlyph(DrawableGlyphId(ordinal));
        return target.GetGlyph(key, FaceForKey(key), sink);
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

// ── Task 6.3 Step 1a: the renderer-owned collection scope ───────────────────
// renderer가 소유한 바로 그 캐시에, EndCollection 호출 계수기를 덧댄 관찰자.
//
// 계수기가 필요한 이유는 GlyphAtlasCache::EndCollection이 멱등이기 때문이다:
// 두 번 부른 캐시와 한 번 부른 캐시는 상태가 같으므로, "정확히 한 번"이라는
// scope의 계약은 캐시 상태로는 관찰되지 않는다. 그런데 그 차이가 곧 위험이다
// — 옮겨진(moved-from) scope가 소유권을 놓지 않으면 두 번째 EndCollection이
// *다음* 수집의 pin을 지워, 그 프레임이 이미 가리키는 page가 축출 가능해진다.
//
// 이 관찰자만으로는 "hook은 부르고 atlas는 안 부르는" 구현을 잡지 못하므로,
// 아래에 atlas 쪽 증인(수집 종료가 page를 봉인한다)이 따로 있다.
class ObservedGlyphAtlas {
public:
    explicit ObservedGlyphAtlas(molga::GlyphAtlasCache& cache) : cache_(&cache) {
        REQUIRE(instance_ == nullptr);
        instance_ = this;
        molga::detail::SetGlyphCollectionEndHookForTest(&Record);
    }
    ~ObservedGlyphAtlas() {
        molga::detail::SetGlyphCollectionEndHookForTest(nullptr);
        instance_ = nullptr;
    }
    ObservedGlyphAtlas(const ObservedGlyphAtlas&) = delete;
    ObservedGlyphAtlas& operator=(const ObservedGlyphAtlas&) = delete;

    molga::GlyphAtlasCache& Cache() const noexcept { return *cache_; }

    std::size_t EndCollectionCount(std::uint64_t frameIndex) const {
        return static_cast<std::size_t>(
            std::count(ends_.begin(), ends_.end(), frameIndex));
    }
    std::size_t TotalEndCollectionCount() const noexcept { return ends_.size(); }

private:
    static void Record(std::uint64_t frameIndex) {
        // hook은 ~GlyphCollectionScope 안에서 불린다. 여기서 REQUIRE를 쓰면
        // 던지는 것이 암묵적 noexcept 소멸자를 통과해 std::terminate가 되어,
        // 이름 있는 실패 대신 출력 없는 죽음이 된다. 이 관찰자는 등록을 자기
        // 소멸자에서 되돌리므로 널일 수 없고, 세는 일은 본문의 단언이 한다.
        if (instance_ == nullptr) return;
        instance_->ends_.push_back(frameIndex);
    }

    molga::GlyphAtlasCache* cache_ = nullptr;
    std::vector<std::uint64_t> ends_;
    static ObservedGlyphAtlas* instance_;
};

ObservedGlyphAtlas* ObservedGlyphAtlas::instance_ = nullptr;

// ── Task 6.3 Step 9: 교체된 폰트와 옛 자원 ──────────────────────────────────
// 자격 트리 사본 하나를 들고, 그 안의 라틴 폰트 원본만 다른 검증된 폰트
// 바이트로 바꾼다. 커밋된 트리는 건드리지 않고, 교체는 진짜 import 발행
// 경로를 탄다 — 새 artifact SHA가 어디에서 오는지가 이 케이스의 절반이다.
class ReplaceableFontCorpus {
public:
    ReplaceableFontCorpus()
        : store_(std::make_shared<const molga::FontArtifactStore>(
              molga::FontArtifactStore::ForProject(tree_.ProjectRoot()))),
          repository_(database_),
          resolver_(database_, repository_) {
        std::string bindError;
        REQUIRE_MESSAGE(database_.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database_.ScanProject(tree_.AssetsRoot());
        REQUIRE(database_.Find(std::string(kFixtureFontGuid)) != nullptr);
    }

    void ReplaceLatinFontWithArabicBytes() {
        const fs::path source =
            tree_.AssetsRoot() / "fonts" / "NotoSans-Regular.ttf";
        const fs::path replacement =
            tree_.AssetsRoot() / "fonts" / "NotoSansArabic-Regular.ttf";
        fs::copy_file(replacement, source,
                      fs::copy_options::overwrite_existing);
        REQUIRE(database_.TryReimport(std::string(kFixtureFontGuid)));
        repository_.Invalidate(std::string(kFixtureFontGuid));
    }

    std::string PublishedArtifactSha() const {
        const molga::AssetRecord* record =
            database_.Find(std::string(kFixtureFontGuid));
        REQUIRE(record != nullptr);
        REQUIRE(record->fontArtifact.has_value());
        return record->fontArtifact->artifactSha256;
    }

    // 그 GUID 하나만 후보로 두고 셰이핑한다. family fallback을 태우면 라틴
    // 폰트가 교체된 뒤 다른 face가 대신 그려 버려서, "옛 자원"이 관찰되지
    // 않는다.
    std::vector<molga::text::ShapedGlyph> ShapeWithLatinFont(
        const std::string& utf8, molga::text::VectorTextDiagnosticSink& sink) {
        auto buffer = molga::text::UnicodeTextBuffer::Build(utf8, sink);
        REQUIRE(buffer);
        auto analysis = molga::text::UnicodeTextAnalyzer::Analyze(
            *buffer, {"und", molga::text::BaseDirection::Auto}, sink);
        REQUIRE(analysis);
        auto family = resolver_.BuildLegacySingleFace(
            std::string(kFixtureFontGuid),
            {400, 100, molga::FontSlant::Upright}, sink);
        REQUIRE(family);
        molga::text::ShapeStyle style;
        molga::text::TextShapingService service;
        std::vector<molga::text::ShapedGlyph> glyphs;
        for (const molga::text::AnalysisItem& item : analysis->Items()) {
            const bool first = item.sourceBytes.begin == 0U;
            const bool last = item.sourceBytes.end == utf8.size();
            auto shaped = service.ShapeAnalysisItem(
                *buffer, *analysis, item, *family, style, {first, last}, sink);
            REQUIRE(shaped);
            for (molga::text::ShapedRun& run : *shaped) {
                for (molga::text::ShapedGlyph& glyph : run.glyphs) {
                    glyphs.push_back(std::move(glyph));
                }
            }
        }
        return glyphs;
    }

private:
    QualificationAssetTreeFixture tree_;
    molga::AssetDatabase database_;
    std::shared_ptr<const molga::FontArtifactStore> store_;
    molga::text::FontRepository repository_;
    molga::text::FontFamilyResolver resolver_;
};

struct TextRendererFixture {
    // glyphs는 face/glyph-ID 공급원으로만 쓴다. 자기 캐시도 함께 들고 있지만
    // 이 픽스처의 단언은 전부 renderer가 소유한 캐시를 향한다.
    TextRendererFixture()
        : atlas(renderer.GlyphAtlas()) {
        // Step 6a: 두 접근자가 같은 하나를 돌려준다. 여기서 못 박지 않으면
        // const 쪽이 두 번째 캐시를 만들어도 아무 단언도 움직이지 않는다.
        const TextRenderer& constRenderer = renderer;
        REQUIRE(&constRenderer.GlyphAtlas() == &renderer.GlyphAtlas());
        REQUIRE(&atlas.Cache() == &renderer.GlyphAtlas());
    }

    // 한 page에 cell 하나, 예산은 page 두 개. GlyphAtlasFixture::OneGlyphPerPage
    // 와 같은 이유다: 새 glyph 하나마다 새 page가 필요해야 pin 규칙이 glyph
    // 크기에 흔들리지 않는다.
    void UseOneGlyphPerPage(std::uint64_t pageBudget) {
        molga::detail::SetGlyphAtlasGlyphsPerPageForTest(renderer.GlyphAtlas(),
                                                         1U);
        renderer.GlyphAtlas().SetResidentBudget(
            pageBudget * renderer.GlyphAtlas().PageBytes());
    }

    molga::GlyphHandle Resolve(std::size_t ordinal) {
        return glyphs.ResolveGlyphInto(renderer.GlyphAtlas(), ordinal);
    }

    TextRenderer renderer;
    GlyphAtlasFixture glyphs;
    ObservedGlyphAtlas atlas;
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

TEST_CASE("peak resident bytes is a high-water mark, not the latest value") {
    // Step 10은 "high-water peak"을 요구하고 Milestone 11의 "resident bytes가
    // 64 MiB를 넘지 않았다"는 증거가 이 필드를 읽는다. 그런데 감사 단계의
    // mutation이 std::max(peak, resident)를 peak = resident로 바꿔도 2491개
    // assertion이 전부 통과했다. 기존 케이스들은 resident가 정점에 있는 순간에만
    // peak를 보기 때문에 두 식이 같은 값을 낸다.
    //
    // 구분하려면 resident가 한 번 내려간 뒤에 다시 올라가야 한다:
    // 두 page까지 채우고(peak=2), 해제해서 0으로 만든 뒤(단조 telemetry는 남는다),
    // page 하나만 다시 만든다(resident=1). max는 2를 유지하고 대입은 1로 덮어쓴다.
    GlyphAtlasFixture f(2 * GlyphAtlasFixture::PageBytes);
    f.cache.BeginFrame(1);
    f.FillWithDistinctGlyphs();
    f.cache.EndCollection(1);
    const std::uint64_t highWater = f.cache.Telemetry().peakResidentBytes;
    REQUIRE(highWater == 2U * GlyphAtlasFixture::PageBytes);

    REQUIRE(f.cache.ReleaseAfterGpuIdle());
    REQUIRE(f.cache.Telemetry().residentBytes == 0U);
    CHECK(f.cache.Telemetry().peakResidentBytes == highWater);

    // 해제 뒤 page 하나만 다시 만든다. resident는 정점보다 낮다.
    f.cache.BeginFrame(100);
    const auto resolved = f.ResolveGlyph(1);
    REQUIRE_FALSE(resolved.proceduralTofu);
    REQUIRE(f.cache.Telemetry().residentBytes == GlyphAtlasFixture::PageBytes);
    REQUIRE(f.cache.Telemetry().residentBytes < highWater);

    // 이 한 줄이 mutant를 죽인다: 대입이었다면 여기서 PageBytes가 된다.
    CHECK(f.cache.Telemetry().peakResidentBytes == highWater);
}

// ── Task 6.2: 제출된 page는 살아 있을 뿐 아니라 바뀌지도 않는다 ──────────────
// 같은 순서를 두 번 돈다. 유일한 차이는 프레임 1의 handle을 프레임 2 전에
// 놓아주는가 — 즉 그 프레임의 fence가 신호해 GpuRetirementQueue가 토큰을
// 반납했는가 — 이다. 놓아준 쪽이 같은 page의 남은 선반에 이어 쓰는 것이,
// 놓지 않은 쪽이 새 page로 가는 이유가 봉인임을 증명한다. 한쪽만 보면
// "원래 프레임마다 새 page를 연다"와 구별되지 않는다.
TEST_CASE("a page an in-flight frame still holds is not written again") {
    for (const bool retired : {true, false}) {
        CAPTURE(retired);
        GlyphAtlasFixture f;
        f.cache.BeginFrame(1);
        auto first = f.ResolveGlyph(1);
        REQUIRE(first.glyph.drawable);
        const std::uint64_t firstPage = first.pageIdentity;
        // 이 page에는 아직 자리가 넉넉하다: 같은 수집의 두 번째 glyph가 같은
        // page에 들어간다. 수집 안에서는 봉인이 없다 — 아직 제출되지 않았다.
        REQUIRE(f.ResolveGlyph(2).pageIdentity == firstPage);
        REQUIRE(f.cache.IsPageWritable(firstPage));
        f.cache.EndCollection(1);

        // 수집이 끝나는 순간 밖에 나가 있는 page는 봉인된다.
        CHECK_FALSE(f.cache.IsPageWritable(firstPage));
        if (retired) first.pageLifetime.reset();
        CHECK(f.cache.IsPageWritable(firstPage) == retired);

        f.cache.BeginFrame(2);
        const auto next = f.ResolveGlyph(3);
        REQUIRE_FALSE(next.proceduralTofu);
        CHECK((next.pageIdentity == firstPage) == retired);
        // 봉인은 축출이 아니다: 어느 쪽이든 page는 그대로 상주하고 hit을 낸다.
        CHECK(f.cache.IsPageResident(firstPage));
        f.cache.EndCollection(2);
    }
}

// 봉인이 한 번 서고 영원히 서 있으면 page는 다시는 채워지지 않는다. 위
// 케이스의 retired 갈래는 토큰이 만료된 "그 순간"만 보므로, 새 토큰이 발급된
// 뒤에도 봉인이 내려가 있는지는 여기서 본다.
TEST_CASE("a reused page is sealed again only by the next collection") {
    GlyphAtlasFixture f;
    f.cache.BeginFrame(1);
    auto first = f.ResolveGlyph(1);
    const std::uint64_t page = first.pageIdentity;
    f.cache.EndCollection(1);
    first.pageLifetime.reset();

    // 새 수집이 같은 page를 다시 열고 새 토큰을 발급한다. 그 수집이 끝나기
    // 전까지는 계속 쓸 수 있어야 한다.
    f.cache.BeginFrame(2);
    auto reused = f.ResolveGlyph(2);
    REQUIRE(reused.pageIdentity == page);
    CHECK(f.cache.IsPageWritable(page));
    REQUIRE(f.ResolveGlyph(3).pageIdentity == page);
    f.cache.EndCollection(2);
    CHECK_FALSE(f.cache.IsPageWritable(page));

    // 상주하지 않는 정체성은 쓸 수 있는 page가 아니다(0도 마찬가지다).
    CHECK_FALSE(f.cache.IsPageWritable(0U));
    CHECK_FALSE(f.cache.IsPageWritable(page + 1000U));
}

// ── Task 6.2: GlyphInfo는 수명 단위가 아니다 ─────────────────────────────────
// 이 하위 시스템의 수명 단위는 page 하나이고, 이름은 pageIdentity, 지분은
// pageLifetime이다. 세 handle 모양 전부에서 그 둘이 짝을 이루는지 못 박는다:
// 이름만 있는 handle은 반납해 줄 지분이 없고, 지분만 있는 handle은
// Renderer::RetainUntilFrameComplete이 0 정체성으로 거절한다.
TEST_CASE("every glyph handle pairs its page name with a page claim") {
    GlyphAtlasFixture f = GlyphAtlasFixture::OneGlyphPerPage();
    f.cache.BeginFrame(1);

    // 1) 그릴 수 있는 glyph: 이름과 지분이 둘 다 있다.
    const auto drawable = f.ResolveGlyph(1);
    REQUIRE(drawable.glyph.drawable);
    CHECK(drawable.pageIdentity != 0U);
    CHECK(drawable.pageLifetime != nullptr);

    // 2) 공백 glyph: 어떤 page에도 놓이지 않으므로 둘 다 없다.
    const molga::GlyphAtlasKey blankKey = f.KeyForGlyph(3);
    const auto blank = f.cache.GetGlyph(blankKey, f.FaceForKey(blankKey), f.sink);
    REQUIRE_FALSE(blank.proceduralTofu);
    REQUIRE_FALSE(blank.glyph.drawable);
    CHECK(blank.pageIdentity == 0U);
    CHECK(blank.pageLifetime == nullptr);
    CHECK(molga::RetainedTexture(blank) == nullptr);

    // 3) 포화 tofu: 한 page짜리 예산에서 두 번째 page는 거절된다.
    const auto tofu = f.ResolveGlyph(2);
    REQUIRE(tofu.proceduralTofu);
    CHECK(tofu.pageIdentity == 0U);
    CHECK(tofu.pageLifetime == nullptr);
    CHECK(molga::RetainedTexture(tofu) == nullptr);
    f.cache.EndCollection(1);

    // 헤드리스에는 GraphicsDevice가 없어 캐시가 Texture를 만들지 않으므로,
    // 위 handle들의 texture는 전부 널이다 — 그것만 보면 언제나 널을 돌려주는
    // 구현도 통과한다. 규칙 자체("지분이 없으면 돌려주지 않는다")를 재려면
    // 널이 아닌 포인터가 필요하므로 handle 하나를 손으로 만든다. 이 주소는
    // 값으로만 쓰이고 역참조되지 않는다.
    molga::GlyphHandle synthetic;
    Texture* const marker =
        reinterpret_cast<Texture*>(static_cast<std::uintptr_t>(0x1000U));
    synthetic.glyph.texture = marker;
    synthetic.pageIdentity = 7U;
    synthetic.pageLifetime = std::make_shared<int>(1);
    CHECK(molga::RetainedTexture(synthetic) == marker);
    synthetic.pageLifetime.reset();
    CHECK(molga::RetainedTexture(synthetic) == nullptr);
}

// ── Task 6.2: 거절된 배치는 봉인된 page의 선반을 건드리지 않는다 ────────────
// 봉인 검사는 TryPlace보다 앞에 와야 한다. TryPlace는 성공하면 선반 커서를
// 옮기므로, 뒤에 두면 거절된 요청마다 봉인된 page의 자리가 사라진다: 비행
// 중인 프레임이 바쁠수록, 정작 glyph 하나 받지 못한 page가 예산만 태운다.
//
// 두 순서 모두 "이번 프레임은 이 page를 쓰지 않는다"는 같은 결과를 내므로,
// 그 결과만 보는 픽스처는 둘을 구별하지 못한다. 구별되는 것은 봉인이 풀린
// 뒤 그 page가 몇 개를 더 받는가뿐이다. 여기서는 같은 순서를 두 번 돌면서
// 봉인 중의 거절만 넣고 빼고, 최종 수용 개수가 같은지를 본다.
TEST_CASE("a rejected placement leaves the sealed page's shelf untouched") {
    std::size_t accepted[2] = {0U, 0U};
    for (int pass = 0; pass < 2; ++pass) {
        const bool interfered = pass == 1;
        CAPTURE(interfered);
        // 한 page짜리 예산. 봉인된 page를 비켜 간 요청은 새 page를 열지 못해
        // tofu가 되므로, 관찰되는 것은 그 요청이 봉인된 page에 무엇을 했는가
        // 하나뿐이다.
        GlyphAtlasFixture f(GlyphAtlasFixture::PageBytes);
        f.cache.BeginFrame(1);
        auto first = f.ResolveGlyph(1);
        REQUIRE(first.glyph.drawable);
        const std::uint64_t page = first.pageIdentity;
        f.cache.EndCollection(1);
        REQUIRE_FALSE(f.cache.IsPageWritable(page));

        f.cache.BeginFrame(2);
        if (interfered) {
            for (std::size_t ordinal = 2U; ordinal <= 9U; ++ordinal) {
                REQUIRE(f.ResolveGlyph(ordinal).proceduralTofu);
            }
        }
        f.cache.EndCollection(2);
        REQUIRE(f.cache.ResidentPageCount() == 1U);

        // 봉인이 풀린다. 이제 남은 선반을 끝까지 쓴다.
        first.pageLifetime.reset();
        f.cache.BeginFrame(3);
        REQUIRE(f.cache.IsPageWritable(page));
        bool saturated = false;
        for (std::size_t ordinal = 2U; ordinal <= 120U; ++ordinal) {
            const molga::GlyphHandle handle = f.ResolveGlyph(ordinal);
            if (handle.proceduralTofu) {
                saturated = true;
                continue;
            }
            REQUIRE(handle.pageIdentity == page);
            ++accepted[pass];
        }
        f.cache.EndCollection(3);
        // 포화하지 않으면 잃어버린 자리가 개수에 드러나지 않는다.
        REQUIRE(saturated);
        REQUIRE(accepted[pass] > 0U);
    }
    CHECK(accepted[0] == accepted[1]);
}

// ── Task 6.3 Step 1a ────────────────────────────────────────────────────────

TEST_CASE("glyph collection scope is non-nestable and exception safe") {
    TextRendererFixture f;
    {
        auto scope = f.renderer.BeginGlyphCollection(41);
        CHECK_THROWS_AS(f.renderer.BeginGlyphCollection(41), std::logic_error);
    }
    CHECK(f.atlas.EndCollectionCount(41) == 1);
}

// 위 케이스는 "두 번 열리지 않는다"만 본다. 실패한 두 번째 열기가 *아무것도
// 바꾸지 않았는가*는 따로 재야 한다 — 거절 전에 atlas_.BeginFrame이 먼저
// 불리면 그 호출이 이미 열려 있던 수집의 pin 집합을 통째로 지워, 이미 큐에
// 들어간 명령이 가리키는 page가 그 자리에서 축출 대상이 된다.
TEST_CASE("a refused nested scope leaves the collection untouched") {
    TextRendererFixture f;
    f.UseOneGlyphPerPage(2U);
    {
        auto scope = f.renderer.BeginGlyphCollection(41);

        molga::GlyphHandle first = f.Resolve(1U);
        REQUIRE_FALSE(first.proceduralTofu);
        const std::uint64_t firstPage = first.pageIdentity;
        molga::GlyphHandle second = f.Resolve(2U);
        REQUIRE_FALSE(second.proceduralTofu);
        // 외부 지분을 놓는다. 이제 이 두 page를 지키는 것은 현재 수집의
        // pin뿐이다.
        first.pageLifetime.reset();
        second.pageLifetime.reset();

        CHECK_THROWS_AS(f.renderer.BeginGlyphCollection(77), std::logic_error);

        // 거절이 pin을 지웠다면 이 요청이 첫 page를 축출하고 성공한다.
        CHECK(f.Resolve(3U).proceduralTofu);
        CHECK(f.renderer.GlyphAtlas().IsPageResident(firstPage));
        // 거절된 열기는 프레임 번호도 바꾸지 않는다: 바꿨다면 아래 종료가
        // 41이 아니라 77로 기록된다.
    }
    CHECK(f.atlas.EndCollectionCount(41) == 1);
    CHECK(f.atlas.EndCollectionCount(77) == 0);

    // 성공 증인. 거절이 상태를 태워 버렸다면 이 두 번째 수집은 열리지 못한다.
    {
        auto scope = f.renderer.BeginGlyphCollection(42);
    }
    CHECK(f.atlas.EndCollectionCount(42) == 1);
    CHECK(f.atlas.TotalEndCollectionCount() == 2U);
}

TEST_CASE("a scope unwound by an exception still ends its collection once") {
    TextRendererFixture f;
    bool threw = false;
    try {
        auto scope = f.renderer.BeginGlyphCollection(58);
        throw std::runtime_error("collection interrupted");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
    CHECK(f.atlas.EndCollectionCount(58) == 1);

    // 그리고 그 프레임의 수집이 정말 닫혔다: 닫히지 않았다면 다음 열기가
    // 중첩으로 거절된다.
    {
        auto scope = f.renderer.BeginGlyphCollection(59);
    }
    CHECK(f.atlas.EndCollectionCount(59) == 1);
}

// 옮겨진 scope는 소유권을 놓는다. 놓지 않으면 EndCollection이 두 번 불리고,
// 그 두 번째가 *다음* 수집의 pin을 지운다.
TEST_CASE("moving a collection scope moves the one end-of-collection call") {
    TextRendererFixture f;
    {
        auto scope = f.renderer.BeginGlyphCollection(64);
        auto moved = std::move(scope);
        // 옮겨진 원본이 아직 소유하고 있다면 여기서 이미 한 번 닫혔을 것이고,
        // 그러면 아래 열기가 중첩이 아니게 된다.
        CHECK_THROWS_AS(f.renderer.BeginGlyphCollection(64), std::logic_error);
        CHECK(f.atlas.TotalEndCollectionCount() == 0U);
    }
    CHECK(f.atlas.EndCollectionCount(64) == 1);
    CHECK(f.atlas.TotalEndCollectionCount() == 1U);
}

// hook 계수기만으로는 "hook은 부르고 atlas는 부르지 않는" 구현을 잡지 못한다.
// 여기서는 renderer가 소유한 캐시 자신이 증인이다: 수집이 열려 있는 동안
// page는 쓸 수 있고, 수집이 닫히는 순간 밖으로 나간 page는 봉인된다.
TEST_CASE("a collection scope drives the renderer's own atlas") {
    TextRendererFixture f;
    f.UseOneGlyphPerPage(2U);

    molga::GlyphHandle first;
    std::uint64_t firstPage = 0U;
    {
        auto scope = f.renderer.BeginGlyphCollection(88);
        first = f.Resolve(1U);
        REQUIRE_FALSE(first.proceduralTofu);
        firstPage = first.pageIdentity;
        REQUIRE(firstPage != 0U);
        REQUIRE(f.renderer.GlyphAtlas().IsPageResident(firstPage));
        // 수집 중에는 쓸 수 있다. 봉인 단언의 반대편이다.
        CHECK(f.renderer.GlyphAtlas().IsPageWritable(firstPage));

        // BeginFrame 증인. 현재 수집의 pin이 기록되지 않았다면 아래 두
        // 요청 중 하나가 이 page를 축출하고 성공한다.
        molga::GlyphHandle second = f.Resolve(2U);
        REQUIRE_FALSE(second.proceduralTofu);
        first.pageLifetime.reset();
        second.pageLifetime.reset();
        CHECK(f.Resolve(3U).proceduralTofu);
        CHECK(f.renderer.GlyphAtlas().IsPageResident(firstPage));
        // 다음 단언이 뜻을 가지려면 이 page가 밖으로 나간 상태여야 한다.
        first = f.Resolve(1U);
        REQUIRE(first.pageIdentity == firstPage);
        REQUIRE(first.pageLifetime);
    }
    // 수집이 닫혔다. 밖으로 나간 page는 이제 봉인되어 있다.
    CHECK_FALSE(f.renderer.GlyphAtlas().IsPageWritable(firstPage));
    CHECK(f.renderer.GlyphAtlas().IsPageResident(firstPage));
    CHECK(f.atlas.EndCollectionCount(88) == 1);
}

// EndCollectionCount가 읽는 것은 hook이 받은 번호, 즉 scope 자기 필드다. 그
// 번호가 실제로 GlyphAtlasCache에 그대로 전달되었는지는 그 계수기가 대답하지
// 못한다 — BeginFrame(0)이나 EndCollection(frameIndex_ + 1)로 바꿔도 모든
// 계수 단언이 그대로 통과한다. 캐시가 프레임 번호를 쓰는 곳은 포화 진단의
// "in frame N" 하나뿐이므로, 그 문장을 증인으로 세운다.
TEST_CASE("the collection scope hands its own frame index to the atlas") {
    TextRendererFixture f;
    f.UseOneGlyphPerPage(1U);

    molga::GlyphHandle held;
    {
        auto scope = f.renderer.BeginGlyphCollection(4242);
        held = f.Resolve(1U);
        REQUIRE_FALSE(held.proceduralTofu);
        REQUIRE(held.pageLifetime);

        // 예산이 page 하나뿐이고 그 하나는 현재 수집이 붙들고 있으므로 이
        // 요청은 거절되고 진단이 나온다. BeginFrame이 받은 번호가 그 문장에
        // 실린다.
        REQUIRE(f.Resolve(2U).proceduralTofu);
        REQUIRE(CountDiagnostics(f.glyphs.sink,
                                 TextDiagnosticCode::AtlasExhausted) == 1U);
        CHECK(f.glyphs.sink.Diagnostics().back().message.find(
                  "in frame 4242") != std::string::npos);
    }

    // 수집이 닫힌 뒤에도 그 프레임 번호는 그대로여야 한다. 외부 지분이 첫
    // page를 살려 두므로 이 요청도 거절되고, EndCollection이 받은 번호가
    // 그 문장에 실린다.
    REQUIRE(held.pageLifetime);
    REQUIRE(f.Resolve(3U).proceduralTofu);
    REQUIRE(CountDiagnostics(f.glyphs.sink,
                             TextDiagnosticCode::AtlasExhausted) == 2U);
    CHECK(f.glyphs.sink.Diagnostics().back().message.find("in frame 4242") !=
          std::string::npos);

    // 반대편. 다른 번호로 연 수집은 다른 번호를 싣는다 — 이것이 없으면
    // "언제나 4242"로 굳혀도 위 두 단언이 통과한다.
    held.pageLifetime.reset();
    {
        auto scope = f.renderer.BeginGlyphCollection(7U);
        REQUIRE_FALSE(f.Resolve(4U).proceduralTofu);
        REQUIRE(f.Resolve(5U).proceduralTofu);
        REQUIRE(CountDiagnostics(f.glyphs.sink,
                                 TextDiagnosticCode::AtlasExhausted) == 3U);
        const std::string& message = f.glyphs.sink.Diagnostics().back().message;
        CHECK(message.find("in frame 7") != std::string::npos);
        CHECK(message.find("in frame 4242") == std::string::npos);
    }
    CHECK(f.atlas.EndCollectionCount(4242) == 1);
    CHECK(f.atlas.EndCollectionCount(7) == 1);
}

// ── Task 6.3 Step 9 ─────────────────────────────────────────────────────────
// 프로덕션 소비자를 하나도 옮기지 않고 소유권만 시험한다. 셰이핑된 glyph가
// 붙들고 있는 face 자원은 그 glyph가 살아 있는 동안 자기 바이트를 유지하므로,
// 폰트가 교체된 뒤에 그 옛 glyph를 atlas에 물어도 래스터는 옛 face에서 나와야
// 한다. 새로 셰이핑한 glyph 쪽에는 교체된 SHA가 실린다.
TEST_CASE("a replaced font leaves an old shaped glyph rasterizing from its own face") {
    ReplaceableFontCorpus corpus;
    molga::text::VectorTextDiagnosticSink sink;

    const std::string oldArtifactSha = corpus.PublishedArtifactSha();
    const std::vector<molga::text::ShapedGlyph> before =
        corpus.ShapeWithLatinFont(u8"A", sink);
    REQUIRE(before.size() == 1U);
    const molga::text::ShapedGlyph oldGlyph = before.front();
    REQUIRE_FALSE(oldGlyph.missing);
    REQUIRE(oldGlyph.faceResource != nullptr);
    REQUIRE(oldGlyph.faceResource->rasterFace != nullptr);
    REQUIRE(oldGlyph.glyphId != 0U);
    REQUIRE(oldGlyph.faceResource->artifactSha256 == oldArtifactSha);

    corpus.ReplaceLatinFontWithArabicBytes();
    const std::string newArtifactSha = corpus.PublishedArtifactSha();
    REQUIRE(newArtifactSha != oldArtifactSha);

    // 새로 셰이핑한 glyph는 교체된 바이트를 쓴다. 라틴 face에는 없는 아랍
    // 문자가 그 증인이다.
    const std::vector<molga::text::ShapedGlyph> after =
        corpus.ShapeWithLatinFont(u8"س", sink);
    REQUIRE(after.size() == 1U);
    const molga::text::ShapedGlyph newGlyph = after.front();
    REQUIRE_FALSE(newGlyph.missing);
    REQUIRE(newGlyph.faceResource != nullptr);
    CHECK(newGlyph.faceResource->artifactSha256 == newArtifactSha);
    CHECK(newGlyph.fontRevision == newArtifactSha + ":0");
    CHECK(newGlyph.fontRevision != oldGlyph.fontRevision);
    // 옛 자원은 교체에 흔들리지 않는다.
    CHECK(oldGlyph.faceResource->artifactSha256 == oldArtifactSha);
    CHECK(oldGlyph.fontRevision == oldArtifactSha + ":0");

    molga::GlyphAtlasKey key;
    key.fontGuid = oldGlyph.fontGuid;
    key.fontRevision = oldGlyph.fontRevision;
    key.faceIndex = oldGlyph.faceIndex;
    key.pixelSize = 32U;
    key.rasterScaleKey = 64U;
    key.variationKey = 0U;
    key.renderMode = molga::GlyphRenderMode::Monochrome;
    key.glyphId = oldGlyph.glyphId;

    TextRenderer textRenderer;
    molga::GlyphHandle handle;
    {
        auto scope = textRenderer.BeginGlyphCollection(93);
        // 강제된 miss. 이 캐시는 방금 만들어졌으므로 이 키는 항목이 없고,
        // 아래 두 계수가 래스터가 실제로 일어났다는 것을 못 박는다 — 없으면
        // 캐시 hit 하나로도 이 케이스가 통과한다.
        const molga::GlyphAtlasTelemetry beforeLookup =
            textRenderer.GlyphAtlas().Telemetry();
        handle = textRenderer.GlyphAtlas().GetGlyph(
            key, *oldGlyph.faceResource->rasterFace, sink);
        CHECK(textRenderer.GlyphAtlas().Telemetry().misses ==
              beforeLookup.misses + 1U);
        CHECK(textRenderer.GlyphAtlas().Telemetry().uploads ==
              beforeLookup.uploads + 1U);
        CHECK(textRenderer.GlyphAtlas().LastUploadedKeyForTest() == key);
    }
    // scope는 단언이 끝나기 전에 닫힌다. 닫힌 수집이 내보낸 page는 봉인되고,
    // 그동안에도 handle이 든 지분은 그대로다.
    CHECK_FALSE(handle.proceduralTofu);
    CHECK(handle.pageIdentity != 0U);
    CHECK(handle.pageLifetime);
    CHECK(handle.glyph.glyphId == oldGlyph.glyphId);
    CHECK_FALSE(textRenderer.GlyphAtlas().IsPageWritable(handle.pageIdentity));

    // 옛 face가 그 glyph ID를 받았다. 새 face는 이 조회에 관여하지 않는다.
    CHECK(oldGlyph.faceResource->rasterFace->LastRasterizedGlyphId() ==
          oldGlyph.glyphId);
    CHECK(newGlyph.faceResource->rasterFace->LastRasterizedGlyphId() == 0U);
    // 그리고 그 두 face는 정말 다른 객체다: 같은 하나였다면 위 두 줄이
    // 서로를 반증하지 못한다.
    CHECK(oldGlyph.faceResource->rasterFace !=
          newGlyph.faceResource->rasterFace);
}
