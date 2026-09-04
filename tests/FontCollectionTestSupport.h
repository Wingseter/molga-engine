#pragma once

// ── A two-face collection the committed corpus cannot supply ────────────────
// FontImporter도 FontFace도 SFNT collection을 지원하지만, 자격 트리의 여섯
// 폰트는 전부 단일 face 파일이라 "카탈로그가 정한 face index"가 0이 아닌 상태를
// 어떤 테스트도 관찰하지 못한다. 그 상태에서는 face index 조회를 통째로 0으로
// 고정해 두어도 스위트가 통과하고, fontRevision의 ":<faceIndex>" 절반도 영원히
// ":0"만 본다.
//
// 그래서 단일 face 파일 하나를 face 둘짜리 collection으로 감싼다. 표 checksum은
// 표 바이트에서만 계산되므로(FontImporter의 ComputeTableChecksum 주석) 파일
// 안에서 표를 통째로 옮겨도 그대로 유효하고, 표 디렉터리의 절대 오프셋만 헤더
// 길이만큼 밀면 된다. 확장자를 .ttf로 두는 것은 의도한 것이다: 등록된 폰트
// 확장자는 .ttf/.otf뿐이고, collection 여부는 확장자가 아니라 파일 앞의 'ttcf'
// 태그가 정한다.
//
// Task 5.1이 resolver 쪽에서 만든 기법을 Task 5.2의 셰이핑 쪽도 그대로 써야
// 하므로 여기로 옮겨 둔다. 두 스위트가 서로 다른 실행 파일이라 헤더가 유일한
// 공유 수단이다.

#include "Core/AssetMeta.h"
#include "doctest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace test_support {

inline std::uint32_t ReadBigEndianU32(const std::vector<unsigned char>& bytes,
                                      std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24) |
           (static_cast<std::uint32_t>(bytes[at + 1U]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 2U]) << 8) |
           static_cast<std::uint32_t>(bytes[at + 3U]);
}

inline void WriteBigEndianU32(std::vector<unsigned char>& bytes,
                              std::size_t at, std::uint32_t value) {
    bytes[at] = static_cast<unsigned char>((value >> 24) & 0xFFU);
    bytes[at + 1U] = static_cast<unsigned char>((value >> 16) & 0xFFU);
    bytes[at + 2U] = static_cast<unsigned char>((value >> 8) & 0xFFU);
    bytes[at + 3U] = static_cast<unsigned char>(value & 0xFFU);
}

inline std::vector<unsigned char> ReadAllBytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    REQUIRE_MESSAGE(input.good(), path.string());
    const std::streamoff size = input.tellg();
    REQUIRE_MESSAGE(size > 0, path.string());
    input.seekg(0);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    REQUIRE_MESSAGE(input.good(), path.string());
    return bytes;
}

inline std::vector<unsigned char> MakeTwoFaceCollection(
    const std::filesystem::path& singleFace) {
    const std::vector<unsigned char> source = ReadAllBytes(singleFace);
    constexpr std::size_t kHeaderBytes = 12U + 4U * 2U;
    std::vector<unsigned char> bytes(kHeaderBytes + source.size(), 0U);
    bytes[0] = 't';
    bytes[1] = 't';
    bytes[2] = 'c';
    bytes[3] = 'f';
    WriteBigEndianU32(bytes, 4U, 0x00010000U);
    WriteBigEndianU32(bytes, 8U, 2U);
    // 두 face가 같은 표 디렉터리를 가리킨다. 이 픽스처가 증명하는 것은 어느
    // face의 모양이 아니라 "카탈로그가 정한 index로 열렸는가"이므로, 서로 다른
    // 두 글리프 집합을 만들 이유가 없다.
    WriteBigEndianU32(bytes, 12U, static_cast<std::uint32_t>(kHeaderBytes));
    WriteBigEndianU32(bytes, 16U, static_cast<std::uint32_t>(kHeaderBytes));
    std::copy(source.begin(), source.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderBytes));

    const std::uint16_t tableCount = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[kHeaderBytes + 4U]) << 8) |
        bytes[kHeaderBytes + 5U]);
    REQUIRE(tableCount > 0U);
    for (std::uint16_t index = 0; index < tableCount; ++index) {
        const std::size_t record =
            kHeaderBytes + 12U + static_cast<std::size_t>(index) * 16U;
        WriteBigEndianU32(bytes, record + 8U,
                          ReadBigEndianU32(bytes, record + 8U) +
                              static_cast<std::uint32_t>(kHeaderBytes));
    }
    return bytes;
}

inline void WriteBytes(const std::filesystem::path& path,
                       const std::vector<unsigned char>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), path.string());
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    REQUIRE_MESSAGE(output.good(), path.string());
}

// 자격 트리의 폰트 .meta와 같은 저작값을 쓴다. 다른 값은 `faceIndex`뿐이고,
// 그 값이 이 픽스처의 존재 이유다. provenance 세 필드는 트리가 고정한 계약이라
// 여기서 바꾸지 않는다(licenses/NotoFonts-ffebf8c1-OFL.txt의 GUID까지 포함).
inline constexpr const char* kNotoLicenseGuid =
    "88888888888888888888888888888888";

// 커밋된 단일 face 파일을 face 둘짜리 collection으로 감싸 `fontsDir` 아래에
// 애셋 원본과 그 .meta 한 쌍으로 저작한다. 한쪽만 쓰면 ScanProject가 새 GUID를
// 만들어 버리므로 언제나 둘을 함께 쓴다.
inline void AuthorTwoFaceCollectionFont(
    const std::filesystem::path& fontsDir, const std::string& fileName,
    const std::string& baseFontName, const std::string& guid,
    std::uint32_t faceIndex) {
    const std::filesystem::path source = fontsDir / fileName;
    // 커밋된 픽스처와 이름이 겹치면 그 GUID 계약을 덮어쓰게 된다.
    REQUIRE_MESSAGE(!std::filesystem::exists(source), source.string());
    WriteBytes(source, MakeTwoFaceCollection(fontsDir / baseFontName));

    const std::filesystem::path metaPath =
        molga::AssetMeta::MetaPathFor(source);
    const nlohmann::json meta{
        {"guid", guid},
        {"importer", "FontImporter"},
        {"importerVersion", 2},
        {"settings",
         nlohmann::json{{"faceIndex", faceIndex},
                        {"weight", 400},
                        {"stretchPercent", 100},
                        {"slant", "Upright"},
                        {"redistributableConfirmed", true},
                        {"licenseKind", "OFL-1.1"},
                        {"copyright", "fixture provenance: Noto Fonts ffebf8c1"},
                        {"licenseAssetGuid", kNotoLicenseGuid}}}};
    std::filesystem::create_directories(metaPath.parent_path());
    std::ofstream output(metaPath, std::ios::trunc);
    REQUIRE_MESSAGE(output.good(), metaPath.string());
    output << meta.dump(2);
    output.close();
    REQUIRE_MESSAGE(output.good(), metaPath.string());
}

} // namespace test_support
