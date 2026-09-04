#include "Assets/FontArtifactStore.h"
#include "Assets/FontAsset.h"
#include "Assets/FontFamilyAsset.h"
#include "Common/Fixed26_6.h"
#include "Core/AssetDatabase.h"
#include "Core/AssetMeta.h"
#include "Common/Sha256.h"
#include "Core/Importers/FontFamilyImporter.h"
#include "Core/Importers/FontImporter.h"
#include "Core/Importers/ImporterRegistry.h"
#include "Core/PersistentStorage.h"
#include "Rendering/FontFace.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"
#include "TextQualificationAssetTree.h"
#include "doctest.h"

// 저작 원본 읽기 검출기는 atime 계측에 기댄다. POSIX가 아니면 utimensat도
// AT_FDCWD도 없으므로 계측 자체를 컴파일에서 뺀다 — 그 위에 선 단언들도 함께
// 빠지도록 아래에서 같은 매크로로 묶는다.
#if defined(_WIN32)
#define MOLGA_TEXT_TEST_ATIME_DETECTOR 0
#else
#define MOLGA_TEXT_TEST_ATIME_DETECTOR 1
#include <fcntl.h>
#include <sys/stat.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
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

void WriteAllBytes(const fs::path& path,
                   const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), "could not write " << path.string());
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    REQUIRE(output.good());
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

// ── Task 4.2: authored font family helpers ──────────────────────────────────

// FromRecord는 거절 사유를 호출자 소유 문자열로 돌려준다. 한 슬롯이면 충분한
// 이유는 모든 호출 결과를 곧바로 단언하기 때문이고, LastFamilyError()는 그
// 사유를 실패 메시지에 실어 보내기 위해 같은 슬롯을 다시 읽는다.
std::string g_familyError;

std::string& TestError() {
    g_familyError.clear();
    return g_familyError;
}

const std::string& LastFamilyError() { return g_familyError; }

// doctest는 DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING 없이 빌드되므로 const
// char* 메시지를 문자열이 아니라 포인터 주소로 찍는다. 표 기반 케이스에서
// 어느 행이 실패했는지 읽히게 하려면 std::string으로 감싸야 한다.
std::string Label(const char* text) { return std::string(text); }

std::vector<std::string> FaceGuids(const molga::FontFamilyAsset& family) {
    std::vector<std::string> guids;
    guids.reserve(family.faces.size());
    for (const molga::FontFamilyFaceEntry& face : family.faces) {
        guids.push_back(face.fontGuid);
    }
    return guids;
}

// primary.fontfamily와 같은 값. 저작 소스에는 guid가 없다.
nlohmann::json ValidFontFamilySourceJson() {
    return nlohmann::json::parse(R"({
        "schemaVersion": 1,
        "faces": [
            {"fontGuid": "44444444444444444444444444444444", "faceIndex": 0,
             "weight": 400, "stretchPercent": 100, "slant": "Upright"},
            {"fontGuid": "55555555555555555555555555555555", "faceIndex": 0,
             "weight": 400, "stretchPercent": 100, "slant": "Upright"},
            {"fontGuid": "12121212121212121212121212121212", "faceIndex": 0,
             "weight": 400, "stretchPercent": 100, "slant": "Upright"},
            {"fontGuid": "13131313131313131313131313131313", "faceIndex": 0,
             "weight": 400, "stretchPercent": 100, "slant": "Upright"}
        ],
        "fallbackFamilyGuids": ["22222222222222222222222222222222",
                                "33333333333333333333333333333333"],
        "unknownAuthoringField": "preserved"
    })");
}

// 커밋된 픽스처는 절대 수정하지 않는다. 변형 케이스는 전부 RAII 임시 파일이다.
class ScopedTempFamily {
public:
    explicit ScopedTempFamily(const std::string& text) {
        static unsigned long long sequence = 0;
        // TempDirectory와 같은 규칙으로 steady_clock 눈금을 섞는다. 프로세스
        // 안에서만 유일한 이름을 쓰면 같은 머신에서 동시에 도는 두 번째
        // test_font_assets가 서로의 픽스처를 잘라내고 지운다.
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = (fs::temp_directory_path() /
                ("molga-family-fixture-" + std::to_string(stamp) + "-" +
                 std::to_string(++sequence) + ".fontfamily"))
                   .string();
        std::ofstream output(path, std::ios::trunc);
        REQUIRE_MESSAGE(output.good(), "could not create " << path);
        output << text;
        output.close();
        REQUIRE(fs::exists(path));
    }
    ScopedTempFamily(const ScopedTempFamily&) = delete;
    ScopedTempFamily& operator=(const ScopedTempFamily&) = delete;
    ~ScopedTempFamily() {
        std::error_code error;
        fs::remove(path, error);
    }

    std::string path;
};

ImportResult ImportTemporaryFontFamily(const nlohmann::json& source) {
    const ScopedTempFamily temporary(source.dump(2));
    return molga::FontFamilyImporter().Import(temporary.path);
}

ImportResult ImportTemporaryFontFamilyText(const std::string& text) {
    const ScopedTempFamily temporary(text);
    return molga::FontFamilyImporter().Import(temporary.path);
}

// import 결과를 카탈로그에 실었다가 다시 읽는다. FromRecord 계약이 실제
// 재적재 경로 위에서 검증되도록, 메모리 안의 ImportResult를 직접 쓰지 않는다.
AssetRecord RoundTripFamilyRecord(const ImportResult& result,
                                  const std::string& guid) {
    AssetRecord record;
    record.guid = guid;
    record.sourcePath = "Assets/Families/round-trip.fontfamily";
    record.importer = "FontFamilyImporter";
    record.importerVersion = 1;
    molga::ApplyImportResultToRecord(result, record);

    std::string error;
    auto restored = molga::AssetRecordFromJson(
        molga::AssetRecordToJson(record), error);
    REQUIRE_MESSAGE(restored.has_value(), error);
    return *restored;
}

// ── Task 4.3: immutable font resources across hot reload ────────────────────

bool HasDiagnostic(const molga::text::VectorTextDiagnosticSink& sink,
                   TextDiagnosticCode code) {
    return HasDiagnostic(sink.Diagnostics(), code);
}

std::shared_ptr<const std::vector<std::uint8_t>> SharedBytes(
    const fs::path& path) {
    return std::make_shared<const std::vector<std::uint8_t>>(
        ReadAllBytes(path));
}

// 저작 원본이 실제로 열렸는지를 관찰하는 검출기.
//
// "발행된 산출물만이 바이트 권한"이라는 이 마일스톤의 계약은 원본을 열지
// 않았다는 사실을 확인해야 의미가 있는데, 프로덕션 코드는 std::ifstream을
// 직접 쓰므로 가로챌 후크가 없다. 그래서 파일 자체를 계측한다: Arm()이
// utimensat으로 atime만 알려진 과거 값으로 되돌리고(mtime은 UTIME_OMIT),
// 그 뒤 파일을 한 번이라도 읽으면 커널이 atime을 올린다. Count()는 기준선
// 이후 읽힌 추적 파일 수다.
//
// 검출기가 조용히 죽어 항상 0을 돌려주는 것을 막기 위해, 이 값을 단언하는
// 모든 케이스가 ReadTrackedSourcesOnce() 양성 대조를 함께 단언한다.
//
// 이 계측은 POSIX(그리고 atime을 실제로 갱신하는 파일 시스템)를 요구한다.
// noatime 마운트나 utimensat이 없는 플랫폼에서는 계측도 대조도 함께 빠지고,
// 그 사실은 CheckAuthoringSourceDetectorIsLive()가 메시지로 남긴다.
class AuthoringSourceReadDetector {
public:
    void Track(const fs::path& path) {
        tracked_.push_back(path);
        Arm();
    }
    void Arm() {
#if MOLGA_TEXT_TEST_ATIME_DETECTOR
        for (const fs::path& path : tracked_) {
            struct timespec times[2];
            times[0].tv_sec = kArmedAtimeSeconds;
            times[0].tv_nsec = 0;
            times[1].tv_sec = 0;
            times[1].tv_nsec = UTIME_OMIT;
            REQUIRE_MESSAGE(
                utimensat(AT_FDCWD, path.c_str(), times, 0) == 0,
                "could not arm the authoring-source read detector for "
                    << path.string());
        }
#endif
    }
    std::size_t Count() const {
        std::size_t read = 0;
#if MOLGA_TEXT_TEST_ATIME_DETECTOR
        for (const fs::path& path : tracked_) {
            struct stat status {};
            REQUIRE_MESSAGE(stat(path.c_str(), &status) == 0,
                            "tracked authoring source disappeared: "
                                << path.string());
            // st_atimespec은 BSD/macOS 이름이고 glibc는 st_atim을 쓴다. 같은
            // 필드를 가리키므로 이름만 갈라 준다.
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__OpenBSD__) || defined(__DragonFly__)
            const struct timespec accessed = status.st_atimespec;
#else
            const struct timespec accessed = status.st_atim;
#endif
            if (accessed.tv_sec != kArmedAtimeSeconds ||
                accessed.tv_nsec != 0) {
                ++read;
            }
        }
#endif
        return read;
    }
    void ReadTrackedSourcesOnce() const {
        for (const fs::path& path : tracked_) {
            std::ifstream input(path, std::ios::binary);
            REQUIRE(input.good());
            std::istreambuf_iterator<char> begin(input);
            const std::istreambuf_iterator<char> end;
            const std::vector<std::uint8_t> bytes(begin, end);
            CHECK_FALSE(bytes.empty());
        }
    }
    std::size_t TrackedCount() const { return tracked_.size(); }

    // 양성 대조. 추적 중인 원본을 실제로 한 번 읽고, 검출기가 그 읽기를
    // 보는지 단언한다. 이것이 없으면 "0번 열렸다"는 죽은 검출기에서도 참이다.
    void CheckIsLive() const {
#if MOLGA_TEXT_TEST_ATIME_DETECTOR
        REQUIRE(TrackedCount() > 0U);
        CHECK(Count() == 0U);
        ReadTrackedSourcesOnce();
        CHECK(Count() == TrackedCount());
#else
        MESSAGE(
            "authoring-source read detection is unavailable on this platform; "
            "the AuthoringSourceOpenCount() == 0 assertions above are vacuous");
#endif
    }

private:
    // 2001-09-09. 어떤 실제 읽기도 이 값을 다시 만들어 낼 수 없을 만큼 과거다.
    static constexpr std::time_t kArmedAtimeSeconds = 1000000000;
    std::vector<fs::path> tracked_;
};

// Task 4.3 Step 1/1a의 verbatim 블록은 폰트를 "font-a"라는 라벨로 부른다.
// 이 코드베이스의 asset GUID는 32자리 hex여야 하므로(Guid::IsValid) 라벨을
// 그대로 GUID로 쓸 수 없고, fixture가 라벨 -> 실제 GUID 사전을 갖는다. 모르는
// 라벨은 즉시 실패시켜, 오타가 "존재하지 않는 GUID"로 조용히 흘러 통과하는
// 일을 막는다.
class FontLabels {
public:
    void Define(std::string label, std::string guid) {
        labels_[std::move(label)] = std::move(guid);
    }
    const std::string& Guid(const std::string& label) const {
        const auto found = labels_.find(label);
        REQUIRE_MESSAGE(found != labels_.end(), "unknown font label: " << label);
        return found->second;
    }

private:
    std::map<std::string, std::string> labels_;
};

// 라벨을 받는 얇은 어댑터. 두 메서드 모두 곧바로 실제 AssetDatabase를 부르고,
// 실패한 import가 record에 남긴 typed diagnostic을 fixture sink로 옮긴다.
// 성공한 reimport는 아무것도 옮기지 않으므로, sink의 FontInvalid는 배관이
// 아니라 손상된 import가 만든 값이다.
class LabelledDatabase {
public:
    LabelledDatabase(molga::AssetDatabase& database, const FontLabels& labels,
                     molga::text::TextDiagnosticSink& sink)
        : database_(database), labels_(labels), sink_(sink) {}

    molga::AssetDatabase& Real() const noexcept { return database_; }

    std::uint64_t ContentGeneration(const std::string& label) const {
        return database_.ContentGeneration(labels_.Guid(label));
    }

    bool TryReimport(const std::string& label) {
        const std::string& guid = labels_.Guid(label);
        const bool imported = database_.TryReimport(guid);
        if (const AssetRecord* record = database_.Find(guid)) {
            for (const molga::text::TextDiagnostic& diagnostic :
                 record->importDiagnostics) {
                sink_.Report(diagnostic);
            }
        }
        return imported;
    }

private:
    molga::AssetDatabase& database_;
    const FontLabels& labels_;
    molga::text::TextDiagnosticSink& sink_;
};

class LabelledRepository {
public:
    LabelledRepository(const molga::AssetDatabase& database,
                       const FontLabels& labels)
        : repository_(database), labels_(labels) {}

    std::optional<molga::text::FontFaceResourcePtr> Load(
        const std::string& label, std::uint32_t faceIndex,
        molga::text::TextDiagnosticSink& sink) const {
        return repository_.Load(labels_.Guid(label), faceIndex, sink);
    }
    void Invalidate(const std::string& label) {
        repository_.Invalidate(labels_.Guid(label));
    }

private:
    molga::text::FontRepository repository_;
    const FontLabels& labels_;
};

class FontRepositoryFixture {
private:
    // 선언 순서가 곧 생성 순서다. public 어댑터들이 아래 private 멤버를
    // 참조로 잡으므로 private가 먼저 와야 한다.
    TempProject project_;
    FontLabels labels_;
    molga::AssetDatabase realDatabase_;
    std::shared_ptr<const molga::FontArtifactStore> store_;
    AuthoringSourceReadDetector detector_;
    fs::path source_;
    fs::path catalogPath_;
    std::optional<AssetRecord> priorRecord_;

public:
    molga::text::VectorTextDiagnosticSink sink;
    LabelledDatabase database;
    LabelledRepository repository;

    explicit FontRepositoryFixture(
        const fs::path& fontSource = fs::path(MOLGA_TEXT_LATIN_FONT),
        const nlohmann::json& settings = ValidStaticFontSettings())
        : project_("repository"),
          store_(ProjectStore(project_.root)),
          database(realDatabase_, labels_, sink),
          repository(realDatabase_, labels_) {
        labels_.Define("font-a", kFontAGuid);
        fs::create_directories(project_.assets / "Fonts");
        source_ = project_.assets / "Fonts" / "font-a.ttf";
        fs::copy_file(fontSource, source_);
        WriteFontMeta(source_, kFontAGuid, settings);
        catalogPath_ = project_.root / "asset_catalog.json";

        std::string bindError;
        REQUIRE_MESSAGE(realDatabase_.BindFontArtifactStore(store_, &bindError),
                        bindError);
        realDatabase_.ScanProject(project_.assets);
        const AssetRecord* record = realDatabase_.Find(kFontAGuid);
        REQUIRE(record != nullptr);
        REQUIRE_MESSAGE(!record->importFailed, record->importError);
        REQUIRE(record->fontArtifact.has_value());
        // import가 원본을 읽는 것은 정상이다. 기준선은 import가 끝난 뒤 잡는다.
        detector_.Track(source_);
    }

    const std::string& Guid(const std::string& label) const {
        return labels_.Guid(label);
    }
    const fs::path& ProjectRoot() const noexcept { return project_.root; }
    const fs::path& AuthoringSource() const noexcept { return source_; }

    std::string CatalogArtifactSha(const std::string& label) const {
        const AssetRecord* record = realDatabase_.Find(labels_.Guid(label));
        REQUIRE(record != nullptr);
        REQUIRE(record->fontArtifact.has_value());
        return record->fontArtifact->artifactSha256;
    }

    fs::path PublishedArtifact(const std::string& label) const {
        const AssetRecord* record = realDatabase_.Find(labels_.Guid(label));
        REQUIRE(record != nullptr);
        REQUIRE(record->fontArtifact.has_value());
        return project_.root / record->fontArtifact->locator.relativePath;
    }

    // 저작 원본을 새 검증된 바이트로 바꾸고, 실제 import 발행 경로를 태운다.
    void ReplaceFontWithVerifiedBytes(const std::string& label,
                                      const fs::path& replacement) {
        RetainPriorRecord(label);
        fs::copy_file(replacement, source_, fs::copy_options::overwrite_existing);
        REQUIRE(database.TryReimport(label));
        detector_.Arm();
    }

