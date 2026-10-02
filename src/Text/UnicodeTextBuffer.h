#pragma once

#include "Text/TextDiagnostic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace molga::text {

// 아래 세 range는 모두 반열린 구간 [begin, end)이고 단위만 다르다. 세 단위를 하나의
// 타입으로 합치지 않는 이유는, 실수로 UTF-16 offset을 byte offset 자리에 넘기는 것이
// 이 계층에서 가장 조용하고 가장 치명적인 버그이기 때문이다. 타입이 다르면 컴파일이
// 막는다.
struct Utf16Range {
    std::uint32_t begin = 0, end = 0;
    friend constexpr bool operator==(Utf16Range a, Utf16Range b) noexcept {
        return a.begin == b.begin && a.end == b.end;
    }
};
struct ScalarRange {
    std::uint32_t begin = 0, end = 0;
    friend constexpr bool operator==(ScalarRange a, ScalarRange b) noexcept {
        return a.begin == b.begin && a.end == b.end;
    }
};
struct GraphemeRange {
    std::uint32_t begin = 0, end = 0;
    friend constexpr bool operator==(GraphemeRange a,
                                     GraphemeRange b) noexcept {
        return a.begin == b.begin && a.end == b.end;
    }
};

// 하나의 scalar와 그 scalar가 원본에서 차지한 byte 구간. value가 U+FFFD라도
// sourceBytes는 치환된 원본 ill-formed subpart 그대로다(길이 1~3 byte). 즉 value로는
// 원본 길이를 되짚을 수 없고, 항상 sourceBytes를 봐야 한다.
//
// 또한 value만으로는 "저작자가 실제로 쓴 U+FFFD"와 "우리가 치환한 U+FFFD"를 구분할
// 수 없다. 둘 다 value == U+FFFD이고 sourceBytes 길이도 똑같이 3 byte일 수 있다
// (EF BF BD). 치환 여부는 오직 진단 sink에 보고된 Utf8Invalid range로만 판별한다.
// U+FFFD 개수를 세어 인코딩 손상을 판정하는 소비자(예: package-time text scan)는
// 반드시 sink를 함께 봐야 한다.
struct DecodedScalar {
    char32_t value = U'\0';
    std::uint32_t scalarIndex = 0;
    SourceByteRange sourceBytes;
    Utf16Range utf16Units;
};

// 저작 텍스트가 엔진에 들어오는 단 하나의 관문.
//
// 계약: 원본 UTF-8 byte는 절대 다시 쓰이지 않는다. ill-formed sequence는 표시용
// UTF-16 view에서만 U+FFFD가 되고, 원본 byte range는 그대로 남는다. 묵시적
// NFC/NFD 정규화도 하지 않는다(정규화가 필요하면 별도의 명시적 authoring 옵션).
// 이 매핑이 한 byte라도 밀리면 HarfBuzz cluster, caret/selection, editor 진단,
// package-time text scan이 전부 조용히 다른 byte를 가리키게 되고 어떤 테스트도
// 그것을 렌더 결과로 보지 못한다.
//
// 두 매핑 함수는 부분 scalar를 절대 넓히지 않는다(clamp/확장 금지). 경계가 정확히
// scalar 경계가 아니면 std::nullopt이며, 호출자는 이를 typed 실패로 전파해야 한다.
// 유일한 예외는 별도 정책(TEXT_INPUT_RANGE_CLAMPED)을 가진 SDL IME 경로다.
//
// 진단 계약: ill-formed byte는 TEXT_UTF8_INVALID를 severity Error로 보고한다.
// Blocker로의 승격은 호출자 몫이다. 설계 실패 행렬은 같은 byte를 저작 asset에서는
// blocking으로, script가 만든 dynamic string에서는 계속 실행으로 요구하는데 이
// buffer는 둘을 구분할 문맥이 없다. 저작/serialized 텍스트 경로(editor import,
// package build)가 이 코드를 받으면 스스로 차단해야 한다.
//
// 이 타입이 UTF-8 디코드의 정본이다. 구형 molga::DecodeUtf8(src/Rendering/Utf8.h)은
// sequence 전체를 모은 뒤 값으로 거르므로 같은 ill-formed byte에 대해 U+FFFD 개수가
// 다르고(예: E0 80 AF -> 여기 3개, 저기 1개) 원본 byte range를 남기지 않는다.
// 하위 호환 전용이며 새 작업을 그쪽으로 보내지 않는다.
//
// ICU를 쓰지 않으므로 TextRuntimeDependencies 준비 여부와 무관하게 동작한다.
// 스레드 계약은 TextDiagnostic.h와 같다: sink 보고가 단일 thread 전용이다.
class UnicodeTextBuffer {
public:
    // 실패는 32bit offset에 담을 수 없는 입력뿐이다. ill-formed UTF-8은 실패가
    // 아니라 U+FFFD + 진단으로 성공한다.
    static std::optional<UnicodeTextBuffer> Build(
        std::string originalUtf8, TextDiagnosticSink&);
    const std::string& OriginalUtf8() const noexcept;
    const std::u16string& SanitizedUtf16() const noexcept;
    const std::vector<DecodedScalar>& Scalars() const noexcept;
    std::optional<SourceByteRange> SourceBytesForUtf16(Utf16Range) const;
    std::optional<Utf16Range> Utf16ForSourceBytes(SourceByteRange) const;
    bool HadDecodeErrors() const noexcept;
private:
    std::string originalUtf8_;
    std::u16string sanitizedUtf16_;
    std::vector<DecodedScalar> scalars_;
    // sanitizedUtf16_와 길이가 같은 unit별 역참조 표. 원소는 그 unit을 만들어낸
    // scalar의 원본 byte range이므로, surrogate pair의 두 unit은 같은 값을 갖는다.
    // 그 동일성이 "pair 내부"를 판정하는 근거다.
    std::vector<SourceByteRange> utf16SourceRanges_;
    bool hadDecodeErrors_ = false;
};

} // namespace molga::text
