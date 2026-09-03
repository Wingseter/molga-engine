#include "Core/Importers/FontImporter.h"

#include "Assets/FontAsset.h"
#include "Common/Sha256.h"
#include "Rendering/FontFace.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace molga {
namespace {

constexpr std::uintmax_t kMaximumFontBytes = 256U * 1024U * 1024U;
constexpr std::uint32_t kTtcTag = 0x74746366U;        // 'ttcf'
constexpr std::uint32_t kTrueTypeSignature = 0x00010000U;
constexpr std::uint32_t kOpenTypeSignature = 0x4F54544FU;  // 'OTTO'

// Production accepts static outline faces only. Every tag here means the file
// carries variations or colour strikes the shaping/atlas contract cannot
// reproduce deterministically, so it is refused before a raster face exists.
constexpr const char* kRejectedTables[] = {
    "fvar", "gvar", "CFF2", "COLR", "CPAL", "CBDT", "CBLC", "sbix", "SVG "};

struct TableRecord {
    std::string tag;
    std::uint32_t checksum = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes,
                      std::uint64_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[static_cast<std::size_t>(offset)]) << 8U) |
        bytes[static_cast<std::size_t>(offset) + 1U]);
}

std::int16_t ReadS16(const std::vector<std::uint8_t>& bytes,
                     std::uint64_t offset) {
    return static_cast<std::int16_t>(ReadU16(bytes, offset));
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes,
                      std::uint64_t offset) {
    const std::size_t at = static_cast<std::size_t>(offset);
    return (static_cast<std::uint32_t>(bytes[at]) << 24U) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[at + 3U]);
}

bool Fits(std::uint64_t offset, std::uint64_t length, std::size_t size) {
    return offset <= size && length <= static_cast<std::uint64_t>(size) - offset;
}

bool ReadFile(const std::string& path, std::vector<std::uint8_t>& out) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return false;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size < 12U || size > kMaximumFontBytes) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    out.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(out.data()),
               static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(input) &&
           static_cast<std::size_t>(input.gcount()) == out.size();
}

// SFNT 표 checksum: 4바이트 경계까지 0으로 채운 뒤 big-endian uint32 합.
// `head`만 예외로 checkSumAdjustment를 0으로 보고 계산한다. 그 규칙 덕분에
// 표를 다른 파일 오프셋으로 옮겨도(TTC 조립 등) checksum이 그대로 유효하다.
std::uint32_t ComputeTableChecksum(const std::vector<std::uint8_t>& bytes,
                                   const TableRecord& table) {
    std::uint32_t sum = 0;
    const bool isHead = table.tag == "head";
    for (std::uint64_t at = 0; at < table.length; at += 4U) {
        std::uint32_t word = 0;
        for (std::uint64_t lane = 0; lane < 4U; ++lane) {
            const std::uint64_t index = at + lane;
            std::uint32_t byte = 0;
            if (index < table.length) {
                byte = bytes[static_cast<std::size_t>(table.offset + index)];
            }
            if (isHead && index >= 8U && index < 12U) byte = 0;
            word = (word << 8U) | byte;
        }
        sum += word;
    }
    return sum;
}

struct SelectedFace {
    std::uint64_t directoryOffset = 0;
    std::vector<TableRecord> tables;
};

const TableRecord* FindTable(const SelectedFace& face, const char* tag) {
    for (const TableRecord& table : face.tables) {
        if (table.tag == tag) return &table;
    }
    return nullptr;
}

void Fail(ImportResult& result, std::string message, std::string remediation) {
    // 첫 실패에서 즉시 반환하므로 애셋 하나가 만드는 진단 수는 상수로 묶인다.
    result.success = false;
    result.metadata.erase("font");
    result.artifactPath.clear();
    molga::text::TextDiagnostic diagnostic;
    diagnostic.code = molga::text::TextDiagnosticCode::FontInvalid;
    diagnostic.severity = molga::text::TextSeverity::Error;
    diagnostic.subsystem = "font-import";
    diagnostic.message = message;
    diagnostic.remediation = std::move(remediation);
    diagnostic.componentType = "FontAsset";
    result.importDiagnostics.push_back(std::move(diagnostic));
    result.error = std::move(message);
}

