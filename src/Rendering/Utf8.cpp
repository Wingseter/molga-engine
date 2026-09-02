#include "Rendering/Utf8.h"

#include "Text/TextDiagnostic.h"
#include "Text/UnicodeTextBuffer.h"

#include <string>

namespace molga {
namespace {

bool IsContinuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

} // namespace

std::uint32_t DecodeNextUtf8(std::string_view text, std::size_t& cursor) {
    if (cursor >= text.size()) {
        return kUnicodeReplacementCharacter;
    }

    const std::size_t start = cursor;
    const auto first = static_cast<unsigned char>(text[cursor]);
    if (first <= 0x7FU) {
        ++cursor;
        return first;
    }

    int length = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
        length = 2;
        value = first & 0x1FU;
        minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        length = 3;
        value = first & 0x0FU;
        minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        length = 4;
        value = first & 0x07U;
        minimum = 0x10000U;
    } else {
        ++cursor;
        return kUnicodeReplacementCharacter;
    }

    for (int index = 1; index < length; ++index) {
        const std::size_t position = start + static_cast<std::size_t>(index);
        if (position >= text.size()) {
            cursor = text.size();
            return kUnicodeReplacementCharacter;
        }
        const auto byte = static_cast<unsigned char>(text[position]);
        if (!IsContinuation(byte)) {
            // Consume the valid prefix and let the next call process the byte
            // that broke the sequence.
            cursor = position;
            return kUnicodeReplacementCharacter;
        }
        value = (value << 6U) | (byte & 0x3FU);
    }

    cursor = start + static_cast<std::size_t>(length);
    if (value < minimum || value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU)) {
        return kUnicodeReplacementCharacter;
    }
    return value;
}

std::vector<std::uint32_t> DecodeUtf8(std::string_view text) {
    // 이 함수의 결과가 DecodeNextUtf8 루프와 달라지는 것이 이 변경의 요점이다.
    // 정본 디코더는 깨진 sequence를 Unicode maximal subpart 단위로 끊으므로
    // 같은 byte에 대해 U+FFFD 개수가 다르다. 두 구현이 공존하면 "어느 쪽이
    // 맞는가"가 호출자마다 달라지므로, 남은 구형 호출자도 전부 정본을 통과한다.
    //
    // 진단은 지역 sink로 받아서 버린다. 이 API에는 진단을 전달할 자리가 없고,
    // 여기서 로거로 흘리면 UTF-8로 잘못 읽힌 파일 하나가 프레임마다 수천 줄을
    // 찍는다. 진단이 필요한 저작/패키징 경로는 UnicodeTextBuffer를 직접 쓴다.
    molga::text::VectorTextDiagnosticSink sink;
    const auto buffer =
        molga::text::UnicodeTextBuffer::Build(std::string(text), sink);
    if (!buffer) return {};

    std::vector<std::uint32_t> codepoints;
    codepoints.reserve(buffer->Scalars().size());
    for (const auto& scalar : buffer->Scalars()) {
        codepoints.push_back(static_cast<std::uint32_t>(scalar.value));
    }
    return codepoints;
}

} // namespace molga
