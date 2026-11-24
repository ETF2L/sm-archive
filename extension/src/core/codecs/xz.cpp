/* Copyright 2026 ETF2L Contributors, Dave
 *
 * xz with liblzma. Files of several xz streams are decompressed whole: after
 * a stream, null bytes in multiples of four are stream padding and a new
 * stream starts with xz's magic; anything else is trailing data. The
 * decoder may use up to kDecoderMemoryLimit, which every preset fits (-9e
 * needs 65 MiB). */
#include "codecs.h"

#include <lzma.h>

namespace smarchive {
namespace codecs {

namespace {

constexpr uint64_t kDecoderMemoryLimit = 128 * kMiB;
constexpr uint8_t kMagicStart = 0xfd;
constexpr uint64_t kPaddingAlignment = 4;

lzma_check CheckOf(XzCheck check)
{
	switch (check) {
	case XzCheck::None:   return LZMA_CHECK_NONE;
	case XzCheck::Crc32:  return LZMA_CHECK_CRC32;
	case XzCheck::Crc64:  return LZMA_CHECK_CRC64;
	case XzCheck::Sha256: return LZMA_CHECK_SHA256;
	}
	return LZMA_CHECK_CRC64;
}

Status LzmaFailure(lzma_ret result)
{
	switch (result) {
	case LZMA_MEM_ERROR:
		return Status::Fail(ErrorCode::Memory, "liblzma ran out of memory", result);
	case LZMA_MEMLIMIT_ERROR:
		return Status::Fail(ErrorCode::Memory, "the xz stream needs more than 128 MiB to decode", result);
	case LZMA_FORMAT_ERROR:
		return Status::Fail(ErrorCode::Corrupt, "the input is no xz stream", result);
	case LZMA_OPTIONS_ERROR:
		return Status::Fail(ErrorCode::Unsupported, "the xz stream uses unsupported options", result);
	case LZMA_DATA_ERROR:
		return Status::Fail(ErrorCode::Corrupt, "the xz data is corrupt", result);
	default:
		return Status::Fail(ErrorCode::Internal, "liblzma failed", result);
	}
}

void Bind(lzma_stream &stream, const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength)
{
	stream.next_in = input;
	stream.avail_in = inputLength;
	stream.next_out = output;
	stream.avail_out = outputLength;
}

class XzEncoder : public Transform {
public:
	~XzEncoder() override { lzma_end(&m_stream); }

	Status Init(const XzOptions &options)
	{
		uint32_t preset = static_cast<uint32_t>(options.preset) | (options.isExtreme ? LZMA_PRESET_EXTREME : 0);
		lzma_ret result = lzma_easy_encoder(&m_stream, preset, CheckOf(options.check));
		return result == LZMA_OK ? Status::Ok() : LzmaFailure(result);
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		Bind(m_stream, input, inputLength, output, outputLength);
		lzma_ret result = lzma_code(&m_stream, isLastInput ? LZMA_FINISH : LZMA_RUN);
		step.consumed = inputLength - m_stream.avail_in;
		step.produced = outputLength - m_stream.avail_out;
		step.isDone = result == LZMA_STREAM_END;
		if (result == LZMA_OK || result == LZMA_STREAM_END || result == LZMA_BUF_ERROR)
			return Status::Ok();
		return LzmaFailure(result);
	}

private:
	lzma_stream m_stream = LZMA_STREAM_INIT;
};

class XzDecoder : public Transform {
public:
	~XzDecoder() override { lzma_end(&m_stream); }

	Status Init()
	{
		lzma_ret result = lzma_stream_decoder(&m_stream, kDecoderMemoryLimit, 0);
		return result == LZMA_OK ? Status::Ok() : LzmaFailure(result);
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		size_t skipped = 0;
		if (m_isAtStreamEnd) {
			Status status = BetweenStreams(input, inputLength, isLastInput, step);
			if (!status.IsOk() || m_isAtStreamEnd)
				return status;
			skipped = step.consumed;
		}

		Bind(m_stream, input + skipped, inputLength - skipped, output, outputLength);
		lzma_ret result = lzma_code(&m_stream, LZMA_RUN);
		step.consumed = inputLength - m_stream.avail_in;
		step.produced = outputLength - m_stream.avail_out;
		if (result == LZMA_STREAM_END) {
			m_isAtStreamEnd = true;
			m_padding = 0;
			step.isDone = isLastInput && step.consumed == inputLength;
			return Status::Ok();
		}
		if (result == LZMA_OK || result == LZMA_BUF_ERROR)
			return Status::Ok();
		return LzmaFailure(result);
	}

private:
	/* Takes stream padding up to the next stream, which it starts. */
	Status BetweenStreams(const uint8_t *input, size_t inputLength, bool isLastInput, TransformStep &step)
	{
		while (step.consumed < inputLength && input[step.consumed] == 0) {
			m_padding++;
			step.consumed++;
		}
		bool isPaddingAligned = m_padding % kPaddingAlignment == 0;
		if (step.consumed == inputLength) {
			if (isLastInput && !isPaddingAligned)
				return Status::Fail(ErrorCode::Corrupt, "the xz stream padding is not a multiple of four bytes");
			step.isDone = isLastInput;
			return Status::Ok();
		}
		if (input[step.consumed] != kMagicStart)
			return Status::Fail(ErrorCode::TrailingData, "data that is no xz stream follows the last stream");
		if (!isPaddingAligned)
			return Status::Fail(ErrorCode::Corrupt, "the xz stream padding is not a multiple of four bytes");
		lzma_end(&m_stream);
		m_stream = LZMA_STREAM_INIT;
		m_isAtStreamEnd = false;
		return Init();
	}

	lzma_stream m_stream = LZMA_STREAM_INIT;
	bool m_isAtStreamEnd = false;
	uint64_t m_padding = 0;
};

} // namespace

Status CreateXzEncoder(const XzOptions &options, std::unique_ptr<Transform> &encoder)
{
	auto created = std::make_unique<XzEncoder>();
	Status status = created->Init(options);
	if (status.IsOk())
		encoder = std::move(created);
	return status;
}

Status CreateXzDecoder(std::unique_ptr<Transform> &decoder)
{
	auto created = std::make_unique<XzDecoder>();
	Status status = created->Init();
	if (status.IsOk())
		decoder = std::move(created);
	return status;
}

std::string XzVersion()
{
	return std::string("liblzma ") + lzma_version_string();
}

} // namespace codecs
} // namespace smarchive