// Step 6: collection이면 authored face offset을 해석하고, 아니면 face 0만
// 받는다. 잘못된 collection 헤더/개수/오프셋은 표를 읽기 전에 거절한다.
bool SelectFaceDirectory(const std::vector<std::uint8_t>& bytes,
                         std::uint32_t faceIndex, ImportResult& result,
                         std::uint64_t& directoryOffset) {
    if (ReadU32(bytes, 0U) == kTtcTag) {
        if (!Fits(0U, 12U, bytes.size())) {
            Fail(result, "TrueType collection header is truncated",
                 "re-export the font collection");
            return false;
        }
        const std::uint32_t version = ReadU32(bytes, 4U);
        if (version != 0x00010000U && version != 0x00020000U) {
            Fail(result, "unsupported TrueType collection version",
                 "re-export the font collection");
            return false;
        }
        const std::uint32_t fontCount = ReadU32(bytes, 8U);
        if (fontCount == 0U || fontCount > 4096U ||
            !Fits(12U, static_cast<std::uint64_t>(fontCount) * 4U,
                  bytes.size())) {
            Fail(result, "TrueType collection font count is invalid",
                 "re-export the font collection");
            return false;
        }
        if (faceIndex >= fontCount) {
            Fail(result, "authored face index " + std::to_string(faceIndex) +
                         " is outside the collection",
                 "author a face index below the collection font count");
            return false;
        }
        directoryOffset = ReadU32(bytes, 12U + std::uint64_t{faceIndex} * 4U);
        if (!Fits(directoryOffset, 12U, bytes.size())) {
            Fail(result, "TrueType collection face offset is outside the file",
                 "re-export the font collection");
            return false;
        }
        return true;
    }
    if (faceIndex != 0U) {
        Fail(result, "a non-collection font file only has face index 0",
             "author face index 0 for a single-face font");
        return false;
    }
    directoryOffset = 0U;
    return true;
}

// Step 6a: 선택된 face directory 검증. 표 범위, 겹침, checksum, outline 종류.
bool ValidateFaceDirectory(const std::vector<std::uint8_t>& bytes,
                           std::uint64_t directoryOffset, ImportResult& result,
                           SelectedFace& face) {
    const std::uint32_t signature = ReadU32(bytes, directoryOffset);
    if (signature != kTrueTypeSignature && signature != kOpenTypeSignature) {
        Fail(result, "unsupported SFNT signature",
             "import a static TrueType or OpenType outline face");
        return false;
    }
    const std::uint16_t tableCount = ReadU16(bytes, directoryOffset + 4U);
    if (tableCount == 0U ||
        !Fits(directoryOffset + 12U,
              static_cast<std::uint64_t>(tableCount) * 16U, bytes.size())) {
        Fail(result, "SFNT table directory is invalid",
             "re-export the font from its source");
        return false;
    }

    face.directoryOffset = directoryOffset;
    face.tables.reserve(tableCount);
    for (std::uint16_t index = 0; index < tableCount; ++index) {
        const std::uint64_t record =
            directoryOffset + 12U + static_cast<std::uint64_t>(index) * 16U;
        TableRecord table;
        table.tag.assign(reinterpret_cast<const char*>(bytes.data()) +
                             static_cast<std::size_t>(record), 4U);
        table.checksum = ReadU32(bytes, record + 4U);
        table.offset = ReadU32(bytes, record + 8U);
        table.length = ReadU32(bytes, record + 12U);
        if (!Fits(table.offset, table.length, bytes.size())) {
            Fail(result, "SFNT table '" + table.tag + "' extends beyond the file",
                 "re-export the font from its source");
            return false;
        }
        if (ComputeTableChecksum(bytes, table) != table.checksum) {
            Fail(result, "SFNT table '" + table.tag +
                         "' does not match its recorded checksum",
                 "re-export the font from its source");
            return false;
        }
        face.tables.push_back(std::move(table));
    }

    std::vector<const TableRecord*> ordered;
    ordered.reserve(face.tables.size());
    for (const TableRecord& table : face.tables) ordered.push_back(&table);
    std::sort(ordered.begin(), ordered.end(),
              [](const TableRecord* lhs, const TableRecord* rhs) {
                  return lhs->offset < rhs->offset;
              });
    for (std::size_t index = 1; index < ordered.size(); ++index) {
        const TableRecord& previous = *ordered[index - 1U];
        if (ordered[index]->offset < previous.offset + previous.length) {
            Fail(result, "SFNT tables '" + previous.tag + "' and '" +
                         ordered[index]->tag + "' overlap",
                 "re-export the font from its source");
            return false;
        }
    }

    const bool trueTypeOutlines =
        FindTable(face, "glyf") != nullptr && FindTable(face, "loca") != nullptr;
    const bool cffOutlines = FindTable(face, "CFF ") != nullptr;
    if (!trueTypeOutlines && !cffOutlines) {
        Fail(result, "the selected face has neither glyf/loca nor CFF outlines",
             "import a static TrueType or OpenType outline face");
        return false;
    }
    return true;
}

