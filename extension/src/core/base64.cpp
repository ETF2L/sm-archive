/* Copyright 2026 ETF2L Contributors, Dave */
#include "base64.h"

namespace smarchive {

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int ValueOf(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

} // namespace

size_t Base64Length(size_t byteCount)
{
	return (byteCount + 2) / 3 * 4;
}

std::string EncodeBase64(const std::vector<uint8_t> &bytes)
{
	std::string text;
	text.reserve(Base64Length(bytes.size()));
	for (size_t i = 0; i < bytes.size(); i += 3) {
		size_t left = bytes.size() - i;
		uint32_t group = static_cast<uint32_t>(bytes[i]) << 16;
		if (left > 1)
			group |= static_cast<uint32_t>(bytes[i + 1]) << 8;
		if (left > 2)
			group |= bytes[i + 2];
		text += kAlphabet[(group >> 18) & 63];
		text += kAlphabet[(group >> 12) & 63];
		text += left > 1 ? kAlphabet[(group >> 6) & 63] : '=';
		text += left > 2 ? kAlphabet[group & 63] : '=';
	}
	return text;
}

bool DecodeBase64(std::string_view text, std::vector<uint8_t> &bytes)
{
	bytes.clear();
	if (text.size() % 4 != 0)
		return false;
	for (size_t i = 0; i < text.size(); i += 4) {
		bool isLast = i + 4 == text.size();
		int padding = 0;
		uint32_t group = 0;
		for (size_t k = 0; k < 4; k++) {
			char c = text[i + k];
			if (c == '=' && isLast && k >= 2) {
				padding++;
				group <<= 6;
				continue;
			}
			int value = ValueOf(c);
			if (value < 0 || padding > 0)
				return false;
			group = (group << 6) | static_cast<uint32_t>(value);
		}
		bytes.push_back(static_cast<uint8_t>(group >> 16));
		if (padding < 2)
			bytes.push_back(static_cast<uint8_t>(group >> 8));
		if (padding < 1)
			bytes.push_back(static_cast<uint8_t>(group));
	}
	return true;
}

} // namespace smarchive
