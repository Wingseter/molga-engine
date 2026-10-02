#pragma once

#include "Assets/FontAsset.h"
#include "Text/FontRepository.h"
#include "Text/TextDiagnostic.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace molga {
class AssetDatabase;
struct FontFamilyAsset;
} // namespace molga

namespace molga::text {

// 셰이핑이 요청하는 스타일. 저작된 face가 아니라 질의이므로 닫힌 저작 범위
// 밖의 값도 받는다(해석 시 정규화된다).
struct FontRequest {
    std::uint16_t weight = 400;
    std::uint16_t stretchPercent = 100;
    molga::FontSlant slant = molga::FontSlant::Upright;
};

// 후보 하나. `resource`가 불변 바이트와 정확한 face를 함께 붙들고 있으므로,
// 이 값을 들고 있는 동안에는 hot reload가 밑에서 바이트를 갈아 끼울 수 없다.
// `fontRevision`은 프로세스 안에서만 의미 있는 세대가 아니라 content address다.
struct ResolvedFace {
    std::string fontGuid;
    std::string fontRevision;
    std::uint32_t faceIndex = 0;
    std::uint32_t authoredFaceIndex = 0;
    FontFaceResourcePtr resource;
};

// 방문된 family 하나의 기록. 없는 GUID도 exists=false로 남긴다: 나중의 패키지
// 검증은 "진단이 있었는가"가 아니라 이 기록을 읽고 배포를 막는다.
struct ResolvedFamilyNode {
    std::string familyGuid;
    bool exists = false;
    std::uint64_t contentGeneration = 0;
    std::vector<std::string> authoredFallbackGuids;
};

struct ResolvedFamily {
    std::string requestedGuid;
    // 프로세스 안에서만 뜻이 있는 무효화 값이다. 절대 직렬화하거나 다른
    // 프로세스의 값과 비교하지 말 것: AssetDatabase::ContentGeneration을
    // 접어 만들고, 그 카운터는 Clear()/LoadCatalog로 0에서 다시 시작하므로
    // 같은 프로젝트라도 실행 사이에 값이 달라진다. 바로 옆 fontRevision이
    // 정반대 규칙(content address만, 세대 금지)을 따르는 것과 대비된다.
    //
    // 이름과 달리 그래프만의 성질도 아니다. 정렬된 후보 목록까지 접으므로
    // 같은 그래프라도 요청 weight/stretch/slant가 다르면 값이 달라진다.
    // 무효화에는 과할 뿐 안전하지만, "그래프가 그대로인가"의 판정에는 쓸 수
    // 없다.
    std::uint64_t fallbackGraphGeneration = 0;
    std::vector<ResolvedFamilyNode> depthFirstFamilyNodes;
    // 방문한 family들의 후보를 순서대로 이어 붙인 것이다. family 사이에서
    // 중복을 제거하지 않는다: 두 family가 같은 (fontGuid, faceIndex)를
    // 저작했다면 그 쌍이 두 번 나온다. 저작 순서를 그대로 보존하는 쪽이
    // 계약이므로, 중복 제거가 필요한 소비자가 자기 쪽에서 판단한다.
    std::vector<ResolvedFace> candidates;
};

// 한 번의 해석이 이 resolver 자신의 이름으로 낼 수 있는 FontFamilyInvalid의
// 상한. 깨진 참조마다 진단을 내면 저작 파일 하나가 프레임마다 로그를 채운다
// (Utf8Invalid가 이미 그 결함을 갖고 있다).
//
// 상한을 넘겨도 해석은 그대로 진행된다. 없는 family에 대해서는 상한이 정보를
// 잃지 않는다: 완전한 기록은 진단 스트림이 아니라 돌려주는 ResolvedFamily의
// exists=false이고, 패키지 검증은 그쪽을 읽는다.
//
// 나머지 두 실패는 그만큼 완전하지 않으니 그대로 적어 둔다. (1) 묶이지 못한
// face의 사유는 FontRepository가 남기는 별도의 FontInvalid에만 있다(아래 참조).
// (2) cycle은 node를 새로 만들지 않으므로 진단이 유일한 실패 신호다 — 다만 그
// 간선 자체는 어느 node의 authoredFallbackGuids에 남아 있으므로 기록에서
// 다시 유도할 수는 있다.
//
// 한 호출의 총 진단 수까지 이 값으로 묶이지는 않는다: 후보 하나를 열려다
// 실패하면 FontRepository가 자기 이름으로 FontInvalid를 하나 더 남긴다. 그
// 개수는 방문된 closure의 저작된 face 수, 즉 프로젝트 속성으로 묶이고 입력
// 텍스트 길이나 grapheme 수에는 비례하지 않는다. 같은 family를 프레임마다
// 해석하는 소비자의 상한은 여전히 그 호출부의 몫이다(LoggerTextDiagnosticSink가
// 같은 context key를 접는다).
inline constexpr std::size_t kMaxFamilyDiagnosticsPerResolve = 8;

// 저작된 family 그래프를 결정적인 후보 목록으로 바꾼다.
//
// 결정성이 이 클래스의 존재 이유다. 순회는 저작 순서 깊이 우선이고, family 안의
// 정렬은 값 기반 전순서이며, 어느 단계도 unordered_map 순회 순서나 호스트 폰트를
// 보지 않는다. 같은 카탈로그 상태에 대한 두 번의 호출은 같은 목록을 낸다.
//
// 단일 thread 전용이다. FontRepository::Load가 const지만 mutable 캐시에 게시하기
// 때문에, const 참조를 나눠 가진 두 thread가 동시에 해석하면 그 캐시를 경쟁적으로
// 고친다.
class FontFamilyResolver {
public:
    FontFamilyResolver(const molga::AssetDatabase&, FontRepository&);
    std::optional<ResolvedFamily> BuildCandidates(
        const std::string& familyGuid, const FontRequest&,
        TextDiagnosticSink&) const;
    std::optional<ResolvedFamily> BuildLegacySingleFace(
        const std::string& fontGuid, const FontRequest&,
        TextDiagnosticSink&) const;

private:
    // 상한을 넘긴 뒤에는 조용히 세기만 한다. 호출자가 진단 개수로 실패 수를
    // 세지 못하도록, 실패의 권한 있는 기록은 언제나 ResolvedFamily 쪽이다.
    struct DiagnosticBudget {
        std::size_t remaining = kMaxFamilyDiagnosticsPerResolve;
    };

