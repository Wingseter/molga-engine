#pragma once

#include "Common/Fixed26_6.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace molga {

// AssetRecord는 이 헤더를 include하므로 여기서는 전방 선언만 쓴다.
struct AssetRecord;

enum class FontSlant : std::uint8_t { Upright, Italic, Oblique };
struct FontLicenseMetadata {
    bool redistributableConfirmed = false;
    std::string licenseKind;
    std::string copyright;
    std::string licenseAssetGuid;
};
struct FontDesignMetrics {
    std::uint16_t unitsPerEm = 0;
    std::int16_t ascender = 0;
    std::int16_t descender = 0;
    std::int16_t lineGap = 0;
};

// 레이아웃이 실제로 쓰는 수직 단위. descent는 아래쪽 거리이므로 SFNT의 음수
// descender와 달리 양수다.
struct ScaledFontDesignMetrics {
    Fixed26_6 ascent = Fixed26_6::FromRaw(0);
    Fixed26_6 descent = Fixed26_6::FromRaw(0);
    Fixed26_6 lineGap = Fixed26_6::FromRaw(0);
};

// 임포트된 정확한 SFNT 정수만 검사된 26.6 비율로 스케일한다. 래스터라이저의
// float 메트릭은 여기 들어오지 않는다: 같은 폰트/같은 크기가 빌드마다 정확히
// 같은 raw 값을 내야 캐시 정체성과 스냅샷 비교가 성립하기 때문이다. 검사에
// 실패하면 포화시키지 않고 실패를 보고한다.
std::optional<ScaledFontDesignMetrics> ScaleFontDesignMetrics(
    const FontDesignMetrics&, Fixed26_6 fontSize) noexcept;
enum class FontArtifactStorage : std::uint8_t {
    ProjectLibrary,
    PackagedResource
};
struct FontArtifactLocator {
    FontArtifactStorage storage = FontArtifactStorage::ProjectLibrary;
    std::filesystem::path relativePath;
    bool operator==(const FontArtifactLocator&) const;
};
struct VerifiedFontArtifact {
    FontArtifactLocator locator;
    std::string sourceSha256;
    std::string artifactSha256;
    std::uint64_t byteSize = 0;
};
struct FontAsset {
    std::string guid;
    std::string sourceSha256;
    FontArtifactLocator artifactLocator;
    std::string artifactSha256;
    std::uint64_t artifactByteSize = 0;
    std::uint32_t faceIndex = 0;
    std::uint16_t weight = 400;
    std::uint16_t stretchPercent = 100;
    FontSlant slant = FontSlant::Upright;
    std::uint64_t contentRevision = 0;
    FontDesignMetrics designMetrics;
    std::vector<std::pair<char32_t, char32_t>> coverage;
    FontLicenseMetadata license;
    static std::optional<FontAsset> FromRecord(
        const AssetRecord&, std::string& errorOut);
};

// 아래 문자열들은 카탈로그에 영구히 기록되는 안정 식별자다. 숫자 enum 값을
// 직렬화하면 나중에 enum 순서를 바꾸는 순간 조용히 다른 값으로 재해석되므로
// 저장·복원은 반드시 이 문자열을 거친다.
const char* StableFontSlant(FontSlant) noexcept;
std::optional<FontSlant> ParseStableFontSlant(std::string_view) noexcept;
const char* StableFontArtifactStorage(FontArtifactStorage) noexcept;
std::optional<FontArtifactStorage> ParseStableFontArtifactStorage(
    std::string_view) noexcept;

// 소문자 16진수 64자리만 SHA-256 식별자로 인정한다. 대소문자를 섞어 받으면
// 같은 내용이 두 개의 다른 content-address를 갖게 된다.
bool IsLowercaseSha256(std::string_view) noexcept;

// 프로젝트 라이브러리의 content-addressed 산출물 경로.
// 항상 "Library/Imported/Fonts/<artifactSha256>.sfnt".
std::string FontArtifactRelativePath(std::string_view artifactSha256);

// 상대 경로를 component 단위로 검사해 정규 forward-slash 문자열로 만든다.
// 절대 경로, root, ".", "..", 빈 component는 전부 거절한다. 문자열에서 ".."를
// 지우는 방식은 "....//" 같은 입력이 다시 escape로 살아나므로 쓰지 않는다.
bool NormalizeFontArtifactRelativePath(const std::filesystem::path& input,
                                       std::string& out);

// 설계가 닫아 둔 authored 범위. 밖의 값은 clamp가 아니라 거절 대상이다.
inline constexpr std::uint32_t kFontWeightMin = 1;
inline constexpr std::uint32_t kFontWeightMax = 1000;
inline constexpr std::uint32_t kFontStretchPercentMin = 50;
inline constexpr std::uint32_t kFontStretchPercentMax = 200;
inline constexpr std::uint32_t kFontUnitsPerEmMin = 16;
inline constexpr std::uint32_t kFontUnitsPerEmMax = 16384;

} // namespace molga
