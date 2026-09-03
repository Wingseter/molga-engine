#include "Assets/FontArtifactStore.h"
#include "Assets/FontAsset.h"
#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Common/Sha256.h"
#include "Core/Importers/FontImporter.h"
#include "Core/PersistentStorage.h"
#include "Text/TextDiagnostic.h"
#include "doctest.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using molga::AssetRecord;
using molga::AssetRecordFromJson;
using molga::ImportResult;
using molga::text::StableTextDiagnosticCode;
using molga::text::TextDiagnosticCode;

namespace fs = std::filesystem;

namespace {

// ── Byte helpers ────────────────────────────────────────────────────────────

std::vector<std::uint8_t> ReadAllBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE_MESSAGE(input.good(), "could not open " << path.string());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

std::uint16_t BeU16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    REQUIRE(offset + 2U <= bytes.size());
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1U]);
}

std::int16_t BeS16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int16_t>(BeU16(bytes, offset));
}

// Unchecked reader for the whole-font checksum loops below, whose bounds this
// file constructs itself. Keeping a doctest assertion in a per-word loop would
// add millions of assertions to every run without checking anything new.
std::uint32_t BeU32Unchecked(const std::vector<std::uint8_t>& bytes,
                             std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::uint32_t BeU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    REQUIRE(offset + 4U <= bytes.size());
    return BeU32Unchecked(bytes, offset);
}

void PutU16(std::vector<std::uint8_t>& bytes, std::size_t offset,
            std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

void PutU32(std::vector<std::uint8_t>& bytes, std::size_t offset,
            std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
}

void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

// ── Step 1d/1g: RAII temporary SFNT fixtures ────────────────────────────────
// Every synthetic font below is written into a process-unique temporary file
// and removed by this destructor. The locked corpus is read-only provenance:
// no fixture ever edits a committed font in place.
class ScopedTempFont {
public:
    ScopedTempFont(const std::vector<std::uint8_t>& bytes,
                   const std::string& label) {
        static unsigned long long sequence = 0;
        path = (fs::temp_directory_path() /
                ("molga-font-fixture-" + label + "-" +
                 std::to_string(++sequence) + ".sfnt"))
                   .string();
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE_MESSAGE(output.good(), "could not create " << path);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        REQUIRE(fs::exists(path));
    }
    ScopedTempFont(const ScopedTempFont&) = delete;
    ScopedTempFont& operator=(const ScopedTempFont&) = delete;
    ~ScopedTempFont() {
        std::error_code error;
        fs::remove(path, error);
    }

    std::string path;
};

struct SfntTable {
    std::string tag;
    std::vector<std::uint8_t> data;
    std::uint32_t recordedChecksum = 0;
    std::uint32_t recordedOffset = 0;
};

// SFNT table checksum: big-endian uint32 sum over the table padded to a
// four-byte boundary. `head` is the documented exception — its
// checkSumAdjustment field is treated as zero, which is exactly why a table
// can be copied to a new file offset without invalidating its checksum.
std::uint32_t TableChecksum(const std::string& tag,
                            const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> padded = data;
    if (tag == "head" && padded.size() >= 12U) {
        padded[8] = padded[9] = padded[10] = padded[11] = 0U;
    }
    while (padded.size() % 4U != 0U) padded.push_back(0U);
    std::uint32_t sum = 0;
    for (std::size_t offset = 0; offset < padded.size(); offset += 4U) {
        sum += BeU32Unchecked(padded, offset);
    }
    return sum;
}

struct SfntFace {
    std::uint32_t sfntVersion = 0;
    std::vector<SfntTable> tables;
};

SfntFace ReadSfntFace(const fs::path& path) {
    const std::vector<std::uint8_t> bytes = ReadAllBytes(path);
    SfntFace face;
    face.sfntVersion = BeU32(bytes, 0);
    const std::uint16_t count = BeU16(bytes, 4);
    REQUIRE(count > 0U);
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::size_t record = 12U + static_cast<std::size_t>(index) * 16U;
        SfntTable table;
        table.tag.assign(reinterpret_cast<const char*>(bytes.data()) + record, 4U);
        table.recordedChecksum = BeU32(bytes, record + 4U);
        table.recordedOffset = BeU32(bytes, record + 8U);
        const std::uint32_t length = BeU32(bytes, record + 12U);
        REQUIRE(static_cast<std::size_t>(table.recordedOffset) + length <=
                bytes.size());
        table.data.assign(bytes.begin() + table.recordedOffset,
                          bytes.begin() + table.recordedOffset + length);
        face.tables.push_back(std::move(table));
    }
    return face;
}

// Rebuilds a complete, structurally valid single-face SFNT: sorted directory,
// four-byte aligned table data, recomputed per-table checksums, and a repaired
// whole-font checkSumAdjustment.
std::vector<std::uint8_t> BuildSfnt(SfntFace face) {
    std::sort(face.tables.begin(), face.tables.end(),
              [](const SfntTable& lhs, const SfntTable& rhs) {
                  return lhs.tag < rhs.tag;
              });
    const std::uint16_t count = static_cast<std::uint16_t>(face.tables.size());
    std::uint16_t entrySelector = 0;
    while ((1U << (entrySelector + 1U)) <= count) ++entrySelector;
    const std::uint16_t searchRange =
        static_cast<std::uint16_t>((1U << entrySelector) * 16U);

    std::vector<std::uint8_t> out;
    AppendU32(out, face.sfntVersion);
    AppendU16(out, count);
    AppendU16(out, searchRange);
    AppendU16(out, entrySelector);
    AppendU16(out, static_cast<std::uint16_t>(count * 16U - searchRange));
    const std::size_t directory = out.size();
    out.resize(directory + static_cast<std::size_t>(count) * 16U, 0U);

    std::size_t headRecord = 0;
    bool hasHead = false;
    for (std::size_t index = 0; index < face.tables.size(); ++index) {
        const SfntTable& table = face.tables[index];
        while (out.size() % 4U != 0U) out.push_back(0U);
        const std::size_t record = directory + index * 16U;
        std::memcpy(out.data() + record, table.tag.data(), 4U);
        PutU32(out, record + 4U, TableChecksum(table.tag, table.data));
        PutU32(out, record + 8U, static_cast<std::uint32_t>(out.size()));
        PutU32(out, record + 12U, static_cast<std::uint32_t>(table.data.size()));
        if (table.tag == "head") {
            headRecord = record;
            hasHead = true;
        }
        out.insert(out.end(), table.data.begin(), table.data.end());
    }
    while (out.size() % 4U != 0U) out.push_back(0U);

    if (hasHead) {
        const std::size_t headOffset = BeU32(out, headRecord + 8U);
        PutU32(out, headOffset + 8U, 0U);
        std::uint32_t whole = 0;
        for (std::size_t offset = 0; offset < out.size(); offset += 4U) {
            whole += BeU32Unchecked(out, offset);
        }
        PutU32(out, headOffset + 8U, 0xB1B0AFB0U - whole);
    }
    return out;
}

// Step 1d.
ScopedTempFont AddSfntTableDirectoryEntry(const fs::path& source,
                                          const std::string& tag) {
    SfntFace face = ReadSfntFace(source);
    SfntTable added;
    added.tag = tag;
    face.tables.push_back(std::move(added));
    return ScopedTempFont(BuildSfnt(std::move(face)), "table-" + tag);
}

