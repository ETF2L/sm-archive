/* Copyright 2026 ETF2L Contributors, Dave
 *
 * Telling formats apart by their first bytes, and by the file name where a
 * format has no magic (brotli, old tar). Pure functions. */
#ifndef SMARCHIVE_CORE_DETECT_H
#define SMARCHIVE_CORE_DETECT_H

#include "codec.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace smarchive {

/* The values of the plugin API's ArchiveFormat (0 is Auto). */
enum class Container : int {
	Tar = 1,
	Zip,
	SevenZip,
	Rar,
};

/* Enough bytes for every magic, tar's at offset 257 included. */
constexpr size_t kDetectHeadSize = 512;

std::optional<Codec> CodecFromMagic(const uint8_t *head, size_t length);

/* By the last extension: ".gz", ".tgz", ".br", ".tbr" and so on. */
std::optional<Codec> CodecFromName(std::string_view name);

/* zip, 7z and RAR (4 and 5); tar is told by IsTarHeader(). */
std::optional<Container> ContainerFromMagic(const uint8_t *head, size_t length);

/* A ustar or GNU tar header: "ustar" at offset 257. */
bool IsTarHeader(const uint8_t *head, size_t length);

/* ".tar", ".tar.<codec>" or one of the short forms (".tgz", ...). */
bool NameSaysTar(std::string_view name);

/* "tar", "zip", "7z", "rar". */
const char *ContainerName(Container container);

} // namespace smarchive

#endif // SMARCHIVE_CORE_DETECT_H