// Step 6b.
bool RejectUnsupportedTables(const SelectedFace& face, ImportResult& result) {
    for (const char* tag : kRejectedTables) {
        if (FindTable(face, tag) != nullptr) {
            Fail(result, std::string("the selected face carries the "
                                     "unsupported table '") + tag + "'",
                 "import a static, non-colour outline face instead");
            return false;
        }
    }
    return true;
}

// Step 6c: head/hhea의 정확한 big-endian 정수만 읽는다. rasterizer나 float
// 파생 값은 레이아웃 정체성에 들어갈 수 없다.
bool ParseDesignMetrics(const std::vector<std::uint8_t>& bytes,
                        const SelectedFace& face, ImportResult& result,
                        FontDesignMetrics& metrics) {
    const TableRecord* head = FindTable(face, "head");
    const TableRecord* hhea = FindTable(face, "hhea");
    if (head == nullptr || hhea == nullptr) {
        Fail(result, "the selected face has no head/hhea metric tables",
             "re-export the font from its source");
        return false;
    }
    if (head->length < 20U || hhea->length < 10U) {
        Fail(result, "the selected face has truncated head/hhea metric fields",
             "re-export the font from its source");
        return false;
    }

    metrics.unitsPerEm = ReadU16(bytes, head->offset + 18U);
    metrics.ascender = ReadS16(bytes, hhea->offset + 4U);
    metrics.descender = ReadS16(bytes, hhea->offset + 6U);
    metrics.lineGap = ReadS16(bytes, hhea->offset + 8U);

    if (metrics.unitsPerEm < kFontUnitsPerEmMin ||
        metrics.unitsPerEm > kFontUnitsPerEmMax) {
        Fail(result, "head.unitsPerEm is outside the supported 16..16384 range",
             "re-export the font with a standard units per em");
        return false;
    }
    if (metrics.ascender <= 0) {
        Fail(result, "hhea.ascender must be positive",
             "re-export the font with valid horizontal metrics");
        return false;
    }
    if (metrics.descender > 0) {
        Fail(result, "hhea.descender must not be positive",
             "re-export the font with valid horizontal metrics");
        return false;
    }
    if (metrics.ascender <= metrics.descender) {
        Fail(result, "hhea.ascender must be greater than hhea.descender",
             "re-export the font with valid horizontal metrics");
        return false;
    }
    if (metrics.lineGap < 0) {
        Fail(result, "hhea.lineGap must not be negative",
             "re-export the font with valid horizontal metrics");
        return false;
    }
    // 나중에 26.6으로 스케일할 때 쓰는 합이 32비트를 넘지 않는지 여기서 확인한다.
    const std::int64_t lineHeight = static_cast<std::int64_t>(metrics.ascender) -
                                    static_cast<std::int64_t>(metrics.descender) +
                                    static_cast<std::int64_t>(metrics.lineGap);
    if (lineHeight <= 0 || lineHeight > 0x7FFFFFFF) {
        Fail(result, "hhea metrics do not form a representable line height",
             "re-export the font with valid horizontal metrics");
        return false;
    }
    return true;
}

void AddCodepoint(std::vector<std::pair<char32_t, char32_t>>& ranges,
                  std::uint32_t codepoint) {
    if (codepoint > 0x10FFFFU) return;
    ranges.emplace_back(static_cast<char32_t>(codepoint),
                        static_cast<char32_t>(codepoint));
}

void AddRange(std::vector<std::pair<char32_t, char32_t>>& ranges,
              std::uint32_t begin, std::uint32_t end) {
    if (begin > end || begin > 0x10FFFFU) return;
    ranges.emplace_back(static_cast<char32_t>(begin),
                        static_cast<char32_t>(std::min(end, 0x10FFFFU)));
}