// Step 1g. Both faces keep their original table bytes and their original
// checksums; only the directory offsets are rewritten to absolute TTC-file
// positions. `head`'s zeroed-adjustment checksum rule is what makes that
// relocation lossless.
ScopedTempFont BuildTwoFaceTtc(const fs::path& first, const fs::path& second) {
    const SfntFace faces[2] = {ReadSfntFace(first), ReadSfntFace(second)};

    std::vector<std::uint8_t> out;
    out.push_back('t'); out.push_back('t'); out.push_back('c'); out.push_back('f');
    AppendU32(out, 0x00010000U);
    AppendU32(out, 2U);
    const std::size_t offsetTable = out.size();
    AppendU32(out, 0U);
    AppendU32(out, 0U);

    for (std::size_t faceIndex = 0; faceIndex < 2U; ++faceIndex) {
        const SfntFace& face = faces[faceIndex];
        while (out.size() % 4U != 0U) out.push_back(0U);
        const std::size_t faceOffset = out.size();
        PutU32(out, offsetTable + faceIndex * 4U,
               static_cast<std::uint32_t>(faceOffset));

        const std::uint16_t count = static_cast<std::uint16_t>(face.tables.size());
        std::uint16_t entrySelector = 0;
        while ((1U << (entrySelector + 1U)) <= count) ++entrySelector;
        const std::uint16_t searchRange =
            static_cast<std::uint16_t>((1U << entrySelector) * 16U);
        AppendU32(out, face.sfntVersion);
        AppendU16(out, count);
        AppendU16(out, searchRange);
        AppendU16(out, entrySelector);
        AppendU16(out, static_cast<std::uint16_t>(count * 16U - searchRange));
        const std::size_t directory = out.size();
        out.resize(directory + static_cast<std::size_t>(count) * 16U, 0U);

        for (std::size_t index = 0; index < face.tables.size(); ++index) {
            const SfntTable& table = face.tables[index];
            while (out.size() % 4U != 0U) out.push_back(0U);
            const std::size_t record = directory + index * 16U;
            std::memcpy(out.data() + record, table.tag.data(), 4U);
            PutU32(out, record + 4U, table.recordedChecksum);
            PutU32(out, record + 8U, static_cast<std::uint32_t>(out.size()));
            PutU32(out, record + 12U,
                   static_cast<std::uint32_t>(table.data.size()));
            out.insert(out.end(), table.data.begin(), table.data.end());
        }
    }
    return ScopedTempFont(out, "ttc");
}

// ── Import settings and diagnostics ─────────────────────────────────────────

nlohmann::json ValidStaticFontSettings() {
    return nlohmann::json{
        {"faceIndex", 0}, {"weight", 400}, {"stretchPercent", 100},
        {"slant", "Upright"}, {"redistributableConfirmed", true},
        {"licenseKind", "OFL-1.1"},
        {"licenseAssetGuid", "88888888888888888888888888888888"}};
}

bool HasDiagnostic(const std::vector<molga::text::TextDiagnostic>& diagnostics,
                   TextDiagnosticCode code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const molga::text::TextDiagnostic& diagnostic) {
                           return diagnostic.code == code;
                       });
}

bool CoverageContains(const nlohmann::json& coverage, char32_t codepoint) {
    if (!coverage.is_array()) return false;
    const auto value = static_cast<std::uint32_t>(codepoint);
    for (const auto& range : coverage) {
        if (!range.is_array() || range.size() != 2U) return false;
        if (range[0].get<std::uint32_t>() <= value &&
            value <= range[1].get<std::uint32_t>()) {
            return true;
        }
    }
    return false;
}

AssetRecord RoundTripAssetRecord(const ImportResult& result) {
    AssetRecord record;
    record.guid = "12121212121212121212121212121212";
    record.sourcePath = "Assets/Fonts/round-trip.ttf";
    record.importer = "FontImporter";
    record.importerVersion = 2;
    molga::ApplyImportResultToRecord(result, record);

    std::string error;
    auto restored = molga::AssetRecordFromJson(
        molga::AssetRecordToJson(record), error);
    REQUIRE_MESSAGE(restored.has_value(), error);
    return *restored;
}

nlohmann::json ValidAssetRecordJson() {
    ImportResult failed;
    failed.importDiagnostics.push_back({
        TextDiagnosticCode::FontInvalid,
        molga::text::TextSeverity::Blocker, "font-import", "variable face",
        "use a static outline face", "", 0, "FontAsset", {12, 16}});
    AssetRecord record;
    record.guid = "13131313131313131313131313131313";
    record.sourcePath = "Assets/Fonts/canonical.ttf";
    record.importer = "FontImporter";
    record.importerVersion = 2;
    molga::ApplyImportResultToRecord(failed, record);
    return molga::AssetRecordToJson(record);
}

// ── Step 1h: independent design-metric reference parser ─────────────────────
// Reads only the four big-endian SFNT integers the importer is required to
// persist, so the assertion cannot be satisfied by echoing the importer.
struct ReferenceDesignMetrics {
    std::uint16_t unitsPerEm = 0;
    std::int16_t ascender = 0;
    std::int16_t descender = 0;
    std::int16_t lineGap = 0;
};

ReferenceDesignMetrics ParseReferenceDesignMetrics(const fs::path& path) {
    const std::vector<std::uint8_t> bytes = ReadAllBytes(path);
    const std::uint16_t count = BeU16(bytes, 4);
    std::size_t head = 0;
    std::size_t hhea = 0;
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::size_t record = 12U + static_cast<std::size_t>(index) * 16U;
        const std::string tag(
            reinterpret_cast<const char*>(bytes.data()) + record, 4U);
        if (tag == "head") head = BeU32(bytes, record + 8U);
        if (tag == "hhea") hhea = BeU32(bytes, record + 8U);
    }
    REQUIRE(head != 0U);
    REQUIRE(hhea != 0U);
    ReferenceDesignMetrics metrics;
    metrics.unitsPerEm = BeU16(bytes, head + 18U);
    metrics.ascender = BeS16(bytes, hhea + 4U);
    metrics.descender = BeS16(bytes, hhea + 6U);
    metrics.lineGap = BeS16(bytes, hhea + 8U);
    return metrics;
}

SfntTable* FindTable(SfntFace& face, const std::string& tag) {
    for (SfntTable& table : face.tables) {
        if (table.tag == tag) return &table;
    }
    return nullptr;
}

// ── Temporary project fixture ───────────────────────────────────────────────

class TempProject {
public:
    explicit TempProject(const std::string& label) {
        static unsigned long long sequence = 0;
        root = fs::temp_directory_path() /
               ("molga-font-project-" + label + "-" +
                std::to_string(++sequence));
        std::error_code error;
        fs::remove_all(root, error);
        assets = root / "Assets";
        fs::create_directories(assets);
    }
    TempProject(const TempProject&) = delete;
    TempProject& operator=(const TempProject&) = delete;
    ~TempProject() {
        std::error_code error;
        fs::remove_all(root, error);
    }

    fs::path root;
    fs::path assets;
};

void WriteFontMeta(const fs::path& source, const std::string& guid,
                   const nlohmann::json& settings) {
    const nlohmann::json meta = {{"guid", guid},
                                 {"importer", "FontImporter"},
                                 {"importerVersion", 2},
                                 {"settings", settings}};
    std::ofstream output(molga::AssetMeta::MetaPathFor(source));
    REQUIRE(output.good());
    output << meta.dump(2);
}

std::shared_ptr<const molga::FontArtifactStore> ProjectStore(
    const fs::path& projectRoot) {
    return std::make_shared<const molga::FontArtifactStore>(
        molga::FontArtifactStore::ForProject(projectRoot));
}

} // namespace

// ── Step 1 ──────────────────────────────────────────────────────────────────

TEST_CASE("font importer emits package-grade static face metadata") {
    nlohmann::json settings = {
        {"faceIndex", 0}, {"weight", 400}, {"stretchPercent", 100},
        {"slant", "Upright"}, {"redistributableConfirmed", true},
        {"licenseKind", "OFL-1.1"},
        {"licenseAssetGuid", "88888888888888888888888888888888"}};
    const auto result = molga::FontImporter().Import(
        MOLGA_TEXT_LATIN_FONT, settings);
    REQUIRE(result.success);
    CHECK(result.metadata["font"]["sourceSha256"] == MOLGA_TEXT_LATIN_SHA256);
    CHECK(result.metadata["font"]["staticOutline"] == true);
    CHECK(result.metadata["font"]["faceIndex"] == 0);
}

