#include "Core/AssetDatabase.h"
#include "Core/Importers/FontImporter.h"
#include "Rendering/FontAtlas.h"
#include "Rendering/FontFace.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/TextRenderer.h"
#include "Rendering/Utf8.h"
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path TestFontPath() {
    return fs::path(MOLGA_TEST_FONT_PATH);
}

fs::path TestKoreanFontPath() {
    return fs::path(MOLGA_TEST_KOREAN_FONT_PATH);
}

fs::path MakeFontProject() {
    const fs::path root = fs::temp_directory_path() / "molga_font_tests";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "Assets" / "Fonts");
    fs::copy_file(TestFontPath(), root / "Assets" / "Fonts" / "Inter-Regular.ttf",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(TestKoreanFontPath(),
                  root / "Assets" / "Fonts" / "NotoSansKR-Regular.ttf",
                  fs::copy_options::overwrite_existing);
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

    CHECK(molga::DecodeUtf8(std::string("\xE0\x80\xAF", 3)) ==
          std::vector<std::uint32_t>{molga::kUnicodeReplacementCharacter});
    CHECK(molga::DecodeUtf8(std::string("\xED\xA0\x80", 3)) ==
          std::vector<std::uint32_t>{molga::kUnicodeReplacementCharacter});
    CHECK(molga::DecodeUtf8(std::string("\xF4\x90\x80\x80", 4)) ==
          std::vector<std::uint32_t>{molga::kUnicodeReplacementCharacter});
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

TEST_CASE("FontFace exposes metrics, glyph bitmaps, advances, and kerning") {
    molga::FontFace face;
    std::string error;
    REQUIRE(face.LoadFromFile(TestFontPath(), &error));
    CHECK(error.empty());
    CHECK(face.IsValid());
    CHECK(face.HasGlyph('A'));

    const molga::FontFaceMetrics metrics = face.Metrics(32.0f);
    CHECK(metrics.ascent > 0.0f);
    CHECK(metrics.descent < 0.0f);
    CHECK(metrics.lineHeight > 0.0f);

    const molga::FontGlyphBitmap glyph = face.Rasterize('A', 32.0f);
    CHECK(glyph.width > 0);
    CHECK(glyph.height > 0);
    CHECK(glyph.xAdvance > 0.0f);
    CHECK(glyph.coverage.size() ==
          static_cast<std::size_t>(glyph.width * glyph.height));
    CHECK(face.Advance('A', 32.0f) == doctest::Approx(glyph.xAdvance));
    CHECK(face.Kerning('A', 'V', 32.0f) <= 0.0f);

    molga::FontFace koreanFace;
    REQUIRE(koreanFace.LoadFromFile(TestKoreanFontPath(), &error));
    CHECK(koreanFace.HasGlyph(0xD55CU));
    const molga::FontGlyphBitmap koreanGlyph = koreanFace.Rasterize(0xD55CU, 32.0f);
    CHECK(koreanGlyph.width > 0);
    CHECK(koreanGlyph.height > 0);
    CHECK_FALSE(koreanGlyph.coverage.empty());
}

TEST_CASE("AssetDatabase imports fonts and lazy atlas allocates extra pages") {
    const fs::path project = MakeFontProject();
    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    database.ScanProject(project / "Assets");

    const std::string guid = database.GuidForSource("Assets/Fonts/Inter-Regular.ttf");
    REQUIRE_FALSE(guid.empty());
    const molga::AssetRecord* record = database.Find(guid);
    REQUIRE(record != nullptr);
    CHECK(record->importer == "FontImporter");
    CHECK_FALSE(record->importFailed);

    molga::FontAtlasCache atlas(64);
    molga::FontFaceMetrics metrics;
    REQUIRE(atlas.GetMetrics(guid, 24, metrics));
    for (std::uint32_t codepoint = 33U; codepoint <= 126U; ++codepoint) {
        molga::FontAtlasGlyph glyph;
        REQUIRE(atlas.GetGlyph(guid, 24, codepoint, glyph));
    }
    CHECK(atlas.GlyphCount(guid, 24) == 94U);
    CHECK(atlas.PageCount(guid, 24) > 1U);

    database.Clear();
    std::error_code error;
    fs::remove_all(project, error);
}

TEST_CASE("TextRenderer measures kerning and submits batchable atlas quads") {
    const fs::path project = MakeFontProject();
    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    database.ScanProject(project / "Assets");
    const std::string guid = database.GuidForSource("Fonts/Inter-Regular.ttf");
    REQUIRE_FALSE(guid.empty());

    TextRenderer& renderer = TextRenderer::Get();
    renderer.InvalidateAllFonts();
    const TextMetrics single = renderer.MeasureText("AV", guid, 32.0f);
    const TextMetrics multiline = renderer.MeasureText("A\nVV", guid, 32.0f, 1.0f, 1.5f);
    CHECK(single.width > 0.0f);
    CHECK(single.lineCount == 1U);
    CHECK(multiline.lineCount == 2U);
    CHECK(multiline.height == doctest::Approx(
        multiline.lineHeight + multiline.lineHeight * 1.5f));

    molga::RenderQueue queue;
    TextDrawParams params;
    params.text = "AV\nTo";
    params.fontGuid = guid;
    params.fontSizePx = 32.0f;
    params.alignment = TextHorizontalAlignment::Center;
    renderer.CollectText(queue, params);
    REQUIRE(queue.GetCommands().size() == 4U);
    for (const molga::RenderCommand& command : queue.GetCommands()) {
        CHECK(command.batchKey.isBatchable);
        CHECK(command.isBatchableSprite);
    }
    CHECK(renderer.GetAtlasPageCount(guid, 32) >= 1U);
    CHECK(renderer.GetCachedFontSizeCount() == 1U);

    database.Reimport(guid);
    CHECK(renderer.GetCachedFontSizeCount() == 0U);

    database.Clear();
    std::error_code error;
    fs::remove_all(project, error);
}

TEST_CASE("TextRenderer bakes and queues requested Korean glyphs by GUID") {
    const fs::path project = MakeFontProject();
    molga::AssetDatabase& database = molga::AssetDatabase::Get();
    database.ScanProject(project / "Assets");
    const std::string guid = database.GuidForSource("Fonts/NotoSansKR-Regular.ttf");
    REQUIRE_FALSE(guid.empty());

    TextRenderer& renderer = TextRenderer::Get();
    renderer.InvalidateAllFonts();
    const std::string title = u8"한글 타이틀";
    const TextMetrics metrics = renderer.MeasureText(title, guid, 40.0f);
    CHECK(metrics.width > 0.0f);
    CHECK(metrics.height > 0.0f);

    molga::RenderQueue leftQueue;
    TextDrawParams left;
    left.text = title;
    left.fontGuid = guid;
    left.fontSizePx = 40.0f;
    left.x = 100.0f;
    left.y = 20.0f;
    renderer.CollectText(leftQueue, left);
    REQUIRE(leftQueue.GetCommands().size() == 5U); // the explicit space has no quad

    molga::RenderQueue centeredQueue;
    TextDrawParams centered = left;
    centered.alignment = TextHorizontalAlignment::Center;
    renderer.CollectText(centeredQueue, centered);
    REQUIRE(centeredQueue.GetCommands().size() == leftQueue.GetCommands().size());
    const float alignmentDelta = centeredQueue.GetCommands().front().vertices[0].x -
                                 leftQueue.GetCommands().front().vertices[0].x;
    CHECK(alignmentDelta == doctest::Approx(-metrics.width * 0.5f));

    for (const molga::RenderCommand& command : centeredQueue.GetCommands()) {
        CHECK(command.batchKey.isBatchable);
        CHECK(command.isBatchableSprite);
        CHECK(std::isfinite(command.vertices[0].x));
        CHECK(std::isfinite(command.vertices[0].y));
    }
    CHECK(renderer.GetAtlasPageCount(guid, 40) >= 1U);

    database.Clear();
    std::error_code error;
    fs::remove_all(project, error);
}

TEST_CASE("Missing font GUID retains the built-in ASCII replacement fallback") {
    TextRenderer& renderer = TextRenderer::Get();
    renderer.Shutdown();
    REQUIRE(renderer.Init()); // safe in this headless test process

    const TextMetrics metrics = renderer.MeasureText(
        u8"A한B", "missing-font-guid", 16.0f);
    CHECK(metrics.width == doctest::Approx(48.0f));
    CHECK(metrics.lineHeight == doctest::Approx(16.0f));

    molga::RenderQueue queue;
    TextDrawParams params;
    params.text = u8"A한B";
    params.fontGuid = "missing-font-guid";
    params.fontSizePx = 16.0f;
    renderer.CollectText(queue, params);
    REQUIRE(queue.GetCommands().size() == 3U);
    for (const molga::RenderCommand& command : queue.GetCommands()) {
        CHECK(command.batchKey.isBatchable);
        CHECK(command.isBatchableSprite);
    }

    renderer.Shutdown();
}