void CollectFormat4(const std::vector<std::uint8_t>& bytes,
                    std::uint64_t subtable, std::uint64_t length,
                    std::vector<std::pair<char32_t, char32_t>>& ranges) {
    if (length < 14U) return;
    const std::uint16_t segCountX2 = ReadU16(bytes, subtable + 6U);
    const std::uint16_t segCount = static_cast<std::uint16_t>(segCountX2 / 2U);
    if (segCount == 0U ||
        !Fits(subtable, 16U + static_cast<std::uint64_t>(segCountX2) * 4U,
              bytes.size())) {
        return;
    }
    const std::uint64_t endCodes = subtable + 14U;
    const std::uint64_t startCodes = endCodes + segCountX2 + 2U;
    for (std::uint16_t segment = 0; segment < segCount; ++segment) {
        const std::uint16_t end =
            ReadU16(bytes, endCodes + std::uint64_t{segment} * 2U);
        const std::uint16_t start =
            ReadU16(bytes, startCodes + std::uint64_t{segment} * 2U);
        if (start == 0xFFFFU && end == 0xFFFFU) continue;
        AddRange(ranges, start, end);
    }
}

void CollectFormat6(const std::vector<std::uint8_t>& bytes,
                    std::uint64_t subtable, std::uint64_t length,
                    std::vector<std::pair<char32_t, char32_t>>& ranges) {
    if (length < 10U) return;
    const std::uint16_t first = ReadU16(bytes, subtable + 6U);
    const std::uint16_t count = ReadU16(bytes, subtable + 8U);
    if (count == 0U) return;
    AddRange(ranges, first, static_cast<std::uint32_t>(first) + count - 1U);
}

void CollectFormat12(const std::vector<std::uint8_t>& bytes,
                     std::uint64_t subtable, std::uint64_t length,
                     std::vector<std::pair<char32_t, char32_t>>& ranges) {
    if (length < 16U) return;
    const std::uint32_t groups = ReadU32(bytes, subtable + 12U);
    if (!Fits(subtable + 16U, static_cast<std::uint64_t>(groups) * 12U,
              bytes.size())) {
        return;
    }
    for (std::uint32_t group = 0; group < groups; ++group) {
        const std::uint64_t at = subtable + 16U + std::uint64_t{group} * 12U;
        AddRange(ranges, ReadU32(bytes, at), ReadU32(bytes, at + 4U));
    }
}

void CollectFormat0(const std::vector<std::uint8_t>& bytes,
                    std::uint64_t subtable, std::uint64_t length,
                    std::vector<std::pair<char32_t, char32_t>>& ranges) {
    if (length < 262U) return;
    for (std::uint32_t codepoint = 0; codepoint < 256U; ++codepoint) {
        if (bytes[static_cast<std::size_t>(subtable + 6U + codepoint)] != 0U) {
            AddCodepoint(ranges, codepoint);
        }
    }
}

// Step 7: 선택된 face의 Unicode cmap subtable을 전부 병합해 정규 coverage
// range를 만든다. UVS(format 14)는 여기서 다루지 않는다 — 변이 선택자 처리는
// shaping 단계의 계약이다.
std::vector<std::pair<char32_t, char32_t>> CollectCoverage(
    const std::vector<std::uint8_t>& bytes, const SelectedFace& face) {
    std::vector<std::pair<char32_t, char32_t>> ranges;
    const TableRecord* cmap = FindTable(face, "cmap");
    if (cmap == nullptr || cmap->length < 4U) return ranges;
    const std::uint16_t subtables = ReadU16(bytes, cmap->offset + 2U);
    if (!Fits(cmap->offset + 4U,
              static_cast<std::uint64_t>(subtables) * 8U, bytes.size())) {
        return ranges;
    }
    for (std::uint16_t index = 0; index < subtables; ++index) {
        const std::uint64_t record =
            cmap->offset + 4U + static_cast<std::uint64_t>(index) * 8U;
        const std::uint16_t platform = ReadU16(bytes, record);
        const std::uint16_t encoding = ReadU16(bytes, record + 2U);
        const bool unicode =
            platform == 0U ||
            (platform == 3U && (encoding == 1U || encoding == 10U));
        if (!unicode) continue;
        const std::uint64_t offset = ReadU32(bytes, record + 4U);
        const std::uint64_t subtable = cmap->offset + offset;
        if (offset >= cmap->length || !Fits(subtable, 4U, bytes.size())) continue;
        const std::uint64_t available = cmap->length - offset;
        switch (ReadU16(bytes, subtable)) {
            case 0U:  CollectFormat0(bytes, subtable, available, ranges); break;
            case 4U:  CollectFormat4(bytes, subtable, available, ranges); break;
            case 6U:  CollectFormat6(bytes, subtable, available, ranges); break;
            case 12U: CollectFormat12(bytes, subtable, available, ranges); break;
            default: break;  // format 14 등은 coverage 계산에 쓰지 않는다.
        }
    }

    std::sort(ranges.begin(), ranges.end());
    std::vector<std::pair<char32_t, char32_t>> merged;
    for (const auto& range : ranges) {
        if (!merged.empty() &&
            static_cast<std::uint64_t>(range.first) <=
                static_cast<std::uint64_t>(merged.back().second) + 1U) {
            merged.back().second = std::max(merged.back().second, range.second);
            continue;
        }
        merged.push_back(range);
    }
    return merged;
}