TEST_CASE("font importer records style, license and coverage identity") {
    auto settings = ValidStaticFontSettings();
    settings["weight"] = 700;
    settings["stretchPercent"] = 75;
    settings["slant"] = "Oblique";
    settings["copyright"] = "Copyright The Noto Project Authors";
    const auto result =
        molga::FontImporter().Import(MOLGA_TEXT_LATIN_FONT, settings);
    REQUIRE(result.success);
    CHECK(result.importDiagnostics.empty());
    const nlohmann::json& font = result.metadata["font"];
    CHECK(font["weight"] == 700);
    CHECK(font["stretchPercent"] == 75);
    CHECK(font["slant"] == "Oblique");
    CHECK(font["license"]["redistributableConfirmed"] == true);
    CHECK(font["license"]["licenseKind"] == "OFL-1.1");
    CHECK(font["license"]["copyright"] ==
          "Copyright The Noto Project Authors");
    CHECK(font["license"]["licenseAssetGuid"] ==
          "88888888888888888888888888888888");
    CHECK(CoverageContains(font["coverage"], U'A'));
    CHECK_FALSE(CoverageContains(font["coverage"], U'\U0001F680'));

    // Every authored field the design treats as closed rejects out-of-range
    // and unparsable values instead of silently clamping them.
    for (const char* field : {"weight", "stretchPercent"}) {
        auto tooSmall = ValidStaticFontSettings();
        tooSmall[field] = 0;
        CHECK_FALSE(molga::FontImporter()
                        .Import(MOLGA_TEXT_LATIN_FONT, tooSmall)
                        .success);
        auto tooLarge = ValidStaticFontSettings();
        tooLarge[field] = 100000;
        CHECK_FALSE(molga::FontImporter()
                        .Import(MOLGA_TEXT_LATIN_FONT, tooLarge)
                        .success);
    }
    auto unknownSlant = ValidStaticFontSettings();
    unknownSlant["slant"] = "Backslanted";
    CHECK_FALSE(
        molga::FontImporter().Import(MOLGA_TEXT_LATIN_FONT, unknownSlant).success);
    auto unconfirmed = ValidStaticFontSettings();
    unconfirmed["redistributableConfirmed"] = false;
    CHECK_FALSE(
        molga::FontImporter().Import(MOLGA_TEXT_LATIN_FONT, unconfirmed).success);
    auto noLicense = ValidStaticFontSettings();
    noLicense["licenseKind"] = "";
    CHECK_FALSE(
        molga::FontImporter().Import(MOLGA_TEXT_LATIN_FONT, noLicense).success);

    CHECK(molga::FontImporter().Version() == 2);
    CHECK(molga::FontImporter().Name() == "FontImporter");
}

TEST_CASE("partly authored font settings never fall back to the legacy path") {
    // No authored settings at all is the compatibility path Task 8.2/15.2
    // close: it validates structure only and publishes no font metadata, which
    // is what keeps unmigrated project fonts importable today.
    const auto noSettings = molga::FontImporter().Import(MOLGA_TEXT_LATIN_FONT);
    CHECK(noSettings.success);
    CHECK(noSettings.metadata.find("font") == noSettings.metadata.end());
    CHECK(noSettings.importDiagnostics.empty());
    const auto emptySettings = molga::FontImporter().Import(
        MOLGA_TEXT_LATIN_FONT, nlohmann::json::object());
    CHECK(emptySettings.success);
    CHECK(emptySettings.metadata.find("font") == emptySettings.metadata.end());
    // The variable fixture is only rejected once settings are authored; the
    // compatibility path deliberately still accepts it.
    CHECK(molga::FontImporter().Import(MOLGA_TEXT_VARIABLE_FONT).success);

    // Removing one authored field must not disable the static-only and license
    // contract for the whole asset.
    for (const char* dropped : {"redistributableConfirmed", "licenseKind",
                                "faceIndex", "slant"}) {
        auto partial = ValidStaticFontSettings();
        partial.erase(dropped);
        const auto result =
            molga::FontImporter().Import(MOLGA_TEXT_LATIN_FONT, partial);
        CHECK_MESSAGE(!result.success, "accepted settings without " << dropped);
        CHECK(HasDiagnostic(result.importDiagnostics,
                            TextDiagnosticCode::FontInvalid));
        CHECK(result.metadata.find("font") == result.metadata.end());
    }

    // The compatibility path still refuses a file that is not a font.
    const ScopedTempFont garbage(
        std::vector<std::uint8_t>{'n', 'o', 't', ' ', 'a', ' ', 'f', 'o',
                                  'n', 't', '!', '!', 0, 0, 0, 0},
        "legacy-garbage");
    CHECK_FALSE(molga::FontImporter().Import(garbage.path).success);
}

// ── Step 1a ─────────────────────────────────────────────────────────────────

TEST_CASE("typed import diagnostics survive catalog reload") {
    ImportResult failed;
    failed.importDiagnostics.push_back({
        molga::text::TextDiagnosticCode::FontInvalid,
        molga::text::TextSeverity::Blocker, "font-import", "variable face",
        "use a static outline face", "font-guid", 0, "FontAsset", {12, 16}});
    const AssetRecord restored = RoundTripAssetRecord(failed);
    REQUIRE(restored.importDiagnostics.size() == 1);
    CHECK(std::string_view(StableTextDiagnosticCode(
              restored.importDiagnostics[0].code)) == "TEXT_FONT_INVALID");
    CHECK(restored.importDiagnostics[0].sourceByteRange.begin == 12);
    CHECK(restored.importDiagnostics[0].remediation == "use a static outline face");
}

TEST_CASE("catalog round trip preserves every diagnostic field") {
    ImportResult failed;
    failed.importDiagnostics.push_back({
        TextDiagnosticCode::MissingGlyph, molga::text::TextSeverity::Warning,
        "font-family", "no face covers U+0633", "add an Arabic fallback",
        "", 4242U, "UILabel", {7, 11}});
    const AssetRecord restored = RoundTripAssetRecord(failed);
    REQUIRE(restored.importDiagnostics.size() == 1);
    const molga::text::TextDiagnostic& diagnostic = restored.importDiagnostics[0];
    CHECK(diagnostic.code == TextDiagnosticCode::MissingGlyph);
    CHECK(diagnostic.severity == molga::text::TextSeverity::Warning);
    CHECK(diagnostic.subsystem == "font-family");
    CHECK(diagnostic.message == "no face covers U+0633");
    CHECK(diagnostic.remediation == "add an Arabic fallback");
    CHECK(diagnostic.sceneObjectId == 4242U);
    CHECK(diagnostic.componentType == "UILabel");
    CHECK(diagnostic.sourceByteRange == molga::text::SourceByteRange{7, 11});
    // Step 3c: an empty diagnostic GUID is filled from the record, and the
    // legacy human-readable field keeps carrying a summary.
    CHECK(diagnostic.assetGuid == "12121212121212121212121212121212");
    CHECK(restored.importError.find("TEXT_MISSING_GLYPH") != std::string::npos);
}

// ── Step 1c ─────────────────────────────────────────────────────────────────

TEST_CASE("unknown persisted diagnostic code fails catalog load closed") {
    auto json = ValidAssetRecordJson();
    json["importDiagnostics"][0]["code"] = "TEXT_UNKNOWN_FROM_FUTURE";
    std::string error;
    CHECK_FALSE(AssetRecordFromJson(json, error));
    CHECK(error.find("unknown diagnostic code") != std::string::npos);
}

TEST_CASE("a canonical asset record with diagnostics parses and numeric codes do not") {
    std::string error;
    const auto accepted = molga::AssetRecordFromJson(ValidAssetRecordJson(), error);
    REQUIRE_MESSAGE(accepted.has_value(), error);
    CHECK(accepted->importDiagnostics.size() == 1);
    CHECK(accepted->importDiagnostics[0].code == TextDiagnosticCode::FontInvalid);

    // A numeric enum value is exactly the aliasing hazard fail-closed parsing
    // exists to stop: it would silently rename a code on the next reorder.
    auto numeric = ValidAssetRecordJson();
    numeric["importDiagnostics"][0]["code"] = 2;
    CHECK_FALSE(molga::AssetRecordFromJson(numeric, error));
    CHECK(error.find("unknown diagnostic code") != std::string::npos);

    auto badSeverity = ValidAssetRecordJson();
    badSeverity["importDiagnostics"][0]["severity"] = "Catastrophic";
    CHECK_FALSE(molga::AssetRecordFromJson(badSeverity, error));
}

