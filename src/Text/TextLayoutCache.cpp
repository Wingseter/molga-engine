#include "Text/TextLayoutCache.h"

#include "Common/Sha256.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

namespace molga::text {
namespace {

// ── 값 비교 도우미 ───────────────────────────────────────────────────────────
// ParagraphStyle이 담는 FontRequest/ShapeStyle/TextAnalysisOptions는 이 계층이
// 소유하지 않는 타입이라 operator==를 붙일 수 없다(붙이면 셰이핑/해석 헤더의
// 공개 계약이 바뀐다). 그래서 비교는 여기 파일 지역 함수로만 존재한다.
//
// 필드를 손으로 나열하는 대신 구조적 바인딩으로 푸는 이유는 하나다: 상류
// 헤더에 필드가 하나 늘면 바인딩 개수가 어긋나 여기서 컴파일이 깨진다. 손으로
// 나열하면 새 필드가 조용히 캐시 정체성에서 빠지고, 서로 다르게 셰이핑되는 두
// 문단이 한 항목을 공유한다 — 시험 표는 캐시 키 자신의 필드 목록을 따라
// 쓰이므로 그 변이를 잡지 못한다.
bool SameFeature(const ShapeFeature& a, const ShapeFeature& b) {
    const auto& [aTag, aValue, aBytes] = a;
    const auto& [bTag, bValue, bBytes] = b;
    return aTag == bTag && aValue == bValue && aBytes == bBytes;
}

bool SameFeatures(const std::vector<ShapeFeature>& a,
                  const std::vector<ShapeFeature>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!SameFeature(a[i], b[i])) return false;
    }
    return true;
}

bool SameShapeStyle(const ShapeStyle& a, const ShapeStyle& b) {
    const auto& [aSize, aLanguage, aFeatures, aCluster] = a;
    const auto& [bSize, bLanguage, bFeatures, bCluster] = b;
    return aSize == bSize && aLanguage == bLanguage && aCluster == bCluster &&
           SameFeatures(aFeatures, bFeatures);
}

bool SameFontRequest(const FontRequest& a, const FontRequest& b) {
    const auto& [aWeight, aStretch, aSlant] = a;
    const auto& [bWeight, bStretch, bSlant] = b;
    return aWeight == bWeight && aStretch == bStretch && aSlant == bSlant;
}

bool SameAnalysisOptions(const TextAnalysisOptions& a,
                         const TextAnalysisOptions& b) {
    const auto& [aLocale, aDirection] = a;
    const auto& [bLocale, bDirection] = b;
    return aLocale == bLocale && aDirection == bDirection;
}

// TextShapeCacheKey는 UnicodeAnalysis::Identity()를 구조체가 아니라 다섯 개의
// 낱 필드로 펴서 담는다. 상류에 여섯째가 생겨도 키는 계속 컴파일되므로, 그
// 필드 개수를 여기서 컴파일 타임에 못 박는다. 호출되지 않는 것이 정상이다 —
// 이 함수의 쓸모는 오직 바인딩 개수가 어긋나면 빌드가 깨지는 것뿐이다.
[[maybe_unused]] bool SameAnalysisIdentity(const UnicodeAnalysisIdentity& a,
                                           const UnicodeAnalysisIdentity& b) {
    const auto& [aGraphemeLocale, aLineBreakLocale, aGraphemeRules,
                 aLineBreakRules, aGeneration] = a;
    const auto& [bGraphemeLocale, bLineBreakLocale, bGraphemeRules,
                 bLineBreakRules, bGeneration] = b;
    return aGraphemeLocale == bGraphemeLocale &&
           aLineBreakLocale == bLineBreakLocale &&
           aGraphemeRules == bGraphemeRules &&
           aLineBreakRules == bLineBreakRules && aGeneration == bGeneration;
}

bool SameConstraints(const LayoutConstraints& a, const LayoutConstraints& b) {
    // engaged 여부까지 optional의 비교가 본다. 제약 없음과 폭 0은 서로 다른
    // 요청이므로 여기서 같아지면 안 된다.
    return a.width == b.width && a.height == b.height;
}

bool SameParagraphStyle(const ParagraphStyle& a, const ParagraphStyle& b) {
    return a.fontFamilyGuid == b.fontFamilyGuid &&
           SameFontRequest(a.fontRequest, b.fontRequest) &&
           SameShapeStyle(a.shape, b.shape) &&
           SameAnalysisOptions(a.analysis, b.analysis) && a.wrap == b.wrap &&
           a.overflow == b.overflow && a.maxLines == b.maxLines &&
           a.lineSpacing == b.lineSpacing && a.horizontal == b.horizontal &&
           a.vertical == b.vertical && a.ellipsisUtf8 == b.ellipsisUtf8;
}

bool SameBoundaries(const ShapeBoundaryFlags& a, const ShapeBoundaryFlags& b) {
    return a.beginningOfText == b.beginningOfText && a.endOfText == b.endOfText;
}

bool SameSelectedFace(const SelectedFaceShapeIdentity& a,
                      const SelectedFaceShapeIdentity& b) {
    return a.fontGuid == b.fontGuid && a.fontRevision == b.fontRevision &&
           a.sourceSha256 == b.sourceSha256 &&
           a.artifactSha256 == b.artifactSha256 &&
           a.artifactLocator == b.artifactLocator &&
           a.artifactByteSize == b.artifactByteSize &&
           a.contentGeneration == b.contentGeneration &&
           a.faceIndex == b.faceIndex;
}

