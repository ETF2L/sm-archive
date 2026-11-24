/* Copyright 2026 ETF2L Contributors, Dave
 *
 * Base64 of RFC 4648 (standard alphabet, "=" padding), for memory results a
 * plugin reads as text and compressed data it passes as text. */
#ifndef SMARCHIVE_CORE_BASE64_H
#define SMARCHIVE_CORE_BASE64_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace smarchive {

std::string EncodeBase64(const std::vector<uint8_t> &bytes);

/* The length of EncodeBase64()'s result for this many bytes. */
size_t Base64Length(size_t byteCount);

/* False for anything but padded base64 (no whitespace). */
bool DecodeBase64(std::string_view text, std::vector<uint8_t> &bytes);

} // namespace smarchive

#endif // SMARCHIVE_CORE_BASE64_H