// ── Step 1b ─────────────────────────────────────────────────────────────────

TEST_CASE("font importer rejects invalid face variable and color tables") {
    const auto base = ValidStaticFontSettings();
    auto wrongFace = base;
    wrongFace["faceIndex"] = 1;
    CHECK_FALSE(molga::FontImporter().Import(
        MOLGA_TEXT_LATIN_FONT, wrongFace).success);
    const auto variable = molga::FontImporter().Import(
        MOLGA_TEXT_VARIABLE_FONT, base);
    CHECK_FALSE(variable.success);
    CHECK(HasDiagnostic(variable.importDiagnostics,
          molga::text::TextDiagnosticCode::FontInvalid));
    const ScopedTempFont color = AddSfntTableDirectoryEntry(
        MOLGA_TEXT_LATIN_FONT, "COLR");
    const auto colorResult = molga::FontImporter().Import(color.path, base);
    CHECK_FALSE(colorResult.success);
    CHECK(HasDiagnostic(colorResult.importDiagnostics,
          molga::text::TextDiagnosticCode::FontInvalid));
}

TEST_CASE("every rejected variable and color table is refused independently") {
    const auto base = ValidStaticFontSettings();
    // The rebuilt fixture without an added table must still import, or the
    // rejections below would prove nothing about the tags themselves.
    const ScopedTempFont control =
        ScopedTempFont(BuildSfnt(ReadSfntFace(MOLGA_TEXT_LATIN_FONT)), "control");
    REQUIRE(molga::FontImporter().Import(control.path, base).success);

    for (const char* tag : {"fvar", "gvar", "CFF2", "COLR", "CPAL", "CBDT",
                            "CBLC", "sbix", "SVG "}) {
        const ScopedTempFont rejected =
            AddSfntTableDirectoryEntry(MOLGA_TEXT_LATIN_FONT, tag);
        const auto result = molga::FontImporter().Import(rejected.path, base);
        CHECK_MESSAGE(!result.success, "accepted a face carrying " << tag);
        CHECK(HasDiagnostic(result.importDiagnostics,
                            TextDiagnosticCode::FontInvalid));
    }
}

TEST_CASE("font importer refuses corrupt containers and broken table ranges") {
    const auto base = ValidStaticFontSettings();
    const ScopedTempFont garbage(
        std::vector<std::uint8_t>{'n', 'o', 't', ' ', 'a', ' ', 'f', 'o',
                                  'n', 't', '!', '!', 0, 0, 0, 0},
        "garbage");
    const auto rubbish = molga::FontImporter().Import(garbage.path, base);
    CHECK_FALSE(rubbish.success);
    CHECK(HasDiagnostic(rubbish.importDiagnostics,
                        TextDiagnosticCode::FontInvalid));

    // A table whose recorded length runs past the end of the file.
    std::vector<std::uint8_t> outOfFile =
        BuildSfnt(ReadSfntFace(MOLGA_TEXT_LATIN_FONT));
    PutU32(outOfFile, 12U + 12U,
           static_cast<std::uint32_t>(outOfFile.size() + 1024U));
    const ScopedTempFont truncated(outOfFile, "out-of-file");
    const auto truncatedResult =
        molga::FontImporter().Import(truncated.path, base);
    CHECK_FALSE(truncatedResult.success);
    CHECK(HasDiagnostic(truncatedResult.importDiagnostics,
                        TextDiagnosticCode::FontInvalid));

    // A table whose bytes no longer match its recorded checksum.
    SfntFace mutated = ReadSfntFace(MOLGA_TEXT_LATIN_FONT);
    SfntTable* maxp = FindTable(mutated, "maxp");
    REQUIRE(maxp != nullptr);
    std::vector<std::uint8_t> corruptChecksum = BuildSfnt(mutated);
    for (std::size_t index = 0; index + 16U <= corruptChecksum.size(); index += 16U) {
        if (std::memcmp(corruptChecksum.data() + 12U + index, "maxp", 4U) == 0) {
            PutU32(corruptChecksum, 12U + index + 4U, 0xDEADBEEFU);
            break;
        }
    }
    const ScopedTempFont badChecksum(corruptChecksum, "checksum");
    const auto checksumResult =
        molga::FontImporter().Import(badChecksum.path, base);
    CHECK_FALSE(checksumResult.success);
    CHECK(HasDiagnostic(checksumResult.importDiagnostics,
                        TextDiagnosticCode::FontInvalid));
}

// ── Step 1f ─────────────────────────────────────────────────────────────────

TEST_CASE("TTC imports the authored static face and rejects out of range") {
    const ScopedTempFont ttc = BuildTwoFaceTtc(
        MOLGA_TEXT_LATIN_FONT, MOLGA_TEXT_ARABIC_FONT);
    auto secondSettings = ValidStaticFontSettings();
    secondSettings["faceIndex"] = 1;
    const auto second = molga::FontImporter().Import(ttc.path, secondSettings);
    REQUIRE(second.success);
    CHECK(second.metadata["font"]["faceIndex"] == 1);
    CHECK(CoverageContains(second.metadata["font"]["coverage"], U'س'));
    secondSettings["faceIndex"] = 2;
    const auto outside =
        molga::FontImporter().Import(ttc.path, secondSettings);
    CHECK_FALSE(outside.success);
    CHECK(HasDiagnostic(outside.importDiagnostics,
          molga::text::TextDiagnosticCode::FontInvalid));
}

TEST_CASE("TTC face zero is the Latin face and never the collection default") {
    const ScopedTempFont ttc = BuildTwoFaceTtc(
        MOLGA_TEXT_LATIN_FONT, MOLGA_TEXT_ARABIC_FONT);
    const auto first =
        molga::FontImporter().Import(ttc.path, ValidStaticFontSettings());
    REQUIRE(first.success);
    CHECK(first.metadata["font"]["faceIndex"] == 0);
    CHECK(CoverageContains(first.metadata["font"]["coverage"], U'A'));
    CHECK_FALSE(CoverageContains(first.metadata["font"]["coverage"], U'س'));
    // The two faces have different design metrics, so a resolver that always
    // read face zero could not produce the Arabic numbers.
    const auto latin = ParseReferenceDesignMetrics(MOLGA_TEXT_LATIN_FONT);
    const auto arabic = ParseReferenceDesignMetrics(MOLGA_TEXT_ARABIC_FONT);
    REQUIRE(latin.ascender != arabic.ascender);
    CHECK(first.metadata["font"]["designMetrics"]["ascender"] == latin.ascender);

    auto secondSettings = ValidStaticFontSettings();
    secondSettings["faceIndex"] = 1;
    const auto second = molga::FontImporter().Import(ttc.path, secondSettings);
    REQUIRE(second.success);
    CHECK(second.metadata["font"]["designMetrics"]["ascender"] == arabic.ascender);

    // A plain (non-collection) file only ever has face zero.
    auto nonCollection = ValidStaticFontSettings();
    nonCollection["faceIndex"] = 1;
    CHECK_FALSE(molga::FontImporter()
                    .Import(MOLGA_TEXT_ARABIC_FONT, nonCollection)
                    .success);
}

// ── Step 1h ─────────────────────────────────────────────────────────────────

