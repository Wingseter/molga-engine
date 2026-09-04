#include "Text/FontRepository.h"

#include "Assets/FontArtifactStore.h"
#include "Common/Sha256.h"
#include "Core/AssetDatabase.h"

#include <utility>

namespace molga::text {
namespace {

// 한 번의 Load 실패에 진단 하나. TextDiagnosticRateLimitKey는 code, assetGuid,
// sceneObjectId, componentType, sourceByteRange를 모두 접지만 이 호출부는 뒤의
// 셋을 기본값으로 두므로 key가 사실상 code+assetGuid로 줄어들고, 로거 경로에서는
// 같은 폰트가 폰트당 하나로 눌린다. 입력 길이에 비례해 늘어나는 sourceByteRange를
// 여기서 절대 채우지 않는 것이 그 성질을 유지하는 조건이다.
//
// 다만 성공만 캐시되고 실패는 캐시되지 않으므로(수리된 산출물이 Invalidate 없이
// 곧바로 성공해야 한다), Load를 N번 부르면 진단도 N개다. VectorTextDiagnosticSink는
// 접지 않으므로, 한 프레임에 같은 폰트를 여러 번 해석하는 소비자의 상한은 이
// 저장소가 아니라 그 호출부가 져야 한다.
void ReportFontInvalid(TextDiagnosticSink& sink, const std::string& fontGuid,
                       std::string message, std::string remediation) {
    TextDiagnostic diagnostic;
    diagnostic.code = TextDiagnosticCode::FontInvalid;
    diagnostic.severity = TextSeverity::Error;
    diagnostic.subsystem = "font-repository";
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    diagnostic.assetGuid = fontGuid;
    sink.Report(std::move(diagnostic));
}

// Step 6의 캐시 정체성. 여덟 항목 중 하나라도 다르면 같은 자원이 아니다.
//
// 이 여덟은 서로 독립이 아니다. 카탈로그가 sourceSha256 == artifactSha256을
// 강제하고 ProjectLibrary locator는 자기 artifactSha256의 content address여야
// 하므로, 프로젝트 모드에서는 그 셋이 항상 함께 움직인다. storage는 카탈로그
// mode가, faceIndex는 캐시 버킷 키와 바로 위의 요청 face 검사가 이미 고정한다.
// 그래서 개별 증인을 가질 수 있는 항목은 contentGeneration, artifactByteSize,
// 그리고 SHA와 독립인 봉인 카탈로그의 relativePath 셋뿐이고 — 그 셋은 실제로
// 각각 테스트가 있다 — 나머지는 중복 방어로 남겨 둔다.
bool MatchesIdentity(const FontFaceResource& resource,
                     const std::string& fontGuid, const molga::FontAsset& asset,
                     std::uint64_t contentGeneration, std::uint32_t faceIndex) {
    return resource.asset != nullptr && resource.asset->guid == fontGuid &&
           resource.contentGeneration == contentGeneration &&
           resource.sourceSha256 == asset.sourceSha256 &&
           resource.artifactSha256 == asset.artifactSha256 &&
           resource.artifactLocator.storage == asset.artifactLocator.storage &&
           resource.artifactLocator.relativePath ==
               asset.artifactLocator.relativePath &&
           resource.asset->artifactByteSize == asset.artifactByteSize &&
           resource.faceIndex == faceIndex;
}

// Step 7b의 마지막 정상 자원 판정. FromRecord가 record를 거절했으므로 비교할
// FontAsset이 없고, 대신 실패한 import가 손대지 않고 남겨 둔 record의 산출물
// 정체성과 맞춘다.
//
// 세대만으로는 부족하다: 모르는 GUID와 한 번도 재발행되지 않은 GUID가 똑같이
// 0이므로, 프로젝트를 갈아 끼운 뒤 같은 GUID가 다른 폰트를 가리키면 옛 캐시가
// "마지막 정상"으로 통과해 버린다. 산출물 정체성까지 맞아야만 그 캐시가 지금
// 카탈로그가 가리키는 바로 그 바이트다.
bool MatchesLastGood(const FontFaceResource& resource,
                     const std::string& fontGuid,
                     const molga::VerifiedFontArtifact& artifact,
                     std::uint64_t contentGeneration, std::uint32_t faceIndex) {
    return resource.asset != nullptr && resource.asset->guid == fontGuid &&
           resource.contentGeneration == contentGeneration &&
           resource.sourceSha256 == artifact.sourceSha256 &&
           resource.artifactSha256 == artifact.artifactSha256 &&
           resource.artifactLocator == artifact.locator &&
           resource.asset->artifactByteSize == artifact.byteSize &&
           resource.faceIndex == faceIndex;
}

} // namespace

FontRepository::FontRepository(const molga::AssetDatabase& database)
    : database_(database) {}

FontFaceResourcePtr FontRepository::FindCached(const std::string& fontGuid,
                                               std::uint32_t faceIndex) const {
    const auto bucket = cache_.find(fontGuid);
    if (bucket == cache_.end()) return nullptr;
    const auto face = bucket->second.find(faceIndex);
    return face == bucket->second.end() ? nullptr : face->second;
}

void FontRepository::Publish(const std::string& fontGuid,
                             FontFaceResourcePtr resource) const {
    cache_[fontGuid][resource->faceIndex] = std::move(resource);
}

void FontRepository::Invalidate(const std::string& fontGuid) {
    cache_.erase(fontGuid);
}

std::optional<FontFaceResourcePtr> FontRepository::Load(
    const std::string& fontGuid, std::uint32_t faceIndex,
    TextDiagnosticSink& sink) const {
    const molga::FontArtifactStore* store = database_.FontArtifacts();
    if (!store) {
        // 바인딩된 바이트 권한이 없으면 어떤 바이트도 폰트 바이트가 아니다.
        // 저작 원본으로 되돌아가는 대체 경로는 존재하지 않는다.
        ReportFontInvalid(sink, fontGuid,
                          "no font artifact store is bound for this asset "
                          "database",
                          "bind the project or sealed-package font artifact "
                          "store before loading fonts");
        return std::nullopt;
    }

    const molga::AssetRecord* record = database_.Find(fontGuid);
    if (!record) {
        ReportFontInvalid(sink, fontGuid,
                          "the font asset is not in the loaded catalog",
                          "rescan the project or rebuild the package catalog");
        return std::nullopt;
    }

    const std::uint64_t contentGeneration =
        database_.ContentGeneration(fontGuid);

    std::string assetError;
    const std::optional<molga::FontAsset> asset =
        molga::FontAsset::FromRecord(*record, assetError);
    if (!asset) {
        // Step 7b: 실패한 import는 새 자원을 만들지 않는다. 새 세대도 발행되지
        // 않았고 record의 산출물도 마지막 성공 상태 그대로이므로, 그 정체성과
        // 맞는 이미 검증된 자원이 있으면 그것이 계속 권한이고 에디터 미리보기는
        // 그대로 유지된다.
        if (const FontFaceResourcePtr last = FindCached(fontGuid, faceIndex)) {
            if (record->fontArtifact &&
                MatchesLastGood(*last, fontGuid, *record->fontArtifact,
                                contentGeneration, faceIndex)) {
                return last;
            }
        }
        ReportFontInvalid(sink, fontGuid,
                          "the font asset record is not a usable static face: " +
                              assetError,
                          "reimport the font asset");
        return std::nullopt;
    }
    if (asset->faceIndex != faceIndex) {
        ReportFontInvalid(sink, fontGuid,
                          "the requested font face index is not the imported "
                          "face of this asset",
                          "author the face index on the font asset before "
                          "requesting it");
        return std::nullopt;
    }

    if (const FontFaceResourcePtr cached = FindCached(fontGuid, faceIndex)) {
        if (MatchesIdentity(*cached, fontGuid, *asset, contentGeneration,
                            faceIndex)) {
            return cached;
        }
    }

    // 카탈로그가 권한을 갖는 정체성 그대로 산출물을 요구한다. 경로도, SHA도,
    // 크기도 여기서 만들어 내지 않는다.
    molga::VerifiedFontArtifact artifact;
    artifact.locator = asset->artifactLocator;
    artifact.sourceSha256 = asset->sourceSha256;
    artifact.artifactSha256 = asset->artifactSha256;
    artifact.byteSize = asset->artifactByteSize;

    const auto bytes = store->ReadVerified(artifact, sink);
    if (!bytes || !*bytes) {
        // ReadVerified가 이미 사유가 있는 FontInvalid를 보고했다.
        return std::nullopt;
    }

    // 정체성을 store 밖에서 한 번 더 확인한다. 이 저장소는 "산출물이 유일한
    // 바이트 권한"이라는 계약의 소비자 쪽 끝이므로, 권한자 한 곳이 열려도
    // 검증되지 않은 바이트가 face가 되지는 않아야 한다.
    //
    // 이 세 검사는 ReadVerified가 방금 같은 값으로 한 것과 정확히 겹친다 —
    // 의도된 중복이다. 두 층 중 한 층만 무너뜨린 회귀는 어떤 테스트도 잡아낼
    // 수 없으므로(공개 API로는 store를 통과한 미검증 바이트를 만들 수 없다),
    // 나중에 두 번째 SHA 패스를 "낭비"로 보고 지우지 말 것.
    if ((*bytes)->size() != asset->artifactByteSize ||
        asset->artifactSha256 != asset->sourceSha256 ||
        molga::Sha256Bytes((*bytes)->data(), (*bytes)->size()) !=
            asset->artifactSha256) {
        ReportFontInvalid(sink, fontGuid,
                          "the font artifact bytes do not match the catalog "
                          "identity of this asset",
                          "reimport the font asset or reinstall the package "
                          "resources");
        return std::nullopt;
    }

    auto rasterFace = std::make_shared<molga::FontFace>();
    std::string faceError;
    if (!rasterFace->LoadFromBytes(*bytes, faceIndex, &faceError) ||
        rasterFace->FaceIndex() != faceIndex) {
        ReportFontInvalid(sink, fontGuid,
                          "the verified font artifact does not open the "
                          "recorded face: " + faceError,
                          "reimport the font asset");
        return std::nullopt;
    }

    auto resource = std::make_shared<FontFaceResource>();
    resource->asset = std::make_shared<const molga::FontAsset>(*asset);
    resource->bytes = *bytes;
    resource->rasterFace = std::move(rasterFace);
    resource->sourceSha256 = asset->sourceSha256;
    resource->artifactSha256 = asset->artifactSha256;
    resource->artifactLocator = asset->artifactLocator;
    resource->designMetrics = asset->designMetrics;
    resource->contentGeneration = contentGeneration;
    resource->faceIndex = faceIndex;

    FontFaceResourcePtr published = std::move(resource);
    Publish(fontGuid, published);
    return published;
}

} // namespace molga::text