    void ReportFamilyInvalid(TextDiagnosticSink&, DiagnosticBudget&,
                             const std::string& assetGuid, std::string message,
                             std::string remediation) const;
    // 방문한 family를 out에 기록하고 그 저작된 fallback 간선을 돌려준다.
    // 없는 family는 exists=false로 기록되고 간선은 비어 있다.
    // `unboundFaceFold`에는 후보가 되지 못한 저작 face의 내용 정체성이 쌓인다.
    std::vector<std::string> VisitFamily(const std::string& familyGuid,
                                         const FontRequest& canonical,
                                         TextDiagnosticSink&, DiagnosticBudget&,
                                         std::uint64_t& unboundFaceFold,
                                         ResolvedFamily& out) const;
    void AppendFamilyCandidates(const molga::FontFamilyAsset&,
                                const FontRequest& canonical,
                                TextDiagnosticSink&, DiagnosticBudget&,
                                std::uint64_t& unboundFaceFold,
                                ResolvedFamily& out) const;
    // 폰트 하나를 불변 자원에 묶는다. 저장소까지 내려간 실패의 사유는
    // FontRepository가 FontInvalid로 남기므로 여기서 되풀이하지 않는다.
    // legacy 단일 face 경로에는 애초에 family가 없어 그래프 쪽 진단도 없다.
    std::optional<ResolvedFace> BindCandidate(const std::string& fontGuid,
                                              std::uint32_t faceIndex,
                                              std::uint32_t authoredFaceIndex,
                                              TextDiagnosticSink&) const;

    const molga::AssetDatabase& database_;
    FontRepository& repository_;
};

} // namespace molga::text
