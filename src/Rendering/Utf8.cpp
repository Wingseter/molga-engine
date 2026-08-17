#include "Rendering/Utf8.h"

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
    std::vector<std::uint32_t> codepoints;
    codepoints.reserve(text.size());

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        codepoints.push_back(DecodeNextUtf8(text, cursor));
    }
    return codepoints;
}

} // namespace molga