    // 바이트는 그대로 두고 authored 설정만 다시 쓴 뒤 실제 import 경로를
    // 태운다. 발행된 산출물은 같은 content address에 머무르므로, 이것만이
    // 세대는 올라가고 SHA/경로/크기는 그대로인 상태를 만든다.
    void RewriteAuthoredSettings(const std::string& label,
                                 const nlohmann::json& settings) {
        RetainPriorRecord(label);
        WriteFontMeta(source_, labels_.Guid(label), settings);
        REQUIRE(database.TryReimport(label));
        detector_.Arm();
    }

    // Step 1b: fixture의 임시 원본만, 같은 크기로, SFNT signature 바이트만
    // 뒤집어 덮어쓴다. 커밋된 폰트는 절대 건드리지 않는다. 직전 카탈로그
    // record는 단언용으로 보관한다.
    void ReplaceFontWithCorruptBytes(const std::string& label) {
        RetainPriorRecord(label);
        CorruptAuthoringSourceWithoutReimport();
        REQUIRE_FALSE(database.TryReimport(label));
        detector_.Arm();
    }

    // 같은 변형이지만 reimport를 태우지 않는다. 카탈로그는 마지막 정상
    // 산출물을 그대로 가리킨 채, 저작 원본만 달라진 상태다.
    void CorruptAuthoringSourceWithoutReimport() {
        std::vector<std::uint8_t> bytes = ReadAllBytes(source_);
        REQUIRE(bytes.size() > 4U);
        const std::uintmax_t before = fs::file_size(source_);
        bytes[0] = static_cast<std::uint8_t>(bytes[0] ^ 0xFFU);
        WriteAllBytes(source_, bytes);
        REQUIRE(fs::file_size(source_) == before);
        detector_.Arm();
    }

    void CorruptPublishedArtifact(const std::string& label) {
        const fs::path artifact = PublishedArtifact(label);
        std::vector<std::uint8_t> bytes = ReadAllBytes(artifact);
        REQUIRE(bytes.size() > 64U);
        intactArtifact_ = bytes;
        bytes[64] = static_cast<std::uint8_t>(bytes[64] ^ 0xFFU);
        WriteAllBytes(artifact, bytes);
        detector_.Arm();
    }

    void RestorePublishedArtifact(const std::string& label) {
        REQUIRE_FALSE(intactArtifact_.empty());
        WriteAllBytes(PublishedArtifact(label), intactArtifact_);
        detector_.Arm();
    }

    fs::path PersistCatalog() const {
        REQUIRE(realDatabase_.SaveCatalog(catalogPath_));
        return catalogPath_;
    }

    const AssetRecord& PriorRecord() const {
        REQUIRE(priorRecord_.has_value());
        return *priorRecord_;
    }

    std::size_t AuthoringSourceOpenCount() const { return detector_.Count(); }
    void ArmAuthoringSourceDetector() { detector_.Arm(); }
    // 위 0 단언들의 양성 대조. 계측이 없는 플랫폼에서는 이 호출이 그 사실을
    // 남기고, 0 단언들이 공짜로 통과하고 있다는 것을 눈에 보이게 한다.
    void CheckAuthoringSourceDetectorIsLive() const { detector_.CheckIsLive(); }
    // 원본이 여전히 열 수 있는 상태인지만 본다. 실제로 읽으면 읽기 검출기의
    // 기준선이 움직이므로, 메타데이터만 확인한다.
    bool AuthoringSourceIsIntact() const {
        std::error_code error;
        return fs::is_regular_file(source_, error) &&
               fs::file_size(source_, error) > 0U;
    }

    static constexpr const char* kFontAGuid =
        "0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a";

private:
    void RetainPriorRecord(const std::string& label) {
        const AssetRecord* record = realDatabase_.Find(labels_.Guid(label));
        REQUIRE(record != nullptr);
        priorRecord_ = *record;
    }

    std::vector<std::uint8_t> intactArtifact_;
};

// Step 1h/1i/1j: 프로젝트 저장소와 봉인 패키지 저장소를 서로 겹치지 않는
// 루트 위에 세우고, 어느 쪽도 열어서는 안 되는 저작 원본을 하나 따로 둔다.
class ArtifactLocatorFixture {
private:
    TempProject temp_;
    AuthoringSourceReadDetector detector_;
    fs::path authoringSource_;

public:
    fs::path projectRoot;
    fs::path runtimeResourceRoot;
    std::string sha256;
    std::uint64_t byteSize = 0;
    molga::text::VectorTextDiagnosticSink sink;

    ArtifactLocatorFixture() : temp_("locator") {
        const fs::path font(MOLGA_TEXT_CJK_FONT);
        std::string shaError;
        sha256 = molga::Sha256File(font, &shaError);
        REQUIRE_MESSAGE(molga::IsLowercaseSha256(sha256), shaError);
        byteSize = static_cast<std::uint64_t>(fs::file_size(font));

        projectRoot = temp_.root / "Project";
        fs::create_directories(projectRoot / "Library" / "Imported" / "Fonts");
        fs::copy_file(font, projectRoot / "Library" / "Imported" / "Fonts" /
                                (sha256 + ".sfnt"));

        runtimeResourceRoot = temp_.root / "Package" / "Contents" / "Resources";
        fs::create_directories(runtimeResourceRoot / "Assets" / "Fonts");
        fs::copy_file(font, runtimeResourceRoot / "Assets" / "Fonts" / "used.otf");

        // 두 저장소 어디에도 속하지 않는 저작 원본. 어떤 경로도 이 파일로
        // 되돌아 열려서는 안 된다.
        fs::create_directories(temp_.root / "Authoring");
        authoringSource_ = temp_.root / "Authoring" / "used.otf";
        fs::copy_file(font, authoringSource_);
        detector_.Track(authoringSource_);
    }

    // 패키지 안의 실제 SFNT 파일 수. symlink는 정규 파일이 아니므로 세지
    // 않는다 — symlink escape 행이 패키지 내용물을 늘리지 않았음을 본다.
    std::size_t PackagedSfntCount() const {
        std::size_t count = 0;
        for (const auto& entry :
             fs::recursive_directory_iterator(runtimeResourceRoot)) {
            std::error_code error;
            if (fs::is_symlink(fs::symlink_status(entry.path(), error))) continue;
            if (!fs::is_regular_file(entry.path(), error)) continue;
            const std::string extension = entry.path().extension().string();
            if (extension == ".otf" || extension == ".ttf" ||
                extension == ".sfnt") {
                ++count;
            }
        }
        return count;
    }

    fs::path CreateSymlinkEscape(const std::string& relative) {
        return CreateSymlink(relative, authoringSource_);
    }

    // 패키지 밖이 아니라 패키지 안을 가리키는 symlink. prefix 검사는 이것을
    // 통과시키므로, 거절은 오직 symlink 자체를 거절하는 규칙에서만 나온다.
    fs::path CreateSymlinkInsidePackage(const std::string& relative) {
        return CreateSymlink(relative,
                             runtimeResourceRoot / "Assets" / "Fonts" /
                                 "used.otf");
    }

    // 내용은 진짜 산출물이지만 이름이 자기 content address가 아닌 프로젝트
    // 파일. ProjectLibrary locator가 content address여야 한다는 규칙 하나만이
    // 이것을 거절할 수 있다.
    std::string CreateProjectDecoyArtifact() {
        const std::string decoy = "Library/Imported/Fonts/decoy.sfnt";
        fs::copy_file(projectRoot / "Library" / "Imported" / "Fonts" /
                          (sha256 + ".sfnt"),
                      projectRoot / "Library" / "Imported" / "Fonts" /
                          "decoy.sfnt");
        return decoy;
    }

    // 크기는 그대로 두고 한 바이트만 뒤집는다. 크기 검사만 남은 회귀는 이
    // 변형을 통과시킨다.
    void CorruptProjectArtifactInPlace() {
        const fs::path artifact =
            projectRoot / "Library" / "Imported" / "Fonts" / (sha256 + ".sfnt");
        std::vector<std::uint8_t> bytes = ReadAllBytes(artifact);
        REQUIRE(bytes.size() > 64U);
        const std::uintmax_t before = fs::file_size(artifact);
        bytes[64] = static_cast<std::uint8_t>(bytes[64] ^ 0xFFU);
        WriteAllBytes(artifact, bytes);
        REQUIRE(fs::file_size(artifact) == before);
    }

    std::size_t AuthoringSourceOpenCount() const { return detector_.Count(); }
    void CheckAuthoringSourceDetectorIsLive() const { detector_.CheckIsLive(); }

private:
    fs::path CreateSymlink(const std::string& relative, const fs::path& target) {
        const fs::path link = runtimeResourceRoot / relative;
        fs::create_directories(link.parent_path());
        std::error_code error;
        fs::create_symlink(target, link, error);
        REQUIRE_MESSAGE(!error,
                        "could not create the test symlink: " << error.message());
        detector_.Arm();
        return link;
    }
};

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

// 아래 두 케이스는 Task 4.1의 품질/통합 리뷰가 스크래치 프로그램으로 재현해
// 보인 두 결함을 고정한다. 둘 다 assertion 없이 조용히 되돌아갈 수 있었다.

TEST_CASE("rescanning an unchanged project yields an identical catalog") {
    // 결함: contentGeneration_이 프로세스 수명 카운터라 리셋되지 않았다. 같은
    // 픽스처를 세 번 스캔하면 contentRevision이 1,2 → 3,4 → 5,6으로 흘러
    // asset_catalog.json이 매번 달라졌다. GameBuilder는 빌드마다 두 번
    // 스캔하므로, 바뀌지 않은 프로젝트가 빌드마다 다른 카탈로그를 냈다 —
    // Milestone 17의 카탈로그 봉인과 Milestone 18의 byte-identical parity가
    // 그대로 물려받았을 결함이다.
    TempProject project("rescan-determinism");
    fs::create_directories(project.assets / "Fonts");
    const fs::path source = project.assets / "Fonts" / "Latin.ttf";
    fs::copy_file(MOLGA_TEXT_LATIN_FONT, source);
    WriteFontMeta(source, "1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a",
                  ValidStaticFontSettings());

    molga::AssetDatabase database;
    REQUIRE(database.BindFontArtifactStore(ProjectStore(project.root)));

    database.ScanProject(project.assets);
    const AssetRecord* first = database.Find("1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a");
    REQUIRE(first != nullptr);
    REQUIRE(first->fontArtifact.has_value());
    const nlohmann::json firstJson = molga::AssetRecordToJson(*first);
    const std::uint64_t firstGeneration = database.ContentGeneration();

    // 같은 입력을 두 번 더 스캔한다. 내용이 바뀌지 않았으므로 직렬화된 record가
    // 바이트 단위로 같아야 한다.
    for (int pass = 0; pass < 2; ++pass) {
        database.ScanProject(project.assets);
        const AssetRecord* again =
            database.Find("1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a");
        REQUIRE(again != nullptr);
        CHECK(molga::AssetRecordToJson(*again) == firstJson);
        CHECK(database.ContentGeneration() == firstGeneration);
    }
}

TEST_CASE("a stale publish lock never wedges a content-addressed artifact") {
    // 결함: 락 획득 실패가 즉시 실패였다. 같은 내용을 동시에 게시하는 네
    // 요청 중 셋이 실패했고, kill -9가 남긴 .lock은 그 아티팩트를 영구히
    // 막았다. 아티팩트는 내용 주소이므로 "다른 게시자가 이미 끝냈다"는 성공
    // 조건이지 실패 조건이 아니다.
    TempProject project("stale-lock");
    const std::vector<std::uint8_t> bytes{'s', 't', 'a', 'l', 'e'};
    const std::string sha = molga::Sha256Bytes(bytes.data(), bytes.size());
    const fs::path destination =
        project.root / "Library" / "Imported" / "Fonts" / (sha + ".sfnt");
    fs::create_directories(destination.parent_path());

    // 소유자가 사라진 오래된 락을 흉내 낸다.
    fs::path lockPath = destination;
    lockPath += ".lock";
    { std::ofstream lock(lockPath); }
    fs::last_write_time(lockPath, fs::file_time_type::clock::now() -
                                      std::chrono::hours(1));

    std::string error;
    CHECK_MESSAGE(PersistentStorage::AtomicPublishImmutableBytes(
                      destination, bytes, sha, &error),
                  error);
    CHECK(ReadAllBytes(destination) == bytes);
    CHECK_FALSE(fs::exists(lockPath));
}

// ── Task 4.2: ordered authored font families ────────────────────────────────

TEST_CASE("font family preserves authored face fallback and unknown-field order") {
    constexpr std::string_view kPrimaryGuid =
        "11111111111111111111111111111111";
    constexpr std::string_view kArabicGuid =
        "22222222222222222222222222222222";
    constexpr std::string_view kCjkGuid =
        "33333333333333333333333333333333";
    const auto result = molga::FontFamilyImporter().Import(
        MOLGA_TEXT_PRIMARY_FAMILY);
    REQUIRE(result.success);
    QualificationAssetTreeFixture fixture;
    molga::AssetDatabase db;
    auto store = std::make_shared<const molga::FontArtifactStore>(
        molga::FontArtifactStore::ForProject(fixture.ProjectRoot()));
    REQUIRE(db.BindFontArtifactStore(store));
    db.ScanProject(fixture.AssetsRoot());
    const molga::AssetRecord* record = db.Find(std::string(kPrimaryGuid));
    REQUIRE(record != nullptr);
    CHECK(record->importer == "FontFamilyImporter");
    CHECK(record->importerVersion == 1);
    const auto family =
        molga::FontFamilyAsset::FromRecord(*record, TestError());
    REQUIRE(family);
    CHECK(family->guid == std::string(kPrimaryGuid));
    CHECK(family->faces.at(0).authoredFaceIndex == 0);
    CHECK(family->faces.at(1).authoredFaceIndex == 1);
    CHECK(family->faces.at(2).authoredFaceIndex == 2);
    CHECK(family->faces.at(3).authoredFaceIndex == 3);
    CHECK(FaceGuids(*family) == std::vector<std::string>{
        "44444444444444444444444444444444",
        "55555555555555555555555555555555",
        "12121212121212121212121212121212",
        "13131313131313131313131313131313"});
    CHECK(family->fallbackFamilyGuids ==
          std::vector<std::string>{std::string(kArabicGuid),
                                   std::string(kCjkGuid)});
    // const json의 operator[]는 없는 key에 대해 abort한다. 가드 없이 두면
    // 이 계약이 깨졌을 때 assertion 하나가 실패하는 대신 프로세스가 죽어
    // 무관한 케이스 11개까지 skipped로 끌고 내려간다. 계획서 blocks의
    // 2026-09-05 amendment로 이 한 줄이 추가되었다.
    REQUIRE(result.metadata.contains("unknownAuthoringField"));
    CHECK(result.metadata["unknownAuthoringField"] == "preserved");
}

TEST_CASE("font family source cannot override sidecar GUID authority") {
    nlohmann::json source = ValidFontFamilySourceJson();
    source["guid"] = "ffffffffffffffffffffffffffffffff";
    const auto result = ImportTemporaryFontFamily(source);
    CHECK_FALSE(result.success);
    CHECK(HasDiagnostic(result.importDiagnostics,
          molga::text::TextDiagnosticCode::FontFamilyInvalid));
}

TEST_CASE("qualification asset tree rescans six fonts and two licenses") {
    QualificationAssetTreeFixture fixture;
    molga::AssetDatabase db;
    auto store = std::make_shared<const molga::FontArtifactStore>(
        molga::FontArtifactStore::ForProject(fixture.ProjectRoot()));
    REQUIRE(db.BindFontArtifactStore(store));
    db.ScanProject(fixture.AssetsRoot());
    const std::vector<std::tuple<std::string, std::string, int>> expected{
        {"44444444444444444444444444444444", "FontImporter", 2},
        {"55555555555555555555555555555555", "FontImporter", 2},
        {"66666666666666666666666666666666", "FontImporter", 2},
        {"12121212121212121212121212121212", "FontImporter", 2},
        {"13131313131313131313131313131313", "FontImporter", 2},
        {"77777777777777777777777777777777", "FontImporter", 2},
        {"88888888888888888888888888888888", "GenericImporter", 1},
        {"99999999999999999999999999999999", "GenericImporter", 1},
    };
    for (const auto& [guid, importer, version] : expected) {
        const molga::AssetRecord* record = db.Find(guid);
        REQUIRE_MESSAGE(record != nullptr, guid);
        CHECK(record->guid == guid);
        CHECK(record->importer == importer);
        CHECK(record->importerVersion == version);
    }
}

