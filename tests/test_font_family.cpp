#include "Assets/FontArtifactStore.h"
#include "Assets/FontAsset.h"
#include "Assets/FontFamilyAsset.h"
#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "FontCollectionTestSupport.h"
#include "Text/FontFamilyResolver.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "TextQualificationAssetTree.h"
#include "doctest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using molga::text::ResolvedFace;
using molga::text::ResolvedFamily;
using molga::text::TextDiagnosticCode;

namespace {

// ── Fixture-tree GUIDs ──────────────────────────────────────────────────────
// 커밋된 자격 트리의 고정 GUID. 이름이 아니라 이 상수들이 계약이다.
constexpr const char* kPrimaryFamily = "11111111111111111111111111111111";
constexpr const char* kArabicFamily = "22222222222222222222222222222222";
constexpr const char* kCjkFamily = "33333333333333333333333333333333";
constexpr const char* kLatinFont = "44444444444444444444444444444444";
constexpr const char* kHebrewFont = "55555555555555555555555555555555";
constexpr const char* kArabicFont = "66666666666666666666666666666666";
constexpr const char* kKoreanFont = "77777777777777777777777777777777";
constexpr const char* kNotoLicense = "88888888888888888888888888888888";
constexpr const char* kCjkLicense = "99999999999999999999999999999999";
constexpr const char* kDevanagariFont = "12121212121212121212121212121212";
constexpr const char* kThaiFont = "13131313131313131313131313131313";

// ── Observation helpers ─────────────────────────────────────────────────────

std::vector<std::string> FontGuids(const ResolvedFamily& resolved) {
    std::vector<std::string> guids;
    guids.reserve(resolved.candidates.size());
    for (const ResolvedFace& face : resolved.candidates) {
        guids.push_back(face.fontGuid);
    }
    return guids;
}

std::vector<std::string> VisitedFamilyGuids(const ResolvedFamily& resolved) {
    std::vector<std::string> guids;
    guids.reserve(resolved.depthFirstFamilyNodes.size());
    for (const molga::text::ResolvedFamilyNode& node :
         resolved.depthFirstFamilyNodes) {
        guids.push_back(node.familyGuid);
    }
    return guids;
}

bool EachFamilyVisitedOnce(const ResolvedFamily& resolved) {
    std::set<std::string> seen;
    for (const molga::text::ResolvedFamilyNode& node :
         resolved.depthFirstFamilyNodes) {
        if (!seen.insert(node.familyGuid).second) return false;
    }
    return true;
}

std::size_t CountDiagnostics(const molga::text::VectorTextDiagnosticSink& sink,
                             TextDiagnosticCode code) {
    std::size_t count = 0;
    for (const molga::text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == code) ++count;
    }
    return count;
}

bool HasDiagnostic(const molga::text::VectorTextDiagnosticSink& sink,
                   TextDiagnosticCode code) {
    return CountDiagnostics(sink, code) > 0;
}

// 진단의 code만 보는 술어는 나머지 필드를 전부 놓친다. assetGuid는 특히
// 장식이 아니다: LoggerTextDiagnosticSink가 code+assetGuid로 rate limit을
// 접으므로, 비어 있으면 서로 다른 깨진 참조가 한 통에 합쳐진다.
std::vector<std::string> DiagnosticAssetGuids(
    const molga::text::VectorTextDiagnosticSink& sink,
    TextDiagnosticCode code) {
    std::vector<std::string> guids;
    for (const molga::text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == code) guids.push_back(diagnostic.assetGuid);
    }
    return guids;
}

std::vector<std::string> DiagnosticComponentTypes(
    const molga::text::VectorTextDiagnosticSink& sink,
    TextDiagnosticCode code) {
    std::vector<std::string> types;
    for (const molga::text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        if (diagnostic.code == code) types.push_back(diagnostic.componentType);
    }
    return types;
}

const molga::text::ResolvedFamilyNode* FindNode(const ResolvedFamily& resolved,
                                                const std::string& guid) {
    for (const molga::text::ResolvedFamilyNode& node :
         resolved.depthFirstFamilyNodes) {
        if (node.familyGuid == guid) return &node;
    }
    return nullptr;
}

// nullopt를 역참조하지 않고 후보를 읽는 유일한 통로. `resolved->candidates`를
// 직접 쓰면 회귀가 성공을 nullopt로 바꿨을 때 단언이 실패하는 대신 UB가 된다.
//
// rvalue 오버로드를 지운다. 임시 optional을 넘기면 돌려준 참조가 그 full
// expression 끝에서 곧바로 매달리고, 해제된 메모리가 우연히 0개 후보로 읽히면
// 테스트는 멀쩡한 코드를 고발한다 — 이 파일에서 실제로 한 번 그렇게 됐다.
// 이제 그 실수는 컴파일되지 않는다.
const ResolvedFamily& Engaged(const std::optional<ResolvedFamily>& resolved) {
    REQUIRE(resolved.has_value());
    return *resolved;
}
const ResolvedFamily& Engaged(std::optional<ResolvedFamily>&&) = delete;

std::shared_ptr<const molga::FontArtifactStore> ProjectStore(
    const fs::path& projectRoot) {
    return std::make_shared<const molga::FontArtifactStore>(
        molga::FontArtifactStore::ForProject(projectRoot));
}

// doctest는 DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING 없이 빌드되므로 const
// char*를 문자열이 아니라 포인터 주소로 찍는다. 실패 메시지가 어느 GUID를
// 가리키는지 읽히게 하려면 std::string으로 감싸야 한다.
std::string Label(const char* text) { return std::string(text); }

void RequireRecord(const molga::AssetDatabase& database, const char* guid,
                   const char* importer, int importerVersion) {
    const molga::AssetRecord* record = database.Find(std::string(guid));
    REQUIRE_MESSAGE(record != nullptr, Label(guid));
    CHECK_MESSAGE(record->importer == std::string(importer), Label(guid));
    CHECK_MESSAGE(record->importerVersion == importerVersion, Label(guid));
}