template <class T, class TPredicate>
bool SameOrderedVector(const std::vector<T>& a, const std::vector<T>& b,
                       TPredicate same) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!same(a[i], b[i])) return false;
    }
    return true;
}

// ── Hash mixing ─────────────────────────────────────────────────────────────
// splitmix64로 한 값을 흩은 뒤 FNV 방식으로 접는다. 접기가 교환법칙을 따르지
// 않으므로 같은 타입의 두 필드를 맞바꾸면 값이 달라진다 — 그게 없으면
// resolvedGraphemeLocale과 resolvedLineBreakLocale을 서로 바꾼 키가 같은
// 해시를 갖는다.
constexpr std::uint64_t kHashSeed = 1469598103934665603ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;

void MixValue(std::uint64_t& state, std::uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    value ^= value >> 31;
    state ^= value;
    state *= kHashPrime;
}

void MixBytes(std::uint64_t& state, std::string_view bytes) noexcept {
    MixValue(state, bytes.size());
    for (const char byte : bytes) {
        MixValue(state, static_cast<std::uint8_t>(byte));
    }
}

void MixFixed(std::uint64_t& state, Fixed26_6 value) noexcept {
    MixValue(state, static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(value.Raw())));
}

void MixOptionalFixed(std::uint64_t& state,
                      const std::optional<Fixed26_6>& value) noexcept {
    MixValue(state, value.has_value() ? 1u : 0u);
    MixFixed(state, value.value_or(Fixed26_6::FromRaw(0)));
}

void MixRange(std::uint64_t& state, SourceByteRange range) noexcept {
    MixValue(state, range.begin);
    MixValue(state, range.end);
}

void MixRange(std::uint64_t& state, GraphemeRange range) noexcept {
    MixValue(state, range.begin);
    MixValue(state, range.end);
}

void MixLocator(std::uint64_t& state,
                const FontArtifactLocator& locator) noexcept {
    MixValue(state, static_cast<std::uint64_t>(locator.storage));
    // path::generic_string()은 std::string을 새로 할당한다. 이 함수는 noexcept
    // 해시 functor 안에서만 불리므로 그 할당의 bad_alloc은 풀리지 않고 곧바로
    // std::terminate가 된다. native()는 이미 있는 저장소를 빌려줄 뿐이고,
    // POSIX에서는 두 표현이 같은 바이트라 bucket 값도 달라지지 않는다.
    const auto& native = locator.relativePath.native();
    MixValue(state, native.size());
    using PathUnit = std::filesystem::path::value_type;
    for (const PathUnit unit : native) {
        MixValue(state, static_cast<std::uint64_t>(
                            static_cast<std::make_unsigned_t<PathUnit>>(unit)));
    }
}

void MixFeatures(std::uint64_t& state,
                 const std::vector<ShapeFeature>& features) noexcept {
    MixValue(state, features.size());
    for (const auto& feature : features) {
        MixValue(state, feature.tag);
        MixValue(state, feature.value);
        MixRange(state, feature.sourceBytes);
    }
}

void MixShapeStyle(std::uint64_t& state, const ShapeStyle& style) noexcept {
    MixFixed(state, style.fontSize);
    MixBytes(state, style.language);
    MixFeatures(state, style.orderedFeatures);
    MixValue(state, static_cast<std::uint64_t>(style.clusterPolicy));
}

void MixConstraints(std::uint64_t& state,
                    const LayoutConstraints& constraints) noexcept {
    MixOptionalFixed(state, constraints.width);
    MixOptionalFixed(state, constraints.height);
}

void MixParagraphStyle(std::uint64_t& state,
                       const ParagraphStyle& style) noexcept {
    MixBytes(state, style.fontFamilyGuid);
    MixValue(state, style.fontRequest.weight);
    MixValue(state, style.fontRequest.stretchPercent);
    MixValue(state, static_cast<std::uint64_t>(style.fontRequest.slant));
    MixShapeStyle(state, style.shape);
    MixBytes(state, style.analysis.locale);
    MixValue(state, static_cast<std::uint64_t>(style.analysis.baseDirection));
    MixValue(state, static_cast<std::uint64_t>(style.wrap));
    MixValue(state, static_cast<std::uint64_t>(style.overflow));
    MixValue(state, style.maxLines);
    MixFixed(state, style.lineSpacing);
    MixValue(state, static_cast<std::uint64_t>(style.horizontal));
    MixValue(state, static_cast<std::uint64_t>(style.vertical));
    MixBytes(state, style.ellipsisUtf8);
}