// 위 세 케이스가 요구하는 성공 경로를 아래에서 양방향으로 못 박는다. 실패
// 단언만 있는 함수는 "항상 실패"로 스텁해도 통과하므로, 모든 거절 표는 먼저
// 유효한 대조군을 통과시킨다.

TEST_CASE("a valid authored family imports and survives catalog reload") {
    const auto result = ImportTemporaryFontFamily(ValidFontFamilySourceJson());
    REQUIRE_MESSAGE(result.success, result.error);
    CHECK(result.importDiagnostics.empty());
    // const nlohmann::json의 operator[]는 없는 키에서 assert로 프로세스를
    // 죽인다. 보존이 깨졌을 때 SIGABRT 대신 이 파일의 다른 케이스를 살려 둔
    // 채로 실패하도록, 읽기 전에 존재부터 못 박는다.
    REQUIRE(result.metadata.contains("schemaVersion"));
    CHECK(result.metadata["schemaVersion"] == 1);
    // 저작 순서는 카탈로그에서도 읽을 수 있어야 한다. 배열 위치만 권한으로
    // 두고 기록을 생략하면 리뷰어가 diff에서 순서를 확인할 수 없다.
    REQUIRE(result.metadata.contains("faces"));
    REQUIRE(result.metadata["faces"].size() == 4U);
    REQUIRE(result.metadata["faces"][0].contains("authoredFaceIndex"));
    REQUIRE(result.metadata["faces"][3].contains("authoredFaceIndex"));
    REQUIRE(result.metadata["faces"][0].contains("fontGuid"));
    CHECK(result.metadata["faces"][0]["authoredFaceIndex"] == 0);
    CHECK(result.metadata["faces"][3]["authoredFaceIndex"] == 3);
    CHECK(result.metadata["faces"][0]["fontGuid"] ==
          "44444444444444444444444444444444");

    const AssetRecord record =
        RoundTripFamilyRecord(result, "11111111111111111111111111111111");
    // 알 수 없는 저작 필드는 카탈로그를 한 번 돌고 나서도 남아야 한다. 새
    // 저작 도구가 붙인 필드를 예전 에디터가 재적재하며 지우면 저작 의도가
    // 조용히 사라진다.
    REQUIRE(record.metadata.contains("unknownAuthoringField"));
    CHECK(record.metadata["unknownAuthoringField"] == "preserved");
    const auto family =
        molga::FontFamilyAsset::FromRecord(record, TestError());
    REQUIRE_MESSAGE(family.has_value(), LastFamilyError());
    CHECK(family->guid == "11111111111111111111111111111111");
    CHECK(family->schemaVersion == molga::FontFamilyAsset::CurrentSchemaVersion);
    REQUIRE(family->faces.size() == 4U);
    CHECK(family->faces[0].fontGuid == "44444444444444444444444444444444");
    CHECK(family->faces[0].faceIndex == 0U);
    CHECK(family->faces[0].weight == 400U);
    CHECK(family->faces[0].stretchPercent == 100U);
    CHECK(family->faces[0].slant == molga::FontSlant::Upright);
    CHECK(FaceGuids(*family) == std::vector<std::string>{
        "44444444444444444444444444444444",
        "55555555555555555555555555555555",
        "12121212121212121212121212121212",
        "13131313131313131313131313131313"});
    CHECK(family->fallbackFamilyGuids ==
          std::vector<std::string>{"22222222222222222222222222222222",
                                   "33333333333333333333333333333333"});
}

TEST_CASE("authored face order is the array order and not a sorted order") {
    // Milestone 5의 candidate 정렬은 authoredFaceIndex를 tie-breaker로 쓴다.
    // 그 순서가 GUID 정렬이나 해시 순서에서 파생되면 fallback이 조용히
    // 달라지므로, 사전순과 어긋나는 저작 순서를 그대로 되돌려받아야 한다.
    nlohmann::json source = ValidFontFamilySourceJson();
    std::swap(source["faces"][0], source["faces"][3]);
    const auto result = ImportTemporaryFontFamily(source);
    REQUIRE_MESSAGE(result.success, result.error);

    const AssetRecord record =
        RoundTripFamilyRecord(result, "11111111111111111111111111111111");
    const auto family =
        molga::FontFamilyAsset::FromRecord(record, TestError());
    REQUIRE_MESSAGE(family.has_value(), LastFamilyError());
    CHECK(FaceGuids(*family) == std::vector<std::string>{
        "13131313131313131313131313131313",
        "55555555555555555555555555555555",
        "12121212121212121212121212121212",
        "44444444444444444444444444444444"});
    // 크기부터 못 박는다. 파서가 fail-open으로 퇴행하면 빈 벡터를 인덱싱해
    // 이 파일 전체가 SIGSEGV로 죽고, 나머지 케이스가 통째로 사라진다.
    REQUIRE(family->faces.size() == 4U);
    CHECK(family->faces[0].authoredFaceIndex == 0U);
    CHECK(family->faces[3].authoredFaceIndex == 3U);
}