bool ReadUnsignedSetting(const nlohmann::json& settings, const char* key,
                         std::uint32_t minimum, std::uint32_t maximum,
                         std::uint32_t& out, ImportResult& result) {
    // nlohmann은 C++에서 만든 정수 리터럴을 number_integer로, 파일에서 파싱한
    // 음이 아닌 정수를 number_unsigned로 저장한다. 두 표현 모두 같은 authored
    // 값이므로 is_number_integer()로 받고 음수만 거절한다.
    const auto found = settings.find(key);
    if (found == settings.end() || !found->is_number_integer()) {
        Fail(result, std::string("font import setting '") + key +
                     "' is missing or is not an integer",
             "author the font import settings in the inspector");
        return false;
    }
    if (!found->is_number_unsigned() && found->get<std::int64_t>() < 0) {
        Fail(result, std::string("font import setting '") + key +
                     "' is outside its supported range",
             "author the font import settings in the inspector");
        return false;
    }
    const std::uint64_t value = found->get<std::uint64_t>();
    if (value < minimum || value > maximum) {
        Fail(result, std::string("font import setting '") + key +
                     "' is outside its supported range",
             "author the font import settings in the inspector");
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

// v2 설정이 authored된 폰트만 엄격 경로를 탄다. 설정이 하나도 없는 폰트는 아직
// 마이그레이션되지 않은 legacy 애셋이고, Task 8.2/15.2가 그 경로를 닫는다.
// 일부만 authored된 설정은 legacy로 되돌리지 않는다 — 그렇게 하면 license
// 확인을 지우는 것만으로 static 전용 검사를 통째로 우회할 수 있다.
bool HasAuthoredFontSettings(const nlohmann::json& settings) {
    if (!settings.is_object()) return false;
    for (const char* key : {"faceIndex", "weight", "stretchPercent", "slant",
                            "redistributableConfirmed", "licenseKind",
                            "licenseAssetGuid", "copyright"}) {
        if (settings.contains(key)) return true;
    }
    return false;
}

} // namespace

bool FontImporter::CanImport(const std::string& extension) const {
    return extension == ".ttf" || extension == ".otf";
}

ImportResult FontImporter::Import(const std::string& absoluteSourcePath) const {
    // 호환 경로: authored 설정이 아직 없는 폰트는 구조 검증만 한다. static
    // 전용/라이선스 확인 계약은 authored 설정이 붙는 순간부터 강제된다.
    ImportResult result;
    FontFace face;
    if (!face.LoadFromFile(absoluteSourcePath, &result.error)) {
        return result;
    }
    result.success = true;
    return result;
}

ImportResult FontImporter::Import(const std::string& absoluteSourcePath,
                                  const nlohmann::json& settings) const {
    if (!HasAuthoredFontSettings(settings)) {
        return Import(absoluteSourcePath);
    }

    ImportResult result;
    std::uint32_t faceIndex = 0;
    std::uint32_t weight = 0;
    std::uint32_t stretchPercent = 0;
    if (!ReadUnsignedSetting(settings, "faceIndex", 0U, 0xFFFFU, faceIndex,
                             result) ||
        !ReadUnsignedSetting(settings, "weight", kFontWeightMin, kFontWeightMax,
                             weight, result) ||
        !ReadUnsignedSetting(settings, "stretchPercent", kFontStretchPercentMin,
                             kFontStretchPercentMax, stretchPercent, result)) {
        return result;
    }

    const auto slantValue = settings.find("slant");
    if (slantValue == settings.end() || !slantValue->is_string()) {
        Fail(result, "font import setting 'slant' is missing",
             "author the font import settings in the inspector");
        return result;
    }
    const auto slant = ParseStableFontSlant(slantValue->get<std::string>());
    if (!slant) {
        Fail(result, "unknown font slant: " + slantValue->get<std::string>(),
             "choose Upright, Italic or Oblique");
        return result;
    }

    FontLicenseMetadata license;
    const auto confirmed = settings.find("redistributableConfirmed");
    if (confirmed == settings.end() || !confirmed->is_boolean() ||
        !confirmed->get<bool>()) {
        Fail(result, "font redistribution has not been confirmed",
             "confirm redistribution rights before importing the font");
        return result;
    }
    license.redistributableConfirmed = true;
    const auto kind = settings.find("licenseKind");
    if (kind == settings.end() || !kind->is_string() ||
        kind->get<std::string>().empty()) {
        Fail(result, "font license kind is missing",
             "record the font license kind, for example OFL-1.1");
        return result;
    }
    license.licenseKind = kind->get<std::string>();
    const auto licenseGuid = settings.find("licenseAssetGuid");
    if (licenseGuid == settings.end() || !licenseGuid->is_string() ||
        licenseGuid->get<std::string>().empty()) {
        Fail(result, "font license asset GUID is missing",
             "reference the imported license file from the font asset");
        return result;
    }
    license.licenseAssetGuid = licenseGuid->get<std::string>();
    const auto copyright = settings.find("copyright");
    if (copyright != settings.end()) {
        if (!copyright->is_string()) {
            Fail(result, "font license copyright must be a string",
                 "author the font import settings in the inspector");
            return result;
        }
        license.copyright = copyright->get<std::string>();
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadFile(absoluteSourcePath, bytes)) {
        Fail(result, "could not read the font source: " + absoluteSourcePath,
             "check that the font file exists and is readable");
        return result;
    }

    std::uint64_t directoryOffset = 0;
    if (!SelectFaceDirectory(bytes, faceIndex, result, directoryOffset)) {
        return result;
    }
    SelectedFace face;
    if (!ValidateFaceDirectory(bytes, directoryOffset, result, face) ||
        !RejectUnsupportedTables(face, result)) {
        return result;
    }
    FontDesignMetrics metrics;
    if (!ParseDesignMetrics(bytes, face, result, metrics)) {
        return result;
    }

    // Step 7: 보안 정체성은 AssetRecord.hash(빠른 비암호 해시)가 아니라
    // 원본 바이트의 SHA-256이다.
    const std::string sourceSha = Sha256Bytes(bytes.data(), bytes.size());
    if (!IsLowercaseSha256(sourceSha)) {
        Fail(result, "could not compute the font source SHA-256",
             "reimport the font asset");
        return result;
    }

    nlohmann::json coverage = nlohmann::json::array();
    for (const auto& range : CollectCoverage(bytes, face)) {
        coverage.push_back(nlohmann::json::array(
            {static_cast<std::uint32_t>(range.first),
             static_cast<std::uint32_t>(range.second)}));
    }
    if (coverage.empty()) {
        Fail(result, "the selected face has no Unicode cmap coverage",
             "import a font with a Unicode character map");
        return result;
    }

    nlohmann::json font;
    font["sourceSha256"] = sourceSha;
    font["staticOutline"] = true;
    font["faceIndex"] = faceIndex;
    font["weight"] = weight;
    font["stretchPercent"] = stretchPercent;
    font["slant"] = StableFontSlant(*slant);
    font["designMetrics"] = {{"unitsPerEm", metrics.unitsPerEm},
                             {"ascender", metrics.ascender},
                             {"descender", metrics.descender},
                             {"lineGap", metrics.lineGap}};
    font["coverage"] = std::move(coverage);
    font["license"] = {
        {"redistributableConfirmed", license.redistributableConfirmed},
        {"licenseKind", license.licenseKind},
        {"copyright", license.copyright},
        {"licenseAssetGuid", license.licenseAssetGuid}};
    // AssetDatabase가 산출물을 발행할 때 실제 세대로 덮어쓴다.
    font["contentRevision"] = 0U;
    result.metadata["font"] = std::move(font);
    result.success = true;
    return result;
}

} // namespace molga