void MixShapeKey(std::uint64_t& state,
                 const TextShapeCacheKey& key) noexcept {
    MixValue(state, key.decodePolicyVersion);
    // 두 원본 문자열은 바이트가 아니라 함께 실려 온 hash 필드로만 해시에
    // 들어간다. 그래야 "같은 hash, 다른 바이트"를 강제한 시험이 실제로 같은
    // bucket을 때리고, 바이트 비교가 유일한 판정자가 된다.
    MixValue(state, key.originalBytesHash);
    MixValue(state, key.shapeInputBytesHash);
    MixValue(state, key.shapeInputMapping.size());
    for (const auto& span : key.shapeInputMapping) {
        MixRange(state, span.shapeInputBytes);
        MixRange(state, span.originalSourceBytes);
        MixRange(state, span.originalGraphemes);
        MixValue(state, span.synthetic ? 1u : 0u);
    }
    MixRange(state, key.paragraphBytes);
    MixRange(state, key.runBytes);
    MixValue(state, key.fallbackGraphGeneration);
    MixValue(state, key.selectedFaces.size());
    for (const auto& face : key.selectedFaces) {
        MixBytes(state, face.fontGuid);
        MixBytes(state, face.fontRevision);
        MixBytes(state, face.sourceSha256);
        MixBytes(state, face.artifactSha256);
        MixLocator(state, face.artifactLocator);
        MixValue(state, face.artifactByteSize);
        MixValue(state, face.contentGeneration);
        MixValue(state, face.faceIndex);
    }
    MixFixed(state, key.fontSize);
    MixValue(state, key.variationKey);
    MixValue(state, key.embeddingLevel);
    MixValue(state, static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(key.direction)));
    MixValue(state, static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(key.scriptCode)));
    MixBytes(state, key.language);
    MixBytes(state, key.componentLocale);
    MixBytes(state, key.resolvedGraphemeLocale);
    MixBytes(state, key.resolvedLineBreakLocale);
    MixBytes(state, key.graphemeRuleIdentity);
    MixBytes(state, key.lineBreakRuleIdentity);
    MixValue(state, key.analysisGeneration);
    MixValue(state, key.boundaries.beginningOfText ? 1u : 0u);
    MixValue(state, key.boundaries.endOfText ? 1u : 0u);
    MixValue(state, key.harfbuzzBufferFlags);
    MixValue(state, static_cast<std::uint64_t>(key.clusterPolicy));
    MixFeatures(state, key.orderedFeatures);
    MixBytes(state, key.harfbuzzRevision);
    MixBytes(state, key.icuRevision);
    MixBytes(state, key.icuDataSha256);
    MixBytes(state, key.dependencyContractSha256);
}

// ── Owned-byte accounting ───────────────────────────────────────────────────
// 예산은 캐시가 실제로 붙들고 있는 바이트 전부를 센다: 불변 payload와 항목이
// 자기 안에 복사해 둔 키 둘 다. 항목 수 상한은 키가 몇 개인지만 묶을 뿐 그
// 바이트를 묶지 못한다 — 최종 키는 줄마다 문단 텍스트를 두 벌씩(원본과 셰이핑
// 입력) 담으므로 키 바이트는 문단 길이 × 줄 수로 자란다. payload만 세면 문단
// 1024개 예산에서 키만으로 64 MiB를 넘어설 수 있고, 그때 상한은 아무것도
// 막지 못한다.
//
// 정확한 할당량이 아니라 결정적인 근사이고, 결정적이라는 점이 계약이다: 같은
// 값이 언제나 같은 수를 내야 축출 순서가 재현된다.
std::uint64_t EstimateGlyphBytes(const ShapedGlyph& glyph) {
    return sizeof(ShapedGlyph) + glyph.fontGuid.size() +
           glyph.fontRevision.size() +
           glyph.adjustedGdefCaretOffsets.size() * sizeof(Fixed26_6);
}

std::uint64_t EstimateFactBytes(const TextValidationFact& fact) {
    return sizeof(TextValidationFact) + fact.subsystem.size() +
           fact.message.size() + fact.remediation.size();
}

std::uint64_t EstimateShapeResultBytes(const CachedShapeResult& result) {
    std::uint64_t total = sizeof(CachedShapeResult);
    for (const auto& run : result.runs) {
        total += sizeof(ShapedRun);
        for (const auto& glyph : run.glyphs) total += EstimateGlyphBytes(glyph);
    }
    for (const auto& fact : result.validationFacts) {
        total += EstimateFactBytes(fact);
    }
    return total;
}

std::uint64_t EstimateLayoutBytes(const TextLayout& layout) {
    std::uint64_t total = sizeof(TextLayout);
    for (const auto& line : layout.lines) {
        total += sizeof(TextLine);
        for (const auto& run : line.visualRuns) {
            total += sizeof(VisualRun);
            for (const auto& positioned : run.glyphs) {
                total += sizeof(PositionedGlyph) -
                         sizeof(ShapedGlyph) +
                         EstimateGlyphBytes(positioned.glyph) +
                         positioned.interiorCarets.size() *
                             sizeof(GlyphInteriorCaret);
            }
        }
    }
    total += layout.caretStops.size() * sizeof(CaretStop);
    for (const auto& fact : layout.validationFacts) {
        total += EstimateFactBytes(fact);
    }
    return total;
}

// 캐시가 항목 안에 복사해 둔 키의 바이트. 위의 payload 추정과 같은 규칙을
// 따른다: 구조체 자신의 크기 + 그 아래 매달린 문자열/벡터의 바이트.
std::uint64_t EstimateLocatorBytes(const FontArtifactLocator& locator) {
    return locator.relativePath.native().size() *
           sizeof(std::filesystem::path::value_type);
}

