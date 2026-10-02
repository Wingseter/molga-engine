#include "AssetDatabaseTestAuthority.h"
#include "Core/AssetDatabase.h"
#include "Core/Importers/FontImporter.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/FontFace.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/TextRenderer.h"
#include "Common/Fixed26_6.h"
#include "Rendering/Utf8.h"
#include "Text/TextDiagnostic.h"
#include "Text/TextLayoutTypes.h"
#include "doctest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path TestFontPath() {
    return fs::path(MOLGA_TEST_FONT_PATH);
}

fs::path TestKoreanFontPath() {
    return fs::path(MOLGA_TEST_KOREAN_FONT_PATH);
}

// Task 8.2: 위 파일은 가변 폰트라 Task 4의 static 전용 import 계약이 거절한다.
// 셰이핑 경로를 지나는 케이스는 자격 트리와 같은 static OTF를 쓴다.
fs::path TestKoreanStaticFontPath() {
    return fs::path(MOLGA_TEST_KOREAN_STATIC_FONT_PATH);
}

std::shared_ptr<const std::vector<std::uint8_t>> SharedFontBytes(
    const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE_MESSAGE(input.good(), "could not open " << path.string());
    return std::make_shared<const std::vector<std::uint8_t>>(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// ── Task 8.2: 저작된 폰트 import 설정이 있어야 검증된 산출물이 발행된다 ──────
// FontImporter는 저작 설정이 없는 폰트를 구조 검증만 하고 지나가며(호환 경로),
// 그때는 metadata["font"]가 없으므로 AssetDatabase가 불변 산출물을 발행하지
// 않는다. 산출물이 없는 record는 FontAsset이 되지 못하고, 그래서 어떤 face도
// 묶이지 않는다. 레거시 codepoint atlas는 원본 경로에서 바이트를 직접 읽어
// 이 계약을 우회했고, Task 8.2가 그 우회로를 지웠다 — 이제 픽스처도 실제
// 프로젝트와 같은 방식으로 폰트를 저작해야 한다.
void WriteFontMeta(const fs::path& fontPath, const std::string& guid) {
    std::ofstream meta(fontPath.string() + ".meta");
    REQUIRE(meta.good());
    meta << R"({
  "guid": ")" << guid << R"(",
  "importer": "FontImporter",
  "importerVersion": 2,
  "settings": {
    "faceIndex": 0,
    "weight": 400,
    "stretchPercent": 100,
    "slant": "Upright",
    "redistributableConfirmed": true,
    "licenseKind": "OFL-1.1",
    "copyright": "test fixture provenance",
    "licenseAssetGuid": "88888888888888888888888888888888"
  }
})";
}

fs::path MakeFontProject() {
    // Each case scans its own subtree of the single project root the singleton
    // database's font artifact store is bound to.
    auto& authority = test_support::AssetDatabaseTestAuthority::Get();
    std::string bindError;
    REQUIRE_MESSAGE(authority.Bind(molga::AssetDatabase::Get(), &bindError),
                    bindError);
    const fs::path root = authority.AssetsCaseRoot("font-project");
    fs::create_directories(root / "Assets" / "Fonts");
    const fs::path latin = root / "Assets" / "Fonts" / "Inter-Regular.ttf";
    const fs::path korean = root / "Assets" / "Fonts" / "NotoSansKR-Regular.otf";
    fs::copy_file(TestFontPath(), latin, fs::copy_options::overwrite_existing);
    fs::copy_file(TestKoreanStaticFontPath(), korean,
                  fs::copy_options::overwrite_existing);
    // 케이스마다 새 subtree이므로 GUID도 케이스마다 새로 만든다. 같은 GUID가
    // 두 subtree에 있으면 AssetDatabase가 중복으로 거절한다.
    static int nextGuid = 0;
    const auto guidFor = [](int ordinal) {
        std::string digits = std::to_string(ordinal);
        return std::string(32U - digits.size(), 'a') + digits;
    };
    WriteFontMeta(latin, guidFor(++nextGuid));
    WriteFontMeta(korean, guidFor(++nextGuid));
    return root;
}

} // namespace