// ── Step 1b: the committed qualification tree, with nothing generated ───────
// 이 픽스처는 커밋된 트리를 그대로 복사해 쓴다. 폰트/라이선스/family sidecar를
// 쓰거나 고쳐 쓰지 않고, 한 root를 다른 root에서 유도하지도 않는다. 생성된
// metadata가 하나라도 끼면 "저작된 순서가 곧 계약"이라는 이 마일스톤의 전제를
// 테스트가 스스로 만들어 낸 값으로 증명하게 된다.
class FontFamilyFixture {
private:
    QualificationAssetTreeFixture tree_;

public:
    molga::AssetDatabase database;
    molga::text::VectorTextDiagnosticSink sink;

private:
    std::shared_ptr<const molga::FontArtifactStore> store_;
    // 선언 순서가 곧 생성 순서다. 이 멤버가 resolver보다 앞에 있어야 Step 1c의
    // "resolver를 만들기 전에 sidecar 권한을 증명한다"가 컴파일러가 보장하는
    // 순서가 된다. 생성자 본문에 두면 이미 resolver가 database를 잡은 뒤다.
    bool sidecarAuthorityProved_;
    molga::text::FontRepository repository_;

public:
    molga::text::FontFamilyResolver resolver;

    FontFamilyFixture()
        : store_(ProjectStore(tree_.ProjectRoot())),
          sidecarAuthorityProved_(BindScanAndProveSidecarAuthority()),
          repository_(database),
          resolver(database, repository_) {}

    const fs::path& ProjectRoot() const { return tree_.ProjectRoot(); }
    const fs::path& AssetsRoot() const { return tree_.AssetsRoot(); }

private:
    bool BindScanAndProveSidecarAuthority() {
        std::string bindError;
        REQUIRE_MESSAGE(database.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database.ScanProject(tree_.AssetsRoot());
        // Step 1c: 이미 바인딩된 프로젝트 store를 통해 한 번 더 스캔한다.
        // 재스캔이 정체성을 흔들면 아래 단언이 먼저 무너진다.
        database.ScanProject(tree_.AssetsRoot());
        RequireRecord(database, kPrimaryFamily, "FontFamilyImporter", 1);
        RequireRecord(database, kDevanagariFont, "FontImporter", 2);
        RequireRecord(database, kThaiFont, "FontImporter", 2);
        // Step 1d: 두 라이선스 record가 모두 존재한다.
        RequireRecord(database, kNotoLicense, "GenericImporter", 1);
        RequireRecord(database, kCjkLicense, "GenericImporter", 1);
        return true;
    }
};

// ── Authored graph shapes the committed tree cannot express ─────────────────
// 커밋된 다섯 family는 정렬 키의 한 점(전부 400/100/Upright)과 사슬 cycle만
// 담고 있다. 스타일 순위, 마름모, 범위 밖 face, 세대 무효화는 저작된 그래프가
// 달라야 관찰되므로, 아래 픽스처만 자격 트리 사본 위에 family를 더 쓴다.
// FontFamilyFixture는 그대로 아무것도 쓰지 않는다.

void WriteJson(const fs::path& path, const nlohmann::json& document) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), path.string());
    output << document.dump(2);
    output.close();
    REQUIRE_MESSAGE(output.good(), path.string());
}

nlohmann::json ReadJson(const fs::path& path) {
    std::ifstream input(path);
    REQUIRE_MESSAGE(input.good(), path.string());
    nlohmann::json document;
    input >> document;
    return document;
}

nlohmann::json FaceEntry(const char* fontGuid, unsigned faceIndex,
                         unsigned weight, unsigned stretchPercent,
                         const char* slant) {
    return nlohmann::json{{"fontGuid", fontGuid},
                          {"faceIndex", faceIndex},
                          {"weight", weight},
                          {"stretchPercent", stretchPercent},
                          {"slant", slant}};
}

nlohmann::json FamilySource(const std::vector<nlohmann::json>& faces,
                            const std::vector<std::string>& fallbacks) {
    nlohmann::json source;
    source["schemaVersion"] = 1;
    source["faces"] = faces;
    source["fallbackFamilyGuids"] = fallbacks;
    return source;
}

struct AuthoredFamily {
    std::string guid;
    std::string fileName;
    nlohmann::json source;
};

// 커밋된 단일 face 파일 하나를 감싼 두 face짜리 collection. `faceIndex`가 이
// record가 여는 face이고, 그 값이 0이 아닐 때에만 "카탈로그가 face index를
// 정한다"는 계약이 관찰된다. 합성은 FontCollectionTestSupport.h에 있다
// (셰이핑 스위트도 같은 이유로 같은 기법을 쓴다).
struct AuthoredCollectionFont {
    std::string guid;
    std::string fileName;
    std::string baseFontName;
    unsigned faceIndex = 0;
};

class AuthoredGraphFixture {
private:
    QualificationAssetTreeFixture tree_;

public:
    molga::AssetDatabase database;
    molga::text::VectorTextDiagnosticSink sink;

private:
    std::shared_ptr<const molga::FontArtifactStore> store_;
    // FontFamilyFixture와 같은 이유로 resolver보다 앞에 둔다: 저작 파일을 다
    // 쓰고 스캔이 끝난 뒤에만 resolver가 database를 잡는다.
    bool prepared_;
    molga::text::FontRepository repository_;

public:
    molga::text::FontFamilyResolver resolver;

    explicit AuthoredGraphFixture(
        const std::vector<AuthoredFamily>& authored,
        const std::vector<AuthoredCollectionFont>& collections = {})
        : store_(ProjectStore(tree_.ProjectRoot())),
          prepared_(WriteBindAndScan(authored, collections)),
          repository_(database),
          resolver(database, repository_) {}

    const fs::path& AssetsRoot() const { return tree_.AssetsRoot(); }

    // 저작 파일을 지우고 다시 스캔한다. 지워진 family는 record가 사라질 뿐
    // 세대는 움직이지 않으므로(ContentGeneration은 모르는 GUID에 0을 돌려준다),
    // 삭제를 관찰할 수 있는 유일한 자리가 node.exists다.
    void DeleteFamily(const char* familyGuid) {
        const fs::path source = database.AbsoluteSourcePath(familyGuid);
        REQUIRE_FALSE(source.empty());
        REQUIRE(fs::remove(molga::AssetMeta::MetaPathFor(source)));
        REQUIRE(fs::remove(source));
        database.ScanProject(tree_.AssetsRoot());
        REQUIRE(database.Find(std::string(familyGuid)) == nullptr);
    }

