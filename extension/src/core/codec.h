/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The stream codecs: their formats, their typed options with the ranges the
 * libraries take, and the factories of their encoders and decoders. Adding
 * a codec means a value here, an options struct, a file in codecs/ and its
 * entries in codec.cpp; everything else (files, memory, limits, progress,
 * tar on top) comes from the core. */
#ifndef SMARCHIVE_CORE_CODEC_H
#define SMARCHIVE_CORE_CODEC_H

#include "stream.h"

#include <memory>
#include <string>
#include <variant>

namespace smarchive {

/* The values of the plugin API's CodecFormat (0 is Auto, 1 None). */
enum class Codec : int {
	Gzip = 2,
	Bzip2,
	Brotli,
	Xz,
	Zstd,
	Lz4,
};

constexpr Codec kAllCodecs[] = {Codec::Gzip, Codec::Bzip2, Codec::Brotli, Codec::Xz, Codec::Zstd, Codec::Lz4};

struct GzipOptions {
	int level = 6;
};

struct Bzip2Options {
	int level = 9;
};

/* The values of brotli's BrotliEncoderMode. */
enum class BrotliMode : int {
	Generic = 0,
	Text = 1,
	Font = 2,
};

constexpr int kBrotliAutomaticWindow = 0;

struct BrotliOptions {
	int quality = 6;
	/* In bits, or kBrotliAutomaticWindow for one sized from the input. */
	int window = kBrotliAutomaticWindow;
	BrotliMode mode = BrotliMode::Generic;
};

enum class XzCheck : int {
	None = 0,
	Crc32,
	Crc64,
	Sha256,
};

struct XzOptions {
	int preset = 6;
	bool isExtreme = false;
	XzCheck check = XzCheck::Crc64;
};

constexpr int kZstdNoLongWindow = 0;

struct ZstdOptions {
	int level = 3;
	/* Long distance matching's window in bits, or kZstdNoLongWindow. */
	int longWindow = kZstdNoLongWindow;
	bool hasChecksum = true;
};

/* The values of lz4's LZ4F_blockSizeID_t less 4. */
enum class Lz4BlockSize : int {
	Size64KiB = 0,
	Size256KiB,
	Size1MiB,
	Size4MiB,
};

struct Lz4Options {
	int level = 1;
	Lz4BlockSize blockSize = Lz4BlockSize::Size4MiB;
	bool hasContentChecksum = true;
};

/* The alternatives in the order of Codec. */
using CodecOptions = std::variant<GzipOptions, Bzip2Options, BrotliOptions, XzOptions, ZstdOptions, Lz4Options>;

Codec CodecOf(const CodecOptions &options);

/* The default options of a codec. */
CodecOptions DefaultOptionsFor(Codec codec);

/* Why the options are out of the codec's ranges, empty when they are fine. */
std::string ValidateOptions(const CodecOptions &options);

/* "gzip" and so on. */
const char *CodecName(Codec codec);

/* The file name extension, with the dot: ".gz" and so on. */
const char *CodecExtension(Codec codec);

/* The library and its version, e.g. "zlib-ng 2.3.3". */
std::string CodecLibraryVersion(Codec codec);

/* An encoder for valid options. inputSize is the input's size in bytes, or
 * -1 when unknown; codecs that size their window from it use it. */
Status CreateEncoder(const CodecOptions &options, int64_t inputSize, std::unique_ptr<Transform> &encoder);

Status CreateDecoder(Codec codec, std::unique_ptr<Transform> &decoder);

} // namespace smarchive

#endif // SMARCHIVE_CORE_CODEC_H