TEST_CASE("UTF-8 decoder handles Korean and rejects ill-formed scalars") {
    const auto korean = molga::DecodeUtf8(u8"한글 타이틀");
    REQUIRE(korean.size() == 6U);
    CHECK(korean[0] == 0xD55CU);
    CHECK(korean[1] == 0xAE00U);
    CHECK(korean[2] == static_cast<std::uint32_t>(' '));
    CHECK(korean[3] == 0xD0C0U);
    CHECK(korean[4] == 0xC774U);
    CHECK(korean[5] == 0xD2C0U);

    // DecodeUtf8 now delegates to molga::text::UnicodeTextBuffer, which rejects
    // a sequence at the byte that breaks it rather than gathering three or four
    // bytes and filtering by the decoded value. So an ill-formed sequence
    // yields one U+FFFD per Unicode maximal subpart, not one per attempted
    // sequence: the counts below are the substantive change, and each is the
    // number of maximal subparts in that input.
    //
    // The three rows here are ill-formed at their SECOND byte — 0x80 outside
    // E0's A0..BF window, 0xA0 inside ED's surrogate range, 0x90 above F4's
    // 80..8F ceiling — so the lead byte alone is the first subpart and every
    // trailing continuation byte is a subpart of its own.
    CHECK(molga::DecodeUtf8(std::string("\xE0\x80\xAF", 3)) ==
          std::vector<std::uint32_t>{molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter});
    CHECK(molga::DecodeUtf8(std::string("\xED\xA0\x80", 3)) ==
          std::vector<std::uint32_t>{molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter});
    CHECK(molga::DecodeUtf8(std::string("\xF4\x90\x80\x80", 4)) ==
          std::vector<std::uint32_t>{molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter,
                                     molga::kUnicodeReplacementCharacter});
    // Deliberately still one. "\xE2\x82" is a well-formed PREFIX truncated by
    // the end of input, so the whole two bytes are a single maximal subpart
    // under both the old decoder and the new one. Widening this row to two
    // would assert the opposite of the maximal-subpart rule.
    CHECK(molga::DecodeUtf8(std::string("\xE2\x82", 2)) ==
          std::vector<std::uint32_t>{molga::kUnicodeReplacementCharacter});

    const auto stray = molga::DecodeUtf8(std::string("A\x80" "B", 3));
    REQUIRE(stray.size() == 3U);
    CHECK(stray[0] == static_cast<std::uint32_t>('A'));
    CHECK(stray[1] == molga::kUnicodeReplacementCharacter);
    CHECK(stray[2] == static_cast<std::uint32_t>('B'));
}

TEST_CASE("FontImporter validates TTF and rejects corrupt font data") {
    molga::FontImporter importer;
    CHECK(importer.Name() == "FontImporter");
    CHECK(importer.CanImport(".ttf"));
    CHECK(importer.CanImport(".otf"));
    CHECK_FALSE(importer.CanImport(".png"));

    const molga::ImportResult valid = importer.Import(TestFontPath().string());
    CHECK(valid.success);
    CHECK(valid.error.empty());

    const fs::path corrupt = fs::temp_directory_path() / "molga_corrupt_font.ttf";
    { std::ofstream output(corrupt, std::ios::binary); output << "not a font"; }
    const molga::ImportResult invalid = importer.Import(corrupt.string());
    CHECK_FALSE(invalid.success);
    CHECK_FALSE(invalid.error.empty());
    std::error_code error;
    fs::remove(corrupt, error);
}