    // 바이트는 그대로 두고 저작 설정만 고친다. 산출물 content address는
    // 그대로이므로 fontRevision이 움직이면 안 되고, 발행 정체성은 달라지므로
    // 그 GUID의 세대는 올라가야 한다.
    void RewriteFontCopyright(const char* fontGuid, const std::string& text) {
        const fs::path source = database.AbsoluteSourcePath(fontGuid);
        REQUIRE_FALSE(source.empty());
        const fs::path metaPath = molga::AssetMeta::MetaPathFor(source);
        nlohmann::json meta = ReadJson(metaPath);
        REQUIRE_MESSAGE(meta.contains("settings"), metaPath.string());
        meta["settings"]["copyright"] = text;
        WriteJson(metaPath, meta);
        std::string error;
        REQUIRE_MESSAGE(database.TryReimport(fontGuid, &error), error);
    }

    void RewriteFamilySource(const char* familyGuid,
                             const nlohmann::json& source) {
        const fs::path path = database.AbsoluteSourcePath(familyGuid);
        REQUIRE_FALSE(path.empty());
        WriteJson(path, source);
        std::string error;
        REQUIRE_MESSAGE(database.TryReimport(familyGuid, &error), error);
    }

private:
    bool WriteBindAndScan(
        const std::vector<AuthoredFamily>& authored,
        const std::vector<AuthoredCollectionFont>& collections) {
        for (const AuthoredCollectionFont& font : collections) {
            test_support::AuthorTwoFaceCollectionFont(
                tree_.AssetsRoot() / "fonts", font.fileName, font.baseFontName,
                font.guid, font.faceIndex);
        }
        for (const AuthoredFamily& family : authored) {
            const fs::path source =
                tree_.AssetsRoot() / "families" / family.fileName;
            // 커밋된 픽스처와 이름이 겹치면 그 GUID 계약을 덮어쓰게 된다.
            REQUIRE_MESSAGE(!fs::exists(source), source.string());
            WriteJson(source, family.source);
            WriteJson(molga::AssetMeta::MetaPathFor(source),
                      nlohmann::json{{"guid", family.guid},
                                     {"importer", "FontFamilyImporter"},
                                     {"importerVersion", 1},
                                     {"settings", nlohmann::json::object()}});
        }
        std::string bindError;
        REQUIRE_MESSAGE(database.BindFontArtifactStore(store_, &bindError),
                        bindError);
        database.ScanProject(tree_.AssetsRoot());
        for (const AuthoredCollectionFont& font : collections) {
            RequireRecord(database, font.guid.c_str(), "FontImporter", 2);
        }
        for (const AuthoredFamily& family : authored) {
            RequireRecord(database, family.guid.c_str(), "FontFamilyImporter",
                          1);
        }
        return true;
    }
};

// 정렬 케이스가 쓰는 저작 GUID. 커밋된 트리의 어느 GUID와도 겹치지 않는다.
constexpr const char* kStyledFamily = "cccccccccccccccccccccccccccccccc";
constexpr const char* kDiamondRoot = "dddddddddddddddddddddddddddddddd";
constexpr const char* kDiamondLeft = "d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1";
constexpr const char* kDiamondRight = "d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2d2";
constexpr const char* kDiamondShared = "d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3d3";
constexpr const char* kBrokenFamily = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
constexpr const char* kAbsentFamily = "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f";
constexpr const char* kAbsentFont = "00000000000000000000000000000000";
constexpr const char* kWideFamily = "efefefefefefefefefefefefefefefef";
// 저작된 fallback 순서가 GUID 오름차순이 아닌 유일한 그래프. 커밋된 다섯
// family의 fallback 목록은 전부 오름차순이라, 그 목록을 정렬해 버리는 회귀가
// 모든 입력에서 무연산이 된다.
constexpr const char* kDescendingFamily = "abababababababababababababababab";
constexpr const char* kDescHigh = "fafafafafafafafafafafafafafafafa";
constexpr const char* kDescLow = "0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a";
constexpr const char* kDeleteRoot = "1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b1b";
constexpr const char* kDeleteLeaf = "2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b";
constexpr const char* kCollectionFont = "1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a";

// Step 4가 고정한 키의 모든 성분이 관찰되도록 저작한다. 5555(Italic)와
// 1212(Oblique)는 stylePenalty를 빼면 완전히 동점이므로, 그 둘의 상대 순서가
// authoredFaceIndex를 fontGuid보다 앞에 두었다는 사실까지 증언한다
// ("12121212..." < "55555555..." 이므로 두 성분을 뒤바꾸면 순서가 달라진다).
std::vector<AuthoredFamily> StyledFamilyTree() {
    return {{kStyledFamily, "styled.fontfamily",
             FamilySource({FaceEntry(kLatinFont, 0, 400, 100, "Upright"),
                           FaceEntry(kHebrewFont, 0, 700, 100, "Italic"),
                           FaceEntry(kArabicFont, 0, 900, 100, "Upright"),
                           FaceEntry(kKoreanFont, 0, 700, 150, "Upright"),
                           FaceEntry(kDevanagariFont, 0, 700, 100, "Oblique"),
                           FaceEntry(kThaiFont, 0, 700, 100, "Upright")},
                          {})}};
}

} // namespace

TEST_CASE("family candidates use exact lexicographic order and first-visit DFS") {
    FontFamilyFixture f;
    f.database.ScanProject(f.AssetsRoot());
    constexpr const char* kPrimary =
        "11111111111111111111111111111111";
    REQUIRE(f.database.Find(kPrimary) != nullptr);
    const auto resolved = f.resolver.BuildCandidates(
        kPrimary, {500, 100, molga::FontSlant::Upright}, f.sink);
    REQUIRE(resolved);
    CHECK(FontGuids(*resolved) == std::vector<std::string>{
        "44444444444444444444444444444444",
        "55555555555555555555555555555555",
        "12121212121212121212121212121212",
        "13131313131313131313131313131313",
        "66666666666666666666666666666666",
        "77777777777777777777777777777777"});
    CHECK(VisitedFamilyGuids(*resolved) == std::vector<std::string>{
        "11111111111111111111111111111111",
        "22222222222222222222222222222222",
        "33333333333333333333333333333333"});
    CHECK(EachFamilyVisitedOnce(*resolved));
}

TEST_CASE("family cycle terminates at first visit with a typed diagnostic") {
    FontFamilyFixture f;
    f.database.ScanProject(f.AssetsRoot());
    constexpr const char* kCycleA =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    REQUIRE(f.resolver.BuildCandidates(
        kCycleA, {400, 100, molga::FontSlant::Upright}, f.sink));
    CHECK(HasDiagnostic(f.sink, molga::text::TextDiagnosticCode::FontFamilyInvalid));
}