std::uint64_t EstimateShapeKeyBytes(const TextShapeCacheKey& key) {
    std::uint64_t total = sizeof(TextShapeCacheKey);
    total += key.originalUtf8.size() + key.shapeInputUtf8.size();
    total += key.shapeInputMapping.size() * sizeof(ShapeInputSourceSpan);
    for (const auto& face : key.selectedFaces) {
        total += sizeof(SelectedFaceShapeIdentity) + face.fontGuid.size() +
                 face.fontRevision.size() + face.sourceSha256.size() +
                 face.artifactSha256.size() +
                 EstimateLocatorBytes(face.artifactLocator);
    }
    total += key.language.size() + key.componentLocale.size() +
             key.resolvedGraphemeLocale.size() +
             key.resolvedLineBreakLocale.size() +
             key.graphemeRuleIdentity.size() + key.lineBreakRuleIdentity.size();
    total += key.orderedFeatures.size() * sizeof(ShapeFeature);
    total += key.harfbuzzRevision.size() + key.icuRevision.size() +
             key.icuDataSha256.size() + key.dependencyContractSha256.size();
    return total;
}

std::uint64_t EstimateParagraphStyleBytes(const ParagraphStyle& style) {
    return style.fontFamilyGuid.size() + style.shape.language.size() +
           style.shape.orderedFeatures.size() * sizeof(ShapeFeature) +
           style.analysis.locale.size() + style.ellipsisUtf8.size();
}

std::uint64_t EstimateRequestKeyBytes(const TextLayoutRequestIndexKey& key) {
    std::uint64_t total = sizeof(TextLayoutRequestIndexKey);
    total += key.originalUtf8.size();
    total += EstimateParagraphStyleBytes(key.style);
    total += key.familyClosure.requestedGuid.size();
    for (const auto& node : key.familyClosure.depthFirstFamilyNodes) {
        total += sizeof(FamilyNodeRequestIdentity) + node.familyGuid.size();
        for (const auto& edge : node.authoredFallbackGuids) {
            total += sizeof(std::string) + edge.size();
        }
    }
    for (const auto& candidate : key.familyClosure.orderedCandidates) {
        total += sizeof(FaceRequestIdentity) + candidate.fontGuid.size() +
                 candidate.fontRevision.size() + candidate.sourceSha256.size() +
                 candidate.artifactSha256.size() +
                 EstimateLocatorBytes(candidate.artifactLocator);
    }
    total += key.harfbuzzRevision.size() + key.icuRevision.size() +
             key.icuDataSha256.size() +
             key.requestedGraphemeRulePolicyIdentity.size() +
             key.requestedLineBreakRulePolicyIdentity.size() +
             key.dependencyContractSha256.size();
    return total;
}

std::uint64_t EstimateFinalKeyBytes(const TextParagraphCacheKey& key) {
    std::uint64_t total = sizeof(TextParagraphCacheKey);
    // 줄마다 완전한 shape 키가 하나씩이고, 그 안에 문단 텍스트가 두 벌 들어
    // 있다. 여기가 캐시가 붙드는 바이트의 대부분이다.
    for (const auto& line : key.finalLineShapeKeys) {
        total += EstimateShapeKeyBytes(line);
    }
    total += key.overlongTokenPolicy.size() + key.ellipsisUtf8.size() +
             key.ellipsisStyle.language.size() +
             key.ellipsisStyle.orderedFeatures.size() * sizeof(ShapeFeature);
    return total;
}

template <class TBuckets, class TIterator>
bool IsIndexed(const TBuckets& buckets, std::size_t bucket, TIterator entry) {
    const auto found = buckets.find(bucket);
    if (found == buckets.end()) return false;
    return std::find(found->second.begin(), found->second.end(), entry) !=
           found->second.end();
}

template <class TBuckets, class TIterator>
void RemoveFromBucket(TBuckets& buckets, std::size_t bucket, TIterator entry) {
    auto found = buckets.find(bucket);
    // 기록된 bucket이 없거나 그 안에 항목이 없다는 것은 장부가 이미 깨졌다는
    // 뜻이다. 조용히 넘어가면 다음 조회가 곧 사라질 목록 노드를 역참조하고,
    // Debug에서는 우연히 값이 달라 그냥 통과한다.
    assert(found != buckets.end());
    if (found == buckets.end()) return;
    auto& members = found->second;
    const auto member = std::find(members.begin(), members.end(), entry);
    assert(member != members.end());
    if (member == members.end()) return;
    members.erase(member);
    if (members.empty()) buckets.erase(found);
}

std::string RulePolicyIdentity(std::string_view policyKind,
                               std::uint32_t policyVersion,
                               std::string_view icuRevision,
                               std::string_view icuDataSha256) {
    std::string payload;
    payload.append(policyKind).append("\n");
    payload.append(std::to_string(policyVersion)).append("\n");
    payload.append(icuRevision).append("\n");
    payload.append(icuDataSha256).append("\n");
    return molga::Sha256String(payload);
}

}  // namespace

// ── Step 5: identity equality ───────────────────────────────────────────────
bool FamilyNodeRequestIdentity::operator==(
    const FamilyNodeRequestIdentity& other) const {
    return familyGuid == other.familyGuid && exists == other.exists &&
           contentGeneration == other.contentGeneration &&
           authoredFallbackGuids == other.authoredFallbackGuids;
}

bool FaceRequestIdentity::operator==(const FaceRequestIdentity& other) const {
    return fontGuid == other.fontGuid && fontRevision == other.fontRevision &&
           sourceSha256 == other.sourceSha256 &&
           artifactSha256 == other.artifactSha256 &&
           artifactLocator == other.artifactLocator &&
           artifactByteSize == other.artifactByteSize &&
           contentGeneration == other.contentGeneration &&
           faceIndex == other.faceIndex &&
           authoredFaceIndex == other.authoredFaceIndex;
}