TEST_CASE("imported design metrics are the exact signed SFNT integers") {
    const auto expected = ParseReferenceDesignMetrics(MOLGA_TEXT_LATIN_FONT);
    const auto result = molga::FontImporter().Import(
        MOLGA_TEXT_LATIN_FONT, ValidStaticFontSettings());
    REQUIRE(result.success);
    const nlohmann::json& metrics = result.metadata["font"]["designMetrics"];
    REQUIRE(metrics.is_object());
    for (const char* field : {"unitsPerEm", "ascender", "descender", "lineGap"}) {
        CHECK_MESSAGE(metrics[field].is_number_integer(),
                      "canonical metric " << field << " must stay an integer");
        CHECK_FALSE(metrics[field].is_number_float());
    }
    CHECK(metrics["unitsPerEm"] == expected.unitsPerEm);
    CHECK(metrics["ascender"] == expected.ascender);
    CHECK(metrics["descender"] == expected.descender);
    CHECK(metrics["lineGap"] == expected.lineGap);

    const AssetRecord restored = RoundTripAssetRecord(result);
    const nlohmann::json& reloaded = restored.metadata["font"]["designMetrics"];
    CHECK(reloaded["unitsPerEm"].get<std::int64_t>() == expected.unitsPerEm);
    CHECK(reloaded["ascender"].get<std::int64_t>() == expected.ascender);
    CHECK(reloaded["descender"].get<std::int64_t>() == expected.descender);
    CHECK(reloaded["lineGap"].get<std::int64_t>() == expected.lineGap);
    CHECK(reloaded["unitsPerEm"].is_number_integer());
    for (auto it = reloaded.begin(); it != reloaded.end(); ++it) {
        CHECK_FALSE(it.value().is_number_float());
    }
}

// ── Step 1i ─────────────────────────────────────────────────────────────────

TEST_CASE("malformed design metrics are refused before any publication") {
    const auto base = ValidStaticFontSettings();

    struct Mutation {
        const char* label;
        const char* table;
        std::size_t offset;
        std::uint16_t value;
    };
    const Mutation mutations[] = {
        {"unitsPerEm=0", "head", 18U, 0U},
        {"unitsPerEm=16385", "head", 18U, 16385U},
        {"ascender=0", "hhea", 4U, 0U},
        {"descender=1", "hhea", 6U, 1U},
        {"lineGap=-1", "hhea", 8U, 0xFFFFU},
    };
    for (const Mutation& mutation : mutations) {
        SfntFace face = ReadSfntFace(MOLGA_TEXT_LATIN_FONT);
        SfntTable* table = FindTable(face, mutation.table);
        REQUIRE(table != nullptr);
        PutU16(table->data, mutation.offset, mutation.value);
        const ScopedTempFont broken(BuildSfnt(std::move(face)), "metrics");
        const auto result = molga::FontImporter().Import(broken.path, base);
        CHECK_MESSAGE(!result.success, "accepted " << mutation.label);
        CHECK(HasDiagnostic(result.importDiagnostics,
                            TextDiagnosticCode::FontInvalid));
        CHECK(result.metadata.find("font") == result.metadata.end());
        CHECK(result.artifactPath.empty());
    }

    {   // ascender <= descender with both individually in range.
        SfntFace face = ReadSfntFace(MOLGA_TEXT_LATIN_FONT);
        SfntTable* hhea = FindTable(face, "hhea");
        REQUIRE(hhea != nullptr);
        PutU16(hhea->data, 4U, 100U);      // ascender = 100
        PutU16(hhea->data, 6U, 0xFF9CU);   // descender = -100
        const ScopedTempFont ordered(BuildSfnt(std::move(face)), "order-ok");
        CHECK(molga::FontImporter().Import(ordered.path, base).success);
    }
    {
        SfntFace face = ReadSfntFace(MOLGA_TEXT_LATIN_FONT);
        SfntTable* hhea = FindTable(face, "hhea");
        REQUIRE(hhea != nullptr);
        PutU16(hhea->data, 4U, 100U);      // ascender = 100
        PutU16(hhea->data, 6U, 0U);        // descender = 0 → ascender > descender
        PutU16(hhea->data, 8U, 0U);
        const ScopedTempFont equal(BuildSfnt(std::move(face)), "order-equal");
        CHECK(molga::FontImporter().Import(equal.path, base).success);
    }

    for (const char* table : {"head", "hhea"}) {
        SfntFace face = ReadSfntFace(MOLGA_TEXT_LATIN_FONT);
        SfntTable* target = FindTable(face, table);
        REQUIRE(target != nullptr);
        target->data.resize(4U);   // truncates every metric field
        const ScopedTempFont truncated(BuildSfnt(std::move(face)), "truncated");
        const auto result = molga::FontImporter().Import(truncated.path, base);
        CHECK_MESSAGE(!result.success, "accepted a truncated " << table);
        CHECK(HasDiagnostic(result.importDiagnostics,
                            TextDiagnosticCode::FontInvalid));
    }

    // A rejected import must leave neither a catalog generation nor an artifact.
    TempProject project("metrics-reject");
    SfntFace face = ReadSfntFace(MOLGA_TEXT_LATIN_FONT);
    SfntTable* head = FindTable(face, "head");
    REQUIRE(head != nullptr);
    PutU16(head->data, 18U, 0U);
    const std::vector<std::uint8_t> brokenBytes = BuildSfnt(std::move(face));
    const fs::path source = project.assets / "broken.ttf";
    {
        std::ofstream output(source, std::ios::binary);
        output.write(reinterpret_cast<const char*>(brokenBytes.data()),
                     static_cast<std::streamsize>(brokenBytes.size()));
    }
    const std::string guid = "0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f";
    WriteFontMeta(source, guid, base);

    molga::AssetDatabase database;
    REQUIRE(database.BindFontArtifactStore(ProjectStore(project.root)));
    database.ScanProject(project.assets);
    const AssetRecord* record = database.Find(guid);
    REQUIRE(record != nullptr);
    CHECK(record->importFailed);
    CHECK_FALSE(record->fontArtifact.has_value());
    CHECK(HasDiagnostic(record->importDiagnostics,
                        TextDiagnosticCode::FontInvalid));
    CHECK(database.ContentGeneration() == 0U);
    CHECK_FALSE(fs::exists(project.root / "Library" / "Imported" / "Fonts"));
}

// ── Step 1j ─────────────────────────────────────────────────────────────────

