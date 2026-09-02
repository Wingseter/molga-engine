#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace molga {

constexpr std::uint32_t kUnicodeReplacementCharacter = 0xFFFDU;

// Compatibility only. molga::text::UnicodeTextBuffer (src/Text/UnicodeTextBuffer.h)
// is the authoritative UTF-8 decode for the engine, and it is what every new
// caller must use. It keeps the original byte range of every scalar, reports a
// typed TEXT_UTF8_INVALID diagnostic for each ill-formed subpart, and produces
// the UTF-16 view ICU and HarfBuzz consume. This header returns bare scalar
// values with no source mapping and no diagnostic, so a caller here cannot tell
// a replaced byte from an authored U+FFFD, cannot report where the damage is,
// and cannot map a glyph back to the byte the author wrote.
//
// Nothing is migrated off these two before Milestone 8; that migration moves
// TextRenderer onto shaped runs rather than scalar loops, and doing it
// piecemeal would leave two different glyph paths live at once.

// Decodes one Unicode scalar value from UTF-8. Ill-formed input produces
// U+FFFD and always advances the cursor, so callers cannot get stuck.
//
// This is NOT the decoder behind DecodeUtf8 any more. It gathers a whole
// sequence and then filters by value, so it collapses a broken multi-byte
// sequence into one U+FFFD where the authoritative decoder reports one per
// Unicode maximal subpart. The two therefore disagree on ill-formed input by
// design; do not use this to predict what DecodeUtf8 will return.
std::uint32_t DecodeNextUtf8(std::string_view text, std::size_t& cursor);

// Strict UTF-8 decoding: overlong encodings, surrogate code points,
// out-of-range values, stray continuation bytes, and truncated sequences are
// replaced with U+FFFD, one per Unicode maximal subpart.
//
// A thin wrapper over UnicodeTextBuffer that drops everything but the scalar
// values, including the diagnostics its local sink collects. Text longer than
// a 32-bit byte index yields an empty vector rather than a truncated one.
std::vector<std::uint32_t> DecodeUtf8(std::string_view text);

} // namespace molga