bool ResolvedFamilyRequestIdentity::operator==(
    const ResolvedFamilyRequestIdentity& other) const {
    return requestedGuid == other.requestedGuid &&
           depthFirstFamilyNodes == other.depthFirstFamilyNodes &&
           orderedCandidates == other.orderedCandidates;
}

bool ShapeInputSourceSpan::operator==(const ShapeInputSourceSpan& other) const {
    return shapeInputBytes == other.shapeInputBytes &&
           originalSourceBytes == other.originalSourceBytes &&
           originalGraphemes == other.originalGraphemes &&
           synthetic == other.synthetic;
}

// ── Step 5a/6a/7: full key equality ─────────────────────────────────────────
bool TextLayoutRequestIndexKey::operator==(
    const TextLayoutRequestIndexKey& other) const {
    // 길이를 먼저 본다. 길이가 다르면 바이트 비교로 갈 필요가 없고, "같은
    // hash, 다른 길이"가 가장 흔한 충돌 모양이다.
    return decodePolicyVersion == other.decodePolicyVersion &&
           familyResolutionPolicyVersion ==
               other.familyResolutionPolicyVersion &&
           fallbackPolicyVersion == other.fallbackPolicyVersion &&
           originalUtf8.size() == other.originalUtf8.size() &&
           originalUtf8 == other.originalUtf8 &&
           originalBytesHash == other.originalBytesHash &&
           SameParagraphStyle(style, other.style) &&
           SameConstraints(constraints, other.constraints) &&
           visualRevision == other.visualRevision &&
           familyClosure == other.familyClosure &&
           harfbuzzRevision == other.harfbuzzRevision &&
           icuRevision == other.icuRevision &&
           icuDataSha256 == other.icuDataSha256 &&
           requestedGraphemeRulePolicyIdentity ==
               other.requestedGraphemeRulePolicyIdentity &&
           requestedLineBreakRulePolicyIdentity ==
               other.requestedLineBreakRulePolicyIdentity &&
           dependencyContractSha256 == other.dependencyContractSha256;
}

bool TextShapeCacheKey::operator==(const TextShapeCacheKey& other) const {
    return decodePolicyVersion == other.decodePolicyVersion &&
           originalUtf8.size() == other.originalUtf8.size() &&
           originalUtf8 == other.originalUtf8 &&
           originalBytesHash == other.originalBytesHash &&
           shapeInputUtf8.size() == other.shapeInputUtf8.size() &&
           shapeInputUtf8 == other.shapeInputUtf8 &&
           shapeInputBytesHash == other.shapeInputBytesHash &&
           shapeInputMapping == other.shapeInputMapping &&
           paragraphBytes == other.paragraphBytes &&
           runBytes == other.runBytes &&
           fallbackGraphGeneration == other.fallbackGraphGeneration &&
           SameOrderedVector(selectedFaces, other.selectedFaces,
                             SameSelectedFace) &&
           fontSize == other.fontSize && variationKey == other.variationKey &&
           embeddingLevel == other.embeddingLevel &&
           direction == other.direction && scriptCode == other.scriptCode &&
           language == other.language &&
           componentLocale == other.componentLocale &&
           resolvedGraphemeLocale == other.resolvedGraphemeLocale &&
           resolvedLineBreakLocale == other.resolvedLineBreakLocale &&
           graphemeRuleIdentity == other.graphemeRuleIdentity &&
           lineBreakRuleIdentity == other.lineBreakRuleIdentity &&
           analysisGeneration == other.analysisGeneration &&
           SameBoundaries(boundaries, other.boundaries) &&
           harfbuzzBufferFlags == other.harfbuzzBufferFlags &&
           clusterPolicy == other.clusterPolicy &&
           SameFeatures(orderedFeatures, other.orderedFeatures) &&
           harfbuzzRevision == other.harfbuzzRevision &&
           icuRevision == other.icuRevision &&
           icuDataSha256 == other.icuDataSha256 &&
           dependencyContractSha256 == other.dependencyContractSha256;
}

bool TextParagraphCacheKey::operator==(
    const TextParagraphCacheKey& other) const {
    return finalLineShapeKeys == other.finalLineShapeKeys &&
           SameConstraints(constraints, other.constraints) &&
           wrap == other.wrap && overflow == other.overflow &&
           overlongTokenPolicy == other.overlongTokenPolicy &&
           ellipsisUtf8 == other.ellipsisUtf8 &&
           SameShapeStyle(ellipsisStyle, other.ellipsisStyle) &&
           maxLines == other.maxLines && lineSpacing == other.lineSpacing &&
           horizontal == other.horizontal && vertical == other.vertical &&
           visualRevision == other.visualRevision;
}

// ── Byte-hash provenance ────────────────────────────────────────────────────
std::uint64_t CacheBytesHash(std::string_view bytes) noexcept {
    std::uint64_t state = kHashSeed;
    MixBytes(state, bytes);
    return state;
}