TEST_CASE("a successful font import publishes one immutable content-addressed artifact") {
    TempProject project("artifact");
    fs::create_directories(project.assets / "Fonts");
    const fs::path source = project.assets / "Fonts" / "Latin.ttf";
    fs::copy_file(MOLGA_TEXT_LATIN_FONT, source);
    const std::string guid = "0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a";
    WriteFontMeta(source, guid, ValidStaticFontSettings());

    const auto store = ProjectStore(project.root);
    molga::AssetDatabase database;
    std::string bindError;
    REQUIRE_MESSAGE(database.BindFontArtifactStore(store, &bindError), bindError);
    // The binding is immutable for the database's lifetime.
    CHECK_FALSE(database.BindFontArtifactStore(ProjectStore(project.root)));
    CHECK_FALSE(database.BindFontArtifactStore(nullptr));
    CHECK(database.FontArtifacts() == store.get());

    database.ScanProject(project.assets);
    const AssetRecord* record = database.Find(guid);
    REQUIRE(record != nullptr);
    REQUIRE_MESSAGE(!record->importFailed, record->importError);
    REQUIRE(record->fontArtifact.has_value());
    CHECK(record->fontArtifact->locator.storage ==
          molga::FontArtifactStorage::ProjectLibrary);
    CHECK(record->fontArtifact->locator.relativePath.generic_string() ==
          "Library/Imported/Fonts/" MOLGA_TEXT_LATIN_SHA256 ".sfnt");
    CHECK(record->fontArtifact->sourceSha256 == MOLGA_TEXT_LATIN_SHA256);
    CHECK(record->fontArtifact->artifactSha256 ==
          record->fontArtifact->sourceSha256);
    CHECK(record->fontArtifact->byteSize == fs::file_size(source));
    CHECK(database.ContentGeneration() == 1U);

    const fs::path published =
        project.root / record->fontArtifact->locator.relativePath;
    REQUIRE(fs::exists(published));
    CHECK(ReadAllBytes(published) == ReadAllBytes(MOLGA_TEXT_LATIN_FONT));
    // No temporary or journal sibling survives a successful publish.
    for (const auto& entry : fs::directory_iterator(published.parent_path())) {
        CHECK(entry.path() == published);
    }

    const auto firstWrite = fs::last_write_time(published);
    molga::AssetDatabase second;
    REQUIRE(second.BindFontArtifactStore(store));
    second.ScanProject(project.assets);
    const AssetRecord* again = second.Find(guid);
    REQUIRE(again != nullptr);
    REQUIRE(again->fontArtifact.has_value());
    CHECK(again->fontArtifact->locator == record->fontArtifact->locator);
    CHECK(again->fontArtifact->artifactSha256 ==
          record->fontArtifact->artifactSha256);
    CHECK(again->fontArtifact->byteSize == record->fontArtifact->byteSize);
    CHECK(fs::last_write_time(published) == firstWrite);

    molga::text::VectorTextDiagnosticSink sink;
    const auto bytes = store->ReadVerified(*record->fontArtifact, sink);
    REQUIRE(bytes.has_value());
    REQUIRE(*bytes != nullptr);
    CHECK((*bytes)->size() == record->fontArtifact->byteSize);
    CHECK(sink.Diagnostics().empty());

    molga::VerifiedFontArtifact crossStorage = *record->fontArtifact;
    crossStorage.locator.storage = molga::FontArtifactStorage::PackagedResource;
    CHECK_FALSE(store->ReadVerified(crossStorage, sink).has_value());
    molga::VerifiedFontArtifact wrongSize = *record->fontArtifact;
    wrongSize.byteSize += 1U;
    CHECK_FALSE(store->ReadVerified(wrongSize, sink).has_value());
    molga::VerifiedFontArtifact escaping = *record->fontArtifact;
    escaping.locator.relativePath = "../escape.sfnt";
    CHECK_FALSE(store->ReadVerified(escaping, sink).has_value());
    CHECK(HasDiagnostic(sink.Diagnostics(), TextDiagnosticCode::FontInvalid));

    CHECK(store->IsProjectAuthorityFor(project.root));
    CHECK_FALSE(store->IsProjectAuthorityFor(project.assets));

    std::string assetError;
    const auto asset = molga::FontAsset::FromRecord(*record, assetError);
    REQUIRE_MESSAGE(asset.has_value(), assetError);
    CHECK(asset->guid == guid);
    CHECK(asset->sourceSha256 == MOLGA_TEXT_LATIN_SHA256);
    CHECK(asset->artifactSha256 == MOLGA_TEXT_LATIN_SHA256);
    CHECK(asset->artifactByteSize == record->fontArtifact->byteSize);
    CHECK(asset->faceIndex == 0U);
    CHECK(asset->weight == 400U);
    CHECK(asset->stretchPercent == 100U);
    CHECK(asset->slant == molga::FontSlant::Upright);
    CHECK(asset->contentRevision == 1U);
    CHECK(asset->designMetrics.unitsPerEm ==
          ParseReferenceDesignMetrics(MOLGA_TEXT_LATIN_FONT).unitsPerEm);
    CHECK(asset->license.redistributableConfirmed);
    CHECK(asset->license.licenseKind == "OFL-1.1");
    CHECK_FALSE(asset->coverage.empty());

    AssetRecord withoutArtifact = *record;
    withoutArtifact.fontArtifact.reset();
    CHECK_FALSE(molga::FontAsset::FromRecord(withoutArtifact, assetError));
    CHECK_FALSE(assetError.empty());
    AssetRecord failedRecord = *record;
    failedRecord.importFailed = true;
    CHECK_FALSE(molga::FontAsset::FromRecord(failedRecord, assetError));
}

TEST_CASE("a font import without a bound artifact store is refused") {
    TempProject project("unbound");
    const fs::path source = project.assets / "Latin.ttf";
    fs::copy_file(MOLGA_TEXT_LATIN_FONT, source);
    WriteFontMeta(source, "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
                  ValidStaticFontSettings());

    molga::AssetDatabase unbound;
    CHECK(unbound.FontArtifacts() == nullptr);
    unbound.ScanProject(project.assets);
    CHECK(unbound.RecordCount() == 0U);
    CHECK_FALSE(fs::exists(project.root / "Library"));
}

// ── Step 7g ─────────────────────────────────────────────────────────────────

TEST_CASE("a sealed package store reads only manifest-authorized artifacts") {
    TempProject project("sealed");
    const fs::path resourceRoot = project.root / "Resources";
    fs::create_directories(resourceRoot / "Assets" / "Fonts");
    const fs::path packaged = resourceRoot / "Assets" / "Fonts" / "Latin.sfnt";
    fs::copy_file(MOLGA_TEXT_LATIN_FONT, packaged);

    molga::text::VectorTextDiagnosticSink sink;
    const auto store = molga::FontArtifactStore::ForSealedPackage(
        resourceRoot,
        {{"Assets/Fonts/Latin.sfnt", MOLGA_TEXT_LATIN_SHA256}}, sink);
    REQUIRE(store.has_value());
    CHECK(sink.Diagnostics().empty());
    CHECK_FALSE(store->IsProjectAuthorityFor(resourceRoot));

    molga::VerifiedFontArtifact artifact;
    artifact.locator.storage = molga::FontArtifactStorage::PackagedResource;
    artifact.locator.relativePath = "Assets/Fonts/Latin.sfnt";
    artifact.sourceSha256 = MOLGA_TEXT_LATIN_SHA256;
    artifact.artifactSha256 = MOLGA_TEXT_LATIN_SHA256;
    artifact.byteSize = fs::file_size(packaged);

    const auto bytes = store->ReadVerified(artifact, sink);
    REQUIRE(bytes.has_value());
    CHECK((*bytes)->size() == artifact.byteSize);
    CHECK(sink.Diagnostics().empty());

    molga::VerifiedFontArtifact unauthorized = artifact;
    unauthorized.locator.relativePath = "Assets/Fonts/Other.sfnt";
    CHECK_FALSE(store->ReadVerified(unauthorized, sink).has_value());
    molga::VerifiedFontArtifact wrongSha = artifact;
    wrongSha.artifactSha256 = std::string(64U, 'a');
    CHECK_FALSE(store->ReadVerified(wrongSha, sink).has_value());
    molga::VerifiedFontArtifact crossStorage = artifact;
    crossStorage.locator.storage = molga::FontArtifactStorage::ProjectLibrary;
    CHECK_FALSE(store->ReadVerified(crossStorage, sink).has_value());
    molga::VerifiedFontArtifact escaping = artifact;
    escaping.locator.relativePath = "../Latin.sfnt";
    CHECK_FALSE(store->ReadVerified(escaping, sink).has_value());
    CHECK(HasDiagnostic(sink.Diagnostics(), TextDiagnosticCode::FontInvalid));

    // A sealed store is never an authoring publisher.
    CHECK_FALSE(store->Publish(packaged, MOLGA_TEXT_LATIN_SHA256, sink).has_value());

    // The manifest authority itself is validated before the store exists.
    molga::text::VectorTextDiagnosticSink manifestSink;
    CHECK_FALSE(molga::FontArtifactStore::ForSealedPackage(
        resourceRoot, {{"Library/Imported/Fonts/x.sfnt", MOLGA_TEXT_LATIN_SHA256}},
        manifestSink).has_value());
    CHECK_FALSE(molga::FontArtifactStore::ForSealedPackage(
        resourceRoot, {{"../outside.sfnt", MOLGA_TEXT_LATIN_SHA256}},
        manifestSink).has_value());
    CHECK_FALSE(molga::FontArtifactStore::ForSealedPackage(
        resourceRoot, {{"Assets/Fonts/Latin.sfnt", "NOT-A-SHA"}},
        manifestSink).has_value());
    CHECK(HasDiagnostic(manifestSink.Diagnostics(),
                        TextDiagnosticCode::FontInvalid));
}

// ── Step 7b ─────────────────────────────────────────────────────────────────

