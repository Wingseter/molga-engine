#include "Assets/FontAsset.h"

#include "Core/AssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace molga {
namespace {

bool ReadUnsigned(const nlohmann::json& parent, const char* key,
                  std::uint32_t minimum, std::uint32_t maximum,
                  std::uint32_t& out, std::string& errorOut) {
    // 파일에서 파싱한 값은 number_unsigned, 코드에서 만든 값은 number_integer로
    // 저장된다. 같은 값의 두 표현이므로 부호만 확인하고 둘 다 받는다.
    const auto found = parent.find(key);
    if (found == parent.end() || !found->is_number_integer()) {
        errorOut = std::string("font metadata field is missing or not an "
                               "integer: ") + key;
        return false;
    }
    if (!found->is_number_unsigned() && found->get<std::int64_t>() < 0) {
        errorOut = std::string("font metadata field is out of range: ") + key;
        return false;
    }
    const std::uint64_t value = found->get<std::uint64_t>();
    if (value < minimum || value > maximum) {
        errorOut = std::string("font metadata field is out of range: ") + key;
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool ReadSigned16(const nlohmann::json& parent, const char* key,
                  std::int16_t& out, std::string& errorOut) {
    const auto found = parent.find(key);
    // is_number_integer() is deliberately used instead of is_number(): a float
    // here would mean a rasterizer-derived metric leaked into layout identity.
    if (found == parent.end() || !found->is_number_integer() ||
        found->is_number_float()) {
        errorOut = std::string("font metric is missing or not an integer: ") + key;
        return false;
    }
    const std::int64_t value = found->get<std::int64_t>();
    if (value < std::numeric_limits<std::int16_t>::min() ||
        value > std::numeric_limits<std::int16_t>::max()) {
        errorOut = std::string("font metric does not fit an SFNT FWORD: ") + key;
        return false;
    }
    out = static_cast<std::int16_t>(value);
    return true;
}

bool ReadString(const nlohmann::json& parent, const char* key, bool required,
                std::string& out, std::string& errorOut) {
    const auto found = parent.find(key);
    if (found == parent.end() || !found->is_string()) {
        if (!required) {
            out.clear();
            return true;
        }
        errorOut = std::string("font metadata field is missing or not a "
                               "string: ") + key;
        return false;
    }
    out = found->get<std::string>();
    return true;
}

} // namespace

bool FontArtifactLocator::operator==(const FontArtifactLocator& other) const {
    return storage == other.storage && relativePath == other.relativePath;
}

const char* StableFontSlant(FontSlant slant) noexcept {
    switch (slant) {
        case FontSlant::Upright: return "Upright";
        case FontSlant::Italic:  return "Italic";
        case FontSlant::Oblique: return "Oblique";
    }
    return "Upright";
}

std::optional<FontSlant> ParseStableFontSlant(std::string_view value) noexcept {
    if (value == "Upright") return FontSlant::Upright;
    if (value == "Italic")  return FontSlant::Italic;
    if (value == "Oblique") return FontSlant::Oblique;
    return std::nullopt;
}

const char* StableFontArtifactStorage(FontArtifactStorage storage) noexcept {
    switch (storage) {
        case FontArtifactStorage::ProjectLibrary:  return "ProjectLibrary";
        case FontArtifactStorage::PackagedResource: return "PackagedResource";
    }
    return "ProjectLibrary";
}

std::optional<FontArtifactStorage> ParseStableFontArtifactStorage(
    std::string_view value) noexcept {
    if (value == "ProjectLibrary")   return FontArtifactStorage::ProjectLibrary;
    if (value == "PackagedResource") return FontArtifactStorage::PackagedResource;
    return std::nullopt;
}

bool IsLowercaseSha256(std::string_view value) noexcept {
    if (value.size() != 64U) return false;
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

std::optional<ScaledFontDesignMetrics> ScaleFontDesignMetrics(
    const FontDesignMetrics& metrics, Fixed26_6 fontSize) noexcept {
    // 임포터가 이미 강제한 불변식을 여기서 다시 요구한다. 이 함수는 카탈로그를
    // 거치지 않은 record도 받을 수 있고, 불변식이 깨진 값에 대해 "그럴듯한"
    // 숫자를 내놓는 것이 가장 나쁜 실패이기 때문이다.
    if (metrics.unitsPerEm < kFontUnitsPerEmMin ||
        metrics.unitsPerEm > kFontUnitsPerEmMax || metrics.ascender <= 0 ||
        metrics.descender > 0 || metrics.ascender <= metrics.descender ||
        metrics.lineGap < 0 || fontSize.Raw() <= 0) {
        return std::nullopt;
    }

    const std::int64_t unitsPerEm = static_cast<std::int64_t>(metrics.unitsPerEm);
    // descender는 SFNT에서 아래쪽을 음수로 적으므로 한 번만 부호를 뒤집는다.
    // int16의 하한(-32768)도 int64로 올린 뒤 뒤집으므로 넘치지 않는다.
    const std::optional<Fixed26_6> ascent = Fixed26_6::CheckedMulDiv(
        fontSize, static_cast<std::int64_t>(metrics.ascender), unitsPerEm);
    const std::optional<Fixed26_6> descent = Fixed26_6::CheckedMulDiv(
        fontSize, -static_cast<std::int64_t>(metrics.descender), unitsPerEm);
    const std::optional<Fixed26_6> lineGap = Fixed26_6::CheckedMulDiv(
        fontSize, static_cast<std::int64_t>(metrics.lineGap), unitsPerEm);
    if (!ascent || !descent || !lineGap) return std::nullopt;

    ScaledFontDesignMetrics scaled;
    scaled.ascent = *ascent;
    scaled.descent = *descent;
    scaled.lineGap = *lineGap;
    return scaled;
}

std::string FontArtifactRelativePath(std::string_view artifactSha256) {
    return "Library/Imported/Fonts/" + std::string(artifactSha256) + ".sfnt";
}

bool NormalizeFontArtifactRelativePath(const std::filesystem::path& input,
                                       std::string& out) {
    if (input.empty() || input.is_absolute() || input.has_root_name() ||
        input.has_root_directory()) {
        return false;
    }
    std::string normalized;
    for (const auto& part : input) {
        const std::string component = part.generic_string();
        if (component.empty() || component == "." || component == ".." ||
            component.find('/') != std::string::npos ||
            component.find('\\') != std::string::npos) {
            return false;
        }
        if (!normalized.empty()) normalized += '/';
        normalized += component;
    }
    if (normalized.empty()) return false;
    out = normalized;
    return true;
}

std::optional<FontAsset> FontAsset::FromRecord(const AssetRecord& record,
                                               std::string& errorOut) {
    errorOut.clear();
    if (record.importer != "FontImporter") {
        errorOut = "asset record was not produced by FontImporter";
        return std::nullopt;
    }
    if (record.importFailed) {
        // 실패한 import의 metadata는 부분적으로만 채워질 수 있으므로 절대
        // 폰트 자산으로 승격하지 않는다.
        errorOut = "font import failed: " + record.importError;
        return std::nullopt;
    }
    if (!record.fontArtifact.has_value()) {
        errorOut = "font asset record has no verified immutable artifact";
        return std::nullopt;
    }
    const auto font = record.metadata.find("font");
    if (font == record.metadata.end() || !font->is_object()) {
        errorOut = "font asset record has no imported font metadata";
        return std::nullopt;
    }

    FontAsset asset;
    asset.guid = record.guid;
    asset.artifactLocator = record.fontArtifact->locator;
    asset.artifactSha256 = record.fontArtifact->artifactSha256;
    asset.artifactByteSize = record.fontArtifact->byteSize;
    asset.sourceSha256 = record.fontArtifact->sourceSha256;

    std::string metadataSha;
    if (!ReadString(*font, "sourceSha256", true, metadataSha, errorOut)) {
        return std::nullopt;
    }
    if (metadataSha != asset.sourceSha256) {
        errorOut = "font metadata and artifact disagree about the source SHA-256";
        return std::nullopt;
    }
    const auto staticOutline = font->find("staticOutline");
    if (staticOutline == font->end() || !staticOutline->is_boolean() ||
        !staticOutline->get<bool>()) {
        errorOut = "font asset is not marked as a static outline face";
        return std::nullopt;
    }

    std::uint32_t scalar = 0;
    if (!ReadUnsigned(*font, "faceIndex", 0U,
                      std::numeric_limits<std::uint32_t>::max(), scalar,
                      errorOut)) {
        return std::nullopt;
    }
    asset.faceIndex = scalar;
    if (!ReadUnsigned(*font, "weight", kFontWeightMin, kFontWeightMax, scalar,
                      errorOut)) {
        return std::nullopt;
    }
    asset.weight = static_cast<std::uint16_t>(scalar);
    if (!ReadUnsigned(*font, "stretchPercent", kFontStretchPercentMin,
                      kFontStretchPercentMax, scalar, errorOut)) {
        return std::nullopt;
    }
    asset.stretchPercent = static_cast<std::uint16_t>(scalar);

    std::string slant;
    if (!ReadString(*font, "slant", true, slant, errorOut)) return std::nullopt;
    const auto parsedSlant = ParseStableFontSlant(slant);
    if (!parsedSlant) {
        errorOut = "unknown font slant: " + slant;
        return std::nullopt;
    }
    asset.slant = *parsedSlant;

    if (!ReadUnsigned(*font, "contentRevision", 0U,
                      std::numeric_limits<std::uint32_t>::max(), scalar,
                      errorOut)) {
        return std::nullopt;
    }
    asset.contentRevision = scalar;

    const auto metrics = font->find("designMetrics");
    if (metrics == font->end() || !metrics->is_object()) {
        errorOut = "font asset record has no design metrics";
        return std::nullopt;
    }
    if (!ReadUnsigned(*metrics, "unitsPerEm", kFontUnitsPerEmMin,
                      kFontUnitsPerEmMax, scalar, errorOut)) {
        return std::nullopt;
    }
    asset.designMetrics.unitsPerEm = static_cast<std::uint16_t>(scalar);
    if (!ReadSigned16(*metrics, "ascender", asset.designMetrics.ascender,
                      errorOut) ||
        !ReadSigned16(*metrics, "descender", asset.designMetrics.descender,
                      errorOut) ||
        !ReadSigned16(*metrics, "lineGap", asset.designMetrics.lineGap,
                      errorOut)) {
        return std::nullopt;
    }

    const auto coverage = font->find("coverage");
    if (coverage == font->end() || !coverage->is_array()) {
        errorOut = "font asset record has no cmap coverage";
        return std::nullopt;
    }
    char32_t previousEnd = 0;
    bool first = true;
    for (const auto& range : *coverage) {
        if (!range.is_array() || range.size() != 2U ||
            !range[0].is_number_unsigned() || !range[1].is_number_unsigned()) {
            errorOut = "font coverage range is not a [begin,end] integer pair";
            return std::nullopt;
        }
        const std::uint64_t begin = range[0].get<std::uint64_t>();
        const std::uint64_t end = range[1].get<std::uint64_t>();
        if (begin > end || end > 0x10FFFFULL) {
            errorOut = "font coverage range is not a valid Unicode interval";
            return std::nullopt;
        }
        if (!first && begin <= previousEnd) {
            errorOut = "font coverage ranges are not canonically ordered";
            return std::nullopt;
        }
        previousEnd = static_cast<char32_t>(end);
        first = false;
        asset.coverage.emplace_back(static_cast<char32_t>(begin),
                                    static_cast<char32_t>(end));
    }

    const auto license = font->find("license");
    if (license == font->end() || !license->is_object()) {
        errorOut = "font asset record has no license metadata";
        return std::nullopt;
    }
    const auto confirmed = license->find("redistributableConfirmed");
    if (confirmed == license->end() || !confirmed->is_boolean()) {
        errorOut = "font license redistribution confirmation is missing";
        return std::nullopt;
    }
    asset.license.redistributableConfirmed = confirmed->get<bool>();
    if (!ReadString(*license, "licenseKind", true, asset.license.licenseKind,
                    errorOut) ||
        !ReadString(*license, "copyright", false, asset.license.copyright,
                    errorOut) ||
        !ReadString(*license, "licenseAssetGuid", true,
                    asset.license.licenseAssetGuid, errorOut)) {
        return std::nullopt;
    }
    if (!asset.license.redistributableConfirmed ||
        asset.license.licenseKind.empty()) {
        errorOut = "font asset is not confirmed redistributable";
        return std::nullopt;
    }
    return asset;
}

} // namespace molga