// ── Hash functors ───────────────────────────────────────────────────────────
std::size_t TextLayoutRequestIndexKeyHash::operator()(
    const TextLayoutRequestIndexKey& key) const noexcept {
    std::uint64_t state = kHashSeed;
    MixValue(state, key.decodePolicyVersion);
    MixValue(state, key.familyResolutionPolicyVersion);
    MixValue(state, key.fallbackPolicyVersion);
    MixValue(state, key.originalBytesHash);
    MixParagraphStyle(state, key.style);
    MixConstraints(state, key.constraints);
    MixValue(state, key.visualRevision);
    MixBytes(state, key.familyClosure.requestedGuid);
    MixValue(state, key.familyClosure.depthFirstFamilyNodes.size());
    for (const auto& node : key.familyClosure.depthFirstFamilyNodes) {
        MixBytes(state, node.familyGuid);
        MixValue(state, node.exists ? 1u : 0u);
        MixValue(state, node.contentGeneration);
        MixValue(state, node.authoredFallbackGuids.size());
        for (const auto& edge : node.authoredFallbackGuids) MixBytes(state, edge);
    }
    MixValue(state, key.familyClosure.orderedCandidates.size());
    for (const auto& candidate : key.familyClosure.orderedCandidates) {
        MixBytes(state, candidate.fontGuid);
        MixBytes(state, candidate.fontRevision);
        MixBytes(state, candidate.sourceSha256);
        MixBytes(state, candidate.artifactSha256);
        MixLocator(state, candidate.artifactLocator);
        MixValue(state, candidate.artifactByteSize);
        MixValue(state, candidate.contentGeneration);
        MixValue(state, candidate.faceIndex);
        MixValue(state, candidate.authoredFaceIndex);
    }
    MixBytes(state, key.harfbuzzRevision);
    MixBytes(state, key.icuRevision);
    MixBytes(state, key.icuDataSha256);
    MixBytes(state, key.requestedGraphemeRulePolicyIdentity);
    MixBytes(state, key.requestedLineBreakRulePolicyIdentity);
    MixBytes(state, key.dependencyContractSha256);
    return static_cast<std::size_t>(state);
}

std::size_t TextShapeCacheKeyHash::operator()(
    const TextShapeCacheKey& key) const noexcept {
    std::uint64_t state = kHashSeed;
    MixShapeKey(state, key);
    return static_cast<std::size_t>(state);
}

std::size_t TextParagraphCacheKeyHash::operator()(
    const TextParagraphCacheKey& key) const noexcept {
    std::uint64_t state = kHashSeed;
    MixValue(state, key.finalLineShapeKeys.size());
    for (const auto& line : key.finalLineShapeKeys) MixShapeKey(state, line);
    MixConstraints(state, key.constraints);
    MixValue(state, static_cast<std::uint64_t>(key.wrap));
    MixValue(state, static_cast<std::uint64_t>(key.overflow));
    MixBytes(state, key.overlongTokenPolicy);
    MixBytes(state, key.ellipsisUtf8);
    MixShapeStyle(state, key.ellipsisStyle);
    MixValue(state, key.maxLines);
    MixFixed(state, key.lineSpacing);
    MixValue(state, static_cast<std::uint64_t>(key.horizontal));
    MixValue(state, static_cast<std::uint64_t>(key.vertical));
    MixValue(state, key.visualRevision);
    return static_cast<std::size_t>(state);
}

// ── Step 9: requested rule-policy identity ──────────────────────────────────
std::string RequestedGraphemeRulePolicyIdentity(
    std::string_view icuRevision, std::string_view icuDataSha256) {
    return RulePolicyIdentity("grapheme-rule-policy",
                              kRequestedGraphemeRulePolicyVersion, icuRevision,
                              icuDataSha256);
}

std::string RequestedLineBreakRulePolicyIdentity(
    std::string_view icuRevision, std::string_view icuDataSha256) {
    return RulePolicyIdentity("line-break-rule-policy",
                              kRequestedLineBreakRulePolicyVersion, icuRevision,
                              icuDataSha256);
}

// ── Step 8: limits ──────────────────────────────────────────────────────────
TextLayoutCacheLimits TextLayoutCacheLimits::Production() noexcept {
    return TextLayoutCacheLimits{4096, 1024, 64ULL * 1024ULL * 1024ULL};
}

// ── Step 8a–12: the cache service ───────────────────────────────────────────
TextLayoutCache::TextLayoutCache(TextLayoutCacheLimits limits)
    : limits_(limits) {}

std::shared_ptr<const CachedShapeResult> TextLayoutCache::FindShape(
    const TextShapeCacheKey& key) {
    const std::size_t bucket = TextShapeCacheKeyHash{}(key);
    auto found = shapeBuckets_.find(bucket);
    if (found != shapeBuckets_.end()) {
        for (auto entry : found->second) {
            // hash 일치는 bucket 선택일 뿐이다. 판정은 언제나 저작/실제 셰이핑
            // 입력의 길이와 바이트, 모든 매핑 span, 불변 분석 정체성 다섯 개,
            // cluster 정책, 그리고 나머지 구조화된 필드 전부의 비교다.
            if (entry->key == key) {
                shapes_.splice(shapes_.begin(), shapes_, entry);
                entry->lastUsed = clock_++;
                ++telemetry_.shapeHits;
                return entry->value;
            }
        }
    }
    ++telemetry_.shapeMisses;
    return nullptr;
}

