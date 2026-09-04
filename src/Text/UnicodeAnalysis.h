#pragma once

#include "Text/TextDiagnostic.h"
#include "Text/UnicodeTextBuffer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace molga::text {

// 문단 기준 방향. Auto는 UBA P2/P3(첫 strong 문자)로 결정하라는 뜻이며, 강제
// LTR/RTL은 저작자가 명시적으로 고른 값이다. "common script이므로 LTR"처럼
// 방향을 script에서 추론하는 경로는 없다.
enum class BaseDirection : std::uint8_t { Auto, LeftToRight, RightToLeft };

// locale은 BCP-47 태그다. 기본값 "und"는 root tailoring을 뜻하고, 잘못된 태그는
// ASCII fallback이 아니라 실패다(Step 9).
struct TextAnalysisOptions {
    std::string locale = "und";
    BaseDirection baseDirection = BaseDirection::Auto;
};

// 셋 이상으로 쪼개지지 않는 분석 단위. 하나의 item 안에서는 문단, BiDi logical
// run, embedding level, 해석된 script가 전부 같고, isolate/control 문자는 자기
// 혼자 하나의 item이 된다. 세 range는 같은 구간을 각각 원본 byte / UTF-16 unit /
// grapheme index로 말한 것이고, 셋 다 UnicodeTextBuffer의 매핑을 통과한 값이다.
//
// embeddingLevel은 ICU ubidi_getLevels가 이 문단에 대해 낸 문자 단위 resolved
// level이다. 방향 parity도 아니고 문단 level도 아니다: level 0과 2는 둘 다
// LTR이지만 서로 다른 run이므로 절대 합치지 않는다. 합치면 중첩된 숫자/괄호가
// 바깥 run과 같은 순서로 배치된다. UBA의 L1까지 적용된 값이라, segment/문단
// separator와 그 앞의 공백은 자기 run이 아니라 문단 level을 갖는다.
//
// 예외는 UBA가 애초에 level을 주지 않는 문자뿐이다. LRE/RLE/LRO/RLO/PDF는 X9가
// 제거하는 문자라 "정확한 level"이라는 것이 존재하지 않고, 여기 들어 있는 값은
// ICU의 보존 규약이다: 여는 쪽(LRE/RLE/LRO/RLO)은 안쪽 level, PDF는 바깥 level.
// isolate(LRI/RLI/FSI/PDI)는 X6a가 바깥 level을 주도록 정해 두었고 ICU도 그렇게
// 준다. 이 아홉 문자는 전부 자기 혼자 item이 되므로, shaping 방향을 그 item의
// level에서 끌어내면 안 된다 — 그릴 glyph가 없고, 규약은 UBA가 보장하는 값이
// 아니다. 위의 모든 문장은 test_unicode_text의 "embeddingLevel is the exact
// resolved level..." 케이스가 실제 ICU 출력으로 고정한다.
//
// logicalRunId는 "같은 BiDi logical run인가"만 답하는 값이다. 분석 전체에서
// 논리 순서대로 단조 증가하고(문단마다 0으로 되돌아가지 않는다), 서로 다른 두
// run이 같은 번호를 갖는 일은 없다. 다만 조밀하지 않다: 번호는 ICU가 낸 run
// 하나마다 올라가므로, grapheme cluster 안쪽에서 시작해 item을 하나도 만들지
// 못한 run은 번호만 쓰고 사라진다. 비교와 정렬에만 쓰고, [0, N) 범위라고
// 가정하거나 배열 색인으로 쓰면 안 된다.
struct AnalysisItem {
    SourceByteRange sourceBytes;
    Utf16Range utf16Units;
    GraphemeRange graphemes;
    std::int32_t scriptCode = 0;
    std::uint8_t embeddingLevel = 0;
    bool paragraphStart = false;
    bool paragraphEnd = false;
    std::uint32_t logicalRunId = 0;
};

// 이 분석이 실제로 무엇으로 만들어졌는지의 기록.
//
// 요청한 locale이 아니라 ICU가 실제로 고른 locale이고, 요청한 버전 문자열이
// 아니라 ICU가 컴파일해 들고 있는 break rule 바이트의 SHA-256이다. 캐시 키가
// 요청 문자열만 담으면, 같은 문자열이 다른 ICU 데이터에서 다른 경계를 내도
// 캐시가 조용히 적중한다. analysisGeneration은 process 안에서 절대 재사용되지
// 않는 값이라 "이 분석 결과가 저 캐시 항목과 같은 것인가"를 문자열 비교 없이
// 판정할 수 있다.
struct UnicodeAnalysisIdentity {
    std::string resolvedGraphemeLocale;
    std::string resolvedLineBreakLocale;
    std::string graphemeRuleIdentity;
    std::string lineBreakRuleIdentity;
    std::uint64_t analysisGeneration = 0;
};

