/* Copyright 2026 ETF2L Contributors, Dave */
#include "codec.h"
#include "codecs/codecs.h"

namespace smarchive {

namespace {

std::string OutOfRange(const char *what, int value, int min, int max)
{
	return std::string(what) + " " + std::to_string(value) + " is outside " + std::to_string(min) + "-" +
	       std::to_string(max);
}

std::string Validate(const GzipOptions &options)
{
	if (options.level < 1 || options.level > 9)
		return OutOfRange("gzip level", options.level, 1, 9);
	return "";
}

std::string Validate(const Bzip2Options &options)
{
	if (options.level < 1 || options.level > 9)
		return OutOfRange("bzip2 level", options.level, 1, 9);
	return "";
}

std::string Validate(const BrotliOptions &options)
{
	if (options.quality < 0 || options.quality > 11)
		return OutOfRange("brotli quality", options.quality, 0, 11);
	bool isWindowValid = options.window == kBrotliAutomaticWindow || (options.window >= 10 && options.window <= 24);
	if (!isWindowValid)
		return "brotli window " + std::to_string(options.window) + " is neither 0 (automatic) nor within 10-24";
	if (options.mode < BrotliMode::Generic || options.mode > BrotliMode::Font)
		return "invalid brotli mode " + std::to_string(static_cast<int>(options.mode));
	return "";
}

std::string Validate(const XzOptions &options)
{
	if (options.preset < 0 || options.preset > 9)
		return OutOfRange("xz preset", options.preset, 0, 9);
	if (options.check < XzCheck::None || options.check > XzCheck::Sha256)
		return "invalid xz check " + std::to_string(static_cast<int>(options.check));
	return "";
}

std::string Validate(const ZstdOptions &options)
{
	if (options.level < 1 || options.level > 22)
		return OutOfRange("zstd level", options.level, 1, 22);
	bool isLongWindowValid = options.longWindow == kZstdNoLongWindow ||
	                         (options.longWindow >= 10 && options.longWindow <= 27);
	if (!isLongWindowValid)
		return "zstd long window " + std::to_string(options.longWindow) + " is neither 0 (off) nor within 10-27";
	return "";
}

std::string Validate(const Lz4Options &options)
{
	if (options.level < 1 || options.level > 12)
		return OutOfRange("lz4 level", options.level, 1, 12);
	if (options.blockSize < Lz4BlockSize::Size64KiB || options.blockSize > Lz4BlockSize::Size4MiB)
		return "invalid lz4 block size " + std::to_string(static_cast<int>(options.blockSize));
	return "";
}

} // namespace

Codec CodecOf(const CodecOptions &options)
{
	return kAllCodecs[options.index()];
}

CodecOptions DefaultOptionsFor(Codec codec)
{
	switch (codec) {
	case Codec::Gzip:   return GzipOptions{};
	case Codec::Bzip2:  return Bzip2Options{};
	case Codec::Brotli: return BrotliOptions{};
	case Codec::Xz:     return XzOptions{};
	case Codec::Zstd:   return ZstdOptions{};
	case Codec::Lz4:    return Lz4Options{};
	}
	return GzipOptions{};
}

std::string ValidateOptions(const CodecOptions &options)
{
	return std::visit([](const auto &typed) { return Validate(typed); }, options);
}

const char *CodecName(Codec codec)
{
	switch (codec) {
	case Codec::Gzip:   return "gzip";
	case Codec::Bzip2:  return "bzip2";
	case Codec::Brotli: return "brotli";
	case Codec::Xz:     return "xz";
	case Codec::Zstd:   return "zstd";
	case Codec::Lz4:    return "lz4";
	}
	return "unknown";
}

const char *CodecExtension(Codec codec)
{
	switch (codec) {
	case Codec::Gzip:   return ".gz";
	case Codec::Bzip2:  return ".bz2";
	case Codec::Brotli: return ".br";
	case Codec::Xz:     return ".xz";
	case Codec::Zstd:   return ".zst";
	case Codec::Lz4:    return ".lz4";
	}
	return "";
}

std::string CodecLibraryVersion(Codec codec)
{
	switch (codec) {
	case Codec::Gzip:   return codecs::GzipVersion();
	case Codec::Bzip2:  return codecs::Bzip2Version();
	case Codec::Brotli: return codecs::BrotliVersion();
	case Codec::Xz:     return codecs::XzVersion();
	case Codec::Zstd:   return codecs::ZstdVersion();
	case Codec::Lz4:    return codecs::Lz4Version();
	}
	return "";
}

Status CreateEncoder(const CodecOptions &options, int64_t inputSize, std::unique_ptr<Transform> &encoder)
{
	switch (CodecOf(options)) {
	case Codec::Gzip:   return codecs::CreateGzipEncoder(std::get<GzipOptions>(options), encoder);
	case Codec::Bzip2:  return codecs::CreateBzip2Encoder(std::get<Bzip2Options>(options), encoder);
	case Codec::Brotli: return codecs::CreateBrotliEncoder(std::get<BrotliOptions>(options), inputSize, encoder);
	case Codec::Xz:     return codecs::CreateXzEncoder(std::get<XzOptions>(options), encoder);
	case Codec::Zstd:   return codecs::CreateZstdEncoder(std::get<ZstdOptions>(options), encoder);
	case Codec::Lz4:    return codecs::CreateLz4Encoder(std::get<Lz4Options>(options), encoder);
	}
	return Status::Fail(ErrorCode::Internal, "unknown codec");
}

Status CreateDecoder(Codec codec, std::unique_ptr<Transform> &decoder)
{
	switch (codec) {
	case Codec::Gzip:   return codecs::CreateGzipDecoder(decoder);
	case Codec::Bzip2:  return codecs::CreateBzip2Decoder(decoder);
	case Codec::Brotli: return codecs::CreateBrotliDecoder(decoder);
	case Codec::Xz:     return codecs::CreateXzDecoder(decoder);
	case Codec::Zstd:   return codecs::CreateZstdDecoder(decoder);
	case Codec::Lz4:    return codecs::CreateLz4Decoder(decoder);
	}
	return Status::Fail(ErrorCode::Internal, "unknown codec");
}

} // namespace smarchive
