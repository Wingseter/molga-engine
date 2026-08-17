#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace molga {

constexpr std::uint32_t kUnicodeReplacementCharacter = 0xFFFDU;

// Decodes one Unicode scalar value from UTF-8. Ill-formed input produces
// U+FFFD and always advances the cursor, so callers cannot get stuck.
std::uint32_t DecodeNextUtf8(std::string_view text, std::size_t& cursor);

// Strict UTF-8 decoding: overlong encodings, surrogate code points,
// out-of-range values, stray continuation bytes, and truncated sequences are
// replaced with U+FFFD.
std::vector<std::uint32_t> DecodeUtf8(std::string_view text);

} // namespace molga