TEST_CASE("AtomicPublishImmutableBytes never replaces a published artifact") {
    TempProject project("immutable-publish");
    const std::vector<std::uint8_t> bytes{'i', 'm', 'm', 'u', 't', 'a', 'b',
                                          'l', 'e'};
    const std::string sha = molga::Sha256Bytes(bytes.data(), bytes.size());
    const fs::path destination =
        project.root / "Library" / "Imported" / "Fonts" / (sha + ".sfnt");

    std::string error;
    REQUIRE_MESSAGE(PersistentStorage::AtomicPublishImmutableBytes(
                        destination, bytes, sha, &error), error);
    CHECK(error.empty());
    CHECK(ReadAllBytes(destination) == bytes);
    // The parent directory holds the artifact and nothing else: no temporary
    // and no lock survive a successful publish.
    for (const auto& entry : fs::directory_iterator(destination.parent_path())) {
        CHECK(entry.path() == destination);
    }

    // Republishing identical bytes is accepted and does not rewrite the file.
    const auto firstWrite = fs::last_write_time(destination);
    CHECK(PersistentStorage::AtomicPublishImmutableBytes(destination, bytes,
                                                         sha, &error));
    CHECK(fs::last_write_time(destination) == firstWrite);

    // Different bytes at the same destination are refused, and the published
    // artifact is left exactly as it was.
    const std::vector<std::uint8_t> other{'d', 'i', 'f', 'f', 'e', 'r', 'e',
                                          'n', 't'};
    const std::string otherSha =
        molga::Sha256Bytes(other.data(), other.size());
    CHECK_FALSE(PersistentStorage::AtomicPublishImmutableBytes(
        destination, other, otherSha, &error));
    CHECK_FALSE(error.empty());
    CHECK(ReadAllBytes(destination) == bytes);
    CHECK(fs::last_write_time(destination) == firstWrite);

    // A digest that does not describe the bytes is refused before any file
    // appears on disk.
    const fs::path fresh = destination.parent_path() / "unwritten.sfnt";
    CHECK_FALSE(PersistentStorage::AtomicPublishImmutableBytes(
        fresh, bytes, otherSha, &error));
    CHECK_FALSE(fs::exists(fresh));
    CHECK_FALSE(PersistentStorage::AtomicPublishImmutableBytes(
        fresh, {}, molga::Sha256Bytes("", 0U), &error));
    CHECK_FALSE(fs::exists(fresh));
}

// ── Diagnostic bound ────────────────────────────────────────────────────────

TEST_CASE("A record carries a bounded number of import diagnostics") {
    ImportResult noisy;
    for (std::size_t index = 0;
         index < molga::kMaxImportDiagnosticsPerRecord * 3U; ++index) {
        noisy.importDiagnostics.push_back({
            TextDiagnosticCode::FontInvalid, molga::text::TextSeverity::Error,
            "font-import", "failure " + std::to_string(index), "reimport", "",
            0, "FontAsset", {static_cast<std::uint32_t>(index),
                            static_cast<std::uint32_t>(index) + 1U}});
    }
    AssetRecord record;
    record.guid = "0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e0e";
    record.sourcePath = "Assets/Fonts/noisy.ttf";
    record.importer = "FontImporter";
    molga::ApplyImportResultToRecord(noisy, record);
    CHECK(record.importDiagnostics.size() ==
          molga::kMaxImportDiagnosticsPerRecord);
    CHECK(record.importDiagnostics.front().message == "failure 0");

    // A catalog written by some other tool cannot smuggle an unbounded array
    // back in either.
    nlohmann::json overBound = molga::AssetRecordToJson(record);
    overBound["importDiagnostics"].push_back(
        overBound["importDiagnostics"][0]);
    std::string error;
    CHECK_FALSE(molga::AssetRecordFromJson(overBound, error));
    CHECK_FALSE(error.empty());
    // The bounded record itself still round-trips.
    std::string boundedError;
    CHECK(molga::AssetRecordFromJson(molga::AssetRecordToJson(record),
                                     boundedError)
              .has_value());
}

// ── Step 4b ─────────────────────────────────────────────────────────────────

TEST_CASE("a project catalog record accepts only its own content-addressed locator") {
    ImportResult imported;
    imported.success = true;
    AssetRecord record;
    record.guid = "0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c";
    record.sourcePath = "Assets/Fonts/Latin.ttf";
    record.importer = "FontImporter";
    record.importerVersion = 2;
    molga::ApplyImportResultToRecord(imported, record);
    molga::VerifiedFontArtifact artifact;
    artifact.locator.storage = molga::FontArtifactStorage::ProjectLibrary;
    artifact.locator.relativePath =
        "Library/Imported/Fonts/" MOLGA_TEXT_LATIN_SHA256 ".sfnt";
    artifact.sourceSha256 = MOLGA_TEXT_LATIN_SHA256;
    artifact.artifactSha256 = MOLGA_TEXT_LATIN_SHA256;
    artifact.byteSize = 569208U;
    record.fontArtifact = artifact;

    const nlohmann::json canonical = molga::AssetRecordToJson(record);
    CHECK(canonical["artifactStorage"] == "ProjectLibrary");
    CHECK(canonical["artifactRelativePath"] ==
          "Library/Imported/Fonts/" MOLGA_TEXT_LATIN_SHA256 ".sfnt");
    CHECK(canonical["artifactByteSize"] == 569208U);

    std::string error;
    const auto restored = molga::AssetRecordFromJson(canonical, error);
    REQUIRE_MESSAGE(restored.has_value(), error);
    REQUIRE(restored->fontArtifact.has_value());
    CHECK(restored->fontArtifact->locator == artifact.locator);
    CHECK(restored->fontArtifact->byteSize == artifact.byteSize);

    const auto rejects = [&](nlohmann::json broken) {
        std::string rejectError;
        const bool refused =
            !molga::AssetRecordFromJson(broken, rejectError).has_value();
        CHECK_FALSE(rejectError.empty());
        return refused;
    };
    auto packaged = canonical;
    packaged["artifactStorage"] = "PackagedResource";
    CHECK(rejects(packaged));
    auto absolute = canonical;
    absolute["artifactRelativePath"] = "/etc/passwd";
    CHECK(rejects(absolute));
    auto escaping = canonical;
    escaping["artifactRelativePath"] =
        "Library/Imported/Fonts/../../../" MOLGA_TEXT_LATIN_SHA256 ".sfnt";
    CHECK(rejects(escaping));
    auto renamed = canonical;
    renamed["artifactRelativePath"] = "Library/Imported/Fonts/other.sfnt";
    CHECK(rejects(renamed));
    auto uppercase = canonical;
    uppercase["artifactSha256"] = std::string(64U, 'A');
    CHECK(rejects(uppercase));
    auto zeroSize = canonical;
    zeroSize["artifactByteSize"] = 0;
    CHECK(rejects(zeroSize));
    auto mismatched = canonical;
    mismatched["sourceSha256"] = std::string(64U, 'b');
    CHECK(rejects(mismatched));

    // A record with no font fields at all stays a perfectly valid record.
    auto plain = canonical;
    for (const char* field : {"sourceSha256", "artifactStorage",
                              "artifactRelativePath", "artifactSha256",
                              "artifactByteSize"}) {
        plain.erase(field);
    }
    std::string plainError;
    const auto plainRecord = molga::AssetRecordFromJson(plain, plainError);
    REQUIRE_MESSAGE(plainRecord.has_value(), plainError);
    CHECK_FALSE(plainRecord->fontArtifact.has_value());

    // The same locator is exactly what a sealed-package catalog refuses.
    std::string sealedError;
    CHECK_FALSE(molga::AssetRecordFromJson(
        canonical, sealedError, molga::AssetCatalogMode::SealedPackage));
    auto sealedRecord = canonical;
    sealedRecord["artifactStorage"] = "PackagedResource";
    sealedRecord["artifactRelativePath"] = "Assets/Fonts/Latin.sfnt";
    CHECK(molga::AssetRecordFromJson(sealedRecord, sealedError,
                                     molga::AssetCatalogMode::SealedPackage)
              .has_value());
}

// 아래 네 케이스는 mutation 스윕이 찾아낸 무증인(unwitnessed) 계약을 고정한다.
// 각 케이스 주석의 "mutation"은 이 assertion이 없을 때 통과해 버리던 변형이다.

