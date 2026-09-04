#pragma once

#include "Assets/FontAsset.h"
#include "Rendering/FontFace.h"
#include "Text/TextDiagnostic.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace molga {
class AssetDatabase;
}

namespace molga::text {

// 하나의 content generation에 묶인 불변 폰트 자원.
//
// 바이트, 그 바이트 위에 만들어진 정확한 face, 그리고 그 둘을 승인한 카탈로그
// 정체성을 함께 소유한다. ResolvedFace/ShapedGlyph/TextLayout/대기 중인 atlas
// 작업이 전부 이 shared_ptr를 붙들기 때문에, hot reload가 옛 레이아웃 밑에서
// GUID를 다시 열거나 새 바이트로 래스터화하는 일이 생길 수 없다.
struct FontFaceResource {
    std::shared_ptr<const molga::FontAsset> asset;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::shared_ptr<const molga::FontFace> rasterFace;
    std::string sourceSha256;
    std::string artifactSha256;
    FontArtifactLocator artifactLocator;
    FontDesignMetrics designMetrics;
    std::uint64_t contentGeneration = 0;
    std::uint32_t faceIndex = 0;
};
using FontFaceResourcePtr = std::shared_ptr<const FontFaceResource>;

// 검증된 산출물 바이트에서만 폰트 자원을 만든다. 저작 원본은 출처일 뿐이며
// 이 클래스는 그 경로를 알지도, 열지도 않는다.
//
// 단일 thread 전용이다. Load는 const지만 mutable 캐시에 게시하므로, const
// 참조를 나눠 가진 두 thread가 동시에 Load를 부르면 같은 unordered_map을
// 경쟁적으로 고친다. 배경 셰이핑이 승인되면 캐시에 잠금을 넣거나 thread마다
// 저장소를 따로 두어야 하고, 그 전까지 const는 thread 안전을 뜻하지 않는다.
class FontRepository {
public:
    explicit FontRepository(const molga::AssetDatabase&);

    std::optional<FontFaceResourcePtr> Load(
        const std::string& fontGuid, std::uint32_t faceIndex,
        TextDiagnosticSink&) const;

    // 앞으로의 조회만 바꾼다. 이미 돌려준 공유 자원은 건드리지 않으므로,
    // 살아 있는 레이아웃은 자기가 셰이핑한 바로 그 바이트를 계속 본다.
    void Invalidate(const std::string& fontGuid);

private:
    // guid당 faceIndex마다 최대 하나. 정체성이 달라진 항목은 보관되지 않고
    // 교체되므로, 반복되는 hot reload가 캐시를 무한히 키우지 않는다.
    using FaceCache = std::unordered_map<std::uint32_t, FontFaceResourcePtr>;

    FontFaceResourcePtr FindCached(const std::string& fontGuid,
                                   std::uint32_t faceIndex) const;
    void Publish(const std::string& fontGuid, FontFaceResourcePtr resource) const;

    const molga::AssetDatabase& database_;
    mutable std::unordered_map<std::string, FaceCache> cache_;
};

} // namespace molga::text
