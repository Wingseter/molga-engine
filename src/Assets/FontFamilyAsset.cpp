#include "Assets/FontFamilyAsset.h"

#include "Core/AssetDatabase.h"
#include "Core/Guid.h"

#include <limits>
#include <string>

namespace molga {
namespace {

// SFNT collection의 face index는 16비트 범위를 넘지 않는다. FontImporter가
// 같은 상한으로 검증하므로, family가 그보다 넓은 값을 저작해 두면 참조된 face를
// 영원히 열 수 없는 항목이 카탈로그에 남는다.
constexpr std::uint32_t kMaxAuthoredFaceIndex = 0xFFFFU;

bool ReadUnsigned(const nlohmann::json& parent, const char* key,
                  std::uint32_t minimum, std::uint32_t maximum,
                  std::uint32_t& out, std::string& errorOut) {
    // 파일에서 파싱한 값은 number_unsigned, 코드에서 만든 값은 number_integer로
    // 저장된다. 같은 값의 두 표현이므로 부호만 확인하고 둘 다 받는다.
    const auto found = parent.find(key);
    if (found == parent.end() || !found->is_number_integer()) {
        errorOut = std::string("font family face field is missing or not an "
                               "integer: ") + key;
        return false;
    }
    if (!found->is_number_unsigned() && found->get<std::int64_t>() < 0) {
        errorOut = std::string("font family face field is out of range: ") + key;
        return false;
    }
    const std::uint64_t value = found->get<std::uint64_t>();
    if (value < minimum || value > maximum) {
        errorOut = std::string("font family face field is out of range: ") + key;
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool ReadGuid(const nlohmann::json& value, const char* what,
              std::string& out, std::string& errorOut) {
    if (!value.is_string()) {
        errorOut = std::string(what) + " must be a string";
        return false;
    }
    out = value.get<std::string>();
    if (!Guid::IsValid(out)) {
        errorOut = std::string(what) + " is not a 32 character asset GUID: " + out;
        return false;
    }
    return true;
}

bool ParseFace(const nlohmann::json& value, std::uint32_t position,
               FontFamilyFaceEntry& out, std::string& errorOut) {
    if (!value.is_object()) {
        errorOut = "font family face entry must be an object";
        return false;
    }
    const auto fontGuid = value.find("fontGuid");
    if (fontGuid == value.end()) {
        errorOut = "font family face entry has no fontGuid";
        return false;
    }
    if (!ReadGuid(*fontGuid, "font family fontGuid", out.fontGuid, errorOut)) {
        return false;
    }

    std::uint32_t scalar = 0;
    if (!ReadUnsigned(value, "faceIndex", 0U, kMaxAuthoredFaceIndex, scalar,
                      errorOut)) {
        return false;
    }
    out.faceIndex = scalar;
    if (!ReadUnsigned(value, "weight", kFontWeightMin, kFontWeightMax, scalar,
                      errorOut)) {
        return false;
    }
    out.weight = static_cast<std::uint16_t>(scalar);
    if (!ReadUnsigned(value, "stretchPercent", kFontStretchPercentMin,
                      kFontStretchPercentMax, scalar, errorOut)) {
        return false;
    }
    out.stretchPercent = static_cast<std::uint16_t>(scalar);

    const auto slant = value.find("slant");
    if (slant == value.end() || !slant->is_string()) {
        errorOut = "font family face entry has no slant";
        return false;
    }
    const auto parsed = ParseStableFontSlant(slant->get<std::string>());
    if (!parsed) {
        errorOut = "unknown font slant: " + slant->get<std::string>();
        return false;
    }
    out.slant = *parsed;

    // 위치가 유일한 권한이다. 문서가 자기 순서를 적어 두었다면 위치와
    // 일치해야 하고, 어긋나면 저작 순서가 손상된 것이므로 거절한다.
    if (value.contains("authoredFaceIndex")) {
        std::uint32_t recorded = 0;
        if (!ReadUnsigned(value, "authoredFaceIndex", 0U,
                          std::numeric_limits<std::uint32_t>::max(), recorded,
                          errorOut)) {
            return false;
        }
        if (recorded != position) {
            errorOut = "font family authoredFaceIndex disagrees with its "
                       "authored array position";
            return false;
        }
    }
    out.authoredFaceIndex = position;
    return true;
}

} // namespace

bool ParseAuthoredFontFamily(const nlohmann::json& document,
                             FontFamilyAsset& out, std::string& errorOut) {
    errorOut.clear();
    if (!document.is_object()) {
        errorOut = "font family document root must be an object";
        return false;
    }
    if (document.contains("guid")) {
        errorOut = "font family identity comes from the .meta sidecar, not "
                   "from an embedded guid field";
        return false;
    }
    if (document.contains("schema")) {
        // 예전 구조화 애셋의 키다. schemaVersion과 나란히 두면 어느 쪽이
        // 버전인지 알 수 없으므로 조용히 무시하지 않고 거절한다.
        errorOut = "font family uses the legacy 'schema' key; author "
                   "schemaVersion instead";
        return false;
    }
    // is_number_integer()는 JSON float에 대해 이미 false다. 부동소수 검사를
    // 따로 덧붙이면 읽는 사람이 그 절이 float를 막는다고 오해한다.
    const auto schemaVersion = document.find("schemaVersion");
    if (schemaVersion == document.end() || !schemaVersion->is_number_integer()) {
        errorOut = "font family schemaVersion is missing or not an integer";
        return false;
    }
    if (schemaVersion->get<std::int64_t>() !=
        FontFamilyAsset::CurrentSchemaVersion) {
        errorOut = "unsupported font family schemaVersion: " +
                   schemaVersion->dump();
        return false;
    }

    // 거절은 호출자의 값을 절대 건드리지 않는다. 지역 사본에 채우고 성공한
    // 뒤에만 옮기므로, 실패한 편집이 face 목록을 첫 불량 항목에서 잘라 놓은
    // 채 남는 일이 없다.
    FontFamilyAsset parsed;
    parsed.schemaVersion = FontFamilyAsset::CurrentSchemaVersion;

    const auto faces = document.find("faces");
    if (faces == document.end() || !faces->is_array()) {
        errorOut = "font family faces must be an array";
        return false;
    }
    if (faces->size() > std::numeric_limits<std::uint32_t>::max()) {
        errorOut = "font family has more faces than an authored index can name";
        return false;
    }
    parsed.faces.reserve(faces->size());
    std::uint32_t position = 0;
    for (const auto& value : *faces) {
        FontFamilyFaceEntry entry;
        if (!ParseFace(value, position, entry, errorOut)) return false;
        parsed.faces.push_back(std::move(entry));
        ++position;
    }

    const auto fallbacks = document.find("fallbackFamilyGuids");
    if (fallbacks == document.end() || !fallbacks->is_array()) {
        errorOut = "font family fallbackFamilyGuids must be an array";
        return false;
    }
    parsed.fallbackFamilyGuids.reserve(fallbacks->size());
    for (const auto& value : *fallbacks) {
        std::string guid;
        if (!ReadGuid(value, "font family fallbackFamilyGuids entry", guid,
                      errorOut)) {
            return false;
        }
        parsed.fallbackFamilyGuids.push_back(std::move(guid));
    }

    out.schemaVersion = parsed.schemaVersion;
    out.faces = std::move(parsed.faces);
    out.fallbackFamilyGuids = std::move(parsed.fallbackFamilyGuids);
    return true;
}

std::optional<FontFamilyAsset> FontFamilyAsset::FromRecord(
    const AssetRecord& record, std::string& errorOut) {
    errorOut.clear();
    if (record.importer != "FontFamilyImporter") {
        errorOut = "asset record was not produced by FontFamilyImporter";
        return std::nullopt;
    }
    if (record.importFailed) {
        // 실패한 import의 metadata는 부분적으로만 채워질 수 있으므로 절대
        // family로 승격하지 않는다.
        errorOut = "font family import failed: " + record.importError;
        return std::nullopt;
    }
    // Step 3: 정체성은 sidecar가 쥔 record.guid뿐이다. importer metadata의
    // 어떤 필드도 이 값을 대신할 수 없고, ParseAuthoredFontFamily는 metadata에
    // 남아 있는 guid 자체를 거절한다.
    if (!Guid::IsValid(record.guid)) {
        errorOut = "font family record GUID is not 32 hexadecimal characters";
        return std::nullopt;
    }

    FontFamilyAsset family;
    if (!ParseAuthoredFontFamily(record.metadata, family, errorOut)) {
        return std::nullopt;
    }
    family.guid = record.guid;
    return family;
}

} // namespace molga
