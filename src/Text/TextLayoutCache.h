#pragma once

#include "Assets/FontAsset.h"
#include "Common/Fixed26_6.h"
#include "Text/TextLayoutTypes.h"
#include "Text/TextShapingService.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 이 헤더는 HarfBuzz로부터 자유롭다. cluster 선택은 프로젝트 enum
// TextClusterPolicy로만 표현되고 hb_* 타입도 HB_BUFFER_* 매크로도 등장하지
// 않는다. HarfBuzz 상수로의 매핑은 TextShapingService.cpp 한 곳에만 있으며,
// tests/test_text_cache_header.cpp가 별도 실행 파일에서 그 사실을 못 박는다.
// 여기 hb.h가 새면 캐시 키를 읽는 모든 소비자(에디터 UI, 패키지 검증)가
// HarfBuzz include 경로를 요구하게 된다.

namespace molga::text {

// ── Step 5: family closure request identity ─────────────────────────────────
// 해석된 family 폐포를 "내용"으로만 적은 것. 프로세스 지역 값은 여기 없다:
// ResolvedFamily::fallbackGraphGeneration은 이름과 달리 순수한 그래프 성질이
// 아니라 요청 weight/slant까지 접은 프로세스 지역 카운터이고(Task 5.1의 계약
// 1번), FontAsset::contentRevision은 디렉터리 순회 순번이라 캐시 정체성에
// 들어갈 수 없다. 그래서 request index는 그 둘 대신 아래 내용 필드를 본다.
struct FamilyNodeRequestIdentity {
    std::string familyGuid;
    // 없는 family도 기록으로 남는다. 없는 GUID가 나중에 생기면 그 자체가
    // 다른 폐포이므로 캐시는 반드시 어긋나야 한다.
    bool exists = false;
    std::uint64_t contentGeneration = 0;
    // 저작 순서 그대로다. 정렬하면 Task 4.2/5.1이 두 번 겪은 결함 — 저작
    // 순서가 우연히 사전순인 fixture 때문에 아무 단언도 움직이지 않는 상태 —
    // 가 캐시 계층에서 되풀이된다.
    std::vector<std::string> authoredFallbackGuids;
    bool operator==(const FamilyNodeRequestIdentity&) const;
};

struct FaceRequestIdentity {
    std::string fontGuid, fontRevision, sourceSha256, artifactSha256;
    FontArtifactLocator artifactLocator;
    std::uint64_t artifactByteSize = 0;
    std::uint64_t contentGeneration = 0;
    std::uint32_t faceIndex = 0, authoredFaceIndex = 0;
    bool operator==(const FaceRequestIdentity&) const;
};

struct ResolvedFamilyRequestIdentity {
    std::string requestedGuid;
    std::vector<FamilyNodeRequestIdentity> depthFirstFamilyNodes;
    // family 사이에서 중복 제거되지 않는다(Task 5.1의 계약 2번). 같은
    // (fontGuid, faceIndex)가 두 번 나오는 것 자체가 폐포의 성질이므로
    // 여기서도 접지 않는다.
    std::vector<FaceRequestIdentity> orderedCandidates;
    bool operator==(const ResolvedFamilyRequestIdentity&) const;
};

// ── Step 5a: the early request-index key ────────────────────────────────────
// 내용 기반 family 해석 뒤, ICU/HarfBuzz 앞에서 만들어지는 warm 조회 키.
// 분석 결과도 최종 줄도 담지 않는다 — 담으면 그것을 만드는 데 이미 ICU와
// HarfBuzz가 돌아야 하므로 warm 경로의 존재 이유가 사라진다.
//
// diagnosticContext는 의도적으로 빠져 있다. 같은 불변 레이아웃을 서로 다른
// 컴포넌트가 재사용하면서 각자의 문맥으로 사실을 다시 진단으로 낼 수 있어야
// 하기 때문이다(TextLayoutTypes.h의 MakeContextualDiagnostic).
struct TextLayoutRequestIndexKey {
    std::uint32_t decodePolicyVersion = 1;
    std::uint32_t familyResolutionPolicyVersion = 1;
    std::uint32_t fallbackPolicyVersion = 1;
    // 원본 바이트와 그 hash를 함께 담는다. hash는 bucket 선택에만 쓰이고
    // 판정은 언제나 길이와 바이트 비교다. 값은 반드시 아래 CacheBytesHash로
    // 채운다 — 자세한 이유는 그 선언 위에 있다.
    std::string originalUtf8;
    std::uint64_t originalBytesHash = 0;
    ParagraphStyle style;
    LayoutConstraints constraints;
    std::uint64_t visualRevision = 0;
    ResolvedFamilyRequestIdentity familyClosure;
    std::string harfbuzzRevision;
    std::string icuRevision;
    std::string icuDataSha256;
    // 요청된 규칙 정책의 정체성이지, ICU가 실제로 고른 iterator의 규칙
    // 정체성이 아니다. 후자는 분석이 끝나야 알 수 있고 이 키는 분석 앞에
    // 있다. 실제 규칙 정체성은 TextShapeCacheKey 쪽 필드가 담는다.
    std::string requestedGraphemeRulePolicyIdentity;
    std::string requestedLineBreakRulePolicyIdentity;
    std::string dependencyContractSha256;
    bool operator==(const TextLayoutRequestIndexKey&) const;
};

// ── Step 6: selected-face and shape-input mapping identity ──────────────────
struct SelectedFaceShapeIdentity {
    std::string fontGuid, fontRevision, sourceSha256, artifactSha256;
    FontArtifactLocator artifactLocator;
    std::uint64_t artifactByteSize = 0;
    std::uint64_t contentGeneration = 0;
    std::uint32_t faceIndex = 0;
};

// 셰이핑에 실제로 넘어간 바이트 구간과, 그것이 원본의 어디에서 왔는가.
// synthetic=true는 원본에 없는 바이트(줄임표 등)라는 뜻이고, 그때
// originalSourceBytes는 그 합성이 대신하는 자리다. 이 매핑이 키에서 빠지면
// 같은 셰이핑 입력을 낸 서로 다른 줄임 지점이 서로를 가린다.
struct ShapeInputSourceSpan {
    SourceByteRange shapeInputBytes;
    SourceByteRange originalSourceBytes;
    GraphemeRange originalGraphemes;
    bool synthetic = false;
    bool operator==(const ShapeInputSourceSpan&) const;
};

// ── Step 6a: the complete structured shape key ──────────────────────────────
struct TextShapeCacheKey {
    std::uint32_t decodePolicyVersion = 1;
    // 두 hash 필드도 request index와 같은 계약이다: 아래 CacheBytesHash로만
    // 채운다.
    std::string originalUtf8;
    std::uint64_t originalBytesHash = 0;
    std::string shapeInputUtf8;
    std::uint64_t shapeInputBytesHash = 0;
    std::vector<ShapeInputSourceSpan> shapeInputMapping;
    SourceByteRange paragraphBytes, runBytes;
    std::uint64_t fallbackGraphGeneration = 0;
    std::vector<SelectedFaceShapeIdentity> selectedFaces;
    Fixed26_6 fontSize = Fixed26_6::FromRaw(0);
    std::uint64_t variationKey = 0;
    // AnalysisItem이 낸 정확한 문자별 resolved level이다. 방향 parity도
    // 문단 level도 아니므로 direction과 함께 담아야 한다: level 0과 2는 둘 다
    // LTR이지만 서로 다른 run이다.
    std::uint8_t embeddingLevel = 0;
    std::int32_t direction = 0, scriptCode = 0;
    std::string language, componentLocale;
    // 아래 다섯은 불변 UnicodeAnalysis::Identity()를 그대로 복사한 것이다.
    // 요청 문자열이 아니라 ICU가 실제로 고른 locale과 실제 규칙 바이트의
    // 정체성이라, 같은 요청이 다른 ICU 데이터에서 다른 경계를 내면 여기서
    // 어긋난다.
    std::string resolvedGraphemeLocale, resolvedLineBreakLocale;
    std::string graphemeRuleIdentity, lineBreakRuleIdentity;
    std::uint64_t analysisGeneration = 0;
    ShapeBoundaryFlags boundaries;
    std::uint32_t harfbuzzBufferFlags = 0;
    TextClusterPolicy clusterPolicy =
        TextClusterPolicy::MonotoneCharacters;
    std::vector<ShapeFeature> orderedFeatures;
    std::string harfbuzzRevision, icuRevision, icuDataSha256;
    std::string dependencyContractSha256;
    bool operator==(const TextShapeCacheKey&) const;
};

// ── Step 7: the complete final paragraph key ────────────────────────────────
// cold 레이아웃이 끝난 뒤에 만들어지는 감사/저장 정체성이다. warm 조회는
// 언제나 TextLayoutRequestIndexKey 쪽을 쓴다 — 이 키를 만들려면 최종 줄이
// 이미 셰이핑되어 있어야 하므로 조회 키로는 쓸 수 없다.
struct TextParagraphCacheKey {
    std::vector<TextShapeCacheKey> finalLineShapeKeys;
    LayoutConstraints constraints;
    TextWrapMode wrap = TextWrapMode::NoWrap;
    TextOverflowMode overflow = TextOverflowMode::Overflow;
    // 위 두 정책과 달리 enum이 아니라 문자열이다. 값은 반드시 한 곳에서 정한
    // 어휘에서만 와야 한다: 같은 정책을 두 호출부가 다르게 적으면 쓸데없는
    // miss로 끝나지만(안전), 서로 다른 두 정책이 같은 철자를 쓰면 서로 다른
    // 배치가 한 항목을 공유한다(안전하지 않음). 어휘 자체는 줄바꿈을 실제로
    // 구현하는 Task 7.2가 정한다.
    std::string overlongTokenPolicy;
    std::string ellipsisUtf8;
    ShapeStyle ellipsisStyle;
    std::uint32_t maxLines = 0;
    Fixed26_6 lineSpacing = Fixed26_6::FromRaw(64);
    TextHorizontalAlignment horizontal = TextHorizontalAlignment::Left;
    TextVerticalAlignment vertical = TextVerticalAlignment::Top;
    std::uint64_t visualRevision = 0;
    bool operator==(const TextParagraphCacheKey&) const;
};

// ── Byte-hash provenance ────────────────────────────────────────────────────
// originalBytesHash / shapeInputBytesHash를 채우는 유일한 방법이다. 두 필드는
// bucket 선택에만 쓰이는 것처럼 보이지만 operator==의 항이기도 하다: 같은
// 바이트에 대해 두 호출부가 서로 다른 값을 넣으면 warm 조회가 영원히 빗나가고,
// 다시 분석하고 다시 셰이핑하면서도 결과는 맞으므로 어떤 시험도 실패하지
// 않는다. 반대로 통째로 잊고 0으로 두면 모든 키가 bucket 0에 쌓여 조회가 선형
// 탐색이 된다 — 이 역시 조용하다.
//
// std::hash<std::string>은 이 자리에 쓸 수 없다. 표준 라이브러리 버전마다 값이
// 달라 같은 프로젝트가 다른 툴체인에서 다른 키를 낸다.
std::uint64_t CacheBytesHash(std::string_view bytes) noexcept;

// ── Hash functors ───────────────────────────────────────────────────────────
// bucket 선택 전용이다. 어느 조회도 hash 일치를 적중으로 받아들이지 않고,
// bucket 안에서 전체 필드를 비교한다. 그래서 문자열 두 쌍(originalUtf8,
// shapeInputUtf8)은 해시에 바이트가 아니라 함께 실려 온 hash 필드로만
// 들어간다 — 그래야 "같은 hash, 다른 바이트"라는 시험이 실제로 같은 bucket을
// 때린다.
struct TextLayoutRequestIndexKeyHash {
    std::size_t operator()(const TextLayoutRequestIndexKey&) const noexcept;
};
struct TextShapeCacheKeyHash {
    std::size_t operator()(const TextShapeCacheKey&) const noexcept;
};
struct TextParagraphCacheKeyHash {
    std::size_t operator()(const TextParagraphCacheKey&) const noexcept;
};

// ── Step 9: requested rule-policy identity ──────────────────────────────────
// 저작된 locale 바이트는 ParagraphStyle::analysis에 이미 그대로 있으므로 여기서
// 정규화하지 않는다. 대신 {정책 종류, 정책 버전, ICU revision, ICU 데이터
// SHA}의 SHA-256을 낸다. ICU가 실제로 고른 iterator의 규칙 정체성이라고
// 주장하지 않는다 — 그 값은 분석이 끝나야 존재하고, request index는 분석
// 앞에 있다. 두 정책 종류가 tuple에 들어가는 이유는 하나뿐이다: 빠지면 두
// 값이 같아져 grapheme 정책과 line-break 정책이 서로를 가린다.
inline constexpr std::uint32_t kRequestedGraphemeRulePolicyVersion = 1;
inline constexpr std::uint32_t kRequestedLineBreakRulePolicyVersion = 1;
std::string RequestedGraphemeRulePolicyIdentity(std::string_view icuRevision,
                                                std::string_view icuDataSha256);
std::string RequestedLineBreakRulePolicyIdentity(std::string_view icuRevision,
                                                 std::string_view icuDataSha256);

// ── Step 8: multi-stage cache values ────────────────────────────────────────
struct CachedShapeResult {
    std::vector<ShapedRun> runs;
    // 문맥 없는 사실만 담는다. 문맥은 warm hit마다 호출자의 것으로 다시
    // 붙는다(TextLayoutTypes.h의 MakeContextualDiagnostic).
    std::vector<TextValidationFact> validationFacts;
};

struct CachedTextLayout {
    TextParagraphCacheKey finalKey;
    std::shared_ptr<const TextLayout> layout;
};

struct TextLayoutCacheTelemetry {
    std::uint64_t shapeHits = 0, shapeMisses = 0, shapeStores = 0;
    std::uint64_t requestHits = 0, requestMisses = 0;
    std::uint64_t finalHits = 0, finalMisses = 0, finalStores = 0;
    std::uint64_t evictions = 0;
};

// 각 값은 단단한 상한이다. 0은 그 자원의 용량이 0이라는 뜻이지 무제한이
// 아니다 — atlas 예산과 같은 규칙이고, 무제한 모드는 존재하지 않는다.
struct TextLayoutCacheLimits {
    std::size_t maxShapeEntries = 0;
    std::size_t maxParagraphEntries = 0;
    std::uint64_t maxOwnedBytes = 0;
    static TextLayoutCacheLimits Production() noexcept;
};

// ── Step 8a: the cache service ──────────────────────────────────────────────
// 단일 thread 전용이다. FontRepository/FontFamilyResolver/TextShapingService와
// 같은 계약이고 같은 이유다: 텍스트 처리는 main-thread deterministic CPU
// phase이며, 아래 LRU 목록과 bucket 색인에는 잠금이 없다.
//
// 축출은 캐시의 지분 하나만 놓는다. 밖에서 같은 shared_ptr를 들고 있는
// 소비자는 자기가 받은 그 불변 값을 그대로 계속 본다.
class TextLayoutCache {
public:
    explicit TextLayoutCache(TextLayoutCacheLimits);
    std::shared_ptr<const CachedShapeResult> FindShape(
        const TextShapeCacheKey&);
    void StoreShape(const TextShapeCacheKey&,
                    std::shared_ptr<const CachedShapeResult>);
    std::optional<CachedTextLayout> FindByRequest(
        const TextLayoutRequestIndexKey&);
    std::shared_ptr<const TextLayout> FindByFinal(
        const TextParagraphCacheKey&);
    void Store(const TextLayoutRequestIndexKey&,
               const TextParagraphCacheKey&,
               std::shared_ptr<const TextLayout>);
    const TextLayoutCacheTelemetry& Telemetry() const noexcept;

private:
    // 아래 색인은 목록 반복자를 담으므로 복사는 새 목록을 가리키지 못하는
    // 반복자를 만든다. private에 지운 선언을 두어 컴파일에서 막는다.
    TextLayoutCache(const TextLayoutCache&) = delete;
    TextLayoutCache& operator=(const TextLayoutCache&) = delete;