TEST_CASE("a fresh database refuses a null store for its own reason") {
    // mutation: BindFontArtifactStore의 `if (!store)` 가드를 제거해도 통과했다.
    // 기존 케이스는 이미 store가 바인딩된 database에 nullptr을 넘겼기 때문에
    // null 가드가 아니라 rebind 가드가 거절하고 있었다 — 옳은 이유로 통과하는
    // 것이 아니었다. 바인딩이 없는 database에서만 null 가드가 유일한 거절자다.
    molga::AssetDatabase fresh;
    std::string error = "untouched";
    CHECK_FALSE(fresh.BindFontArtifactStore(nullptr, &error));
    CHECK(error.find("null") != std::string::npos);

    // 그리고 같은 database가 정상 store는 받아들인다: 위 거절이 "항상 거절"이
    // 아니라 null에 대한 거절임을 증명한다.
    TempProject project("fresh-null-bind");
    std::string bindError;
    CHECK_MESSAGE(fresh.BindFontArtifactStore(ProjectStore(project.root),
                                              &bindError),
                  bindError);
}

TEST_CASE("Clear never erases the font artifact store binding") {
    // mutation: Clear()에 fontArtifacts_.reset()을 추가해도 통과했다. 바인딩이
    // Clear()가 지우지 않는 유일한 상태라는 계약에 증인이 없었다.
    TempProject project("clear-keeps-binding");
    molga::AssetDatabase database;
    std::string bindError;
    REQUIRE_MESSAGE(database.BindFontArtifactStore(ProjectStore(project.root),
                                                   &bindError),
                    bindError);

    database.Clear();

    // 바인딩이 살아 있으면 재바인딩은 여전히 거절된다. Clear()가 바인딩을
    // 지웠다면 이 호출이 성공해 버린다.
    std::string rebindError;
    CHECK_FALSE(database.BindFontArtifactStore(ProjectStore(project.root),
                                               &rebindError));
    CHECK(rebindError.find("already bound") != std::string::npos);
}

TEST_CASE("Publish refuses bytes whose digest is not the expected source SHA") {
    // mutation: `sourceSha != expectedSourceSha256` 비교를 제거해도 통과했다.
    // store가 바이트 권한이라는 계약 전체가 무증인이었고, 제거하면 잘못된
    // 폰트 바이트가 조용히 통과하는 fail-open이 된다.
    TempProject project("publish-sha-authority");
    const auto store = molga::FontArtifactStore::ForProject(project.root);
    const fs::path source = project.root / "Assets" / "Fonts" / "Mismatch.ttf";
    fs::create_directories(source.parent_path());
    const std::vector<std::uint8_t> bytes{'s', 'h', 'a', '-', 'a', 'u', 't',
                                          'h'};
    {
        std::ofstream out(source, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    const std::string actual = molga::Sha256Bytes(bytes.data(), bytes.size());
    // 같은 길이의 유효한 lowercase hex이지만 내용은 다르다: 형식 검사가 아니라
    // 내용 비교만이 이것을 잡을 수 있다.
    std::string wrong = actual;
    wrong[0] = (actual[0] == '0') ? '1' : '0';
    REQUIRE(wrong != actual);
    REQUIRE(wrong.size() == actual.size());

    molga::text::VectorTextDiagnosticSink sink;
    CHECK_FALSE(store.Publish(source, wrong, sink).has_value());
    CHECK_FALSE(sink.Diagnostics().empty());

    // 올바른 digest는 받아들여진다 — 위 거절이 "항상 거절"이 아님을 증명한다.
    molga::text::VectorTextDiagnosticSink okSink;
    CHECK(store.Publish(source, actual, okSink).has_value());
}

TEST_CASE("Publish refuses a malformed expected source SHA before hashing") {
    // mutation: `!IsLowercaseSha256(expectedSourceSha256)` 가드를 제거해도
    // 통과했다.
    TempProject project("publish-sha-format");
    const auto store = molga::FontArtifactStore::ForProject(project.root);
    const fs::path source = project.root / "Assets" / "Fonts" / "Format.ttf";
    fs::create_directories(source.parent_path());
    const std::vector<std::uint8_t> bytes{'f', 'o', 'r', 'm', 'a', 't'};
    {
        std::ofstream out(source, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    const std::string actual = molga::Sha256Bytes(bytes.data(), bytes.size());
    std::string upper = actual;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::toupper(c));
                   });

    // 형식 가드를 제거하면 잘못된 형식의 digest도 내용 비교까지 흘러가 결국
    // 거절되기는 한다 — 그래서 "거절되었는가"만 보는 assertion으로는 이 가드를
    // 잡을 수 없다. 관측 가능한 차이는 진단이다: 형식 위반은 digest를 다시
    // 계산하라고 말하고, 내용 불일치는 asset을 다시 import하라고 말한다. 이
    // suite는 이미 진단 message를 고정하는 관례를 쓴다(위 U+0633 케이스).
    for (const std::string bad : {std::string(), std::string("zz"), upper,
                                  actual.substr(0, actual.size() - 1),
                                  actual + "0"}) {
        molga::text::VectorTextDiagnosticSink sink;
        CHECK_FALSE(store.Publish(source, bad, sink).has_value());
        REQUIRE(sink.Diagnostics().size() == 1U);
        const auto& diagnostic = sink.Diagnostics().front();
        CHECK(diagnostic.code == molga::text::TextDiagnosticCode::FontInvalid);
        CHECK(diagnostic.message ==
              "font source SHA-256 is not lowercase hexadecimal");
        CHECK(diagnostic.remediation ==
              "recompute the source digest before publishing");
    }

    // 대조군: 형식은 옳지만 내용이 다른 digest는 다른 진단을 낸다. 두 경로가
    // 실제로 구분된다는 증거이므로, 위 assertion이 우연히 통과하는 것이 아니다.
    std::string wrong = actual;
    wrong[0] = (actual[0] == '0') ? '1' : '0';
    molga::text::VectorTextDiagnosticSink mismatchSink;
    CHECK_FALSE(store.Publish(source, wrong, mismatchSink).has_value());
    REQUIRE(mismatchSink.Diagnostics().size() == 1U);
    CHECK(mismatchSink.Diagnostics().front().message ==
          "the font source changed between import and publication");
}

TEST_CASE("a sealed catalog record refuses a locator outside Assets/") {
    // mutation: AssetRecordFromJson의 packaged 분기에서 `Assets/` 접두어 검사를
    // 제거해도 통과했다. FontArtifactStore의 manifest 경로 검사에는 증인이
    // 있었지만(그 변형은 잡혔다) catalog 파싱 쪽 같은 규칙에는 없었다.
    ImportResult imported;
    imported.success = true;
    AssetRecord record;
    record.guid = "0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d";
    record.sourcePath = "Assets/Fonts/Latin.ttf";
    record.importer = "FontImporter";
    record.importerVersion = 2;
    molga::ApplyImportResultToRecord(imported, record);
    molga::VerifiedFontArtifact artifact;
    artifact.locator.storage = molga::FontArtifactStorage::PackagedResource;
    artifact.locator.relativePath = "Assets/Fonts/Latin.sfnt";
    artifact.sourceSha256 = MOLGA_TEXT_LATIN_SHA256;
    artifact.artifactSha256 = MOLGA_TEXT_LATIN_SHA256;
    artifact.byteSize = 569208U;
    record.fontArtifact = artifact;

    nlohmann::json json = molga::AssetRecordToJson(record);
    std::string error;
    REQUIRE(molga::AssetRecordFromJson(json, error,
                                       molga::AssetCatalogMode::SealedPackage)
                .has_value());

    // Assets/ 밖의 locator는 거절된다. 경로 자체는 안전한 상대 경로이므로
    // safe-relative 검사가 아니라 접두어 검사만이 이것을 잡는다.
    for (const std::string outside : {"Engine/Text/Latin.sfnt",
                                      "Library/Imported/Fonts/Latin.sfnt",
                                      "Latin.sfnt"}) {
        nlohmann::json bad = json;
        bad["artifactRelativePath"] = outside;
        std::string badError;
        CHECK_FALSE(molga::AssetRecordFromJson(
                        bad, badError, molga::AssetCatalogMode::SealedPackage)
                        .has_value());
        CHECK_FALSE(badError.empty());
    }
}
