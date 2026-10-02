#include "doctest.h"
#include "Text/TextLayoutCache.h"
#include <type_traits>
#ifdef HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS
#error "TextLayoutCache.h leaked a HarfBuzz cluster macro"
#endif
#ifdef HB_H
#error "TextLayoutCache.h leaked hb.h"
#endif
static_assert(std::is_same_v<
    decltype(molga::text::TextShapeCacheKey{}.clusterPolicy),
    molga::text::TextClusterPolicy>);
TEST_CASE("public cache key uses the project cluster policy") {
    CHECK(molga::text::TextShapeCacheKey{}.clusterPolicy ==
          molga::text::TextClusterPolicy::MonotoneCharacters);
}

// 계획서가 고정한 나머지 기본값. 두 구조 어디에서도 기본 lineSpacing이 그대로
// 관찰되는 자리가 없어(fixture는 언제나 덮어쓴다) 이 값이 0이 되어도 아무
// 단언이 움직이지 않는다. 한 em 간격이라는 기본 자체가 계약이므로 여기서
// 못 박는다.
TEST_CASE("the authored default line spacing is one 26.6 em unit") {
    CHECK(molga::text::ParagraphStyle{}.lineSpacing.Raw() == 64);
    CHECK(molga::text::TextParagraphCacheKey{}.lineSpacing.Raw() == 64);
}