    struct ShapeEntry {
        TextShapeCacheKey key;
        std::shared_ptr<const CachedShapeResult> value;
        std::size_t bucket = 0;
        std::uint64_t ownedBytes = 0;
        std::uint64_t lastUsed = 0;
    };
    // request key와 final key가 하나의 항목을 함께 가리킨다. 둘을 따로 두면
    // 한쪽만 축출됐을 때 request 색인이 사라진 레이아웃을 가리키거나, final
    // 색인이 아무도 찾을 수 없는 레이아웃을 붙들게 된다.
    struct ParagraphEntry {
        TextLayoutRequestIndexKey requestKey;
        TextParagraphCacheKey finalKey;
        std::shared_ptr<const TextLayout> layout;
        std::size_t requestBucket = 0;
        std::size_t finalBucket = 0;
        std::uint64_t ownedBytes = 0;
        std::uint64_t lastUsed = 0;
    };
    using ShapeList = std::list<ShapeEntry>;
    using ParagraphList = std::list<ParagraphEntry>;
    using ShapeBuckets =
        std::unordered_map<std::size_t, std::vector<ShapeList::iterator>>;
    using ParagraphBuckets =
        std::unordered_map<std::size_t, std::vector<ParagraphList::iterator>>;

    void EraseShape(ShapeList::iterator);
    void EraseParagraph(ParagraphList::iterator);
    void EnforceLimits();

    TextLayoutCacheLimits limits_;
    TextLayoutCacheTelemetry telemetry_;
    ShapeList shapes_;
    ParagraphList paragraphs_;
    ShapeBuckets shapeBuckets_;
    ParagraphBuckets requestBuckets_;
    ParagraphBuckets finalBuckets_;
    std::uint64_t ownedBytes_ = 0;
    // 두 목록에 걸친 LRU 순서를 하나로 세는 시계. 목록마다 순서를 따로 두면
    // 바이트 예산을 맞추려 축출할 때 어느 쪽이 더 오래됐는지 답할 수 없다.
    std::uint64_t clock_ = 0;
};

} // namespace molga::text