// Step 1d: 해석된 face가 가리키는 라이선스 record가 실제로 존재하고, 그 GUID가
// 저작된 것과 정확히 같은지 확인한다. 라이선스는 카탈로그에 함께 실려야만
// 패키지 검증이 재배포 권한을 판정할 수 있다.
TEST_CASE("every resolved face names its existing license record") {
    FontFamilyFixture f;
    const std::map<std::string, std::string> expectedLicense{
        {kLatinFont, kNotoLicense},   {kHebrewFont, kNotoLicense},
        {kDevanagariFont, kNotoLicense}, {kThaiFont, kNotoLicense},
        {kArabicFont, kNotoLicense},  {kKoreanFont, kCjkLicense}};

    const auto built = f.resolver.BuildCandidates(
        kPrimaryFamily, {400, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& resolved = Engaged(built);
    REQUIRE(resolved.candidates.size() == expectedLicense.size());
    for (const ResolvedFace& face : resolved.candidates) {
        REQUIRE_MESSAGE(face.resource != nullptr, face.fontGuid);
        REQUIRE_MESSAGE(face.resource->asset != nullptr, face.fontGuid);
        const auto expected = expectedLicense.find(face.fontGuid);
        REQUIRE_MESSAGE(expected != expectedLicense.end(), face.fontGuid);
        CHECK_MESSAGE(face.resource->asset->license.licenseAssetGuid ==
                          expected->second,
                      face.fontGuid);
        const molga::AssetRecord* license =
            f.database.Find(face.resource->asset->license.licenseAssetGuid);
        REQUIRE_MESSAGE(license != nullptr, face.fontGuid);
        CHECK_MESSAGE(license->importer == "GenericImporter", face.fontGuid);
        CHECK_MESSAGE(license->importerVersion == 1, face.fontGuid);
    }
}

// Step 4: 정렬 키는 (stylePenalty, |stretch-target|, |weight-target|,
// authoredFaceIndex, fontGuid, faceIndex)이고 그 순서까지 계약이다. 하나의
// 저작 family를 세 요청으로 해석해 성분과 성분 사이의 우선순위를 함께 고정한다.
TEST_CASE("per-family faces sort by the exact style/stretch/weight key") {
    AuthoredGraphFixture f(StyledFamilyTree());

    SUBCASE("upright target ranks exact style first, then stretch, then weight") {
        const auto built = f.resolver.BuildCandidates(
            kStyledFamily, {700, 100, molga::FontSlant::Upright}, f.sink);
        CHECK(FontGuids(Engaged(built)) ==
              std::vector<std::string>{kThaiFont, kArabicFont, kLatinFont,
                                       kKoreanFont, kHebrewFont,
                                       kDevanagariFont});
    }
    SUBCASE("italic target prefers italic, then oblique, then upright") {
        const auto built = f.resolver.BuildCandidates(
            kStyledFamily, {700, 100, molga::FontSlant::Italic}, f.sink);
        CHECK(FontGuids(Engaged(built)) ==
              std::vector<std::string>{kHebrewFont, kDevanagariFont, kThaiFont,
                                       kArabicFont, kLatinFont, kKoreanFont});
    }
    SUBCASE("oblique target prefers oblique, then italic, then upright") {
        const auto built = f.resolver.BuildCandidates(
            kStyledFamily, {700, 100, molga::FontSlant::Oblique}, f.sink);
        CHECK(FontGuids(Engaged(built)) ==
              std::vector<std::string>{kDevanagariFont, kHebrewFont, kThaiFont,
                                       kArabicFont, kLatinFont, kKoreanFont});
    }
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontFamilyInvalid));
}

// 요청은 저작물이 아니라 질의이므로 닫힌 범위 밖의 값도 해석되어야 한다.
// 거절하면 잘못 저작된 라벨 하나가 통째로 렌더링 불가가 된다.
//
// 이 케이스가 증명하지 못하는 것도 적어 둔다: 범위 밖 요청과 그 접힌 요청이
// 같은 순서를 낸다는 사실은 접기가 실제로 일어났다는 증거가 아니다. 거리
// 함수가 범위 밖에서 단조라 접기는 상대 순서를 바꿀 수 없고, 그래서 어떤
// 테스트도 접기의 유무를 구별할 수 없다(FontFamilyResolver.cpp의 Canonicalize
// 주석 참조). 여기서 고정되는 것은 "범위 밖 요청도 거절되지 않는다"이다.
TEST_CASE("a request outside the authored range still resolves the family") {
    AuthoredGraphFixture f(StyledFamilyTree());
    const auto clamped = f.resolver.BuildCandidates(
        kStyledFamily, {5000, 400, molga::FontSlant::Upright}, f.sink);
    const auto authored = f.resolver.BuildCandidates(
        kStyledFamily, {1000, 200, molga::FontSlant::Upright}, f.sink);
    CHECK(FontGuids(Engaged(clamped)) == FontGuids(Engaged(authored)));
    CHECK(Engaged(clamped).candidates.size() == 6U);

    // 범위 아래쪽 절반. 접기 자체는 관찰할 수 없지만 "어느 끝으로 접는가"는
    // 관찰된다: 아래로 벗어난 요청을 위쪽 끝으로 접으면 가장 무거운 face가
    // 먼저 서므로, 기대 순서를 명시해 그 방향까지 고정한다.
    const auto below = f.resolver.BuildCandidates(
        kStyledFamily, {0, 0, molga::FontSlant::Upright}, f.sink);
    const auto lightest = f.resolver.BuildCandidates(
        kStyledFamily,
        {static_cast<std::uint16_t>(molga::kFontWeightMin),
         static_cast<std::uint16_t>(molga::kFontStretchPercentMin),
         molga::FontSlant::Upright},
        f.sink);
    CHECK(FontGuids(Engaged(below)) ==
          std::vector<std::string>{kLatinFont, kThaiFont, kArabicFont,
                                   kKoreanFont, kHebrewFont, kDevanagariFont});
    CHECK(FontGuids(Engaged(below)) == FontGuids(Engaged(lightest)));
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontFamilyInvalid));
}

