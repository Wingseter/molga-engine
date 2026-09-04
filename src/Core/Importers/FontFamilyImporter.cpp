#include "Core/Importers/FontFamilyImporter.h"

#include "Assets/FontFamilyAsset.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace molga {
namespace {

// 저작 파일 하나가 카탈로그를 무한히 키우지 못하도록 상한을 둔다. family는
// 정렬된 GUID 목록일 뿐이라 정상 파일은 이보다 몇 자릿수 작다.
constexpr std::uintmax_t kMaximumFontFamilyBytes = 4U * 1024U * 1024U;

void Fail(ImportResult& result, std::string message, std::string remediation) {
    // 첫 실패에서 즉시 반환하므로 애셋 하나가 만드는 진단 수는 상수(1)로
    // 묶인다. 항목마다 진단을 쌓으면 잘못 생성된 저작 파일 하나가 카탈로그를
    // 항목 수만큼 부풀린다.
    result.success = false;
    result.metadata = nlohmann::json::object();
    result.dependencies.clear();
    molga::text::TextDiagnostic diagnostic;
    diagnostic.code = molga::text::TextDiagnosticCode::FontFamilyInvalid;
    diagnostic.severity = molga::text::TextSeverity::Error;
    diagnostic.subsystem = "font-family-import";
    diagnostic.message = message;
    diagnostic.remediation = std::move(remediation);
    diagnostic.componentType = "FontFamilyAsset";
    result.importDiagnostics.push_back(std::move(diagnostic));
    result.error = std::move(message);
}

} // namespace

bool FontFamilyImporter::CanImport(const std::string& extension) const {
    return extension == ".fontfamily";
}

ImportResult FontFamilyImporter::Import(
    const std::string& absoluteSourcePath) const {
    ImportResult result;

    std::error_code error;
    if (!std::filesystem::is_regular_file(absoluteSourcePath, error)) {
        Fail(result, "could not read the font family source: " +
                         absoluteSourcePath,
             "check that the font family file exists and is readable");
        return result;
    }
    const std::uintmax_t size =
        std::filesystem::file_size(absoluteSourcePath, error);
    if (error || size > kMaximumFontFamilyBytes) {
        Fail(result, "font family source is missing or too large: " +
                         absoluteSourcePath,
             "author the family as an ordered list of face and fallback GUIDs");
        return result;
    }

    nlohmann::json document;
    try {
        std::ifstream input(absoluteSourcePath);
        if (!input) {
            Fail(result, "could not open the font family source: " +
                             absoluteSourcePath,
                 "check that the font family file exists and is readable");
            return result;
        }
        // operator>>는 strict=false로 파싱해서 첫 JSON 값 뒤에 남은 바이트를
        // 말없이 버린다. 그러면 문서를 두 개 이어 붙인 저작 파일이 앞의 것만
        // 반영된 채 "성공"으로 들어오므로, 끝까지 소비하는 parse()를 쓴다.
        document = nlohmann::json::parse(input);
    } catch (const std::exception& parseError) {
        Fail(result,
             std::string("invalid font family JSON: ") + parseError.what(),
             "fix the font family JSON syntax");
        return result;
    }

    // Step 5: 로컬 필드만 검증한다. face/fallback GUID가 실제로 존재하는지와
    // fallback graph의 cycle 여부는 Task 5.1의 해석 단계가 판단한다. importer가
    // 미리 거절하면 그 단계가 검사해야 할 그래프를 볼 수 없다.
    FontFamilyAsset family;
    std::string reason;
    if (!ParseAuthoredFontFamily(document, family, reason)) {
        Fail(result, std::move(reason),
             "author the font family with schemaVersion 1, ordered faces and "
             "ordered fallback family GUIDs");
        return result;
    }

    // Step 4a: 알 수 없는 최상위 필드는 그대로 남긴다. 새 저작 도구가 붙인
    // 필드를 예전 에디터가 조용히 지우면 저작 의도가 사라진다. 정체성만은
    // 예약어라 ParseAuthoredFontFamily가 이미 거절했다.
    nlohmann::json metadata = document;
    metadata["schemaVersion"] = family.schemaVersion;
    nlohmann::json faces = nlohmann::json::array();
    for (std::size_t index = 0; index < family.faces.size(); ++index) {
        const FontFamilyFaceEntry& face = family.faces[index];
        // 저작된 항목 위에 정규화된 값을 덮어써서 face 단위 확장 필드도 함께
        // 보존한다. 순서 권한은 배열 위치이므로 authoredFaceIndex는 항상 다시
        // 쓴다.
        nlohmann::json entry = document["faces"][index];
        entry["fontGuid"] = face.fontGuid;
        entry["faceIndex"] = face.faceIndex;
        entry["weight"] = face.weight;
        entry["stretchPercent"] = face.stretchPercent;
        entry["slant"] = StableFontSlant(face.slant);
        entry["authoredFaceIndex"] = face.authoredFaceIndex;
        faces.push_back(std::move(entry));
    }
    metadata["faces"] = std::move(faces);
    metadata["fallbackFamilyGuids"] = family.fallbackFamilyGuids;

    result.metadata = std::move(metadata);
    result.success = true;
    return result;
}

} // namespace molga