void TextLayoutCache::StoreShape(
    const TextShapeCacheKey& key,
    std::shared_ptr<const CachedShapeResult> value) {
    // 빈 지분은 색인하지 않는다. 색인하면 FindShape가 nullptr을 돌려주면서
    // telemetry에는 적중으로 세므로 호출자는 miss와 구분할 수 없고, 그 좀비
    // 항목이 슬롯 하나를 영원히 붙든다(ownedBytes가 0이라 예산도 회수하지
    // 못한다). 이미 저장된 진짜 값이 있다면 그대로 둔다.
    if (!value) return;
    ++telemetry_.shapeStores;
    const std::size_t bucket = TextShapeCacheKeyHash{}(key);
    const std::uint64_t keyBytes = EstimateShapeKeyBytes(key);
    auto& members = shapeBuckets_[bucket];
    for (auto entry : members) {
        if (entry->key == key) {
            ownedBytes_ -= entry->ownedBytes;
            entry->value = std::move(value);
            entry->ownedBytes = keyBytes + EstimateShapeResultBytes(*entry->value);
            ownedBytes_ += entry->ownedBytes;
            shapes_.splice(shapes_.begin(), shapes_, entry);
            entry->lastUsed = clock_++;
            EnforceLimits();
            return;
        }
    }
    // 같은 bucket의 다른 키는 가리지 않고 나란히 놓인다. 덮어쓰면 강제 충돌이
    // 서로 다른 바이트를 조용히 삼킨다.
    ShapeEntry entry;
    entry.key = key;
    entry.value = std::move(value);
    entry.bucket = bucket;
    entry.ownedBytes = keyBytes + EstimateShapeResultBytes(*entry.value);
    entry.lastUsed = clock_++;
    const std::uint64_t addedBytes = entry.ownedBytes;
    shapes_.push_front(std::move(entry));
    members.push_back(shapes_.begin());
    // 목록과 색인에 실제로 들어간 다음에 센다. 앞에서 세면 push_front가 던졌을
    // 때 존재하지 않는 항목의 몫이 예산에 영구히 남아, 결국 살아 있는 항목을
    // 계속 축출하다 캐시가 조용히 죽는다.
    ownedBytes_ += addedBytes;
    EnforceLimits();
}

std::optional<CachedTextLayout> TextLayoutCache::FindByRequest(
    const TextLayoutRequestIndexKey& key) {
    // ICU도 HarfBuzz도 부르지 않는다. 이 경로가 warm 조회의 전부이므로,
    // 여기서 분석이나 셰이핑이 일어나면 캐시가 아무것도 아끼지 못한다.
    const std::size_t bucket = TextLayoutRequestIndexKeyHash{}(key);
    auto found = requestBuckets_.find(bucket);
    if (found != requestBuckets_.end()) {
        for (auto entry : found->second) {
            if (entry->requestKey == key) {
                paragraphs_.splice(paragraphs_.begin(), paragraphs_, entry);
                entry->lastUsed = clock_++;
                ++telemetry_.requestHits;
                CachedTextLayout warm;
                warm.finalKey = entry->finalKey;
                warm.layout = entry->layout;
                return warm;
            }
        }
    }
    ++telemetry_.requestMisses;
    return std::nullopt;
}

std::shared_ptr<const TextLayout> TextLayoutCache::FindByFinal(
    const TextParagraphCacheKey& key) {
    // 감사와 한 필드 시험을 위한 경로다. 이른 request 조회를 대신하지 않는다:
    // 이 키를 만들려면 최종 줄이 이미 셰이핑되어 있어야 한다.
    //
    // 저장은 request key로만 항목을 찾으므로, 서로 다른 두 request가 같은 최종
    // 키를 낸다면 두 항목이 이 bucket에 함께 놓인다. 그때 여기서는 먼저 들어온
    // 쪽이 나온다. 같은 최종 키는 같은 레이아웃을 뜻하므로 감사에는 문제가
    // 없지만, "가장 최근"을 기대하고 쓰면 어긋난다.
    const std::size_t bucket = TextParagraphCacheKeyHash{}(key);
    auto found = finalBuckets_.find(bucket);
    if (found != finalBuckets_.end()) {
        for (auto entry : found->second) {
            if (entry->finalKey == key) {
                paragraphs_.splice(paragraphs_.begin(), paragraphs_, entry);
                entry->lastUsed = clock_++;
                ++telemetry_.finalHits;
                return entry->layout;
            }
        }
    }
    ++telemetry_.finalMisses;
    return nullptr;
}

