/* Copyright 2026 ETF2L Contributors, Dave */
#include "detect.h"

#include <cstring>

namespace smarchive {

namespace {

bool StartsWith(const uint8_t *head, size_t length, const char *magic, size_t magicLength)
{
	return length >= magicLength && memcmp(head, magic, magicLength) == 0;
}

bool EndsWith(std::string_view name, std::string_view suffix)
{
	if (name.size() < suffix.size())
		return false;
	std::string_view tail = name.substr(name.size() - suffix.size());
	for (size_t i = 0; i < tail.size(); i++) {
		char c = tail[i];
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		if (c != suffix[i])
			return false;
	}
	return true;
}

struct NamedCodec {
	const char *suffix;
	const char *shortTarSuffix;
	Codec codec;
};

const NamedCodec kCodecNames[] = {
	{".gz",  ".tgz",  Codec::Gzip},
	{".bz2", ".tbz2", Codec::Bzip2},
	{".bz2", ".tbz",  Codec::Bzip2},
	{".br",  ".tbr",  Codec::Brotli},
	{".xz",  ".txz",  Codec::Xz},
	{".zst", ".tzst", Codec::Zstd},
	{".lz4", ".tlz4", Codec::Lz4},
};

} // namespace

std::optional<Codec> CodecFromMagic(const uint8_t *head, size_t length)
{
	if (StartsWith(head, length, "\x1f\x8b", 2))
		return Codec::Gzip;
	if (StartsWith(head, length, "BZh", 3) && length >= 4 && head[3] >= '1' && head[3] <= '9')
		return Codec::Bzip2;
	if (StartsWith(head, length, "\xfd" "7zXZ\x00", 6))
		return Codec::Xz;
	if (StartsWith(head, length, "\x28\xb5\x2f\xfd", 4))
		return Codec::Zstd;
	if (StartsWith(head, length, "\x04\x22\x4d\x18", 4))
		return Codec::Lz4;
	return std::nullopt;
}

std::optional<Codec> CodecFromName(std::string_view name)
{
	for (const NamedCodec &named : kCodecNames)
		if (EndsWith(name, named.suffix) || EndsWith(name, named.shortTarSuffix))
			return named.codec;
	return std::nullopt;
}

std::optional<Container> ContainerFromMagic(const uint8_t *head, size_t length)
{
	if (StartsWith(head, length, "PK\x03\x04", 4) || StartsWith(head, length, "PK\x05\x06", 4) ||
	    StartsWith(head, length, "PK\x07\x08", 4))
		return Container::Zip;
	if (StartsWith(head, length, "7z\xbc\xaf\x27\x1c", 6))
		return Container::SevenZip;
	if (StartsWith(head, length, "Rar!\x1a\x07\x00", 7) || StartsWith(head, length, "Rar!\x1a\x07\x01\x00", 8))
		return Container::Rar;
	return std::nullopt;
}

bool IsTarHeader(const uint8_t *head, size_t length)
{
	constexpr size_t kMagicOffset = 257;
	return length >= kMagicOffset + 5 && memcmp(head + kMagicOffset, "ustar", 5) == 0;
}

bool NameSaysTar(std::string_view name)
{
	if (EndsWith(name, ".tar"))
		return true;
	for (const NamedCodec &named : kCodecNames) {
		if (EndsWith(name, named.shortTarSuffix))
			return true;
		if (EndsWith(name, named.suffix)) {
			std::string_view inner = name.substr(0, name.size() - strlen(named.suffix));
			if (EndsWith(inner, ".tar"))
				return true;
		}
	}
	return false;
}

const char *ContainerName(Container container)
{
	switch (container) {
	case Container::Tar:      return "tar";
	case Container::Zip:      return "zip";
	case Container::SevenZip: return "7z";
	case Container::Rar:      return "rar";
	}
	return "unknown";
}

} // namespace smarchive