// cycle 케이스의 반대편. 두 family가 같은 하위 family를 함께 가리키는 마름모는
// 정상적인 저작이다. visited 집합 하나만으로 cycle을 판정하면 이 케이스가
// FontFamilyInvalid로 잘못 고발되고, 그 진단은 아무도 고칠 수 없다.
TEST_CASE("a shared fallback family is visited once without a cycle report") {
    AuthoredGraphFixture f({
        {kDiamondRoot, "diamond-root.fontfamily",
         FamilySource({}, {kDiamondLeft, kDiamondRight})},
        {kDiamondLeft, "diamond-left.fontfamily",
         FamilySource({}, {kDiamondShared})},
        {kDiamondRight, "diamond-right.fontfamily",
         FamilySource({}, {kDiamondShared})},
        {kDiamondShared, "diamond-shared.fontfamily",
         FamilySource({FaceEntry(kKoreanFont, 0, 400, 100, "Upright")}, {})},
    });
    const auto built = f.resolver.BuildCandidates(
        kDiamondRoot, {400, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& resolved = Engaged(built);
    CHECK(VisitedFamilyGuids(resolved) ==
          std::vector<std::string>{kDiamondRoot, kDiamondLeft, kDiamondShared,
                                   kDiamondRight});
    CHECK(EachFamilyVisitedOnce(resolved));
    CHECK(FontGuids(resolved) == std::vector<std::string>{kKoreanFont});
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontFamilyInvalid));
}

// Task 4.2가 세운 계약을 이 마일스톤이 소비하는 지점: 저작된 fallback 순서는
// 전순서이자 안정이며 재정렬 대상이 아니다. 커밋된 다섯 family의 fallback
// 목록은 전부 GUID 오름차순이라, 그 목록을 정렬해 버리는 회귀가 어떤 입력에서도
// 무연산이 되어 스위트를 통과한다. 그래서 내림차순으로 저작된 그래프 하나를
// 둔다 — 순회 순서와 기록된 간선 벡터를 함께 고정한다.
TEST_CASE("a descending authored fallback order survives resolution") {
    AuthoredGraphFixture f({
        {kDescendingFamily, "descending.fontfamily",
         FamilySource({}, {kDescHigh, kDescLow})},
        {kDescHigh, "desc-high.fontfamily",
         FamilySource({FaceEntry(kArabicFont, 0, 400, 100, "Upright")}, {})},
        {kDescLow, "desc-low.fontfamily",
         FamilySource({FaceEntry(kKoreanFont, 0, 400, 100, "Upright")}, {})},
    });
    REQUIRE(std::string(kDescHigh) > std::string(kDescLow));

    const auto built = f.resolver.BuildCandidates(
        kDescendingFamily, {400, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& resolved = Engaged(built);
    CHECK(VisitedFamilyGuids(resolved) ==
          std::vector<std::string>{kDescendingFamily, kDescHigh, kDescLow});
    CHECK(FontGuids(resolved) ==
          std::vector<std::string>{kArabicFont, kKoreanFont});
    const molga::text::ResolvedFamilyNode* root =
        FindNode(resolved, kDescendingFamily);
    REQUIRE(root != nullptr);
    CHECK(root->authoredFallbackGuids ==
          std::vector<std::string>{kDescHigh, kDescLow});
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontFamilyInvalid));
}

// Step 7: 삭제도 전이적 편집이다. 비어 있는 leaf family는 살아 있을 때도
// 지워진 뒤에도 세대가 0이고(ContentGeneration은 모르는 GUID에 0을 준다) 후보와
// 간선도 그대로이므로, node.exists를 접지 않으면 두 상태가 같은 값으로 접힌다.
// 그 상태의 캐시는 이미 사라진 family를 계속 살아 있다고 보고한다.
TEST_CASE("deleting an empty fallback family changes the graph generation") {
    AuthoredGraphFixture f({
        {kDeleteRoot, "delete-root.fontfamily",
         FamilySource({FaceEntry(kThaiFont, 0, 400, 100, "Upright")},
                      {kDeleteLeaf})},
        {kDeleteLeaf, "delete-leaf.fontfamily", FamilySource({}, {})},
    });
    const molga::text::FontRequest request{400, 100,
                                           molga::FontSlant::Upright};

    const auto before =
        f.resolver.BuildCandidates(kDeleteRoot, request, f.sink);
    const ResolvedFamily& first = Engaged(before);
    const molga::text::ResolvedFamilyNode* leaf = FindNode(first, kDeleteLeaf);
    REQUIRE(leaf != nullptr);
    CHECK(leaf->exists);
    REQUIRE(f.database.ContentGeneration(kDeleteLeaf) == 0U);
    const std::uint64_t generationBefore = first.fallbackGraphGeneration;

    f.DeleteFamily(kDeleteLeaf);
    // 삭제가 세대로는 관찰되지 않는다는 사실을 그대로 고정한다. 이것이
    // exists 비트를 접어야 하는 이유다.
    REQUIRE(f.database.ContentGeneration(kDeleteLeaf) == 0U);

    const auto after = f.resolver.BuildCandidates(kDeleteRoot, request, f.sink);
    const ResolvedFamily& second = Engaged(after);
    const molga::text::ResolvedFamilyNode* gone = FindNode(second, kDeleteLeaf);
    REQUIRE(gone != nullptr);
    CHECK_FALSE(gone->exists);
    CHECK(VisitedFamilyGuids(second) == VisitedFamilyGuids(first));
    CHECK(FontGuids(second) == std::vector<std::string>{kThaiFont});
    CHECK(second.fallbackGraphGeneration != generationBefore);
}