TEST_CASE("authored fallback order is the array order and not a sorted order") {
    // fallback은 Task 5.1이 저작 순서 그대로 깊이 우선으로 훑는 목록이다.
    // 커밋된 픽스처의 fallback GUID들은 이미 사전순이라 "정렬해도 그대로"이므로,
    // 정렬/역순 퇴행을 잡으려면 사전순과 어긋나는 목록이 따로 필요하다.
    nlohmann::json source = ValidFontFamilySourceJson();
    source["fallbackFamilyGuids"] = nlohmann::json::array(
        {"33333333333333333333333333333333",
         "22222222222222222222222222222222",
         "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"});
    const auto result = ImportTemporaryFontFamily(source);
    REQUIRE_MESSAGE(result.success, result.error);

    // 왕복 단언만 두면 파싱 한 곳에 들어간 역순 같은 대합(involution) 결함이
    // 두 번 적용되며 상쇄된다. importer가 내놓은 metadata를 직접 읽어 단일
    // 적용 증인을 남긴다.
    REQUIRE(result.metadata.contains("fallbackFamilyGuids"));
    CHECK(result.metadata["fallbackFamilyGuids"] ==
          nlohmann::json::array({"33333333333333333333333333333333",
                                 "22222222222222222222222222222222",
                                 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}));

    const AssetRecord record =
        RoundTripFamilyRecord(result, "11111111111111111111111111111111");
    const auto family =
        molga::FontFamilyAsset::FromRecord(record, TestError());
    REQUIRE_MESSAGE(family.has_value(), LastFamilyError());
    CHECK(family->fallbackFamilyGuids ==
          std::vector<std::string>{"33333333333333333333333333333333",
                                   "22222222222222222222222222222222",
                                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"});
}

TEST_CASE("authored face entries keep their own unknown fields in place") {
    // importer는 저작된 face 객체 위에 정규화된 값만 덮어쓴다. 그 사본이
    // 사라지거나 한 칸 밀리면 새 저작 도구가 붙인 face 단위 필드가 조용히
    // 없어지거나 엉뚱한 face로 옮겨 간다.
    nlohmann::json source = ValidFontFamilySourceJson();
    source["faces"][1]["unknownFaceField"] = "second";
    source["faces"][2]["unknownFaceField"] = "third";
    const auto result = ImportTemporaryFontFamily(source);
    REQUIRE_MESSAGE(result.success, result.error);
    REQUIRE(result.metadata.contains("faces"));
    REQUIRE(result.metadata["faces"].size() == 4U);
    CHECK_FALSE(result.metadata["faces"][0].contains("unknownFaceField"));
    REQUIRE(result.metadata["faces"][1].contains("unknownFaceField"));
    CHECK(result.metadata["faces"][1]["unknownFaceField"] == "second");
    REQUIRE(result.metadata["faces"][2].contains("unknownFaceField"));
    CHECK(result.metadata["faces"][2]["unknownFaceField"] == "third");
    CHECK_FALSE(result.metadata["faces"][3].contains("unknownFaceField"));
}

TEST_CASE("the inclusive ends of every authored range are accepted") {
    // 거절 표는 범위 바깥만 찌르므로 한쪽만 증명한다. 상·하한을 한 칸씩
    // 좁히는 퇴행은 그 표를 통째로 통과하므로, 합법적인 끝값이 실제로
    // 살아남는지 여기서 못 박는다.
    nlohmann::json source = ValidFontFamilySourceJson();
    source["faces"][0]["weight"] = molga::kFontWeightMin;
    source["faces"][0]["stretchPercent"] = molga::kFontStretchPercentMin;
    source["faces"][1]["weight"] = molga::kFontWeightMax;
    source["faces"][1]["stretchPercent"] = molga::kFontStretchPercentMax;
    // SFNT collection이 이름 붙일 수 있는 가장 큰 face index.
    source["faces"][2]["faceIndex"] = 0xFFFF;
    source["faces"][3]["faceIndex"] = 0;
    const auto result = ImportTemporaryFontFamily(source);
    REQUIRE_MESSAGE(result.success, result.error);
    CHECK(result.importDiagnostics.empty());

    const AssetRecord record =
        RoundTripFamilyRecord(result, "11111111111111111111111111111111");
    const auto family =
        molga::FontFamilyAsset::FromRecord(record, TestError());
    REQUIRE_MESSAGE(family.has_value(), LastFamilyError());
    REQUIRE(family->faces.size() == 4U);
    CHECK(family->faces[0].weight == molga::kFontWeightMin);
    CHECK(family->faces[0].stretchPercent == molga::kFontStretchPercentMin);
    CHECK(family->faces[1].weight == molga::kFontWeightMax);
    CHECK(family->faces[1].stretchPercent == molga::kFontStretchPercentMax);
    CHECK(family->faces[2].faceIndex == 0xFFFFU);
    CHECK(family->faces[3].faceIndex == 0U);
}

TEST_CASE("authored fallback cycles import because graph validation is Task 5.1") {
    // importer는 cross-asset GUID를 해석하지 않는다. 서로를 가리키는 두 family가
    // 여기서 실패하면 Task 5.1이 검사해야 할 cycle을 import가 먼저 삼켜 버린다.
    const fs::path familyRoot = MOLGA_TEXT_FAMILY_FIXTURE_ROOT;
    const auto cycleA = molga::FontFamilyImporter().Import(
        (familyRoot / "cycle-a.fontfamily").string());
    const auto cycleB = molga::FontFamilyImporter().Import(
        (familyRoot / "cycle-b.fontfamily").string());
    REQUIRE_MESSAGE(cycleA.success, cycleA.error);
    REQUIRE_MESSAGE(cycleB.success, cycleB.error);
    CHECK(cycleA.importDiagnostics.empty());
    CHECK(cycleB.importDiagnostics.empty());

    const auto familyA = molga::FontFamilyAsset::FromRecord(
        RoundTripFamilyRecord(cycleA, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        TestError());
    REQUIRE_MESSAGE(familyA.has_value(), LastFamilyError());
    CHECK(familyA->faces.empty());
    CHECK(familyA->fallbackFamilyGuids ==
          std::vector<std::string>{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"});
    const auto familyB = molga::FontFamilyAsset::FromRecord(
        RoundTripFamilyRecord(cycleB, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        TestError());
    REQUIRE_MESSAGE(familyB.has_value(), LastFamilyError());
    CHECK(familyB->fallbackFamilyGuids ==
          std::vector<std::string>{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"});
}

TEST_CASE("malformed authored family fields are refused with one diagnostic") {
    REQUIRE(ImportTemporaryFontFamily(ValidFontFamilySourceJson()).success);

    struct Mutation {
        const char* label;
        std::function<void(nlohmann::json&)> apply;
    };
    const std::vector<Mutation> mutations{
        {"missing schemaVersion", [](nlohmann::json& s) { s.erase("schemaVersion"); }},
        {"future schemaVersion", [](nlohmann::json& s) { s["schemaVersion"] = 2; }},
        {"float schemaVersion", [](nlohmann::json& s) { s["schemaVersion"] = 1.0; }},
        {"legacy schema key", [](nlohmann::json& s) {
             s.erase("schemaVersion");
             s["schema"] = 1;
         }},
        {"legacy schema beside schemaVersion",
         [](nlohmann::json& s) { s["schema"] = 1; }},
        {"missing faces", [](nlohmann::json& s) { s.erase("faces"); }},
        {"faces not an array", [](nlohmann::json& s) { s["faces"] = 4; }},
        {"face not an object", [](nlohmann::json& s) { s["faces"][1] = "44444444444444444444444444444444"; }},
        {"missing fallbackFamilyGuids",
         [](nlohmann::json& s) { s.erase("fallbackFamilyGuids"); }},
        {"fallback list not an array",
         [](nlohmann::json& s) { s["fallbackFamilyGuids"] = "22222222222222222222222222222222"; }},
        {"fallback entry not a string",
         [](nlohmann::json& s) { s["fallbackFamilyGuids"][0] = 22; }},
        {"fallback entry not 32 hex",
         [](nlohmann::json& s) { s["fallbackFamilyGuids"][1] = "3333"; }},
        {"fallback entry has a non-hex digit",
         [](nlohmann::json& s) { s["fallbackFamilyGuids"][0] = "2222222222222222222222222222222g"; }},
        {"font GUID not a string", [](nlohmann::json& s) { s["faces"][0]["fontGuid"] = 44; }},
        {"font GUID not 32 hex",
         [](nlohmann::json& s) { s["faces"][2]["fontGuid"] = "121212"; }},
        {"missing font GUID", [](nlohmann::json& s) { s["faces"][3].erase("fontGuid"); }},
        {"missing faceIndex", [](nlohmann::json& s) { s["faces"][0].erase("faceIndex"); }},
        {"negative faceIndex", [](nlohmann::json& s) { s["faces"][0]["faceIndex"] = -1; }},
        {"float faceIndex", [](nlohmann::json& s) { s["faces"][0]["faceIndex"] = 0.5; }},
        {"faceIndex above the SFNT collection range",
         [](nlohmann::json& s) { s["faces"][0]["faceIndex"] = 65536; }},
        {"missing weight", [](nlohmann::json& s) { s["faces"][1].erase("weight"); }},
        {"weight below the authored range",
         [](nlohmann::json& s) { s["faces"][1]["weight"] = 0; }},
        {"weight above the authored range",
         [](nlohmann::json& s) { s["faces"][1]["weight"] = 1001; }},
        {"missing stretchPercent",
         [](nlohmann::json& s) { s["faces"][2].erase("stretchPercent"); }},
        {"stretch below the authored range",
         [](nlohmann::json& s) { s["faces"][2]["stretchPercent"] = 49; }},
        {"stretch above the authored range",
         [](nlohmann::json& s) { s["faces"][2]["stretchPercent"] = 201; }},
        {"missing slant", [](nlohmann::json& s) { s["faces"][3].erase("slant"); }},
        {"unknown slant", [](nlohmann::json& s) { s["faces"][3]["slant"] = "Slanted"; }},
        {"slant not a string", [](nlohmann::json& s) { s["faces"][0]["slant"] = 0; }},
        {"authored face index disagrees with its array position",
         [](nlohmann::json& s) { s["faces"][1]["authoredFaceIndex"] = 3; }},
    };
    for (const Mutation& mutation : mutations) {
        nlohmann::json source = ValidFontFamilySourceJson();
        mutation.apply(source);
        const auto result = ImportTemporaryFontFamily(source);
        CHECK_MESSAGE(!result.success, Label(mutation.label));
        CHECK_MESSAGE(HasDiagnostic(result.importDiagnostics,
                                    TextDiagnosticCode::FontFamilyInvalid),
                      Label(mutation.label));
        // 진단 수는 항목당이 아니라 애셋당 상수여야 한다. 잘못된 항목마다
        // 하나씩 쌓으면 큰 저작 파일 하나가 카탈로그를 무한히 키운다.
        CHECK_MESSAGE(result.importDiagnostics.size() == 1U,
                      Label(mutation.label));
        CHECK_MESSAGE(result.metadata.empty(), Label(mutation.label));
    }
}

TEST_CASE("a non-object or unreadable family source is refused") {
    for (const char* text : {"[]", "17", "\"primary\"", "{\"schemaVersion\":1,",
                             ""}) {
        const auto result = ImportTemporaryFontFamilyText(text);
        CHECK_MESSAGE(!result.success, Label(text));
        CHECK_MESSAGE(HasDiagnostic(result.importDiagnostics,
                                    TextDiagnosticCode::FontFamilyInvalid),
                      Label(text));
        CHECK_MESSAGE(result.importDiagnostics.size() == 1U, Label(text));
    }
    const auto missing = molga::FontFamilyImporter().Import(
        (fs::temp_directory_path() / "molga-no-such.fontfamily").string());
    CHECK_FALSE(missing.success);
    CHECK(HasDiagnostic(missing.importDiagnostics,
                        TextDiagnosticCode::FontFamilyInvalid));
}

TEST_CASE("trailing bytes after a complete family document are refused") {
    // 대조군: 같은 문서 하나만 있으면 통과한다.
    const std::string document =
        R"({"schemaVersion":1,"faces":[],"fallbackFamilyGuids":[]})";
    REQUIRE(ImportTemporaryFontFamilyText(document).success);

    // 첫 JSON 값 뒤의 바이트를 조용히 버리면, 문서를 두 개 이어 붙인 저작
    // 파일이 뒤쪽을 통째로 잃은 채 "성공"으로 카탈로그에 들어간다.
    const std::vector<std::pair<const char*, std::string>> trailing{
        {"a second document", R"({"schemaVersion":2})"},
        {"prose", " not json at all"},
        {"binary garbage", "\xff\xfe"}};
    for (const auto& variant : trailing) {
        const std::string label = Label(variant.first);
        const auto result =
            ImportTemporaryFontFamilyText(document + variant.second);
        CHECK_MESSAGE(!result.success, label);
        CHECK_MESSAGE(HasDiagnostic(result.importDiagnostics,
                                    TextDiagnosticCode::FontFamilyInvalid),
                      label);
        CHECK_MESSAGE(result.importDiagnostics.size() == 1U, label);
    }
}

TEST_CASE("an oversized authored family source is refused at the byte cap") {
    // FontFamilyImporter의 상한과 같은 값. 저작 문서 전체가 카탈로그
    // metadata로 들어가므로 이 상한이 record 하나의 성장 한계다. 상수만 있고
    // 증인이 없으면 상한을 지워도 아무 테스트가 울지 않는다.
    constexpr std::uintmax_t kMaximumFontFamilyBytes = 4U * 1024U * 1024U;
    const std::string prefix =
        R"({"schemaVersion":1,"faces":[],"fallbackFamilyGuids":[],"pad":")";
    const std::string suffix = R"("})";
    const auto padded = [&](std::uintmax_t totalBytes) {
        return prefix +
               std::string(static_cast<std::size_t>(totalBytes) -
                               prefix.size() - suffix.size(),
                           'p') +
               suffix;
    };

    const std::string atCap = padded(kMaximumFontFamilyBytes);
    REQUIRE(atCap.size() == kMaximumFontFamilyBytes);
    const auto accepted = ImportTemporaryFontFamilyText(atCap);
    CHECK_MESSAGE(accepted.success, accepted.error);

    const std::string overCap = padded(kMaximumFontFamilyBytes + 1U);
    REQUIRE(overCap.size() == kMaximumFontFamilyBytes + 1U);
    const auto refused = ImportTemporaryFontFamilyText(overCap);
    CHECK_FALSE(refused.success);
    CHECK(HasDiagnostic(refused.importDiagnostics,
                        TextDiagnosticCode::FontFamilyInvalid));
    CHECK(refused.importDiagnostics.size() == 1U);
    CHECK(refused.metadata.empty());
}

TEST_CASE("only .fontfamily reaches the family importer") {
    const molga::FontFamilyImporter importer;
    CHECK(importer.Name() == "FontFamilyImporter");
    CHECK(importer.Version() == 1);
    CHECK(importer.CanImport(".fontfamily"));
    for (const char* extension : {".ttf", ".otf", ".prefab", ".json", ".meta",
                                  ".fontfamilies", ""}) {
        CHECK_MESSAGE(!importer.CanImport(extension), Label(extension));
    }
    // 등록도 확장자 하나만 잡아야 한다. .ttf가 여기로 새면 폰트 바이트가
    // 검증 없이 통과한다.
    const molga::IImporter* registered =
        molga::ImporterRegistry::Get().FindForExtension(".fontfamily");
    REQUIRE(registered != nullptr);
    CHECK(registered->Name() == "FontFamilyImporter");
    const molga::IImporter* forFont =
        molga::ImporterRegistry::Get().FindForExtension(".ttf");
    REQUIRE(forFont != nullptr);
    CHECK(forFont->Name() == "FontImporter");
}

TEST_CASE("FontFamilyAsset::FromRecord refuses records it cannot trust") {
    const auto result = ImportTemporaryFontFamily(ValidFontFamilySourceJson());
    REQUIRE_MESSAGE(result.success, result.error);

    // 대조군: 손대지 않은 record는 통과한다.
    {
        const AssetRecord good =
            RoundTripFamilyRecord(result, "11111111111111111111111111111111");
        CHECK(molga::FontFamilyAsset::FromRecord(good, TestError()).has_value());
    }

    SUBCASE("another importer's record is not an authored family") {
        AssetRecord record =
            RoundTripFamilyRecord(result, "11111111111111111111111111111111");
        record.importer = "GenericImporter";
        CHECK_FALSE(
            molga::FontFamilyAsset::FromRecord(record, TestError()).has_value());
    }
    SUBCASE("a failed import never becomes a family") {
        AssetRecord record =
            RoundTripFamilyRecord(result, "11111111111111111111111111111111");
        record.importFailed = true;
        CHECK_FALSE(
            molga::FontFamilyAsset::FromRecord(record, TestError()).has_value());
    }
    SUBCASE("the record GUID must be 32 hexadecimal characters") {
        for (const char* guid : {"", "1111", "not-a-guid",
                                 "1111111111111111111111111111111g"}) {
            AssetRecord record = RoundTripFamilyRecord(
                result, "11111111111111111111111111111111");
            record.guid = guid;
            CHECK_MESSAGE(!molga::FontFamilyAsset::FromRecord(record, TestError())
                               .has_value(),
                          Label(guid));
        }
    }
    SUBCASE("embedded identity in importer metadata is never authoritative") {
        // 카탈로그가 손으로 편집돼 guid가 다시 들어오면, 조용히 무시하는 대신
        // record를 거절한다. 무시하면 어느 쪽이 정체성인지 리뷰로 알 수 없다.
        AssetRecord record =
            RoundTripFamilyRecord(result, "11111111111111111111111111111111");
        record.metadata["guid"] = "ffffffffffffffffffffffffffffffff";
        CHECK_FALSE(
            molga::FontFamilyAsset::FromRecord(record, TestError()).has_value());
    }
    SUBCASE("a reordered persisted authoredFaceIndex is refused") {
        AssetRecord record =
            RoundTripFamilyRecord(result, "11111111111111111111111111111111");
        record.metadata["faces"][2]["authoredFaceIndex"] = 0;
        CHECK_FALSE(
            molga::FontFamilyAsset::FromRecord(record, TestError()).has_value());
    }
    SUBCASE("a record with no family metadata is refused") {
        AssetRecord record =
            RoundTripFamilyRecord(result, "11111111111111111111111111111111");
        record.metadata = nlohmann::json::object();
        CHECK_FALSE(
            molga::FontFamilyAsset::FromRecord(record, TestError()).has_value());
    }
}

TEST_CASE("a refused document leaves the caller's family untouched") {
    // ParseAuthoredFontFamily는 헤더가 공개한 단일 검증 권한이다. 거절하면서
    // out을 절반만 덮어쓰면, 편집을 거절했다고 믿는 호출자가 첫 불량 항목에서
    // 잘린 face 목록을 그대로 들고 있게 된다.
    molga::FontFamilyAsset family;
    std::string error;
    REQUIRE(molga::ParseAuthoredFontFamily(ValidFontFamilySourceJson(), family,
                                           error));
    family.guid = "11111111111111111111111111111111";
    const molga::FontFamilyAsset before = family;

    nlohmann::json broken = ValidFontFamilySourceJson();
    // 두 번째 항목에서 거절된다. 첫 항목은 이미 파싱된 뒤다.
    broken["faces"][1]["fontGuid"] = "5555";
    CHECK_FALSE(molga::ParseAuthoredFontFamily(broken, family, error));
    CHECK_FALSE(error.empty());
    CHECK(family.guid == before.guid);
    CHECK(family.schemaVersion == before.schemaVersion);
    CHECK(FaceGuids(family) == FaceGuids(before));
    CHECK(family.fallbackFamilyGuids == before.fallbackFamilyGuids);

    // fallback 목록에서 거절될 때도 마찬가지다.
    nlohmann::json brokenFallback = ValidFontFamilySourceJson();
    brokenFallback["fallbackFamilyGuids"][1] = "3333";
    CHECK_FALSE(
        molga::ParseAuthoredFontFamily(brokenFallback, family, error));
    CHECK(FaceGuids(family) == FaceGuids(before));
    CHECK(family.fallbackFamilyGuids == before.fallbackFamilyGuids);
}

TEST_CASE("the qualification tree copies every source with its sidecar") {
    QualificationAssetTreeFixture fixture;
    std::size_t files = 0;
    for (const auto& entry :
         fs::recursive_directory_iterator(fixture.AssetsRoot())) {
        if (entry.is_regular_file()) ++files;
    }
    // 13개 소스 × (소스 + .meta). 한 짝이라도 빠지면 ScanProject가 새 GUID를
    // 만들어 고정된 GUID 계약이 조용히 무너진다.
    CHECK(files == kQualificationSources.size() * 2U);
    for (const std::string_view relative : kQualificationSources) {
        const fs::path source = fixture.AssetsRoot() / std::string(relative);
        CHECK_MESSAGE(fs::is_regular_file(source), relative);
        CHECK_MESSAGE(fs::is_regular_file(molga::AssetMeta::MetaPathFor(source)),
                      relative);
    }
}

TEST_CASE("every qualification tree record imports into a usable asset") {
    // 위의 재스캔 케이스는 GUID/importer/version만 본다. 여섯 폰트가 전부
    // 거절당해도 record는 남으므로 그 케이스는 그대로 통과한다. Milestone 5는
    // 이 트리가 실제로 열리는 face와 family를 준다고 가정하므로, 성공 증인을
    // 여기서 따로 못 박는다.
    QualificationAssetTreeFixture fixture;
    molga::AssetDatabase db;
    REQUIRE(db.BindFontArtifactStore(ProjectStore(fixture.ProjectRoot())));
    db.ScanProject(fixture.AssetsRoot());

    for (const char* guid : {"44444444444444444444444444444444",
                             "55555555555555555555555555555555",
                             "66666666666666666666666666666666",
                             "12121212121212121212121212121212",
                             "13131313131313131313131313131313",
                             "77777777777777777777777777777777"}) {
        const AssetRecord* record = db.Find(guid);
        REQUIRE_MESSAGE(record != nullptr, Label(guid));
        CHECK_MESSAGE(!record->importFailed, record->importError);
        CHECK_MESSAGE(record->importDiagnostics.empty(), Label(guid));
        const auto font = molga::FontAsset::FromRecord(*record, TestError());
        REQUIRE_MESSAGE(font.has_value(), LastFamilyError());
        CHECK_MESSAGE(font->guid == guid, Label(guid));
        CHECK_MESSAGE(font->license.redistributableConfirmed, Label(guid));
        CHECK_MESSAGE(!font->coverage.empty(), Label(guid));
    }

    // 다섯 family는 전부 열리고, primary만 face를 네 개 저작한다.
    const std::vector<std::pair<std::string, std::size_t>> families{
        {"11111111111111111111111111111111", 4U},
        {"22222222222222222222222222222222", 1U},
        {"33333333333333333333333333333333", 1U},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 0U},
        {"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 0U}};
    for (const auto& expected : families) {
        // 구조적 바인딩을 doctest 메시지 람다가 캡처하면 C++20 확장 경고가
        // 나므로, 이름 있는 지역 변수로 받는다.
        const std::string& guid = expected.first;
        const std::size_t faceCount = expected.second;
        const AssetRecord* record = db.Find(guid);
        REQUIRE_MESSAGE(record != nullptr, guid);
        CHECK_MESSAGE(!record->importFailed, record->importError);
        // family는 저작된 GUID 목록일 뿐이라 불변 바이트 권한을 만들지 않는다.
        // 여기에 산출물이 붙으면 폰트가 아닌 파일이 폰트 바이트 경로로 발행된
        // 것이므로, 없음을 명시적으로 못 박는다.
        CHECK_MESSAGE(!record->fontArtifact.has_value(), guid);
        // 일반 캐시 필드는 그대로 유지된다(보안 정체성이 아니다).
        CHECK_MESSAGE(!record->hash.empty(), guid);
        const auto family =
            molga::FontFamilyAsset::FromRecord(*record, TestError());
        REQUIRE_MESSAGE(family.has_value(), LastFamilyError());
        CHECK_MESSAGE(family->faces.size() == faceCount, guid);
    }

    // 저작된 face GUID가 같은 트리 안의 폰트 record를 실제로 가리킨다.
    // Task 5.1이 해석할 대상이 있어야 이 픽스처가 의미를 가진다.
    const AssetRecord* primary = db.Find("11111111111111111111111111111111");
    REQUIRE(primary != nullptr);
    const auto family =
        molga::FontFamilyAsset::FromRecord(*primary, TestError());
    REQUIRE_MESSAGE(family.has_value(), LastFamilyError());
    for (const molga::FontFamilyFaceEntry& face : family->faces) {
        CHECK_MESSAGE(db.Find(face.fontGuid) != nullptr, face.fontGuid);
    }
    for (const std::string& fallback : family->fallbackFamilyGuids) {
        CHECK_MESSAGE(db.Find(fallback) != nullptr, fallback);
    }
}

// ── Task 4.3 Step 1 ─────────────────────────────────────────────────────────

TEST_CASE("old font resource keeps exact bytes after successful replacement") {
    FontRepositoryFixture fixture;
    auto oldResource = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(oldResource);
    const std::string oldSha = (*oldResource)->sourceSha256;
    fixture.ReplaceFontWithVerifiedBytes("font-a", MOLGA_TEXT_ARABIC_FONT);
    fixture.repository.Invalidate("font-a");
    auto newResource = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(newResource);
    CHECK((*oldResource)->sourceSha256 == oldSha);
    CHECK((*newResource)->sourceSha256 != oldSha);
    CHECK((*oldResource)->rasterFace->FaceIndex() == 0);
}

// 위 블록은 옛 소유자가 살아남는지만 본다. 그 소유자가 여전히 "쓸 수 있는"
// 자원인지 — 옛 바이트로 만들어진 래스터 face가 새 바이트를 절대 보지 않는지
// — 는 아래에서 못 박는다. 라틴 face에는 없는 아랍 문자가 그 증인이다.
TEST_CASE("a replaced font never re-rasterizes an old resource against new bytes") {
    FontRepositoryFixture fixture;
    const auto oldResource = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(oldResource);
    // 아래는 전부 두 포인터를 역참조한다. 이 둘이 널로 회귀하면 진단 가능한
    // 실패가 아니라 정의되지 않은 동작이 되므로 먼저 못 박는다.
    REQUIRE((*oldResource)->bytes != nullptr);
    REQUIRE((*oldResource)->rasterFace != nullptr);
    const std::vector<std::uint8_t> oldBytes = *(*oldResource)->bytes;
    REQUIRE((*oldResource)->rasterFace->HasCodepoint(U'A'));
    REQUIRE_FALSE((*oldResource)->rasterFace->HasCodepoint(U'س'));

    fixture.ReplaceFontWithVerifiedBytes("font-a", MOLGA_TEXT_ARABIC_FONT);
    fixture.repository.Invalidate("font-a");
    const auto newResource = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(newResource);
    REQUIRE((*newResource)->rasterFace != nullptr);
    CHECK((*newResource)->rasterFace->HasCodepoint(U'س'));

    CHECK(*(*oldResource)->bytes == oldBytes);
    CHECK((*oldResource)->rasterFace->HasCodepoint(U'A'));
    CHECK_FALSE((*oldResource)->rasterFace->HasCodepoint(U'س'));
    CHECK((*oldResource)->artifactSha256 != (*newResource)->artifactSha256);
    CHECK((*oldResource)->designMetrics.ascender !=
          (*newResource)->designMetrics.ascender);
}

TEST_CASE("repeated matching loads share one resource and Invalidate drops it") {
    FontRepositoryFixture fixture;
    const auto first = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(first);
    const auto second = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(second);
    // 같은 정체성의 반복 적재는 같은 공유 자원이어야 한다.
    CHECK(*first == *second);

    fixture.repository.Invalidate("font-a");
    const auto rebuilt = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(rebuilt);
    // Invalidate는 캐시 소유권만 버린다: 새 적재는 다른 객체지만 같은 내용이고,
    // 이미 돌려준 공유 자원은 손대지 않는다.
    CHECK_FALSE(*rebuilt == *first);
    CHECK((*rebuilt)->artifactSha256 == (*first)->artifactSha256);
    CHECK(*(*rebuilt)->bytes == *(*first)->bytes);
    CHECK((*first)->rasterFace->HasCodepoint(U'A'));
    CHECK(fixture.sink.Diagnostics().empty());
    const auto cached = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(cached);
    CHECK(*cached == *rebuilt);

    // 발행된 내용이 실제로 바뀌면 Invalidate 없이도 새 적재는 새 자원이다.
    // 캐시 정체성이 guid/face만 본다면 여기서 옛 바이트가 그대로 돌아온다.
    fixture.ReplaceFontWithVerifiedBytes("font-a", MOLGA_TEXT_ARABIC_FONT);
    const auto replaced = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(replaced);
    CHECK_FALSE(*replaced == *rebuilt);
    CHECK((*replaced)->artifactSha256 != (*rebuilt)->artifactSha256);
    CHECK((*replaced)->rasterFace->HasCodepoint(U'س'));
    CHECK((*rebuilt)->rasterFace->HasCodepoint(U'A'));
}

// ── Task 4.3 Step 1a ────────────────────────────────────────────────────────

TEST_CASE("failed font replacement preserves last-good generation and resource") {
    FontRepositoryFixture fixture;
    const auto before = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(before);
    const auto generation = fixture.database.ContentGeneration("font-a");
    fixture.ReplaceFontWithCorruptBytes("font-a");
    CHECK_FALSE(fixture.database.TryReimport("font-a"));
    CHECK(fixture.database.ContentGeneration("font-a") == generation);
    const auto after = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(after);
    CHECK(*after == *before);
    CHECK(HasDiagnostic(fixture.sink,
          molga::text::TextDiagnosticCode::FontInvalid));
}

TEST_CASE("a failed reimport keeps the last-good catalog artifact untouched") {
    FontRepositoryFixture fixture;
    const auto before = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(before);
    fixture.ReplaceFontWithCorruptBytes("font-a");
    const AssetRecord* failed =
        fixture.database.Real().Find(fixture.Guid("font-a"));
    REQUIRE(failed != nullptr);
    CHECK(failed->importFailed);
    REQUIRE(failed->fontArtifact.has_value());
    REQUIRE(fixture.PriorRecord().fontArtifact.has_value());
    CHECK(failed->fontArtifact->artifactSha256 ==
          fixture.PriorRecord().fontArtifact->artifactSha256);
    CHECK(failed->fontArtifact->locator ==
          fixture.PriorRecord().fontArtifact->locator);
    CHECK(fs::exists(fixture.PublishedArtifact("font-a")));
    // 손상된 저작 원본은 발행된 산출물과 다른 바이트다. 그런데도 마지막 정상
    // 자원은 그대로다 — 원본이 권한이었다면 이 단언이 깨진다.
    std::string sourceShaError;
    CHECK(molga::Sha256File(fixture.AuthoringSource(), &sourceShaError) !=
          failed->fontArtifact->artifactSha256);
}

TEST_CASE("a content generation advances only on a successfully published change") {
    FontRepositoryFixture fixture;
    const std::uint64_t initial = fixture.database.ContentGeneration("font-a");
    CHECK(initial == 0U);
    // 같은 바이트를 다시 발행해도 발행된 정체성은 그대로다.
    REQUIRE(fixture.database.TryReimport("font-a"));
    CHECK(fixture.database.ContentGeneration("font-a") == initial);
    // 실제로 다른 바이트가 발행되면 정확히 한 번 오른다.
    fixture.ReplaceFontWithVerifiedBytes("font-a", MOLGA_TEXT_ARABIC_FONT);
    CHECK(fixture.database.ContentGeneration("font-a") == initial + 1U);
    // 성공한 두 번의 재발행은 진단을 하나도 남기지 않는다. 아래 손상 케이스의
    // FontInvalid가 fixture 배관이 아니라 importer 출력임을 가르는 대조다.
    CHECK(fixture.sink.Diagnostics().empty());
    // 실패한 import는 세대를 올리지 않는다.
    fixture.ReplaceFontWithCorruptBytes("font-a");
    CHECK(HasDiagnostic(fixture.sink, TextDiagnosticCode::FontInvalid));
    CHECK(fixture.database.ContentGeneration("font-a") == initial + 1U);
    CHECK_FALSE(fixture.database.TryReimport("font-a"));
    CHECK(fixture.database.ContentGeneration("font-a") == initial + 1U);
    // 모르는 GUID는 세대를 갖지 않는다.
    CHECK(fixture.database.Real().ContentGeneration(
              "ffffffffffffffffffffffffffffffff") == 0U);
}

TEST_CASE("a font family content generation follows its authored document") {
    QualificationAssetTreeFixture tree;
    molga::AssetDatabase database;
    REQUIRE(database.BindFontArtifactStore(ProjectStore(tree.ProjectRoot())));
    database.ScanProject(tree.AssetsRoot());
    const std::string family = "11111111111111111111111111111111";
    REQUIRE(database.Find(family) != nullptr);
    CHECK(database.ContentGeneration(family) == 0U);
    REQUIRE(database.TryReimport(family));
    CHECK(database.ContentGeneration(family) == 0U);

    const fs::path source = database.AbsoluteSourcePath(family);
    REQUIRE(fs::exists(source));
    nlohmann::json document;
    { std::ifstream input(source); REQUIRE(input.good()); input >> document; }
    document["fallbackFamilyGuids"] = nlohmann::json::array(
        {"33333333333333333333333333333333",
         "22222222222222222222222222222222"});
    { std::ofstream output(source, std::ios::trunc); REQUIRE(output.good());
      output << document.dump(2); }
    REQUIRE(database.TryReimport(family));
    CHECK(database.ContentGeneration(family) == 1U);
}

// 위 두 케이스는 TryReimport만 태운다. 에디터 밖에서 바이트가 바뀌는 흔한
// 경로는 전체 재스캔이고, 재스캔은 byGuid_를 통째로 다시 세운다 — 직전 발행을
// 그 자리에서 찾으면 언제나 비어 있으므로, 스냅샷 없이는 완전히 다른 폰트를
// 스캔해도 세대가 움직이지 않는다.
TEST_CASE("a rescan publishes a new generation only when the bytes changed") {
    TempProject project("rescan-generation");
    fs::create_directories(project.assets / "Fonts");
    const fs::path source = project.assets / "Fonts" / "font.ttf";
    fs::copy_file(MOLGA_TEXT_LATIN_FONT, source);
    const std::string guid = "0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c";
    WriteFontMeta(source, guid, ValidStaticFontSettings());

    molga::AssetDatabase database;
    REQUIRE(database.BindFontArtifactStore(ProjectStore(project.root)));
    database.ScanProject(project.assets);
    REQUIRE(database.Find(guid) != nullptr);
    CHECK(database.ContentGeneration(guid) == 0U);

    // 같은 바이트를 다시 스캔해도 발행된 정체성은 그대로다.
    database.ScanProject(project.assets);
    CHECK(database.ContentGeneration(guid) == 0U);

    // 실제로 다른 폰트가 발행되면 정확히 한 번 오른다.
    fs::copy_file(MOLGA_TEXT_ARABIC_FONT, source,
                  fs::copy_options::overwrite_existing);
    database.ScanProject(project.assets);
    const AssetRecord* replaced = database.Find(guid);
    REQUIRE(replaced != nullptr);
    REQUIRE_MESSAGE(!replaced->importFailed, replaced->importError);
    CHECK(database.ContentGeneration(guid) == 1U);

    // 카탈로그를 버리는 지점은 세대도 함께 버린다. 남겨 두면 프로젝트 A가
    // 올려 둔 세대가, 같은 GUID를 .meta로 물려받은 프로젝트 B의 다른 폰트에
    // 그대로 붙어 옛 캐시가 최신으로 보인다.
    database.Clear();
    CHECK(database.ContentGeneration(guid) == 0U);
}

// 중복 GUID는 두 record를 모두 실패로 만든다. 실패한 import는 세대를 발행하지
// 않는다 — 발행하면 마지막 정상 리소스가 이유 없이 무효가 되고, 되돌릴 지점도
// 없다.
TEST_CASE("a duplicate font guid publishes no content generation") {
    TempProject project("duplicate-generation");
    fs::create_directories(project.assets / "Fonts");
    const std::string guid = "0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d0d";
    const fs::path first = project.assets / "Fonts" / "a.ttf";
    fs::copy_file(MOLGA_TEXT_LATIN_FONT, first);
    WriteFontMeta(first, guid, ValidStaticFontSettings());
    const fs::path second = project.assets / "Fonts" / "b.ttf";
    fs::copy_file(MOLGA_TEXT_ARABIC_FONT, second);
    WriteFontMeta(second, guid, ValidStaticFontSettings());

    molga::AssetDatabase database;
    REQUIRE(database.BindFontArtifactStore(ProjectStore(project.root)));
    database.ScanProject(project.assets);
    const AssetRecord* clashed = database.Find(guid);
    REQUIRE(clashed != nullptr);
    CHECK(clashed->importFailed);
    // 나중에 색인된 파일은 앞선 record와 발행된 정체성이 달라 보이지만, 중복
    // 검사가 그것을 실패로 만든다. 어느 순서로 색인되든 세대는 움직이지 않는다.
    CHECK(database.ContentGeneration(guid) == 0U);

    // 성공 증인: 충돌을 없애면 같은 스캔이 정상 record를 만든다.
    fs::remove(second);
    fs::remove(molga::AssetMeta::MetaPathFor(second));
    database.ScanProject(project.assets);
    const AssetRecord* healthy = database.Find(guid);
    REQUIRE(healthy != nullptr);
    CHECK_FALSE(healthy->importFailed);
}

// ── Task 4.3 Step 1c ────────────────────────────────────────────────────────

TEST_CASE("import publication, not a cached resource, owns the last-good bytes") {
    FontRepositoryFixture fixture;
    const std::string artifactSha = fixture.CatalogArtifactSha("font-a");
    // 아직 어떤 자원도 만들지 않은 상태에서 저작 원본만 손상시킨다.
    fixture.CorruptAuthoringSourceWithoutReimport();
    std::string sourceShaError;
    REQUIRE(molga::Sha256File(fixture.AuthoringSource(), &sourceShaError) !=
            artifactSha);
    // 위 확인 자체가 원본을 한 번 읽으므로 기준선을 다시 잡는다.
    fixture.ArmAuthoringSourceDetector();

    const auto resource = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(resource);
    REQUIRE((*resource)->bytes != nullptr);
    CHECK((*resource)->artifactSha256 == artifactSha);
    CHECK((*resource)->sourceSha256 == artifactSha);
    CHECK(molga::Sha256Bytes((*resource)->bytes->data(),
                             (*resource)->bytes->size()) == artifactSha);
    CHECK(*(*resource)->bytes == ReadAllBytes(fixture.PublishedArtifact("font-a")));
    REQUIRE((*resource)->rasterFace != nullptr);
    CHECK((*resource)->rasterFace->GlyphId(U'A') != 0U);
    CHECK((*resource)->rasterFace->HasCodepoint(U'A'));
    CHECK(fixture.sink.Diagnostics().empty());
    CHECK(fixture.AuthoringSourceOpenCount() == 0U);
    // 양성 대조: 검출기가 죽어서 0을 돌려준 것이 아니다.
    fixture.CheckAuthoringSourceDetectorIsLive();
}

// ── Task 4.3 Step 1d ────────────────────────────────────────────────────────

TEST_CASE("a restarted database and repository serve the same last-good font") {
    FontRepositoryFixture fixture;
    // 재시작 전에 세대를 실제로 한 번 올려 둔다. 그래야 아래 "재시작은 세대를
    // 물려받지 않는다"가 0 == 0인 항등식이 아니라 단언이 된다. 바이트는 그대로
    // 두는 저작 변경이라, 아래 SHA/메트릭/글리프 비교는 그대로 성립한다.
    auto heavier = ValidStaticFontSettings();
    heavier["weight"] = 700;
    fixture.RewriteAuthoredSettings("font-a", heavier);
    const auto original = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(original);
    REQUIRE((*original)->asset != nullptr);
    REQUIRE((*original)->contentGeneration == 1U);
    const std::uint64_t generation = fixture.database.ContentGeneration("font-a");
    REQUIRE(generation == 1U);
    // 지속되는 것은 마지막으로 성공한 발행의 카탈로그다.
    const fs::path catalog = fixture.PersistCatalog();
    fixture.ReplaceFontWithCorruptBytes("font-a");
    CHECK_FALSE(fixture.database.TryReimport("font-a"));

    // fixture가 임시 프로젝트 트리를 소유하므로 원본 database/repository를
    // 실제로 파괴하면 산출물까지 사라진다. 가능한 가장 강한 형태는 상태 통로를
    // 하나도 공유하지 않는 새 객체다: 별도의 AssetDatabase, 별도로 구성한
    // store, 인스턴스마다 따로인 repository 캐시, 그리고 AssetDatabase::Get()
    // 싱글턴을 거치지 않는 경로.
    molga::AssetDatabase restarted;
    REQUIRE(restarted.BindFontArtifactStore(ProjectStore(fixture.ProjectRoot())));
    std::string catalogError;
    REQUIRE_MESSAGE(restarted.LoadCatalog(catalog, fixture.ProjectRoot(),
                                          molga::AssetCatalogMode::Project,
                                          &catalogError),
                    catalogError);
    molga::text::FontRepository repository(restarted);
    molga::text::VectorTextDiagnosticSink restartedSink;
    fixture.ArmAuthoringSourceDetector();

    const auto reloaded =
        repository.Load(fixture.Guid("font-a"), 0, restartedSink);
    REQUIRE(reloaded);
    REQUIRE((*reloaded)->asset != nullptr);
    CHECK((*reloaded)->asset->guid == fixture.Guid("font-a"));
    // 세대는 프로세스 지역 신호이고 카탈로그에 실리지 않는다. 재시작은 "내용이
    // 바뀐 것"이 아니므로 새 프로세스는 다시 0에서 시작하고, 같은 폰트라는
    // 근거는 세대가 아니라 아래의 SHA/경로/메트릭/글리프다.
    CHECK(restarted.ContentGeneration(fixture.Guid("font-a")) == 0U);
    CHECK((*reloaded)->contentGeneration == 0U);
    CHECK((*reloaded)->asset->weight == (*original)->asset->weight);
    CHECK((*reloaded)->sourceSha256 == (*original)->sourceSha256);
    CHECK((*reloaded)->artifactSha256 == (*original)->artifactSha256);
    CHECK((*reloaded)->artifactLocator == (*original)->artifactLocator);
    CHECK((*reloaded)->faceIndex == (*original)->faceIndex);
    CHECK((*reloaded)->bytes->size() == (*original)->bytes->size());
    CHECK(*(*reloaded)->bytes == *(*original)->bytes);
    CHECK((*reloaded)->designMetrics.unitsPerEm ==
          (*original)->designMetrics.unitsPerEm);
    CHECK((*reloaded)->designMetrics.ascender ==
          (*original)->designMetrics.ascender);
    CHECK((*reloaded)->designMetrics.descender ==
          (*original)->designMetrics.descender);
    CHECK((*reloaded)->designMetrics.lineGap ==
          (*original)->designMetrics.lineGap);
    CHECK((*reloaded)->rasterFace->GlyphId(U'A') ==
          (*original)->rasterFace->GlyphId(U'A'));
    CHECK((*reloaded)->rasterFace->GlyphId(U'A') != 0U);
    CHECK(restartedSink.Diagnostics().empty());
    CHECK(fixture.AuthoringSourceOpenCount() == 0U);
    fixture.CheckAuthoringSourceDetectorIsLive();
}

// ── Task 4.3 Step 1e ────────────────────────────────────────────────────────

TEST_CASE("a corrupt artifact fails closed and never reopens the intact source") {
    FontRepositoryFixture fixture;
    fixture.CorruptPublishedArtifact("font-a");
    const auto refused = fixture.repository.Load("font-a", 0, fixture.sink);
    CHECK_FALSE(refused.has_value());
    CHECK(HasDiagnostic(fixture.sink, TextDiagnosticCode::FontInvalid));
    CHECK(fixture.AuthoringSourceIsIntact());
    CHECK(fixture.AuthoringSourceOpenCount() == 0U);

    // 두 방향의 대조. 산출물을 되돌리면 같은 호출이 성공하므로, 위 실패는
    // 배관이 아니라 산출물 불일치가 만든 것이다.
    fixture.RestorePublishedArtifact("font-a");
    const auto repaired = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(repaired);
    CHECK((*repaired)->artifactSha256 == fixture.CatalogArtifactSha("font-a"));
    CHECK(fixture.AuthoringSourceOpenCount() == 0U);
    fixture.CheckAuthoringSourceDetectorIsLive();
}

TEST_CASE("a font resource load refuses a face the catalog does not carry") {
    FontRepositoryFixture fixture;
    REQUIRE(fixture.repository.Load("font-a", 0, fixture.sink));
    CHECK(fixture.sink.Diagnostics().empty());
    CHECK_FALSE(fixture.repository.Load("font-a", 1, fixture.sink).has_value());
    CHECK(HasDiagnostic(fixture.sink, TextDiagnosticCode::FontInvalid));
}

TEST_CASE("a font repository without a bound artifact store fails closed") {
    molga::AssetDatabase unbound;
    REQUIRE(unbound.FontArtifacts() == nullptr);
    molga::text::FontRepository repository(unbound);
    molga::text::VectorTextDiagnosticSink sink;
    CHECK_FALSE(repository.Load(FontRepositoryFixture::kFontAGuid, 0, sink)
                    .has_value());
    REQUIRE(sink.Diagnostics().size() == 1U);
    CHECK(sink.Diagnostics().front().code == TextDiagnosticCode::FontInvalid);
    CHECK(sink.Diagnostics().front().assetGuid ==
          FontRepositoryFixture::kFontAGuid);
    const std::string unboundMessage = sink.Diagnostics().front().message;

    // 대조: store는 있으나 record가 없는 경우는 다른 사유로 거절된다. 두 경로가
    // 같은 문자열로 무너지면 어느 쪽이 걸렸는지 구분할 수 없다.
    FontRepositoryFixture fixture;
    const auto loaded = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(loaded);
    CHECK(*loaded != nullptr);
    molga::text::VectorTextDiagnosticSink missingSink;
    molga::text::FontRepository bound(fixture.database.Real());
    CHECK_FALSE(bound.Load("ffffffffffffffffffffffffffffffff", 0, missingSink)
                    .has_value());
    REQUIRE(missingSink.Diagnostics().size() == 1U);
    CHECK(missingSink.Diagnostics().front().message != unboundMessage);
}

// 여기까지의 저장소 케이스는 전부 face 0짜리 단일 face 폰트를 face 0으로
// 적재한다. 그러면 요청 index를 무시하는 회귀 — 캐시 키에서, 요청 검사에서,
// 혹은 LoadFromBytes 호출에서 — 가 아무 단언도 깨뜨리지 않는다. 저작된 face가
// 1인 컬렉션 하나가 그 차원 전체를 못 박는다.
TEST_CASE("a repository resource binds the authored collection face index") {
    const ScopedTempFont ttc =
        BuildTwoFaceTtc(MOLGA_TEXT_LATIN_FONT, MOLGA_TEXT_ARABIC_FONT);
    auto settings = ValidStaticFontSettings();
    settings["faceIndex"] = 1;
    FontRepositoryFixture fixture(ttc.path, settings);

    const auto resource = fixture.repository.Load("font-a", 1, fixture.sink);
    REQUIRE(resource);
    REQUIRE((*resource)->bytes != nullptr);
    REQUIRE((*resource)->rasterFace != nullptr);
    CHECK((*resource)->faceIndex == 1U);
    CHECK((*resource)->rasterFace->FaceIndex() == 1U);
    // 컬렉션의 두 face는 서로에게 없는 문자를 갖는다. face 0이 열렸다면 아래
    // 두 단언이 정확히 뒤집힌다.
    CHECK((*resource)->rasterFace->HasCodepoint(U'س'));
    CHECK_FALSE((*resource)->rasterFace->HasCodepoint(U'A'));
    const auto arabic = ParseReferenceDesignMetrics(MOLGA_TEXT_ARABIC_FONT);
    const auto latin = ParseReferenceDesignMetrics(MOLGA_TEXT_LATIN_FONT);
    REQUIRE(arabic.ascender != latin.ascender);
    CHECK((*resource)->designMetrics.ascender == arabic.ascender);
    CHECK(fixture.sink.Diagnostics().empty());

    // 저작된 face가 1이면 face 0 요청은 자원이 아니다.
    CHECK_FALSE(fixture.repository.Load("font-a", 0, fixture.sink).has_value());
    CHECK(HasDiagnostic(fixture.sink, TextDiagnosticCode::FontInvalid));

    // 캐시 항목은 face 1에 있다. face 0으로 게시되었다면 아래 반복 적재가 새
    // 객체를 만든다.
    const auto again = fixture.repository.Load("font-a", 1, fixture.sink);
    REQUIRE(again);
    CHECK(*again == *resource);
}

// Step 7b의 마지막 정상 자원은 "캐시에 뭔가 있다"가 아니라 "그것이 지금
// 카탈로그가 가리키는 마지막 성공 발행이다"라는 조건이다. 성공한 교체가
// 그 사이에 끼면 옛 캐시는 더 이상 마지막 정상이 아니다.
TEST_CASE("a superseded resource is not served as last-good after a failure") {
    FontRepositoryFixture fixture;
    const auto latin = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(latin);
    REQUIRE((*latin)->rasterFace != nullptr);
    REQUIRE((*latin)->rasterFace->HasCodepoint(U'A'));
    const std::uint64_t cachedGeneration =
        fixture.database.ContentGeneration("font-a");

    // Invalidate 없이 성공한 교체를 발행한다. 캐시에는 여전히 라틴 자원이
    // 있고, 카탈로그는 이미 아랍 산출물을 가리킨다.
    fixture.ReplaceFontWithVerifiedBytes("font-a", MOLGA_TEXT_ARABIC_FONT);
    REQUIRE(fixture.database.ContentGeneration("font-a") == cachedGeneration + 1U);
    // 그 위에서 import를 실패시킨다. 실패는 카탈로그의 아랍 산출물을 그대로
    // 남기므로, 라틴 캐시를 미리보기로 돌려주면 옛 폰트로 되돌아가 버린다.
    fixture.ReplaceFontWithCorruptBytes("font-a");
    molga::text::VectorTextDiagnosticSink supersededSink;
    CHECK_FALSE(
        fixture.repository.Load("font-a", 0, supersededSink).has_value());
    CHECK(HasDiagnostic(supersededSink, TextDiagnosticCode::FontInvalid));
    // 이미 돌려준 자원 자체는 불변이다.
    CHECK((*latin)->rasterFace->HasCodepoint(U'A'));
    CHECK_FALSE((*latin)->rasterFace->HasCodepoint(U'س'));
}

// 같은 규칙의 다른 절반. 산출물 바이트는 그대로이고 authored 정체성만 바뀐 뒤
// import가 실패하면, 캐시된 자원과 record의 산출물 정체성은 전부 일치한다 —
// 세대만이 "이 캐시는 옛 저작이다"를 말해 준다.
TEST_CASE("a last-good resource whose authored identity moved on is refused") {
    FontRepositoryFixture fixture;
    const auto first = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(first);
    REQUIRE((*first)->asset != nullptr);
    REQUIRE((*first)->asset->weight == 400U);

    auto heavier = ValidStaticFontSettings();
    heavier["weight"] = 700;
    fixture.RewriteAuthoredSettings("font-a", heavier);
    REQUIRE(fixture.database.ContentGeneration("font-a") == 1U);
    // 같은 바이트이므로 SHA도 경로도 크기도 그대로다.
    REQUIRE(fixture.CatalogArtifactSha("font-a") == (*first)->artifactSha256);

    fixture.ReplaceFontWithCorruptBytes("font-a");
    molga::text::VectorTextDiagnosticSink staleSink;
    CHECK_FALSE(fixture.repository.Load("font-a", 0, staleSink).has_value());
    CHECK(HasDiagnostic(staleSink, TextDiagnosticCode::FontInvalid));
}

// Step 6 정체성의 contentGeneration 항목. 산출물이 한 바이트도 바뀌지 않은 채
// 발행된 정체성만 달라지는 유일한 방법이라, 이 케이스만이 그 항목과 자원에
// 실린 세대 값을 함께 못 박는다.
TEST_CASE("a republished authored identity refreshes the cached resource") {
    FontRepositoryFixture fixture;
    const auto first = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(first);
    REQUIRE((*first)->asset != nullptr);
    CHECK((*first)->contentGeneration ==
          fixture.database.ContentGeneration("font-a"));

    auto heavier = ValidStaticFontSettings();
    heavier["weight"] = 700;
    fixture.RewriteAuthoredSettings("font-a", heavier);
    const std::uint64_t generation = fixture.database.ContentGeneration("font-a");
    REQUIRE(generation == 1U);

    // Invalidate 없이 다시 적재한다. SHA도 경로도 크기도 그대로이므로 캐시를
    // 갈아 끼울 근거는 세대뿐이다.
    const auto refreshed = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(refreshed);
    REQUIRE((*refreshed)->asset != nullptr);
    CHECK_FALSE(*refreshed == *first);
    CHECK((*refreshed)->contentGeneration == generation);
    CHECK((*refreshed)->asset->weight == 700U);
    CHECK((*refreshed)->artifactSha256 == (*first)->artifactSha256);
    CHECK((*refreshed)->artifactLocator == (*first)->artifactLocator);
    CHECK(*(*refreshed)->bytes == *(*first)->bytes);
    // 옛 자원은 자기 세대와 자기 authored 정체성을 그대로 들고 있다.
    CHECK((*first)->contentGeneration == 0U);
    CHECK((*first)->asset->weight == 400U);
    CHECK(fixture.sink.Diagnostics().empty());
}

// Step 6 정체성의 artifactByteSize 항목. 카탈로그가 SHA 등식과 content-address
// 규칙을 강제하므로 두 SHA와 프로젝트 경로는 언제나 함께 움직이지만, 기록된
// 바이트 크기는 그 셋과 독립으로 어긋날 수 있다.
TEST_CASE("a changed artifact byte size is not shadowed by the cached resource") {
    FontRepositoryFixture fixture;
    const auto cached = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(cached);
    const fs::path catalog = fixture.PersistCatalog();

    nlohmann::json document;
    { std::ifstream input(catalog); REQUIRE(input.good()); input >> document; }
    std::size_t rewritten = 0;
    for (auto& record : document["records"]) {
        if (record.value("importer", std::string{}) != "FontImporter") continue;
        record["artifactByteSize"] =
            record["artifactByteSize"].get<std::uint64_t>() + 1U;
        ++rewritten;
    }
    REQUIRE(rewritten == 1U);
    const fs::path mutated = fixture.ProjectRoot() / "mutated_catalog.json";
    { std::ofstream output(mutated, std::ios::trunc); REQUIRE(output.good());
      output << document.dump(2); }

    // 같은 database, 같은 repository. 캐시는 살아 있고 카탈로그의 크기만
    // 하나 달라졌다.
    std::string error;
    REQUIRE_MESSAGE(
        fixture.database.Real().LoadCatalog(mutated, fixture.ProjectRoot(),
                                            molga::AssetCatalogMode::Project,
                                            &error),
        error);
    molga::text::VectorTextDiagnosticSink mutatedSink;
    CHECK_FALSE(fixture.repository.Load("font-a", 0, mutatedSink).has_value());
    CHECK(HasDiagnostic(mutatedSink, TextDiagnosticCode::FontInvalid));

    // 두 방향의 대조: 원래 카탈로그로 되돌리면 같은 호출이 다시 성공한다.
    REQUIRE_MESSAGE(
        fixture.database.Real().LoadCatalog(catalog, fixture.ProjectRoot(),
                                            molga::AssetCatalogMode::Project,
                                            &error),
        error);
    const auto restored = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(restored);
    CHECK((*restored)->artifactSha256 == (*cached)->artifactSha256);
    CHECK((*restored)->asset->artifactByteSize ==
          (*cached)->asset->artifactByteSize);
}

// ── Task 4.3 Step 1f ────────────────────────────────────────────────────────

TEST_CASE("scaled design metrics are exact checked 26.6 integers") {
    const molga::FontDesignMetrics metrics{1000, 750, -250, 125};
    const molga::Fixed26_6 size = molga::Fixed26_6::FromRaw(1056);
    const auto scaled = molga::ScaleFontDesignMetrics(metrics, size);
    REQUIRE(scaled.has_value());

    const auto expectedAscent = molga::Fixed26_6::CheckedMulDiv(size, 750, 1000);
    const auto expectedDescent = molga::Fixed26_6::CheckedMulDiv(size, 250, 1000);
    const auto expectedLineGap = molga::Fixed26_6::CheckedMulDiv(size, 125, 1000);
    REQUIRE(expectedAscent.has_value());
    REQUIRE(expectedDescent.has_value());
    REQUIRE(expectedLineGap.has_value());
    CHECK(scaled->ascent.Raw() == expectedAscent->Raw());
    CHECK(scaled->descent.Raw() == expectedDescent->Raw());
    CHECK(scaled->lineGap.Raw() == expectedLineGap->Raw());
    // CheckedMulDiv를 그대로 되풀이하는 대신, 손으로 계산한 raw도 못 박는다.
    CHECK(scaled->ascent.Raw() == 792);
    CHECK(scaled->descent.Raw() == 264);
    CHECK(scaled->lineGap.Raw() == 132);

    // 공유되는 "0에서 먼 쪽" 반올림 규칙을 실제로 태우는 행.
    const molga::FontDesignMetrics tiny{16, 3, -1, 1};
    const molga::Fixed26_6 tinySize = molga::Fixed26_6::FromRaw(8);
    const auto tinyScaled = molga::ScaleFontDesignMetrics(tiny, tinySize);
    REQUIRE(tinyScaled.has_value());
    const auto tinyAscent = molga::Fixed26_6::CheckedMulDiv(tinySize, 3, 16);
    REQUIRE(tinyAscent.has_value());
    CHECK(tinyScaled->ascent.Raw() == tinyAscent->Raw());
    CHECK(tinyScaled->ascent.Raw() == 2);
    CHECK(tinyScaled->descent.Raw() == 1);
    CHECK(tinyScaled->lineGap.Raw() == 1);

    // descender가 int16의 하한이어도 부호 반전이 넘치지 않는다.
    const molga::FontDesignMetrics deepest{
        1000, 750, std::numeric_limits<std::int16_t>::min(), 125};
    const auto deepestScaled = molga::ScaleFontDesignMetrics(deepest, size);
    REQUIRE(deepestScaled.has_value());
    const auto expectedDeepest = molga::Fixed26_6::CheckedMulDiv(size, 32768, 1000);
    REQUIRE(expectedDeepest.has_value());
    CHECK(deepestScaled->descent.Raw() == expectedDeepest->Raw());
    CHECK(deepestScaled->descent.Raw() == 34603);

    // 최대 폰트 크기: 검사된 정확한 값이거나 nullopt이고, 그 중간은 없다.
    const molga::Fixed26_6 largest =
        molga::Fixed26_6::FromRaw(std::numeric_limits<std::int32_t>::max());
    const auto largestScaled = molga::ScaleFontDesignMetrics(metrics, largest);
    REQUIRE(largestScaled.has_value());
    const auto expectedLargest =
        molga::Fixed26_6::CheckedMulDiv(largest, 750, 1000);
    REQUIRE(expectedLargest.has_value());
    CHECK(largestScaled->ascent.Raw() == expectedLargest->Raw());
    CHECK(largestScaled->ascent.Raw() == 1610612735);
    // 같은 최대 크기라도 em당 상승폭이 크면 결과가 int32를 넘는다. 포화가
    // 아니라 실패로 보고되어야 한다.
    const molga::FontDesignMetrics steepest{16, 32767, -1, 1};
    CHECK_FALSE(molga::ScaleFontDesignMetrics(steepest, largest).has_value());
    CHECK(molga::ScaleFontDesignMetrics(steepest, size).has_value());

    // 위 행은 ascent가 먼저 넘치므로 하나의 guard가 나머지 두 갈래를 가려
    // 준다. descent만, 그리고 lineGap만 넘치는 행을 따로 둬서 세 갈래가 각각
    // 증인을 갖게 한다 — 이 셋 중 하나가 조용히 0으로 포화되는 것이 레이아웃이
    // 가장 두려워하는 실패다.
    const molga::FontDesignMetrics deepDescentOnly{
        16, 1, std::numeric_limits<std::int16_t>::min(), 0};
    CHECK(molga::ScaleFontDesignMetrics(deepDescentOnly, size).has_value());
    CHECK_FALSE(
        molga::ScaleFontDesignMetrics(deepDescentOnly, largest).has_value());
    const molga::FontDesignMetrics wideGapOnly{16, 1, -1, 32767};
    CHECK(molga::ScaleFontDesignMetrics(wideGapOnly, size).has_value());
    CHECK_FALSE(molga::ScaleFontDesignMetrics(wideGapOnly, largest).has_value());
    // 위 두 행이 실제로 "한 갈래만" 넘치는지 확인한다. ascent가 먼저 넘치면
    // 두 행 모두 이미 있는 증인과 다를 바 없어진다.
    const auto survivingAscent =
        molga::Fixed26_6::CheckedMulDiv(largest, 1, 16);
    CHECK(survivingAscent.has_value());
    CHECK_FALSE(molga::Fixed26_6::CheckedMulDiv(largest, 32768, 16).has_value());
    CHECK_FALSE(molga::Fixed26_6::CheckedMulDiv(largest, 32767, 16).has_value());

    // 이미 검증된 불변식과 양의 폰트 크기를 요구한다.
    CHECK_FALSE(molga::ScaleFontDesignMetrics(metrics,
                                              molga::Fixed26_6::FromRaw(0))
                    .has_value());
    CHECK_FALSE(molga::ScaleFontDesignMetrics(metrics,
                                              molga::Fixed26_6::FromRaw(-64))
                    .has_value());
    CHECK_FALSE(molga::ScaleFontDesignMetrics({0, 750, -250, 125}, size)
                    .has_value());
    CHECK_FALSE(molga::ScaleFontDesignMetrics({15, 750, -250, 125}, size)
                    .has_value());
    CHECK_FALSE(molga::ScaleFontDesignMetrics({16385, 750, -250, 125}, size)
                    .has_value());
    CHECK_FALSE(molga::ScaleFontDesignMetrics({1000, 0, -250, 125}, size)
                    .has_value());
    CHECK_FALSE(molga::ScaleFontDesignMetrics({1000, 750, 1, 125}, size)
                    .has_value());
    CHECK_FALSE(molga::ScaleFontDesignMetrics({1000, 750, -250, -1}, size)
                    .has_value());
    // 두 경계는 포함이다.
    CHECK(molga::ScaleFontDesignMetrics({16, 750, -250, 125}, size).has_value());
    CHECK(molga::ScaleFontDesignMetrics({16384, 750, -250, 125}, size)
              .has_value());
}

TEST_CASE("an imported face scales its own SFNT integers through the same rule") {
    FontRepositoryFixture fixture;
    const auto resource = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(resource);
    const auto reference = ParseReferenceDesignMetrics(MOLGA_TEXT_LATIN_FONT);
    CHECK((*resource)->designMetrics.unitsPerEm == reference.unitsPerEm);
    CHECK((*resource)->designMetrics.ascender == reference.ascender);
    CHECK((*resource)->designMetrics.descender == reference.descender);
    CHECK((*resource)->designMetrics.lineGap == reference.lineGap);

    const molga::Fixed26_6 size = molga::Fixed26_6::FromRaw(16 * 64);
    const auto scaled =
        molga::ScaleFontDesignMetrics((*resource)->designMetrics, size);
    REQUIRE(scaled.has_value());
    const auto expected = molga::Fixed26_6::CheckedMulDiv(
        size, static_cast<std::int64_t>(reference.ascender),
        static_cast<std::int64_t>(reference.unitsPerEm));
    REQUIRE(expected.has_value());
    CHECK(scaled->ascent.Raw() == expected->Raw());
    CHECK(scaled->ascent.Raw() > 0);
    CHECK(scaled->descent.Raw() > 0);
}

// ── Task 4.3 Step 3: the immutable-byte raster face ─────────────────────────

TEST_CASE("FontFace binds the exact collection face index it was asked for") {
    const ScopedTempFont ttc =
        BuildTwoFaceTtc(MOLGA_TEXT_LATIN_FONT, MOLGA_TEXT_ARABIC_FONT);
    const auto bytes = SharedBytes(ttc.path);
    std::string error;

    molga::FontFace first;
    REQUIRE_MESSAGE(first.LoadFromBytes(bytes, 0, &error), error);
    CHECK(first.FaceIndex() == 0U);
    CHECK(first.HasCodepoint(U'A'));
    CHECK_FALSE(first.HasCodepoint(U'س'));

    molga::FontFace second;
    REQUIRE_MESSAGE(second.LoadFromBytes(bytes, 1, &error), error);
    CHECK(second.FaceIndex() == 1U);
    CHECK(second.HasCodepoint(U'س'));
    CHECK(second.GlyphId(U'س') != 0U);
    CHECK(first.GlyphId(U'س') == 0U);

    molga::FontFace outside;
    CHECK_FALSE(outside.LoadFromBytes(bytes, 2, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(outside.IsValid());
}

// face index 방어는 세 겹이고 서로를 가려 준다. 어느 겹이 거절했는지는 사유
// 문자열로만 구분되므로, 겹마다 자기 문자열을 그대로 못 박는다. 그러지 않으면
// 한 겹을 지워도 다른 겹이 대신 거절해 아무 테스트도 깨지지 않는다.
TEST_CASE("each font face index guard reports its own refusal") {
    const ScopedTempFont ttc =
        BuildTwoFaceTtc(MOLGA_TEXT_LATIN_FONT, MOLGA_TEXT_ARABIC_FONT);
    const auto collection = SharedBytes(ttc.path);
    const auto single = SharedBytes(MOLGA_TEXT_LATIN_FONT);
    std::string error;

    molga::FontFace beyondCollection;
    CHECK_FALSE(beyondCollection.LoadFromBytes(collection, 2, &error));
    CHECK(error == "font face index is outside the collection");

    molga::FontFace beyondFile;
    CHECK_FALSE(beyondFile.LoadFromBytes(single, 1, &error));
    CHECK(error == "font face index is outside the file");

    // 성공 증인: 같은 두 바이트 묶음이 허용된 index에서는 열린다.
    molga::FontFace lastCollectionFace;
    REQUIRE_MESSAGE(lastCollectionFace.LoadFromBytes(collection, 1, &error),
                    error);
    CHECK(error.empty());
    molga::FontFace onlyFileFace;
    REQUIRE_MESSAGE(onlyFileFace.LoadFromBytes(single, 0, &error), error);
    CHECK(onlyFileFace.HasCodepoint(U'A'));
}

// 실패 경로가 stb 상태를 비우는지는 이미 적재된 face 위에서만 관찰된다.
// 한 번도 적재하지 않은 face는 멤버가 이미 0이라 초기화가 보이지 않는다.
TEST_CASE("a failed reload leaves the face invalid and releases its bytes") {
    const ScopedTempFont ttc =
        BuildTwoFaceTtc(MOLGA_TEXT_LATIN_FONT, MOLGA_TEXT_ARABIC_FONT);
    const auto bytes = SharedBytes(ttc.path);
    std::string error;

    molga::FontFace face;
    REQUIRE_MESSAGE(face.LoadFromBytes(bytes, 1, &error), error);
    REQUIRE(face.IsValid());
    REQUIRE(face.FaceIndex() == 1U);
    REQUIRE(face.GlyphId(U'س') != 0U);
    const long ownersWhileLoaded = bytes.use_count();
    REQUIRE(ownersWhileLoaded > 1);

    CHECK_FALSE(face.LoadFromBytes(bytes, 7, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(face.IsValid());
    CHECK(face.FaceIndex() == 0U);
    CHECK(face.GlyphId(U'س') == 0U);
    CHECK_FALSE(face.HasCodepoint(U'س'));
    // 바이트 지분도 함께 놓는다: 실패한 face가 죽은 바이트를 붙들고 있으면
    // 안 되고, 붙들고 있는지는 참조 수로만 보인다.
    CHECK(bytes.use_count() == ownersWhileLoaded - 1);

    // 두 방향의 대조: 같은 객체가 다시 성공적으로 적재된다.
    REQUIRE_MESSAGE(face.LoadFromBytes(bytes, 0, &error), error);
    CHECK(face.IsValid());
    CHECK(face.FaceIndex() == 0U);
    CHECK(face.HasCodepoint(U'A'));
    CHECK_FALSE(face.HasCodepoint(U'س'));
    CHECK(bytes.use_count() == ownersWhileLoaded);
}

// ── Task 4.3 Step 1h ────────────────────────────────────────────────────────

TEST_CASE("artifact stores accept only their authorized storage locator") {
    ArtifactLocatorFixture f;
    const auto project = molga::FontArtifactStore::ForProject(f.projectRoot);
    auto packaged = molga::FontArtifactStore::ForSealedPackage(
        f.runtimeResourceRoot,
        std::vector<molga::FontArtifactStore::PackagedAuthority>{
            {"Assets/Fonts/used.otf", f.sha256}},
        f.sink);
    REQUIRE(packaged);
    const molga::VerifiedFontArtifact projectRecord{
        {molga::FontArtifactStorage::ProjectLibrary,
         std::filesystem::path("Library/Imported/Fonts") /
             (f.sha256 + ".sfnt")},
        f.sha256, f.sha256, f.byteSize};
    const molga::VerifiedFontArtifact packageRecord{
        {molga::FontArtifactStorage::PackagedResource,
         "Assets/Fonts/used.otf"},
        f.sha256, f.sha256, f.byteSize};
    CHECK(project.ReadVerified(projectRecord, f.sink));
    CHECK(packaged->ReadVerified(packageRecord, f.sink));
    CHECK_FALSE(project.ReadVerified(packageRecord, f.sink));
    CHECK_FALSE(packaged->ReadVerified(projectRecord, f.sink));
    CHECK(f.AuthoringSourceOpenCount() == 0);
}

// 위 블록은 계획서가 그대로 옮기라고 준 것이므로 한 줄도 더하지 않는다. 그
// 블록의 마지막 단언이 죽은 검출기 위에서 공짜로 참이 되지 않는다는 사실은
// 여기서 따로 못 박는다: 같은 fixture, 같은 검출기가 실제 읽기를 본다.
TEST_CASE("the authoring-source read detector observes a real read") {
    ArtifactLocatorFixture f;
    CHECK(f.AuthoringSourceOpenCount() == 0);
    f.CheckAuthoringSourceDetectorIsLive();
}

// ── Task 4.3 Step 1i ────────────────────────────────────────────────────────

TEST_CASE("a sealed package refuses every unauthorized font locator") {
    ArtifactLocatorFixture f;
    const fs::path escape = f.CreateSymlinkEscape("Assets/Fonts/link.otf");
    REQUIRE(fs::is_symlink(escape));
    auto packaged = molga::FontArtifactStore::ForSealedPackage(
        f.runtimeResourceRoot,
        std::vector<molga::FontArtifactStore::PackagedAuthority>{
            {"Assets/Fonts/used.otf", f.sha256},
            {"Assets/Fonts/link.otf", f.sha256}},
        f.sink);
    REQUIRE(packaged);
    REQUIRE(f.PackagedSfntCount() == 1U);

    // 성공 증인이 없으면 이 표는 전부 실패하도록 스텁을 넣어도 통과한다.
    const molga::VerifiedFontArtifact authorized{
        {molga::FontArtifactStorage::PackagedResource,
         "Assets/Fonts/used.otf"},
        f.sha256, f.sha256, f.byteSize};
    molga::text::VectorTextDiagnosticSink authorizedSink;
    REQUIRE(packaged->ReadVerified(authorized, authorizedSink).has_value());
    CHECK(authorizedSink.Diagnostics().empty());

    const std::string otherSha(64U, 'b');
    const std::vector<std::pair<const char*, molga::VerifiedFontArtifact>> rows{
        {"parent escape",
         {{molga::FontArtifactStorage::PackagedResource,
           "../Assets/Fonts/used.otf"},
          f.sha256, f.sha256, f.byteSize}},
        {"absolute path",
         {{molga::FontArtifactStorage::PackagedResource,
           (f.runtimeResourceRoot / "Assets" / "Fonts" / "used.otf")},
          f.sha256, f.sha256, f.byteSize}},
        {"symlink escape",
         {{molga::FontArtifactStorage::PackagedResource,
           "Assets/Fonts/link.otf"},
          f.sha256, f.sha256, f.byteSize}},
        {"unmanifested font",
         {{molga::FontArtifactStorage::PackagedResource,
           "Assets/Fonts/extra.otf"},
          f.sha256, f.sha256, f.byteSize}},
        {"authorized path with a different SHA",
         {{molga::FontArtifactStorage::PackagedResource,
           "Assets/Fonts/used.otf"},
          otherSha, otherSha, f.byteSize}},
        {"authority differing only by case",
         {{molga::FontArtifactStorage::PackagedResource,
           "Assets/Fonts/USED.otf"},
          f.sha256, f.sha256, f.byteSize}},
    };
    for (const auto& row : rows) {
        molga::text::VectorTextDiagnosticSink rowSink;
        CHECK_MESSAGE(
            !packaged->ReadVerified(row.second, rowSink).has_value(),
            Label(row.first));
        CHECK_MESSAGE(HasDiagnostic(rowSink, TextDiagnosticCode::FontInvalid),
                      Label(row.first));
    }
    CHECK(f.PackagedSfntCount() == 1U);
    CHECK(f.AuthoringSourceOpenCount() == 0U);
    f.CheckAuthoringSourceDetectorIsLive();
}

// 위 표의 여섯 행은 저마다 여러 규칙에 동시에 걸린다. 아래 네 가지는 각각
// 오직 하나의 규칙만이 거절할 수 있는 입력이다 — 그 규칙을 지우면 store가
// 승인되지 않은 바이트를 조용히 열어 준다.
TEST_CASE("an artifact store refuses identities no other rule would catch") {
    ArtifactLocatorFixture f;
    const auto project = molga::FontArtifactStore::ForProject(f.projectRoot);
    const molga::VerifiedFontArtifact authorized{
        {molga::FontArtifactStorage::ProjectLibrary,
         fs::path(molga::FontArtifactRelativePath(f.sha256))},
        f.sha256, f.sha256, f.byteSize};
    molga::text::VectorTextDiagnosticSink acceptedSink;
    REQUIRE(project.ReadVerified(authorized, acceptedSink).has_value());
    CHECK(acceptedSink.Diagnostics().empty());

    // sourceSha256 != artifactSha256. 둘 다 형식은 멀쩡하고 경로는 여전히
    // artifactSha256의 content address이며 그 파일의 다이제스트도 맞는다.
    // 필수 등식 검사 하나만이 이것을 거절한다 — Task 17이 봉인 record를 다시
    // 쓰면서 그 등식을 깨뜨리는 것을 잡아 낼 유일한 지점이다.
    const std::string otherSha(64U, 'c');
    molga::VerifiedFontArtifact split = authorized;
    split.sourceSha256 = otherSha;
    molga::text::VectorTextDiagnosticSink splitSink;
    CHECK_FALSE(project.ReadVerified(split, splitSink).has_value());
    CHECK(HasDiagnostic(splitSink, TextDiagnosticCode::FontInvalid));

    // 내용은 진짜 산출물이지만 이름이 자기 content address가 아닌 프로젝트
    // 파일. 경로도 안전하고 루트 아래이며 다이제스트도 맞는다.
    molga::VerifiedFontArtifact decoy = authorized;
    decoy.locator.relativePath = fs::path(f.CreateProjectDecoyArtifact());
    molga::text::VectorTextDiagnosticSink decoySink;
    CHECK_FALSE(project.ReadVerified(decoy, decoySink).has_value());
    CHECK(HasDiagnostic(decoySink, TextDiagnosticCode::FontInvalid));

    // 크기는 그대로이고 바이트만 다른 산출물. 크기 검사만 남은 회귀는 이것을
    // 통과시킨다.
    f.CorruptProjectArtifactInPlace();
    molga::text::VectorTextDiagnosticSink corruptSink;
    CHECK_FALSE(project.ReadVerified(authorized, corruptSink).has_value());
    CHECK(HasDiagnostic(corruptSink, TextDiagnosticCode::FontInvalid));

    CHECK(f.AuthoringSourceOpenCount() == 0U);
    f.CheckAuthoringSourceDetectorIsLive();
}

// 표의 "symlink escape" 행은 루트 밖을 가리키므로 prefix 검사만으로도 걸린다.
// 패키지 안을 가리키는 symlink는 prefix 검사를 통과하고 다이제스트도 맞으므로,
// symlink 자체를 거절하는 규칙만이 이것을 막는다.
TEST_CASE("a sealed package refuses a symlink that stays inside the package") {
    ArtifactLocatorFixture f;
    const fs::path alias = f.CreateSymlinkInsidePackage("Assets/Fonts/alias.otf");
    REQUIRE(fs::is_symlink(alias));
    REQUIRE(fs::exists(alias));
    auto packaged = molga::FontArtifactStore::ForSealedPackage(
        f.runtimeResourceRoot,
        std::vector<molga::FontArtifactStore::PackagedAuthority>{
            {"Assets/Fonts/used.otf", f.sha256},
            {"Assets/Fonts/alias.otf", f.sha256}},
        f.sink);
    REQUIRE(packaged);

    const molga::VerifiedFontArtifact real{
        {molga::FontArtifactStorage::PackagedResource, "Assets/Fonts/used.otf"},
        f.sha256, f.sha256, f.byteSize};
    molga::text::VectorTextDiagnosticSink realSink;
    REQUIRE(packaged->ReadVerified(real, realSink).has_value());
    CHECK(realSink.Diagnostics().empty());

    const molga::VerifiedFontArtifact aliased{
        {molga::FontArtifactStorage::PackagedResource, "Assets/Fonts/alias.otf"},
        f.sha256, f.sha256, f.byteSize};
    molga::text::VectorTextDiagnosticSink aliasSink;
    CHECK_FALSE(packaged->ReadVerified(aliased, aliasSink).has_value());
    CHECK(HasDiagnostic(aliasSink, TextDiagnosticCode::FontInvalid));
    CHECK(f.PackagedSfntCount() == 1U);
    CHECK(f.AuthoringSourceOpenCount() == 0U);
    f.CheckAuthoringSourceDetectorIsLive();
}

// ── Task 4.3 Step 1j ────────────────────────────────────────────────────────

TEST_CASE("a sealed catalog restart serves the same font through the package") {
    FontRepositoryFixture fixture;
    const auto projectResource = fixture.repository.Load("font-a", 0, fixture.sink);
    REQUIRE(projectResource);
    const fs::path projectCatalog = fixture.PersistCatalog();
    const std::string artifactSha = fixture.CatalogArtifactSha("font-a");

    // Task 17이 하는 그대로: 검증된 manifest 항목에서 봉인 경로를 만들고,
    // 필터된 카탈로그 record의 locator만 다시 쓴다. SHA와 바이트 크기는
    // 그대로 보존된다.
    const fs::path resources = fixture.ProjectRoot() / "Package" / "Contents" /
                               "Resources";
    fs::create_directories(resources / "Assets" / "Fonts");
    fs::copy_file(fixture.PublishedArtifact("font-a"),
                  resources / "Assets" / "Fonts" / "used.otf");

    nlohmann::json catalog;
    { std::ifstream input(projectCatalog); REQUIRE(input.good()); input >> catalog; }
    std::size_t rewritten = 0;
    for (auto& record : catalog["records"]) {
        if (record.value("importer", std::string{}) != "FontImporter") continue;
        REQUIRE(record.value("artifactSha256", std::string{}) == artifactSha);
        record["artifactStorage"] = "PackagedResource";
        record["artifactRelativePath"] = "Assets/Fonts/used.otf";
        ++rewritten;
    }
    REQUIRE(rewritten == 1U);
    const fs::path sealedCatalog = resources / "asset_catalog.json";
    { std::ofstream output(sealedCatalog, std::ios::trunc);
      REQUIRE(output.good()); output << catalog.dump(2); }

    molga::text::VectorTextDiagnosticSink storeSink;
    auto sealedStore = molga::FontArtifactStore::ForSealedPackage(
        resources,
        std::vector<molga::FontArtifactStore::PackagedAuthority>{
            {"Assets/Fonts/used.otf", artifactSha}},
        storeSink);
    REQUIRE(sealedStore);
    molga::AssetDatabase sealed;
    REQUIRE(sealed.BindFontArtifactStore(
        std::make_shared<const molga::FontArtifactStore>(*sealedStore)));
    std::string sealedError;
    REQUIRE_MESSAGE(sealed.LoadCatalog(sealedCatalog, resources,
                                       molga::AssetCatalogMode::SealedPackage,
                                       &sealedError),
                    sealedError);

    molga::text::FontRepository sealedRepository(sealed);
    molga::text::VectorTextDiagnosticSink sealedSink;
    // SaveCatalog는 저장 시점 해시를 다시 계산하느라 저작 원본을 읽는다. 아래
    // 단언이 보려는 것은 봉인 적재 경로이므로 기준선을 여기서 다시 잡는다.
    fixture.ArmAuthoringSourceDetector();
    const auto packagedResource =
        sealedRepository.Load(fixture.Guid("font-a"), 0, sealedSink);
    REQUIRE(packagedResource);
    REQUIRE((*packagedResource)->asset != nullptr);
    CHECK((*packagedResource)->asset->guid == fixture.Guid("font-a"));
    CHECK((*packagedResource)->sourceSha256 == (*projectResource)->sourceSha256);
    CHECK((*packagedResource)->artifactSha256 ==
          (*projectResource)->artifactSha256);
    CHECK((*packagedResource)->bytes->size() ==
          (*projectResource)->bytes->size());
    CHECK(*(*packagedResource)->bytes == *(*projectResource)->bytes);
    CHECK((*packagedResource)->faceIndex == (*projectResource)->faceIndex);
    CHECK((*packagedResource)->designMetrics.unitsPerEm ==
          (*projectResource)->designMetrics.unitsPerEm);
    CHECK((*packagedResource)->designMetrics.ascender ==
          (*projectResource)->designMetrics.ascender);
    CHECK((*packagedResource)->rasterFace->GlyphId(U'A') ==
          (*projectResource)->rasterFace->GlyphId(U'A'));
    CHECK((*packagedResource)->rasterFace->GlyphId(U'A') != 0U);
    CHECK((*packagedResource)->artifactLocator.storage ==
          molga::FontArtifactStorage::PackagedResource);
    CHECK((*packagedResource)->artifactLocator.relativePath.generic_string() ==
          "Assets/Fonts/used.otf");
    CHECK(sealedSink.Diagnostics().empty());
    // 패키지는 프로젝트 Library 사본을 담지도 참조하지도 않는다.
    CHECK_FALSE(fs::exists(resources / "Library"));
    CHECK(fixture.AuthoringSourceOpenCount() == 0U);

    // Step 8a: manifest 경로, 봉인 카탈로그 locator, 그리고 실제로 staged된
    // 단 하나의 SFNT 경로는 슬래시 정규화 후 바이트 단위로 같아야 한다. 셋 중
    // 하나라도 어긋나면 런타임은 승인되지 않은 바이트를 읽거나 아무것도 읽지
    // 못한다.
    const std::string manifestPath = "Assets/Fonts/used.otf";
    std::vector<std::string> stagedFonts;
    for (const auto& entry : fs::recursive_directory_iterator(resources)) {
        std::error_code entryError;
        if (!fs::is_regular_file(entry.path(), entryError)) continue;
        const std::string extension = entry.path().extension().string();
        if (extension != ".otf" && extension != ".ttf" && extension != ".sfnt") {
            continue;
        }
        stagedFonts.push_back(
            fs::relative(entry.path(), resources).generic_string());
    }
    REQUIRE(stagedFonts.size() == 1U);
    CHECK(stagedFonts.front() == manifestPath);
    const AssetRecord* sealedRecord = sealed.Find(fixture.Guid("font-a"));
    REQUIRE(sealedRecord != nullptr);
    REQUIRE(sealedRecord->fontArtifact.has_value());
    CHECK(sealedRecord->fontArtifact->locator.relativePath.generic_string() ==
          manifestPath);
    CHECK(sealedRecord->fontArtifact->artifactSha256 == artifactSha);
    CHECK(sealedRecord->fontArtifact->byteSize ==
          (*projectResource)->bytes->size());

    // Step 8: 봉인 런타임에서도 산출물 바이트가 카탈로그 정체성과 어긋나면
    // 자원이 없다. 이미 검증되어 캐시된 자원은 불변이므로 그대로 살아 있는
    // 것이 옳고, 새로 만드는 경로만이 이 판정을 받는다 — 그래서 먼저
    // 무효화한다.
    std::vector<std::uint8_t> packagedBytes =
        ReadAllBytes(resources / "Assets" / "Fonts" / "used.otf");
    REQUIRE(packagedBytes.size() > 64U);
    packagedBytes[64] = static_cast<std::uint8_t>(packagedBytes[64] ^ 0xFFU);
    WriteAllBytes(resources / "Assets" / "Fonts" / "used.otf", packagedBytes);
    sealedRepository.Invalidate(fixture.Guid("font-a"));
    molga::text::VectorTextDiagnosticSink corruptSink;
    CHECK_FALSE(
        sealedRepository.Load(fixture.Guid("font-a"), 0, corruptSink)
            .has_value());
    CHECK(HasDiagnostic(corruptSink, TextDiagnosticCode::FontInvalid));
    // 이미 돌려준 봉인 자원은 그 실패에 영향받지 않는다.
    CHECK((*packagedResource)->rasterFace->GlyphId(U'A') != 0U);

    // 같은 record를 평범한 프로젝트 카탈로그로 읽으면 닫힌 실패다.
    molga::AssetDatabase asProject;
    REQUIRE(asProject.BindFontArtifactStore(ProjectStore(fixture.ProjectRoot())));
    std::string projectError;
    CHECK_FALSE(asProject.LoadCatalog(sealedCatalog, fixture.ProjectRoot(),
                                      molga::AssetCatalogMode::Project,
                                      &projectError));
    CHECK_FALSE(projectError.empty());

    fixture.CheckAuthoringSourceDetectorIsLive();
}

// 봉인 카탈로그에서는 locator가 SHA의 함수가 아니다 — 프로젝트 모드와 달리
// "경로는 자기 content address"라는 규칙이 없다. 그래서 Task 17이 같은 바이트를
// 다른 승인 경로로 다시 쓰면, 캐시된 자원과 새 record를 가르는 항목은
// artifactLocator.relativePath 하나뿐이다.
TEST_CASE("a sealed locator rewrite alone rebuilds the cached resource") {
    FontRepositoryFixture fixture;
    REQUIRE(fixture.repository.Load("font-a", 0, fixture.sink));
    const fs::path projectCatalog = fixture.PersistCatalog();
    const std::string artifactSha = fixture.CatalogArtifactSha("font-a");

    const fs::path resources =
        fixture.ProjectRoot() / "Package" / "Contents" / "Resources";
    fs::create_directories(resources / "Assets" / "Fonts");
    fs::copy_file(fixture.PublishedArtifact("font-a"),
                  resources / "Assets" / "Fonts" / "used.otf");
    fs::copy_file(fixture.PublishedArtifact("font-a"),
                  resources / "Assets" / "Fonts" / "alias.otf");

    nlohmann::json catalog;
    { std::ifstream input(projectCatalog); REQUIRE(input.good());
      input >> catalog; }
    const auto writeSealedCatalog = [&](const std::string& relative) {
        nlohmann::json rewrittenCatalog = catalog;
        std::size_t rewritten = 0;
        for (auto& record : rewrittenCatalog["records"]) {
            if (record.value("importer", std::string{}) != "FontImporter") {
                continue;
            }
            record["artifactStorage"] = "PackagedResource";
            record["artifactRelativePath"] = relative;
            ++rewritten;
        }
        REQUIRE(rewritten == 1U);
        const fs::path path = resources / "asset_catalog.json";
        std::ofstream output(path, std::ios::trunc);
        REQUIRE(output.good());
        output << rewrittenCatalog.dump(2);
        return path;
    };

    molga::text::VectorTextDiagnosticSink storeSink;
    auto sealedStore = molga::FontArtifactStore::ForSealedPackage(
        resources,
        std::vector<molga::FontArtifactStore::PackagedAuthority>{
            {"Assets/Fonts/used.otf", artifactSha},
            {"Assets/Fonts/alias.otf", artifactSha}},
        storeSink);
    REQUIRE(sealedStore);
    molga::AssetDatabase sealed;
    REQUIRE(sealed.BindFontArtifactStore(
        std::make_shared<const molga::FontArtifactStore>(*sealedStore)));
    molga::text::FontRepository sealedRepository(sealed);
    molga::text::VectorTextDiagnosticSink sealedSink;

    std::string catalogError;
    REQUIRE_MESSAGE(
        sealed.LoadCatalog(writeSealedCatalog("Assets/Fonts/used.otf"), resources,
                           molga::AssetCatalogMode::SealedPackage, &catalogError),
        catalogError);
    const auto used =
        sealedRepository.Load(fixture.Guid("font-a"), 0, sealedSink);
    REQUIRE(used);
    REQUIRE((*used)->asset != nullptr);
    CHECK((*used)->artifactLocator.relativePath.generic_string() ==
          "Assets/Fonts/used.otf");

    // locator만 다시 쓴다. 두 SHA도, 기록된 바이트 크기도, 세대도 그대로다.
    REQUIRE_MESSAGE(
        sealed.LoadCatalog(writeSealedCatalog("Assets/Fonts/alias.otf"), resources,
                           molga::AssetCatalogMode::SealedPackage, &catalogError),
        catalogError);
    const auto aliased =
        sealedRepository.Load(fixture.Guid("font-a"), 0, sealedSink);
    REQUIRE(aliased);
    REQUIRE((*aliased)->asset != nullptr);
    CHECK_FALSE(*aliased == *used);
    CHECK((*aliased)->artifactLocator.relativePath.generic_string() ==
          "Assets/Fonts/alias.otf");
    CHECK((*aliased)->artifactSha256 == (*used)->artifactSha256);
    CHECK((*aliased)->sourceSha256 == (*used)->sourceSha256);
    CHECK((*aliased)->asset->artifactByteSize ==
          (*used)->asset->artifactByteSize);
    CHECK((*aliased)->contentGeneration == (*used)->contentGeneration);
    CHECK(*(*aliased)->bytes == *(*used)->bytes);
    CHECK(sealedSink.Diagnostics().empty());
}