// 하나의 문단 집합에 대한 확정된 분석. 값 타입이고 mutator가 없다.
//
// 모든 offset은 저작 원본 UTF-8 byte offset이다. ICU가 쓰는 UTF-16 offset이
// 밖으로 새면 그 순간 HarfBuzz cluster, caret, 진단이 전부 다른 byte를 가리키게
// 되고 렌더 결과로는 드러나지 않는다.
//
// GraphemeBoundaries()와 LineBreakBoundaries()는 오름차순이고 각각 0과 원본
// byte 길이를 포함한다. 줄바꿈 경계는 grapheme 경계의 부분집합이다. 예외는
// 하나뿐이고 현재 ICU 78에서는 실제로 발생하지 않는다: UAX#14의 강제 줄바꿈이
// grapheme 경계가 아닌 곳에 놓이면 그 경계는 남는다. 저작자가 적은 줄바꿈을
// 버리는 쪽이 cluster를 쪼개는 쪽보다 나쁘기 때문이다. 둘이 충돌하는 입력이
// 생긴다면 이 우선순위가 답이고, 부분집합 성질은 그때만 깨진다.
class UnicodeAnalysis {
public:
    const std::vector<std::uint32_t>& GraphemeBoundaries() const noexcept;
    const std::vector<std::uint32_t>& LineBreakBoundaries() const noexcept;
    const std::vector<AnalysisItem>& Items() const noexcept;
    const UnicodeAnalysisIdentity& Identity() const noexcept;

private:
    friend class UnicodeTextAnalyzer;
    // Test-only seam, and a narrower one than the two detail seams at the
    // bottom of this header: it names a class that nothing in molga_core
    // defines, so no symbol that can assemble a UnicodeAnalysis from raw parts
    // ships at all. A violator has to write the class definition itself, which
    // is a far louder act than calling an exported function.
    //
    // It exists for exactly one observation. Task 5.2's fresh-process case must
    // call TextShapingService::ShapeAnalysisItem while the text runtime is
    // still NeverInitialized, and the only other way to hold a UnicodeAnalysis
    // — UnicodeTextAnalyzer::Analyze — refuses to run in that process by
    // construction, so no such object can otherwise exist there.
    //
    // Nothing that ships may define it. An analysis assembled this way is not
    // cross-checked against any buffer, against its own boundaries, or against
    // ICU, and it carries an analysisGeneration ICU never issued — which is
    // part of cache identity. Every consumer downstream trusts all of that.
    friend class UnicodeAnalysisTestAccess;
    UnicodeAnalysis() = default;

    std::vector<std::uint32_t> graphemeBoundaries_;
    std::vector<std::uint32_t> lineBreakBoundaries_;
    std::vector<AnalysisItem>  items_;
    UnicodeAnalysisIdentity    identity_;
};

// UnicodeTextBuffer + 준비된 ICU 수명 -> UnicodeAnalysis.
//
// 실패 정책은 fail-closed 하나뿐이다. ICU가 준비되지 않았거나(DependencyInvalid),
// locale이 잘못됐거나 ICU 호출이 실패하면(LayoutInvalid) 진단 하나를 남기고
// nullopt를 돌려준다. ASCII/휴리스틱 대체 분석은 없다. 그런 대체 경로가 있으면
// 데이터가 빠진 패키지가 "글자는 나오는데 줄바꿈만 이상한" 상태로 출하된다.
//
// 진단 상한: Analyze 한 번은 진단을 최대 하나 보고한다. 모든 보고 지점이 곧바로
// nullopt를 반환하므로 grapheme/줄바꿈/run 개수에 비례하는 진단은 존재할 수 없다.
//
// 스레드 계약: TextDiagnostic.h/TextRuntimeDependencies.h와 같다. 단일 thread
// 전용이다. 아래 generation allocator만 mutex를 갖는데, 그것은 동시 호출을
// 허용하기 위해서가 아니라 generation 재사용이 다른 어떤 실패보다 조용하기
// 때문이다.
class UnicodeTextAnalyzer {
public:
    static std::optional<UnicodeAnalysis> Analyze(
        const UnicodeTextBuffer&, const TextAnalysisOptions&,
        TextDiagnosticSink&);
};

namespace detail {

// Test-only seams by convention, not by construction. Be precise about which,
// because the difference is the whole value of the claim.
//
// These functions are compiled into molga_core with the rest of this file, and
// molga::text::detail is an ordinary exported namespace: any translation unit
// that includes this header can call them. What tests/CMakeLists.txt asserts at
// configure time is narrower — that tests/UnicodeTextAnalyzerTestAccess.cpp,
// the wrapper the analysis tests use, is compiled into exactly the two analysis
// test executables and into no product target. That check cannot stop shipped
// code from calling ExchangeAnalysisGenerationState directly, and no runtime
// latch can either: Step 1h has to raise the allocator and then put the
// previous state back, so "only ever forward" is not available as a guard.
//
// So: nothing that ships may reset the counter or renumber a generation, and
// that is a rule enforced at review, not by the build. A caller that broke it
// would reissue generations 1..k, and two structurally different analyses would
// then compare equal by analysisGeneration — silently, with no failing test.

// Attempted ICU iterator/object creations, counted at the call site so a
// refused ubrk_open still registers. This exists in the shipped build because
// the only claim worth testing — "no ICU object is created before the runtime
// is ready" — is about production code paths, and a counter compiled only into
// tests would be measuring a different program.
std::uint64_t IcuObjectCreationCount() noexcept;
void          ResetIcuObjectCreationCount() noexcept;

// The process-wide generation allocator, whole. Exchanged rather than set so
// the test companion can restore the previous state exactly, including the
// exhaustion latch.
struct AnalysisGenerationState {
    std::uint64_t next      = 1;
    bool          exhausted = false;
};
AnalysisGenerationState ExchangeAnalysisGenerationState(
    AnalysisGenerationState) noexcept;

}  // namespace detail

}  // namespace molga::text
