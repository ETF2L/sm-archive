/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The path rules, format detection and base64. */
#include "check.h"
#include "base64.h"
#include "detect.h"
#include "paths.h"

#include <cstring>

using namespace smarchive;

namespace {

std::string Normalized(const std::string &stored, ErrorCode expected = ErrorCode::None)
{
	std::string normalized;
	ErrorCode code = NormalizeEntryPath(stored, normalized);
	CHECK(code == expected);
	return normalized;
}

} // namespace

TEST(GamePathsStayBelowTheGameDirectory)
{
	CHECK(IsGameRelativePath("demos/match.dem"));
	CHECK(IsGameRelativePath("a"));
	CHECK(!IsGameRelativePath(""));
	CHECK(!IsGameRelativePath("/etc/passwd"));
	CHECK(!IsGameRelativePath("file://etc/passwd"));
	CHECK(!IsGameRelativePath("a//b"));
	CHECK(!IsGameRelativePath("a/"));
	CHECK(!IsGameRelativePath("./a"));
	CHECK(!IsGameRelativePath("a/../b"));
	CHECK(!IsGameRelativePath(".."));
	CHECK(!IsGameRelativePath("..\\cfg\\server.cfg"));
	CHECK(!IsGameRelativePath("a\\b"));
}

TEST(Utf8IsCheckedStrictly)
{
	CHECK(IsValidUtf8("plain"));
	CHECK(IsValidUtf8("Gr\xc3\xbc\xc3\x9f" "e"));
	CHECK(IsValidUtf8("\xf0\x9f\x8e\xae"));
	CHECK(!IsValidUtf8("caf\xe9"));
	CHECK(!IsValidUtf8("\xc0\xaf"));
	CHECK(!IsValidUtf8("\xed\xa0\x80"));
	CHECK(!IsValidUtf8("\xf4\x90\x80\x80"));
	CHECK(!IsValidUtf8("\xe2\x82"));
}

TEST(EntryPathsAreNormalized)
{
	CHECK(Normalized("./addons//sourcemod/") == "addons/sourcemod");
	CHECK(Normalized("addons\\sourcemod\\plugins\\x.smx") == "addons/sourcemod/plugins/x.smx");
	CHECK(Normalized("./") == "");
	CHECK(Normalized("a/./b") == "a/b");
	Normalized("../x", ErrorCode::UnsafeEntry);
	Normalized("a/../../x", ErrorCode::UnsafeEntry);
	Normalized("a\\..\\x", ErrorCode::UnsafeEntry);
	Normalized("/etc/passwd", ErrorCode::UnsafeEntry);
	Normalized("\\x", ErrorCode::UnsafeEntry);
	Normalized("caf\xe9.txt", ErrorCode::EntryName);
}

TEST(CleanArchivePaths)
{
	CHECK(IsCleanArchivePath(""));
	CHECK(IsCleanArchivePath("logs/L1121.log"));
	CHECK(!IsCleanArchivePath("logs/"));
	CHECK(!IsCleanArchivePath("./logs"));
	CHECK(!IsCleanArchivePath("../logs"));
	CHECK(!IsCleanArchivePath("a\\b"));
}

TEST(TempPathsAreUniquePerJob)
{
	CHECK(UniqueTempPath("demos/a.dem.bz2", 7) == "demos/a.dem.bz2.7.smarchive.tmp");
	CHECK(UniqueTempPath("x", 7) != UniqueTempPath("x", 8));
	CHECK(IsUniqueTempNameOf("a.dem.bz2.7.smarchive.tmp", "a.dem.bz2"));
	CHECK(IsUniqueTempNameOf("a.123.smarchive.tmp", "a"));
	CHECK(!IsUniqueTempNameOf("a..smarchive.tmp", "a"));
	CHECK(!IsUniqueTempNameOf("a.7x.smarchive.tmp", "a"));
	CHECK(!IsUniqueTempNameOf("ab.7.smarchive.tmp", "a"));
	CHECK(!IsUniqueTempNameOf("a.7.smarchive.tmp", "a.7"));
	CHECK(!IsUniqueTempNameOf("a.tmp", "a"));
}