// Step 5/5a: 없는 family, 없는 폰트, 범위 밖 face는 전부 회복 가능한 실패다.
// 결과는 계속 돌아오고, 살아남은 후보만 목록에 남는다.
TEST_CASE("missing and out-of-range references are recorded, not fatal") {
    AuthoredGraphFixture f({
        {kBrokenFamily, "broken.fontfamily",
         FamilySource({FaceEntry(kLatinFont, 1, 400, 100, "Upright"),
                       FaceEntry(kAbsentFont, 0, 400, 100, "Upright"),
                       FaceEntry(kThaiFont, 0, 400, 100, "Upright")},
                      {kAbsentFamily})},
    });
    const auto built = f.resolver.BuildCandidates(
        kBrokenFamily, {400, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& resolved = Engaged(built);

    CHECK(FontGuids(resolved) == std::vector<std::string>{kThaiFont});
    CHECK(VisitedFamilyGuids(resolved) ==
          std::vector<std::string>{kBrokenFamily, kAbsentFamily});
    const molga::text::ResolvedFamilyNode* root =
        FindNode(resolved, kBrokenFamily);
    REQUIRE(root != nullptr);
    CHECK(root->exists);
    // Step 5: 존재하는 node는 저작된 fallback 간선 벡터를 그대로 들고 있어야
    // 한다. 참조가 깨졌다고 간선을 지우면 저작 의도가 기록에서 사라진다.
    CHECK(root->authoredFallbackGuids ==
          std::vector<std::string>{kAbsentFamily});
    const molga::text::ResolvedFamilyNode* absent =
        FindNode(resolved, kAbsentFamily);
    REQUIRE(absent != nullptr);
    CHECK_FALSE(absent->exists);
    CHECK(absent->authoredFallbackGuids.empty());

    // 못 푼 face 둘 + 없는 family 하나. 실패마다 정확히 하나씩이다.
    CHECK(CountDiagnostics(f.sink, TextDiagnosticCode::FontFamilyInvalid) == 3U);
    // 세 진단이 서로 다른 세 GUID를 지목한다. 이름이 비면 세 실패가 로거의
    // 같은 rate-limit 통으로 들어가 하나만 보인다.
    CHECK(DiagnosticAssetGuids(f.sink, TextDiagnosticCode::FontFamilyInvalid) ==
          std::vector<std::string>{kLatinFont, kAbsentFont, kAbsentFamily});
    for (const molga::text::TextDiagnostic& diagnostic : f.sink.Diagnostics()) {
        if (diagnostic.code != TextDiagnosticCode::FontFamilyInvalid) continue;
        CHECK_MESSAGE(diagnostic.severity == molga::text::TextSeverity::Error,
                      diagnostic.assetGuid);
    }
    // 범위 밖 face와 없는 폰트는 저장소까지 내려가 사유가 있는 FontInvalid를
    // 남긴다. 해석기의 그래프 진단이 그 사유를 대신하지 않는다.
    CHECK(CountDiagnostics(f.sink, TextDiagnosticCode::FontInvalid) == 2U);

    // Step 7: 묶이지 못한 저작 face도 후손이다. 바이트는 그대로 두고 발행
    // 정체성만 바꾸는 편집은 후보 목록을 전혀 건드리지 않으므로, 이 face의
    // 세대를 접지 않으면 폰트가 고쳐진 뒤에도 낡은 해석이 캐시에 남는다.
    const std::uint64_t generationBefore = resolved.fallbackGraphGeneration;
    REQUIRE(f.database.ContentGeneration(kLatinFont) == 0U);
    f.RewriteFontCopyright(kLatinFont, "fixture provenance: edited");
    REQUIRE(f.database.ContentGeneration(kLatinFont) == 1U);
    const auto reresolved = f.resolver.BuildCandidates(
        kBrokenFamily, {400, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& second = Engaged(reresolved);
    CHECK(FontGuids(second) == std::vector<std::string>{kThaiFont});
    CHECK(second.fallbackGraphGeneration != generationBefore);
}

// 진단은 사람이 읽는 채널이라 상한이 있고, 기계 판독 기록에는 상한이 없다.
// 상한이 기록까지 잘라내면 패키지 검증이 깨진 참조를 놓친다.
TEST_CASE("resolver diagnostics are bounded while the record stays complete") {
    std::vector<std::string> manyAbsent;
    for (int digit = 0; digit < 10; ++digit) {
        // 16진수만 유효한 GUID다. 16진수가 아닌 글자를 섞으면 family 자체가
        // import에서 거절되어 이 케이스가 그래프를 아예 보지 못한다.
        manyAbsent.push_back(std::string(31, 'a') +
                             static_cast<char>('0' + digit));
    }
    REQUIRE(manyAbsent.size() > molga::text::kMaxFamilyDiagnosticsPerResolve);
    AuthoredGraphFixture f({{kWideFamily, "wide.fontfamily",
                             FamilySource({}, manyAbsent)}});
    // 열 GUID가 정말로 카탈로그에 없어야 이 케이스가 "없는 참조"를 세는
    // 것이 된다. 하나라도 기존 record와 겹치면 다른 분기가 섞여 들어간다.
    for (const std::string& guid : manyAbsent) {
        REQUIRE_MESSAGE(f.database.Find(guid) == nullptr, guid);
    }

    const auto built = f.resolver.BuildCandidates(
        kWideFamily, {400, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& resolved = Engaged(built);
    REQUIRE(resolved.depthFirstFamilyNodes.size() == manyAbsent.size() + 1U);
    for (const std::string& guid : manyAbsent) {
        const molga::text::ResolvedFamilyNode* node = FindNode(resolved, guid);
        REQUIRE_MESSAGE(node != nullptr, guid);
        CHECK_MESSAGE(!node->exists, guid);
    }
    CHECK(CountDiagnostics(f.sink, TextDiagnosticCode::FontFamilyInvalid) ==
          molga::text::kMaxFamilyDiagnosticsPerResolve);
}

// Step 6: 후보의 정체성은 content address이고, 그 사실은 후보가 붙들고 있는
// 불변 자원이 증언한다. Step 7의 안정성(같은 상태 → 같은 값)도 함께 본다.
TEST_CASE("candidates are content-bound and repeat exactly") {
    FontFamilyFixture f;
    const auto first = f.resolver.BuildCandidates(
        kPrimaryFamily, {500, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& a = Engaged(first);
    REQUIRE(a.candidates.size() == 6U);
    CHECK(a.requestedGuid == std::string(kPrimaryFamily));
    // 후보는 자기 저작 배열 위치를 들고 나간다. 정렬은 FontFamilyFaceEntry의
    // authoredFaceIndex를 읽으므로 이 필드가 엉뚱한 값이어도 순서는 그대로다 —
    // 그래서 순서 단언만으로는 이 필드가 증언되지 않는다. 뒤의 0,0은 face가
    // 하나뿐인 두 fallback family의 위치다.
    std::vector<std::uint32_t> authoredPositions;
    for (const ResolvedFace& face : a.candidates) {
        authoredPositions.push_back(face.authoredFaceIndex);
    }
    CHECK(authoredPositions ==
          std::vector<std::uint32_t>{0U, 1U, 2U, 3U, 0U, 0U});
    // 저작된 fallback 간선 벡터를 그대로 기록한다. Task 4.2가 세운 "저작 순서가
    // 곧 계약"을 이 마일스톤이 소비하는 지점이므로, 재정렬/중복 제거가 끼어들면
    // 여기서 갈라진다.
    for (const molga::text::ResolvedFamilyNode& node :
         a.depthFirstFamilyNodes) {
        CHECK_MESSAGE(node.exists, node.familyGuid);
    }
    const molga::text::ResolvedFamilyNode* primary =
        FindNode(a, kPrimaryFamily);
    REQUIRE(primary != nullptr);
    CHECK(primary->authoredFallbackGuids ==
          std::vector<std::string>{kArabicFamily, kCjkFamily});
    const molga::text::ResolvedFamilyNode* arabic = FindNode(a, kArabicFamily);
    REQUIRE(arabic != nullptr);
    CHECK(arabic->authoredFallbackGuids.empty());
    for (const ResolvedFace& face : a.candidates) {
        REQUIRE_MESSAGE(face.resource != nullptr, face.fontGuid);
        REQUIRE_MESSAGE(face.resource->asset != nullptr, face.fontGuid);
        CHECK_MESSAGE(face.fontRevision == face.resource->artifactSha256 + ":" +
                                               std::to_string(face.faceIndex),
                      face.fontGuid);
        // 강제된 등식. 산출물이 소스와 다른 바이트면 후보가 되어서는 안 된다.
        CHECK_MESSAGE(face.resource->sourceSha256 ==
                          face.resource->artifactSha256,
                      face.fontGuid);
        // 권한 있는 산출물 상대 경로와 크기가 함께 살아 있다.
        CHECK_MESSAGE(face.resource->artifactLocator.relativePath
                              .generic_string() ==
                          molga::FontArtifactRelativePath(
                              face.resource->artifactSha256),
                      face.fontGuid);
        CHECK_MESSAGE(face.resource->asset->artifactByteSize > 0U,
                      face.fontGuid);
        CHECK_MESSAGE(face.resource->rasterFace != nullptr, face.fontGuid);
    }
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontFamilyInvalid));
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontInvalid));

    const auto second = f.resolver.BuildCandidates(
        kPrimaryFamily, {500, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& b = Engaged(second);
    CHECK(FontGuids(a) == FontGuids(b));
    CHECK(VisitedFamilyGuids(a) == VisitedFamilyGuids(b));
    CHECK(a.fallbackGraphGeneration == b.fallbackGraphGeneration);
    REQUIRE(b.candidates.size() == a.candidates.size());
    for (std::size_t index = 0; index < a.candidates.size(); ++index) {
        CHECK(a.candidates[index].fontRevision ==
              b.candidates[index].fontRevision);
    }
}

// Step 7: 전이적 편집은 전부 fallbackGraphGeneration을 바꾼다. 두 편집을
// 일부러 후보 목록이 그대로인 모양으로 고른다 — 후보가 함께 달라지면 세대가
// 아니라 후보를 보고 통과하는 테스트가 되기 때문이다.
TEST_CASE("any transitive edit changes the fallback graph generation") {
    AuthoredGraphFixture f({});
    const molga::text::FontRequest request{500, 100,
                                           molga::FontSlant::Upright};

    const auto before = f.resolver.BuildCandidates(kPrimaryFamily, request,
                                                   f.sink);
    const ResolvedFamily& first = Engaged(before);
    REQUIRE(first.candidates.size() == 6U);
    const std::vector<std::string> guidsBefore = FontGuids(first);
    std::vector<std::string> revisionsBefore;
    for (const ResolvedFace& face : first.candidates) {
        revisionsBefore.push_back(face.fontRevision);
    }
    const std::uint64_t generationBefore = first.fallbackGraphGeneration;

    SUBCASE("an unchanged rescan is not an edit") {
        f.database.ScanProject(f.AssetsRoot());
        const auto again = f.resolver.BuildCandidates(kPrimaryFamily, request,
                                                      f.sink);
        CHECK(Engaged(again).fallbackGraphGeneration == generationBefore);
    }

    SUBCASE("a descendant font reimport invalidates without moving revisions") {
        REQUIRE(f.database.ContentGeneration(kArabicFont) == 0U);
        f.RewriteFontCopyright(kArabicFont, "fixture provenance: edited");
        // 양성 대조: 편집이 실제로 관찰되었는가.
        REQUIRE(f.database.ContentGeneration(kArabicFont) == 1U);

        const auto after = f.resolver.BuildCandidates(kPrimaryFamily, request,
                                                      f.sink);
        const ResolvedFamily& second = Engaged(after);
        CHECK(FontGuids(second) == guidsBefore);
        std::vector<std::string> revisionsAfter;
        for (const ResolvedFace& face : second.candidates) {
            revisionsAfter.push_back(face.fontRevision);
        }
        // 바이트가 그대로이므로 content address도 그대로여야 한다. 프로세스
        // 지역 세대가 리비전 문자열에 섞여 들어갔다면 여기서 갈라진다.
        CHECK(revisionsAfter == revisionsBefore);
        CHECK(second.fallbackGraphGeneration != generationBefore);
    }

    SUBCASE("a descendant family reimport invalidates the graph") {
        REQUIRE(f.database.ContentGeneration(kCjkFamily) == 0U);
        nlohmann::json edited =
            FamilySource({FaceEntry(kKoreanFont, 0, 400, 100, "Upright")}, {});
        // 후보에는 전혀 영향이 없는 저작 확장 필드만 바꾼다.
        edited["unknownAuthoringField"] = "edited";
        f.RewriteFamilySource(kCjkFamily, edited);
        REQUIRE(f.database.ContentGeneration(kCjkFamily) == 1U);

        const auto after = f.resolver.BuildCandidates(kPrimaryFamily, request,
                                                      f.sink);
        const ResolvedFamily& second = Engaged(after);
        CHECK(FontGuids(second) == guidsBefore);
        CHECK(second.fallbackGraphGeneration != generationBefore);
    }
}

// Step 5a: nullopt는 레이아웃을 만들 수 없는 상태 실패 하나뿐이다. 묶인 바이트
// 권한이 없는 것은 애셋 내용이 아니라 호스트 설정의 결함이므로, tofu로 위장하면
// 고쳐야 할 버그가 "폰트가 없다"로 보인다.
TEST_CASE("resolution returns nullopt only when no byte authority is bound") {
    FontFamilyFixture bound;
    const molga::text::FontRequest request{400, 100,
                                           molga::FontSlant::Upright};

    // 바인딩 없는 데이터베이스는 스캔 자체가 거절되므로 비어 있는 것이 정상이고,
    // 순서를 틀린 호스트가 실제로 만드는 상태가 바로 이것이다.
    molga::AssetDatabase unbound;
    REQUIRE(unbound.FontArtifacts() == nullptr);
    REQUIRE(unbound.RecordCount() == 0U);
    molga::text::VectorTextDiagnosticSink unboundSink;
    molga::text::FontRepository unboundRepository(unbound);
    const molga::text::FontFamilyResolver unboundResolver(unbound,
                                                          unboundRepository);
    CHECK_FALSE(unboundResolver.BuildCandidates(kPrimaryFamily, request,
                                                unboundSink)
                    .has_value());
    CHECK_FALSE(unboundResolver.BuildLegacySingleFace(kLatinFont, request,
                                                      unboundSink)
                    .has_value());
    CHECK(CountDiagnostics(unboundSink,
                           TextDiagnosticCode::DependencyInvalid) == 2U);
    CHECK(DiagnosticAssetGuids(unboundSink,
                               TextDiagnosticCode::DependencyInvalid) ==
          std::vector<std::string>{kPrimaryFamily, kLatinFont});
    // 두 경로가 같은 componentType을 쓰면 legacy 단일 폰트의 실패가 존재하지도
    // 않는 family 문서를 고치라고 지목하게 된다.
    CHECK(DiagnosticComponentTypes(unboundSink,
                                   TextDiagnosticCode::DependencyInvalid) ==
          std::vector<std::string>{"FontFamilyAsset", "FontAsset"});

    // 같은 입력에 대한 양성 대조. 이것이 없으면 항상 nullopt를 돌려주는
    // 구현으로도 위 단언들이 통과한다.
    const auto positive =
        bound.resolver.BuildCandidates(kPrimaryFamily, request, bound.sink);
    CHECK(Engaged(positive).candidates.size() == 6U);

    // Step 5: 요청된 GUID 자체가 없는 것은 회복 가능한 실패다. 카탈로그 조회
    // 실패를 nullopt로 바꾸면 이 단언이 무너지므로, 위의 nullopt가 "record가
    // 없다"가 아니라 "바이트 권한이 없다"를 뜻한다는 것이 여기서 고정된다.
    const auto absent =
        bound.resolver.BuildCandidates(kAbsentFamily, request, bound.sink);
    const ResolvedFamily& missing = Engaged(absent);
    CHECK(missing.requestedGuid == std::string(kAbsentFamily));
    CHECK(missing.candidates.empty());
    REQUIRE(missing.depthFirstFamilyNodes.size() == 1U);
    CHECK(missing.depthFirstFamilyNodes.front().familyGuid ==
          std::string(kAbsentFamily));
    CHECK_FALSE(missing.depthFirstFamilyNodes.front().exists);
    CHECK(HasDiagnostic(bound.sink, TextDiagnosticCode::FontFamilyInvalid));
}

// legacy 단일 face 경로. family를 거치지 않았다는 사실 자체가 기록이다.
TEST_CASE("legacy single face binds exactly one content-addressed candidate") {
    FontFamilyFixture f;
    const molga::text::FontRequest request{400, 100,
                                           molga::FontSlant::Upright};

    const auto built = f.resolver.BuildLegacySingleFace(kLatinFont, request,
                                                        f.sink);
    const ResolvedFamily& resolved = Engaged(built);
    CHECK(resolved.requestedGuid == std::string(kLatinFont));
    CHECK(resolved.depthFirstFamilyNodes.empty());
    REQUIRE(resolved.candidates.size() == 1U);
    const ResolvedFace& face = resolved.candidates.front();
    CHECK(face.fontGuid == std::string(kLatinFont));
    CHECK(face.faceIndex == 0U);
    CHECK(face.authoredFaceIndex == 0U);
    REQUIRE(face.resource != nullptr);
    CHECK(face.fontRevision == face.resource->artifactSha256 + ":0");
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontInvalid));

    const auto missing =
        f.resolver.BuildLegacySingleFace(kAbsentFont, request, f.sink);
    const ResolvedFamily& none = Engaged(missing);
    CHECK(none.requestedGuid == std::string(kAbsentFont));
    CHECK(none.candidates.empty());
    // 폰트가 없는 것은 회복 가능한 실패다: 결과는 돌아오고 레이아웃이 tofu를
    // 만들 수 있으며, 사유는 저장소가 남긴 FontInvalid에 있다.
    CHECK(HasDiagnostic(f.sink, TextDiagnosticCode::FontInvalid));
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontFamilyInvalid));
}

// legacy 경로가 여는 face는 짐작이 아니라 카탈로그가 정한다. 자격 트리의 여섯
// 폰트가 전부 단일 face라서 이 조회를 통째로 0으로 고정해도 스위트가 통과하므로,
// 여기서만 face 둘짜리 collection을 저작해 face index 1을 관찰한다. 같은
// 케이스가 fontRevision의 ":<faceIndex>" 절반도 처음으로 ":0" 밖에서 본다.
TEST_CASE("the legacy path opens the face index the catalog authored") {
    AuthoredGraphFixture f({}, {{kCollectionFont, "thai-collection.ttf",
                                 "NotoSansThai-Regular.ttf", 1U}});
    const auto built = f.resolver.BuildLegacySingleFace(
        kCollectionFont, {400, 100, molga::FontSlant::Upright}, f.sink);
    const ResolvedFamily& resolved = Engaged(built);
    REQUIRE(resolved.candidates.size() == 1U);
    const ResolvedFace& face = resolved.candidates.front();
    CHECK(face.fontGuid == std::string(kCollectionFont));
    CHECK(face.faceIndex == 1U);
    REQUIRE(face.resource != nullptr);
    CHECK(face.resource->faceIndex == 1U);
    CHECK(face.fontRevision == face.resource->artifactSha256 + ":1");
    CHECK(face.resource->rasterFace != nullptr);
    CHECK_FALSE(HasDiagnostic(f.sink, TextDiagnosticCode::FontInvalid));
}