void TextLayoutCache::Store(const TextLayoutRequestIndexKey& requestKey,
                            const TextParagraphCacheKey& finalKey,
                            std::shared_ptr<const TextLayout> layout) {
    // 반쯤 채워진 항목을 공개하지 않는다. 색인하면 FindByRequest가 engaged
    // optional 안에 nullptr layout을 담아 돌려주고, 계약대로 `*warm->layout`을
    // 쓰는 호출자가 곧바로 null을 역참조한다.
    if (!layout) return;
    ++telemetry_.finalStores;
    const std::size_t requestBucket = TextLayoutRequestIndexKeyHash{}(requestKey);
    const std::size_t finalBucket = TextParagraphCacheKeyHash{}(finalKey);
    const std::uint64_t keyBytes =
        EstimateRequestKeyBytes(requestKey) + EstimateFinalKeyBytes(finalKey);

    auto found = requestBuckets_.find(requestBucket);
    if (found != requestBuckets_.end()) {
        for (auto entry : found->second) {
            if (entry->requestKey == requestKey) {
                // 같은 request가 다른 최종 키를 낼 수 있다 — 오히려 그쪽이
                // 정상이다: analysisGeneration은 프로세스 안에서 재사용되지
                // 않으므로 다시 배치하면 최종 줄 키가 반드시 달라진다. 옛
                // bucket에서 빼지 않으면 FindByFinal(옛 키)가 새 레이아웃을
                // 내주고, 축출 뒤에는 죽은 노드를 역참조한다.
                if (!(entry->finalKey == finalKey)) {
                    RemoveFromBucket(finalBuckets_, entry->finalBucket, entry);
                    entry->finalKey = finalKey;
                    entry->finalBucket = finalBucket;
                    finalBuckets_[finalBucket].push_back(entry);
                }
                ownedBytes_ -= entry->ownedBytes;
                entry->layout = std::move(layout);
                entry->ownedBytes = keyBytes + EstimateLayoutBytes(*entry->layout);
                ownedBytes_ += entry->ownedBytes;
                paragraphs_.splice(paragraphs_.begin(), paragraphs_, entry);
                entry->lastUsed = clock_++;
                EnforceLimits();
                return;
            }
        }
    }
    // request key, final key와 불변 레이아웃이 하나의 항목으로 묶인다. 축출이
    // 그 항목을 지우면 두 색인이 함께 사라지므로, 어느 쪽도 없어진 레이아웃을
    // 가리킨 채 남지 않는다.
    ParagraphEntry entry;
    entry.requestKey = requestKey;
    entry.finalKey = finalKey;
    entry.layout = std::move(layout);
    entry.requestBucket = requestBucket;
    entry.finalBucket = finalBucket;
    entry.ownedBytes = keyBytes + EstimateLayoutBytes(*entry.layout);
    entry.lastUsed = clock_++;
    const std::uint64_t addedBytes = entry.ownedBytes;
    paragraphs_.push_front(std::move(entry));
    const ParagraphList::iterator inserted = paragraphs_.begin();
    requestBuckets_[requestBucket].push_back(inserted);
    finalBuckets_[finalBucket].push_back(inserted);
    // 세 삽입이 모두 끝난 다음에 센다(StoreShape와 같은 이유).
    ownedBytes_ += addedBytes;
    EnforceLimits();
}

const TextLayoutCacheTelemetry& TextLayoutCache::Telemetry() const noexcept {
    return telemetry_;
}

void TextLayoutCache::EraseShape(ShapeList::iterator entry) {
    RemoveFromBucket(shapeBuckets_, entry->bucket, entry);
    // 색인 어디에도 남아 있지 않아야 한다. 하나라도 남으면 다음 조회가 방금
    // 지운 목록 노드를 역참조하는데, Debug에서는 그 메모리가 우연히 다른 키로
    // 읽혀 miss처럼 보이고 ASan에서만 드러난다. 여기서 즉시 못 박는다.
    assert(!IsIndexed(shapeBuckets_, entry->bucket, entry));
    ownedBytes_ -= entry->ownedBytes;
    // shared_ptr 지분 하나만 놓는다. 밖에서 같은 결과를 들고 있는 소비자는
    // 자기가 받은 그 불변 값을 그대로 계속 본다.
    shapes_.erase(entry);
}

void TextLayoutCache::EraseParagraph(ParagraphList::iterator entry) {
    RemoveFromBucket(requestBuckets_, entry->requestBucket, entry);
    RemoveFromBucket(finalBuckets_, entry->finalBucket, entry);
    assert(!IsIndexed(requestBuckets_, entry->requestBucket, entry));
    assert(!IsIndexed(finalBuckets_, entry->finalBucket, entry));
    ownedBytes_ -= entry->ownedBytes;
    paragraphs_.erase(entry);
}

void TextLayoutCache::EnforceLimits() {
    // 모든 상한이 단단하다. 0은 그 자원의 용량이 0이라는 뜻이므로, 방금 저장한
    // 항목이라도 예외 없이 여기서 다시 나간다.
    while (shapes_.size() > limits_.maxShapeEntries) {
        ++telemetry_.evictions;
        EraseShape(std::prev(shapes_.end()));
    }
    while (paragraphs_.size() > limits_.maxParagraphEntries) {
        ++telemetry_.evictions;
        EraseParagraph(std::prev(paragraphs_.end()));
    }
    while (ownedBytes_ > limits_.maxOwnedBytes) {
        const bool haveShape = !shapes_.empty();
        const bool haveParagraph = !paragraphs_.empty();
        if (!haveShape && !haveParagraph) break;
        ++telemetry_.evictions;
        if (!haveParagraph) {
            EraseShape(std::prev(shapes_.end()));
            continue;
        }
        if (!haveShape) {
            EraseParagraph(std::prev(paragraphs_.end()));
            continue;
        }
        // 두 목록에 걸친 하나의 시계로 더 오래된 쪽을 고른다. 목록별로 따로
        // 세면 셰이핑만 하는 프레임이 문단 항목을 영원히 살려 둔다.
        const ShapeList::iterator shapeVictim = std::prev(shapes_.end());
        const ParagraphList::iterator paragraphVictim =
            std::prev(paragraphs_.end());
        if (shapeVictim->lastUsed <= paragraphVictim->lastUsed) {
            EraseShape(shapeVictim);
        } else {
            EraseParagraph(paragraphVictim);
        }
    }
}

}  // namespace molga::text
