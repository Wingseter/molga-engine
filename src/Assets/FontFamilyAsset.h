#pragma once

#include "Assets/FontAsset.h"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace molga {

// 애셋 모델이 카탈로그 헤더(Core/AssetDatabase.h)를 끌고 들어오지 않도록
// 전방 선언만 쓴다. FontAsset.h와 달리 AssetDatabase.h는 이 헤더를 include하지
// 않으므로 여기에 끊어야 할 include 순환은 없다.
struct AssetRecord;

struct FontFamilyFaceEntry {
    std::string fontGuid;
    std::uint32_t faceIndex = 0;
    std::uint16_t weight = 400;
    std::uint16_t stretchPercent = 100;
    FontSlant slant = FontSlant::Upright;
    std::uint32_t authoredFaceIndex = 0;
};
struct FontFamilyAsset {
    static constexpr int CurrentSchemaVersion = 1;
    std::string guid;
    int schemaVersion = CurrentSchemaVersion;
    std::vector<FontFamilyFaceEntry> faces;
    std::vector<std::string> fallbackFamilyGuids;
    static std::optional<FontFamilyAsset> FromRecord(
        const AssetRecord&, std::string& errorOut);
};

// 저작 소스(.fontfamily)와 카탈로그 metadata를 같은 규칙으로 읽는다. 두
// 곳에 규칙을 복제하면 한쪽만 조여지는 순간 카탈로그가 importer라면 거절했을
// 값을 받아들이게 되고, 그때 fallback 순서는 리뷰 없이 달라진다.
// `guid`는 여기서 예약어다. 정체성은 언제나 .meta sidecar가 쥔다.
// authoredFaceIndex는 배열 위치에서만 나온다. 문서가 값을 적어 두었다면
// 그 위치와 같아야 하며, 다르면 순서가 손상된 것이므로 거절한다.
// `out`은 true를 돌려줄 때만 바뀐다. 거절된 문서가 호출자의 기존 family를
// 절반만 덮어쓰면 "편집을 거절했다"는 응답과 실제 상태가 어긋난다.
// `out.guid`는 어느 경우에도 건드리지 않는다.
bool ParseAuthoredFontFamily(const nlohmann::json& document,
                             FontFamilyAsset& out, std::string& errorOut);

} // namespace molga
