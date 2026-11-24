/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The factories of the codec implementations, one file each, called only by
 * codec.cpp with options it has validated. */
#ifndef SMARCHIVE_CORE_CODECS_CODECS_H
#define SMARCHIVE_CORE_CODECS_CODECS_H

#include "../codec.h"

namespace smarchive {
namespace codecs {

Status CreateGzipEncoder(const GzipOptions &options, std::unique_ptr<Transform> &encoder);
Status CreateGzipDecoder(std::unique_ptr<Transform> &decoder);
std::string GzipVersion();

Status CreateBzip2Encoder(const Bzip2Options &options, std::unique_ptr<Transform> &encoder);
Status CreateBzip2Decoder(std::unique_ptr<Transform> &decoder);
std::string Bzip2Version();

Status CreateBrotliEncoder(const BrotliOptions &options, int64_t inputSize, std::unique_ptr<Transform> &encoder);
Status CreateBrotliDecoder(std::unique_ptr<Transform> &decoder);
std::string BrotliVersion();
/* The window the brotli tool chooses for an input of this many bytes (the
 * largest for an unknown size, -1). */
int BrotliAutomaticWindow(int64_t inputSize);

Status CreateXzEncoder(const XzOptions &options, std::unique_ptr<Transform> &encoder);
Status CreateXzDecoder(std::unique_ptr<Transform> &decoder);
std::string XzVersion();

Status CreateZstdEncoder(const ZstdOptions &options, std::unique_ptr<Transform> &encoder);
Status CreateZstdDecoder(std::unique_ptr<Transform> &decoder);
std::string ZstdVersion();

Status CreateLz4Encoder(const Lz4Options &options, std::unique_ptr<Transform> &encoder);
Status CreateLz4Decoder(std::unique_ptr<Transform> &decoder);
std::string Lz4Version();

} // namespace codecs
} // namespace smarchive

#endif // SMARCHIVE_CORE_CODECS_CODECS_H
