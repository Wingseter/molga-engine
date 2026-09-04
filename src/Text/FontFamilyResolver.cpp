#include "Text/FontFamilyResolver.h"

#include "Assets/FontFamilyAsset.h"
#include "Core/AssetDatabase.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace molga::text {
namespace {

// ── Step 4: 요청 정규화 ─────────────────────────────────────────────────────
// 저작된 weight/stretch 범위는 설계가 닫아 두었고 importer가 그 밖의 값을
// 거절한다. 반면 요청은 저작물이 아니라 질의이므로 거절 대상이 아니다. 범위
// 안으로 접어 두면 목표값이 언제나 실제 저작 가능한 값이 되고, 거리 계산이
// 저작 정의역 안에서만 일어난다.
//
// 정직하게 적어 둔다: 거리 함수가 범위 밖에서 단조이므로 이 접기는 face들의
// 상대 순서를 바꾸지 못한다. 관찰 가능한 동작이 아니라 정의역 보장이다.
std::uint16_t ClampToAuthoredRange(std::uint32_t value, std::uint32_t minimum,
                                   std::uint32_t maximum) noexcept {
    if (value < minimum) return static_cast<std::uint16_t>(minimum);
    if (value > maximum) return static_cast<std::uint16_t>(maximum);
    return static_cast<std::uint16_t>(value);
}

FontRequest Canonicalize(const FontRequest& request) noexcept {
    FontRequest canonical;
    canonical.weight =
        ClampToAuthoredRange(request.weight, kFontWeightMin, kFontWeightMax);
    canonical.stretchPercent = ClampToAuthoredRange(
        request.stretchPercent, kFontStretchPercentMin, kFontStretchPercentMax);
    // slant는 이미 닫힌 enum이라 정규화할 값이 없다.
    canonical.slant = request.slant;
    return canonical;
}

// exact=0, Italic/Oblique 호환=1, 그 밖=2. Italic과 Oblique는 서로 대체할 수
// 있는 기울임이지만 Upright는 어느 쪽과도 대체되지 않는다.
std::uint32_t StylePenalty(molga::FontSlant face,
                           molga::FontSlant target) noexcept {
    if (face == target) return 0U;
    const bool faceSloped = face != molga::FontSlant::Upright;
    const bool targetSloped = target != molga::FontSlant::Upright;
    return (faceSloped && targetSloped) ? 1U : 2U;
}

// uint16끼리 빼면 목표가 더 클 때 감싸 돌아간다. 부호 있는 넓은 타입에서만
// 계산한다.
std::int64_t Distance(std::uint16_t value, std::uint16_t target) noexcept {
    const std::int64_t difference =
        static_cast<std::int64_t>(value) - static_cast<std::int64_t>(target);
    return difference < 0 ? -difference : difference;
}

// 설계가 고정한 정렬 키. 순서까지 계약이다.
//
// fontGuid를 string_view로 든다. 이 키는 비교자 안에서만 만들어져 그 비교가
// 끝나면 사라지므로 비교 대상 원소보다 오래 살지 않고, string_view의 비교도
// 같은 사전식 순서다. std::string으로 들면 GUID 32자가 SSO를 넘겨 비교 한
// 번마다 힙 할당이 두 번 일어나는데, family 해석은 설계상 ICU/HarfBuzz보다
// 앞선 warm lookup 경로다.
using FaceSortKey = std::tuple<std::uint32_t, std::int64_t, std::int64_t,
                               std::uint32_t, std::string_view, std::uint32_t>;

FaceSortKey SortKey(const molga::FontFamilyFaceEntry& face,
                    const FontRequest& canonical) {
    return FaceSortKey{StylePenalty(face.slant, canonical.slant),
                       Distance(face.stretchPercent, canonical.stretchPercent),
                       Distance(face.weight, canonical.weight),
                       face.authoredFaceIndex, face.fontGuid, face.faceIndex};
}

// ── Step 7: 전이적 그래프 세대 ──────────────────────────────────────────────
// FNV-1a 64비트. 값 하나가 아니라 방문한 family 세대와 후손 face의 내용
// 정체성을 전부 접기 때문에, 그래프 어디를 고쳐도 결과가 달라진다.
constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void FoldByte(std::uint64_t& state, std::uint8_t byte) noexcept {
    state ^= byte;
    state *= kFnvPrime;
}

// 항목 경계를 반드시 함께 접는다. 경계가 없으면 ("ab","c")와 ("a","bc")가 같은
// 값이 된다. 지금 접히는 문자열은 fontRevision을 빼면 전부 길이가 고정된
// 32자 GUID라 그 충돌을 실제로 만들 수는 없다 — 방어적으로 둔다.
void FoldString(std::uint64_t& state, const std::string& text) noexcept {
    for (const char character : text) {
        FoldByte(state, static_cast<std::uint8_t>(character));
    }
    FoldByte(state, 0U);
}

void FoldUnsigned(std::uint64_t& state, std::uint64_t value) noexcept {
    for (int shift = 56; shift >= 0; shift -= 8) {
        FoldByte(state, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint64_t FoldGraphGeneration(const ResolvedFamily& resolved,
                                  std::uint64_t unboundFaceFold) noexcept {
    std::uint64_t state = kFnvOffsetBasis;
    // 후보가 되지 못한 저작 face의 세대. 후보 목록에는 흔적이 남지 않으므로
    // 여기서 따로 접지 않으면, 고장 난 채로 저작 설정만 바뀐 폰트의 편집이
    // 그래프 세대를 전혀 움직이지 못한다.
    FoldUnsigned(state, unboundFaceFold);
    FoldString(state, resolved.requestedGuid);
    for (const ResolvedFamilyNode& node : resolved.depthFirstFamilyNodes) {
        FoldString(state, node.familyGuid);
        FoldByte(state, node.exists ? 1U : 0U);
        FoldUnsigned(state, node.contentGeneration);
        // 간선까지 접는다. face를 하나도 내지 못하는 family의 fallback 목록만
        // 고친 편집도 다음 해석의 후보 순서를 바꾸기 때문이다.
        for (const std::string& edge : node.authoredFallbackGuids) {
            FoldString(state, edge);
        }
    }
    for (const ResolvedFace& face : resolved.candidates) {
        FoldString(state, face.fontGuid);
        // contentGeneration만으로도 재발행은 전부 잡힌다(바이트가 바뀌면
        // 세대도 오른다). fontRevision은 그 위에 얹는 방어적 중복이다.
        FoldString(state, face.fontRevision);
        FoldUnsigned(state, face.faceIndex);
        FoldUnsigned(state, face.authoredFaceIndex);
        FoldUnsigned(state,
                     face.resource ? face.resource->contentGeneration : 0U);
    }
    return state;
}

// 묶인 바이트 권한이 없는 것은 애셋 내용의 문제가 아니라 프로세스 상태의
// 문제다. 호출당 많아야 하나이므로 예산을 쓰지 않는다.
//
// componentType은 호출한 경로가 정한다. legacy 단일 폰트 실패까지
// "FontFamilyAsset"이라고 적으면 존재하지도 않는 family 문서를 고치라고
// 지목하게 된다.
void ReportUnboundArtifactStore(TextDiagnosticSink& sink,
                                const std::string& assetGuid,
                                const char* componentType) {
    TextDiagnostic diagnostic;
    diagnostic.code = TextDiagnosticCode::DependencyInvalid;
    diagnostic.severity = TextSeverity::Blocker;
    diagnostic.subsystem = "font-family-resolver";
    diagnostic.message = "no font artifact store is bound for this asset "
                         "database, so no candidate can be content-bound";
    diagnostic.remediation = "bind the project or sealed-package font artifact "
                             "store before resolving font families";
    diagnostic.assetGuid = assetGuid;
    diagnostic.componentType = componentType;
    sink.Report(std::move(diagnostic));
}

// 저장소가 성공을 돌려줬는데 소비자 쪽 재확인이 거절한 경우에만 쓴다. 저장소는
// 아무 사유도 남기지 않았으므로, 여기서 조용히 후보만 버리면 "성공했는데 진단이
// 하나도 없이 tofu가 나오는" 상태가 된다.
void ReportRejectedFaceIdentity(TextDiagnosticSink& sink,
                                const std::string& fontGuid,
                                std::uint32_t faceIndex) {
    TextDiagnostic diagnostic;
    diagnostic.code = TextDiagnosticCode::FontInvalid;
    diagnostic.severity = TextSeverity::Error;
    diagnostic.subsystem = "font-family-resolver";
    diagnostic.message = "the loaded font face does not match the identity it "
                         "was requested with: " + fontGuid + " face " +
                         std::to_string(faceIndex);
    diagnostic.remediation = "reimport the font asset and rescan the project";
    diagnostic.assetGuid = fontGuid;
    diagnostic.componentType = "FontAsset";
    sink.Report(std::move(diagnostic));
}

} // namespace

FontFamilyResolver::FontFamilyResolver(const molga::AssetDatabase& database,
                                       FontRepository& repository)
    : database_(database), repository_(repository) {}

void FontFamilyResolver::ReportFamilyInvalid(TextDiagnosticSink& sink,
                                             DiagnosticBudget& budget,
                                             const std::string& assetGuid,
                                             std::string message,
                                             std::string remediation) const {
    if (budget.remaining == 0U) return;
    --budget.remaining;
    TextDiagnostic diagnostic;
    diagnostic.code = TextDiagnosticCode::FontFamilyInvalid;
    diagnostic.severity = TextSeverity::Error;
    diagnostic.subsystem = "font-family-resolver";
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    diagnostic.assetGuid = assetGuid;
    diagnostic.componentType = "FontFamilyAsset";
    sink.Report(std::move(diagnostic));
}

std::optional<ResolvedFace> FontFamilyResolver::BindCandidate(
    const std::string& fontGuid, std::uint32_t faceIndex,
    std::uint32_t authoredFaceIndex, TextDiagnosticSink& sink) const {
    // Step 6: 바이트는 오직 FontRepository를 거쳐서만 들어온다. 저작 원본
    // 경로는 이 클래스가 알지도, 열지도 않는다.
    const std::optional<FontFaceResourcePtr> loaded =
        repository_.Load(fontGuid, faceIndex, sink);
    if (!loaded || !*loaded) return std::nullopt;

    const FontFaceResource& resource = **loaded;
    // 저장소가 방금 확인한 것과 겹치는 소비자 쪽 확인이다. 의도된 중복이다:
    // 한 층만 무너뜨린 회귀는 공개 API로 만들어 낼 수 없으므로 어떤 테스트도
    // 잡지 못한다. 나중에 "낭비"로 보고 지우지 말 것.
    if (resource.asset == nullptr || resource.asset->guid != fontGuid ||
        resource.faceIndex != faceIndex || resource.artifactSha256.empty() ||
        resource.artifactSha256 != resource.sourceSha256 ||
        resource.artifactLocator.relativePath.empty() ||
        resource.asset->artifactByteSize == 0U) {
        ReportRejectedFaceIdentity(sink, fontGuid, faceIndex);
        return std::nullopt;
    }

    ResolvedFace face;
    face.fontGuid = fontGuid;
    // content address + 정확한 face. 프로세스 지역 세대는 들어가지 않는다:
    // 재시작마다 0에서 다시 시작하는 값을 리비전에 넣으면 같은 바이트가 실행
    // 때마다 다른 정체성을 갖게 되고, 봉인된 패키지의 캐시 키가 깨진다.
    face.fontRevision =
        resource.artifactSha256 + ":" + std::to_string(faceIndex);
    face.faceIndex = faceIndex;
    face.authoredFaceIndex = authoredFaceIndex;
    // 산출물 상대 경로/크기와 같은 값의 source SHA는 이 자원이 계속 붙들고
    // 있으므로, 후보를 들고 있는 한 그 권한 있는 사실도 함께 살아 있다.
    face.resource = *loaded;
    return face;
}

void FontFamilyResolver::AppendFamilyCandidates(
    const molga::FontFamilyAsset& family, const FontRequest& canonical,
    TextDiagnosticSink& sink, DiagnosticBudget& budget,
    std::uint64_t& unboundFaceFold, ResolvedFamily& out) const {
    // 값 비교자로 안정 정렬한다. 키가 저작 위치까지 포함하므로 family 안에서
    // 전순서이고, 정렬 알고리즘의 동점 처리에 순서가 기대지 않는다.
    std::vector<molga::FontFamilyFaceEntry> ordered = family.faces;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&canonical](const molga::FontFamilyFaceEntry& left,
                                  const molga::FontFamilyFaceEntry& right) {
                         return SortKey(left, canonical) <
                                SortKey(right, canonical);
                     });

    for (const molga::FontFamilyFaceEntry& face : ordered) {
        std::optional<ResolvedFace> candidate =
            BindCandidate(face.fontGuid, face.faceIndex, face.authoredFaceIndex,
                          sink);
        if (!candidate) {
            // Step 7: 후보가 되지 못했어도 이 face는 여전히 후손이다. 세대를
            // 접어 두어야 "고장 난 채로 저작 설정만 바뀐" 편집이 무효화된다.
            FoldString(unboundFaceFold, face.fontGuid);
            FoldUnsigned(unboundFaceFold, face.faceIndex);
            FoldUnsigned(unboundFaceFold,
                         database_.ContentGeneration(face.fontGuid));
            // 저장소가 이미 사유가 있는 FontInvalid를 보고했다. 여기서 더하는
            // 사실은 폰트가 아니라 그래프 쪽이다: 이 family의 저작된 참조가
            // 후보를 내지 못했으므로 fallback 순서가 저작된 대로 서지 않는다.
            // assetGuid는 깨진 폰트를 지목한다 — 로거의 rate limit이
            // code+assetGuid로 접히므로, family를 지목하면 한 family의 서로
            // 다른 깨진 참조가 한 줄로 합쳐진다.
            ReportFamilyInvalid(
                sink, budget, face.fontGuid,
                "the font family " + family.guid +
                    " authors a face this catalog cannot resolve: " +
                    face.fontGuid + " face " + std::to_string(face.faceIndex),
                "import the referenced font at the authored face index or "
                "remove the face from the family");
            continue;
        }
        out.candidates.push_back(std::move(*candidate));
    }
}

std::vector<std::string> FontFamilyResolver::VisitFamily(
    const std::string& familyGuid, const FontRequest& canonical,
    TextDiagnosticSink& sink, DiagnosticBudget& budget,
    std::uint64_t& unboundFaceFold, ResolvedFamily& out) const {
    ResolvedFamilyNode node;
    node.familyGuid = familyGuid;
    node.contentGeneration = database_.ContentGeneration(familyGuid);

    const molga::AssetRecord* record = database_.Find(familyGuid);
    std::string error;
    std::optional<molga::FontFamilyAsset> family;
    if (record != nullptr) {
        family = molga::FontFamilyAsset::FromRecord(*record, error);
    }
    if (!family) {
        // Step 5: 없는/못 쓰는 참조도 node로 남긴다. 진단은 사람이 읽는
        // 채널이고, 패키지 검증이 읽는 기계 판독 기록은 이 exists=false다.
        node.exists = false;
        ReportFamilyInvalid(
            sink, budget, familyGuid,
            record == nullptr
                ? "the referenced font family is not in the loaded catalog: " +
                      familyGuid
                : "the referenced font family record is not a usable authored "
                  "family: " + error,
            record == nullptr
                ? "author the missing font family or remove the reference"
                : "reimport the font family asset");
        out.depthFirstFamilyNodes.push_back(std::move(node));
        return {};
    }

    node.exists = true;
    // 저작된 순서 그대로다. 여기서 정렬하거나 중복을 제거하면 fallback 순서
    // 계약이 리뷰 없이 달라진다.
    node.authoredFallbackGuids = family->fallbackFamilyGuids;
    std::vector<std::string> fallbacks = node.authoredFallbackGuids;
    out.depthFirstFamilyNodes.push_back(std::move(node));
    AppendFamilyCandidates(*family, canonical, sink, budget, unboundFaceFold,
                           out);
    return fallbacks;
}

std::optional<ResolvedFamily> FontFamilyResolver::BuildCandidates(
    const std::string& familyGuid, const FontRequest& request,
    TextDiagnosticSink& sink) const {
    if (database_.FontArtifacts() == nullptr) {
        // Step 5a의 유일한 nullopt 경로. 내용이 없는 것이 아니라 바이트 권한이
        // 묶여 있지 않은 것이므로, tofu를 돌려주면 고쳐야 할 호스트 설정 결함이
        // "폰트가 없다"로 위장된다.
        ReportUnboundArtifactStore(sink, familyGuid, "FontFamilyAsset");
        return std::nullopt;
    }

    const FontRequest canonical = Canonicalize(request);
    ResolvedFamily resolved;
    resolved.requestedGuid = familyGuid;

    DiagnosticBudget budget;
    std::uint64_t unboundFaceFold = kFnvOffsetBasis;
    // 재귀 대신 명시적 스택을 쓴다. 저작된 그래프의 깊이는 프로젝트가 정하는
    // 값이고, 깊게 사슬을 이룬 family가 에디터를 스택 오버플로로 죽이면 그것은
    // 진단이 아니라 크래시다.
    struct Frame {
        std::string familyGuid;
        std::vector<std::string> fallbacks;
        std::size_t next = 0;
    };
    std::vector<Frame> stack;
    // Step 5의 "하나의 visited 집합". 종료·기록·후보 배치를 전부 이 집합이
    // 맡는다.
    std::unordered_set<std::string> visited;
    // cycle 판정은 visited가 아니라 현재 DFS 경로가 정한다. visited 하나로
    // 판정하면 두 family가 같은 CJK family를 함께 가리키는 정상적인 마름모
    // 저작이 cycle로 고발되고, 그 진단은 아무도 고칠 수 없다. 경로는 이미
    // stack이 들고 있으므로 별도의 집합을 두지 않고 그대로 훑는다 — 저작된
    // 그래프의 깊이만큼만 도는 비용이다.
    const auto onCurrentPath = [&stack](const std::string& guid) {
        for (const Frame& frame : stack) {
            if (frame.familyGuid == guid) return true;
        }
        return false;
    };

    visited.insert(familyGuid);
    stack.push_back(Frame{familyGuid,
                          VisitFamily(familyGuid, canonical, sink, budget,
                                      unboundFaceFold, resolved),
                          0U});

    while (!stack.empty()) {
        Frame& top = stack.back();
        if (top.next == top.fallbacks.size()) {
            stack.pop_back();
            continue;
        }
        const std::string child = top.fallbacks[top.next];
        ++top.next;

        if (onCurrentPath(child)) {
            // 고칠 수 있는 것은 되돌아오는 간선을 저작한 family 쪽이므로 그
            // GUID까지 함께 적는다.
            ReportFamilyInvalid(
                sink, budget, child,
                "the authored font family fallback graph contains a cycle: " +
                    top.familyGuid + " falls back to its ancestor " + child,
                "remove the fallback edge that returns to an ancestor family");
            continue;
        }
        // 첫 방문만 기록하고 순회한다. 이것이 편집 중인 프로젝트에서도 미리보기
        // 해석이 반드시 끝난다는 보장이다.
        if (!visited.insert(child).second) continue;

        std::vector<std::string> fallbacks = VisitFamily(
            child, canonical, sink, budget, unboundFaceFold, resolved);
        // top은 여기서 무효가 된다. 이 push 뒤로는 절대 다시 읽지 않는다.
        stack.push_back(Frame{child, std::move(fallbacks), 0U});
    }

    resolved.fallbackGraphGeneration =
        FoldGraphGeneration(resolved, unboundFaceFold);
    return resolved;
}

std::optional<ResolvedFamily> FontFamilyResolver::BuildLegacySingleFace(
    const std::string& fontGuid, const FontRequest&,
    TextDiagnosticSink& sink) const {
    // 요청은 받되 쓰이지 않는다. 이름이 지목된 단일 face에는 순위를 매길
    // 대안이 없기 때문이다. 인자는 이 경로를 BuildCandidates와 서명 호환으로
    // 두어, 호출부가 마이그레이션할 때 한 줄만 바꾸면 되게 한다.
    if (database_.FontArtifacts() == nullptr) {
        ReportUnboundArtifactStore(sink, fontGuid, "FontAsset");
        return std::nullopt;
    }

    ResolvedFamily resolved;
    resolved.requestedGuid = fontGuid;
    // family를 거치지 않았다는 사실 자체가 기록이다: depthFirstFamilyNodes는
    // 비어 있고, 그래서 이 결과는 fallback을 갖지 않는다.

    // 어느 face를 열지는 카탈로그가 정한다. 저작된 face index를 짐작하지
    // 않으므로, 잘못된 face가 그럴듯하게 렌더링되는 일이 없다.
    std::uint32_t faceIndex = 0;
    if (const molga::AssetRecord* record = database_.Find(fontGuid)) {
        std::string error;
        if (const std::optional<molga::FontAsset> asset =
                molga::FontAsset::FromRecord(*record, error)) {
            faceIndex = asset->faceIndex;
        }
    }

    // 실패해도 후보 없는 ResolvedFamily를 돌려준다. 레이아웃은 그 위에서
    // 절차적 tofu를 만들 수 있고, 패키지 검증은 빈 후보 목록을 읽고 막는다.
    if (std::optional<ResolvedFace> candidate =
            BindCandidate(fontGuid, faceIndex, 0U, sink)) {
        resolved.candidates.push_back(std::move(*candidate));
    }
    // 이 경로에는 저작된 family가 없으므로 묶이지 못한 후손 face도 없다.
    resolved.fallbackGraphGeneration =
        FoldGraphGeneration(resolved, kFnvOffsetBasis);
    return resolved;
}

} // namespace molga::text