TEST(CodecsAreToldByMagicOrName)
{
	CHECK(CodecFromMagic(reinterpret_cast<const uint8_t *>("\x1f\x8b\x08"), 3) == Codec::Gzip);
	CHECK(CodecFromMagic(reinterpret_cast<const uint8_t *>("BZh9"), 4) == Codec::Bzip2);
	CHECK(!CodecFromMagic(reinterpret_cast<const uint8_t *>("BZh0"), 4));
	CHECK(CodecFromMagic(reinterpret_cast<const uint8_t *>("\xfd" "7zXZ\x00\x00"), 7) == Codec::Xz);
	CHECK(CodecFromMagic(reinterpret_cast<const uint8_t *>("\x28\xb5\x2f\xfd"), 4) == Codec::Zstd);
	CHECK(CodecFromMagic(reinterpret_cast<const uint8_t *>("\x04\x22\x4d\x18"), 4) == Codec::Lz4);
	CHECK(!CodecFromMagic(reinterpret_cast<const uint8_t *>("\x1f"), 1));
	CHECK(CodecFromName("x.dem.BR") == Codec::Brotli);
	CHECK(CodecFromName("x.tgz") == Codec::Gzip);
	CHECK(CodecFromName("x.tbz") == Codec::Bzip2);
	CHECK(!CodecFromName("x.tar"));
}

TEST(ContainersAreToldByMagicOrName)
{
	CHECK(ContainerFromMagic(reinterpret_cast<const uint8_t *>("PK\x03\x04"), 4) == Container::Zip);
	CHECK(ContainerFromMagic(reinterpret_cast<const uint8_t *>("PK\x05\x06"), 4) == Container::Zip);
	CHECK(ContainerFromMagic(reinterpret_cast<const uint8_t *>("7z\xbc\xaf\x27\x1c"), 6) == Container::SevenZip);
	CHECK(ContainerFromMagic(reinterpret_cast<const uint8_t *>("Rar!\x1a\x07\x00"), 7) == Container::Rar);
	CHECK(ContainerFromMagic(reinterpret_cast<const uint8_t *>("Rar!\x1a\x07\x01\x00"), 8) == Container::Rar);
	CHECK(!ContainerFromMagic(reinterpret_cast<const uint8_t *>("PK"), 2));

	uint8_t header[512] = {};
	memcpy(header + 257, "ustar\0", 6);
	CHECK(IsTarHeader(header, sizeof(header)));
	CHECK(!IsTarHeader(header, 100));
	CHECK(NameSaysTar("release.tar"));
	CHECK(NameSaysTar("release.tar.zst"));
	CHECK(NameSaysTar("release.TXZ"));
	CHECK(!NameSaysTar("release.zst"));
	CHECK(!NameSaysTar("tar"));
}

TEST(Base64RoundTrips)
{
	CHECK(EncodeBase64({}) == "");
	CHECK(EncodeBase64({'f'}) == "Zg==");
	CHECK(EncodeBase64({'f', 'o'}) == "Zm8=");
	CHECK(EncodeBase64({'f', 'o', 'o'}) == "Zm9v");
	CHECK(Base64Length(4) == 8);
	std::vector<uint8_t> bytes;
	CHECK(DecodeBase64("Zm9vYmFy", bytes) && bytes == (std::vector<uint8_t>{'f', 'o', 'o', 'b', 'a', 'r'}));
	CHECK(DecodeBase64("Zg==", bytes) && bytes == std::vector<uint8_t>{'f'});
	CHECK(DecodeBase64("", bytes) && bytes.empty());
	CHECK(!DecodeBase64("Zg=", bytes));
	CHECK(!DecodeBase64("Z=g=", bytes));
	CHECK(!DecodeBase64("Zg==Zg==", bytes));
	CHECK(!DecodeBase64("Zm9v YmFy", bytes));
	std::vector<uint8_t> all;
	for (int i = 0; i < 256; i++)
		all.push_back(static_cast<uint8_t>(i));
	CHECK(DecodeBase64(EncodeBase64(all), bytes) && bytes == all);
}