// Task 8.2 Step 8a.1/8b: codepoint 기반 measurement/rasterization은 지워졌다.
// stb에 남은 진입점은 셰이핑된 glyph ID 하나를 그리는 RasterizeGlyph뿐이고,
// 이 케이스가 그 하나를 붙든다. xAdvance가 0으로 남는 것까지가 계약이다 —
// 논리 advance는 HarfBuzz의 값이고, 래스터라이저의 근사로 그것을 덮어쓰면
// 같은 문자열이 폰트마다 다른 자리에 놓인다.
TEST_CASE("FontFace rasterizes shaped glyph IDs and reports no logical advance") {
    molga::FontFace face;
    std::string error;
    REQUIRE(face.LoadFromFile(TestFontPath(), &error));
    CHECK(error.empty());
    CHECK(face.IsValid());

    const std::uint32_t glyphId = face.GlyphId(U'A');
    REQUIRE(glyphId != 0U);
    const molga::FontGlyphBitmap glyph =
        face.RasterizeGlyph(glyphId, 32U, 64U);
    CHECK(glyph.width > 0);
    CHECK(glyph.height > 0);
    CHECK(glyph.xAdvance == doctest::Approx(0.0f));
    CHECK(glyph.coverage.size() ==
          static_cast<std::size_t>(glyph.width * glyph.height));
    CHECK(face.LastRasterizedGlyphId() == glyphId);

    molga::FontFace koreanFace;
    REQUIRE(koreanFace.LoadFromFile(TestKoreanFontPath(), &error));
    const std::uint32_t koreanGlyphId = koreanFace.GlyphId(0xD55CU);
    REQUIRE(koreanGlyphId != 0U);
    const molga::FontGlyphBitmap koreanGlyph =
        koreanFace.RasterizeGlyph(koreanGlyphId, 32U, 64U);
    CHECK(koreanGlyph.width > 0);
    CHECK(koreanGlyph.height > 0);
    CHECK_FALSE(koreanGlyph.coverage.empty());
}

// ── Task 8.2 Step 8a.1: HasCodepoint/GlyphId는 검사 전용이다 ─────────────────
// 두 함수는 cmap 조회일 뿐 셰이핑도 fallback도 대신하지 못한다. 셰이핑이 그
// 둘로 face를 고르기 시작하면 결과는 여전히 그럴듯하게 나오고 — 라틴 문자는
// 어느 face로 골라도 그려진다 — 실패는 합자와 fallback 순서에서만 드러난다.
// 그래서 주장은 코드를 읽어서가 아니라 소스 스캔으로 지킨다.
TEST_CASE("no shaping, layout, or render consumer calls the cmap inspection helpers") {
    const fs::path sourceRoot(MOLGA_ENGINE_SOURCE_ROOT);
    // 셰이핑/레이아웃/렌더의 프로덕션 소비자 전부. FontFace 자신은 이 둘을
    // 정의하는 곳이므로 목록에 없다.
    const char* const consumers[] = {
        "src/Text/TextShapingService.cpp", "src/Text/TextLayoutService.cpp",
        "src/Text/FontFamilyResolver.cpp", "src/Text/FontRepository.cpp",
        "src/Text/TextLayoutCache.cpp",    "src/Rendering/TextRenderer.cpp",
        "src/Rendering/FontAtlas.cpp",     "src/UI/UISystem.cpp",
        "src/ECS/Components/TextRenderer2D.cpp",
        "src/ECS/Components/UILabel.cpp",
    };
    std::size_t scannedBytes = 0U;
    for (const char* relative : consumers) {
        const fs::path path = sourceRoot / relative;
        INFO("consumer " << path.string());
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        const std::string source((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        REQUIRE(source.size() > 128U);
        scannedBytes += source.size();
        CHECK(source.find("HasCodepoint(") == std::string::npos);
        CHECK(source.find("GlyphId(") == std::string::npos);
    }
    // 스캐너 자신의 증인. 목록이 통째로 빗나가면 위의 "없다"들이 빈 문자열
    // 위에서 공짜로 참이 된다.
    CHECK(scannedBytes > 10000U);
}

// Task 4.3 Step 3/3a: FontFace가 검증된 불변 바이트 위에서만 열리고, 그
// 바이트의 소유권을 face 수명 내내 붙들고 있는지 본다. 호출자가 자기 소유
// 지분을 놓아도 face는 계속 유효해야 한다.
TEST_CASE("FontFace loads immutable shared bytes and outlives its caller's owner") {
    auto bytes = SharedFontBytes(TestFontPath());
    const std::size_t byteCount = bytes->size();
    molga::FontFace face;
    std::string error;
    REQUIRE_MESSAGE(face.LoadFromBytes(bytes, 0, &error), error);
    CHECK(error.empty());
    CHECK(face.IsValid());
    CHECK(face.FaceIndex() == 0U);
    CHECK(face.HasCodepoint(U'A'));
    const std::uint32_t glyph = face.GlyphId(U'A');
    CHECK(glyph != 0U);
    CHECK_FALSE(face.HasCodepoint(0x10FFFEU));
    CHECK(face.GlyphId(0x10FFFEU) == 0U);
    // 유니코드 범위 밖은 cmap을 건드리지 않고 .notdef이다.
    CHECK(face.GlyphId(0x110000U) == 0U);
    CHECK_FALSE(face.HasCodepoint(0x110000U));

    CHECK(bytes.use_count() > 1);
    bytes.reset();
    CHECK(face.IsValid());
    CHECK(face.GlyphId(U'A') == glyph);
    CHECK(face.HasCodepoint(U'A'));
    const molga::FontGlyphBitmap bitmap = face.RasterizeGlyph(glyph, 32U, 64U);
    CHECK(bitmap.width > 0);
    CHECK(byteCount > 0U);

    molga::FontFace koreanFace;
    REQUIRE(koreanFace.LoadFromBytes(SharedFontBytes(TestKoreanFontPath()), 0,
                                     &error));
    CHECK(koreanFace.HasCodepoint(0xD55CU));
    CHECK(koreanFace.GlyphId(0xD55CU) != 0U);

    // 컬렉션이 아닌 파일에는 face 0밖에 없고, 빈 소유권은 face가 아니다.
    molga::FontFace outside;
    CHECK_FALSE(outside.LoadFromBytes(SharedFontBytes(TestFontPath()), 1, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(outside.IsValid());
    CHECK(outside.FaceIndex() == 0U);
    CHECK(outside.GlyphId(U'A') == 0U);
    CHECK_FALSE(outside.HasCodepoint(U'A'));

    molga::FontFace empty;
    CHECK_FALSE(empty.LoadFromBytes(nullptr, 0, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(empty.IsValid());
    CHECK_FALSE(empty.LoadFromBytes(
        std::make_shared<const std::vector<std::uint8_t>>(), 0, &error));
    CHECK_FALSE(empty.IsValid());
}

// Task 8.2 Step 8a: 레거시 codepoint atlas는 지워졌다. 이 케이스에 남은
// 주장은 임포트 쪽이고, page 배분은 GlyphAtlasCache의 것이라 test_glyph_atlas가
// 붙든다.
TEST_CASE("AssetDatabase imports fonts through the font importer") {
    const fs::path project = MakeFontProject();
    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    database.ScanProject(project / "Assets");

    const std::string guid = database.GuidForSource("Assets/Fonts/Inter-Regular.ttf");
    REQUIRE_FALSE(guid.empty());
    const molga::AssetRecord* record = database.Find(guid);
    REQUIRE(record != nullptr);
    CHECK(record->importer == "FontImporter");
    CHECK_FALSE(record->importFailed);

    database.Clear();
    std::error_code error;
    fs::remove_all(project, error);
}

// ── Task 8.2 Step 7e: 공유 파이프라인 위의 GUID 기반 텍스트 ─────────────────
// 이 GUID들은 schema 1의 폰트 지목이므로 레거시 단일 face 경로로 간다(Task 8.2
// 설계 개정): face 하나, fallback 없음. 예전 케이스가 재던 MeasureText/
// CollectText/GetAtlasPageCount는 셰이핑을 보지 않으므로, 그 자리를 배치와
// page 소유권이 대신한다.
TEST_CASE("a legacy font GUID shapes and submits page-backed atlas quads") {
    const fs::path project = MakeFontProject();
    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    database.ScanProject(project / "Assets");
    const std::string guid = database.GuidForSource("Fonts/Inter-Regular.ttf");
    REQUIRE_FALSE(guid.empty());
    molga::text::VectorTextDiagnosticSink sink;
    TextRenderer& renderer = TextRenderer::Get();
    REQUIRE(renderer.Init(database, sink));

    molga::text::TextLayoutRequest request;
    request.utf8 = "AV\nTo";
    request.style.legacyFontGuid = guid;
    request.style.shape.fontSize = molga::Fixed26_6::FromRaw(32 * 64);
    request.diagnosticContext.componentType = "UILabel";
    const auto layout = renderer.Layout(request, sink);
    REQUIRE(layout.has_value());
    // 명시적 개행이 두 줄을 만든다. 줄바꿈은 배치의 것이지 렌더러의 것이
    // 아니므로, 여기서 세는 것은 줄 수이지 예전의 lineCount 필드가 아니다.
    CHECK((*layout)->lines.size() == 2U);

    molga::RenderQueue queue;
    TextCollectContext collect;
    collect.cameraPass = 2;
    collect.sortingLayer = 3;
    collect.sortingOrder = -4;
    collect.depthOrYSort = 27.5f;
    {
        auto scope = renderer.BeginGlyphCollection(1);
        renderer.CollectLayout(queue, **layout, collect, sink);
    }
    REQUIRE(queue.GetCommands().size() == 4U);
    for (const molga::RenderCommand& command : queue.GetCommands()) {
        CHECK(command.batchKey.isBatchable);
        CHECK(command.isBatchableSprite);
        CHECK(command.sortKey.cameraPass == 2);
        CHECK(command.sortKey.sortingLayer == 3);
        CHECK(command.sortKey.sortingOrder == -4);
        CHECK(command.sortKey.depthOrYSort == doctest::Approx(27.5f));
        // 텍스처를 든 명령은 자기 page의 이름과 지분도 든다(Task 6.3).
        CHECK(command.resourceLifetimeIdentity != 0U);
        CHECK(static_cast<bool>(command.resourceLifetime));
    }
    queue.Clear();

    database.Clear();
    std::error_code error;
    fs::remove_all(project, error);
}

TEST_CASE("a legacy Korean font GUID shapes every drawable syllable") {
    const fs::path project = MakeFontProject();
    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    database.ScanProject(project / "Assets");
    const std::string guid = database.GuidForSource("Fonts/NotoSansKR-Regular.otf");
    REQUIRE_FALSE(guid.empty());

    molga::text::VectorTextDiagnosticSink sink;
    TextRenderer& renderer = TextRenderer::Get();
    REQUIRE(renderer.Init(database, sink));

    molga::text::TextLayoutRequest request;
    request.utf8 = u8"한글 타이틀";
    request.style.legacyFontGuid = guid;
    request.style.shape.fontSize = molga::Fixed26_6::FromRaw(40 * 64);
    request.diagnosticContext.componentType = "UILabel";
    const auto layout = renderer.Layout(request, sink);
    REQUIRE(layout.has_value());

    molga::RenderQueue leftQueue;
    TextCollectContext left;
    left.layoutToOutput.tx = 100.0f;
    left.layoutToOutput.ty = 20.0f;
    {
        auto scope = renderer.BeginGlyphCollection(2);
        renderer.CollectLayout(leftQueue, **layout, left, sink);
    }
    // 명시적 공백에는 그릴 것이 없다.
    REQUIRE(leftQueue.GetCommands().size() == 5U);

    // 같은 불변 배치를 다른 원점으로 수집하면 정확히 그 차이만큼 옮겨진다.
    // 정렬을 배치가 처리한다는 것이 이 케이스의 내용이므로, 렌더러 쪽에서
    // 원점을 되짚는 경로가 남아 있으면 이 차이가 어긋난다.
    molga::RenderQueue shiftedQueue;
    TextCollectContext shifted = left;
    shifted.layoutToOutput.tx = 60.0f;
    {
        auto scope = renderer.BeginGlyphCollection(3);
        renderer.CollectLayout(shiftedQueue, **layout, shifted, sink);
    }
    REQUIRE(shiftedQueue.GetCommands().size() == leftQueue.GetCommands().size());
    const float delta = shiftedQueue.GetCommands().front().vertices[0].x -
                        leftQueue.GetCommands().front().vertices[0].x;
    CHECK(delta == doctest::Approx(-40.0f));

    for (const molga::RenderCommand& command : shiftedQueue.GetCommands()) {
        CHECK(command.batchKey.isBatchable);
        CHECK(command.isBatchableSprite);
        CHECK(std::isfinite(command.vertices[0].x));
        CHECK(std::isfinite(command.vertices[0].y));
        CHECK(command.resourceLifetimeIdentity != 0U);
    }
    leftQueue.Clear();
    shiftedQueue.Clear();

    database.Clear();
    std::error_code error;
    fs::remove_all(project, error);
}

// ── Task 8.2 Step 10/10a: 내장 ASCII 대체는 없다 ─────────────────────────────
// 예전에는 없는 GUID가 조용히 8픽셀 비트맵 폰트로 그려졌다. 조용히 다른 폰트로
// 그리는 것은 텍스트가 없는 것보다 나쁘다: 저작자는 폰트가 빠진 줄 모른 채
// 출하하게 된다. 지금은 grapheme마다 절차적 두부 하나와 타입 있는 진단이다.
TEST_CASE("a missing font GUID renders tofu instead of a built-in ASCII bitmap") {
    const fs::path project = MakeFontProject();
    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    database.ScanProject(project / "Assets");

    molga::text::VectorTextDiagnosticSink sink;
    TextRenderer& renderer = TextRenderer::Get();
    REQUIRE(renderer.Init(database, sink));

    molga::text::TextLayoutRequest request;
    request.utf8 = u8"A한B";
    request.style.legacyFontGuid = "missing-font-guid";
    request.style.shape.fontSize = molga::Fixed26_6::FromRaw(16 * 64);
    request.diagnosticContext.componentType = "UILabel";
    const auto layout = renderer.Layout(request, sink);
    REQUIRE(layout.has_value());

    molga::RenderQueue queue;
    const std::uint64_t lookupsBefore =
        renderer.GlyphAtlas().LookupCountForTest();
    {
        auto scope = renderer.BeginGlyphCollection(4);
        renderer.CollectLayout(queue, **layout, TextCollectContext{}, sink);
    }
    REQUIRE(queue.GetCommands().size() == 3U);
    for (const molga::RenderCommand& command : queue.GetCommands()) {
        CHECK(command.batchKey.isBatchable);
        CHECK(command.isBatchableSprite);
        // 두부는 유효하지 않은 핸들을 그대로 들고 나가고, page 지분도 없다.
        CHECK_FALSE(static_cast<bool>(command.batchKey.texture));
        CHECK(command.batchKey.textureStableId == 0U);
        CHECK(command.resourceLifetimeIdentity == 0U);
        CHECK_FALSE(static_cast<bool>(command.resourceLifetime));
    }
    // 없는 face는 atlas에 닿지도 않는다. 조회 자체가 page를 만들 수 있으므로,
    // 예산이 0인 화면에서도 이 경로는 비용이 없다.
    CHECK(renderer.GlyphAtlas().LookupCountForTest() == lookupsBefore);
    queue.Clear();

    database.Clear();
    std::error_code error;
    fs::remove_all(project, error);
}
