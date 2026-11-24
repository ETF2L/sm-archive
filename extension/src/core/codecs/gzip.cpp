/* Copyright 2026 ETF2L Contributors, Dave
 *
 * gzip with zlib-ng in its zlib-compatible mode. The header has no file name
 * and an mtime of 0, as gzip -n writes it, so the same input always gives
 * the same bytes. Files of several gzip members are decompressed whole. */
#include "codecs.h"

#include <zlib.h>

#include <cstring>

namespace smarchive {
namespace codecs {

namespace {

/* deflate's and inflate's windowBits for a gzip wrapper. */
constexpr int kGzipWindowBits = 15 + 16;
constexpr int kDefaultMemoryLevel = 8;

void Bind(z_stream &stream, const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength)
{
	stream.next_in = const_cast<Bytef *>(input);
	stream.avail_in = static_cast<uInt>(inputLength);
	stream.next_out = output;
	stream.avail_out = static_cast<uInt>(outputLength);
}

Status ZlibFailure(int result, const z_stream &stream)
{
	std::string message = stream.msg ? stream.msg : "zlib failed";
	if (result == Z_MEM_ERROR)
		return Status::Fail(ErrorCode::Memory, message, result);
	if (result == Z_DATA_ERROR)
		return Status::Fail(ErrorCode::Corrupt, message, result);
	return Status::Fail(ErrorCode::Internal, message, result);
}

class GzipEncoder : public Transform {
public:
	~GzipEncoder() override { deflateEnd(&m_stream); }

	Status Init(int level)
	{
		int result = deflateInit2(&m_stream, level, Z_DEFLATED, kGzipWindowBits, kDefaultMemoryLevel,
		                          Z_DEFAULT_STRATEGY);
		return result == Z_OK ? Status::Ok() : ZlibFailure(result, m_stream);
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		Bind(m_stream, input, inputLength, output, outputLength);
		int result = deflate(&m_stream, isLastInput ? Z_FINISH : Z_NO_FLUSH);
		step.consumed = inputLength - m_stream.avail_in;
		step.produced = outputLength - m_stream.avail_out;
		step.isDone = result == Z_STREAM_END;
		if (result == Z_OK || result == Z_STREAM_END || result == Z_BUF_ERROR)
			return Status::Ok();
		return ZlibFailure(result, m_stream);
	}

private:
	z_stream m_stream{};
};

class GzipDecoder : public Transform {
public:
	~GzipDecoder() override { inflateEnd(&m_stream); }

	Status Init()
	{
		int result = inflateInit2(&m_stream, kGzipWindowBits);
		return result == Z_OK ? Status::Ok() : ZlibFailure(result, m_stream);
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		if (m_isAtMemberEnd) {
			if (inputLength == 0) {
				step.isDone = isLastInput;
				return Status::Ok();
			}
			if (!StartsLikeAMember(input, inputLength))
				return Status::Fail(ErrorCode::TrailingData, "data that is no gzip member follows the last member");
			inflateReset(&m_stream);
			m_isAtMemberEnd = false;
		}

		Bind(m_stream, input, inputLength, output, outputLength);
		int result = inflate(&m_stream, Z_NO_FLUSH);
		step.consumed = inputLength - m_stream.avail_in;
		step.produced = outputLength - m_stream.avail_out;
		if (result == Z_STREAM_END) {
			m_isAtMemberEnd = true;
			step.isDone = isLastInput && step.consumed == inputLength;
			return Status::Ok();
		}
		if (result == Z_OK || result == Z_BUF_ERROR)
			return Status::Ok();
		return ZlibFailure(result, m_stream);
	}

private:
	/* gzip's magic is 1f 8b; with one byte at hand only the first is known. */
	static bool StartsLikeAMember(const uint8_t *input, size_t inputLength)
	{
		return input[0] == 0x1f && (inputLength < 2 || input[1] == 0x8b);
	}

	z_stream m_stream{};
	bool m_isAtMemberEnd = false;
};

} // namespace

Status CreateGzipEncoder(const GzipOptions &options, std::unique_ptr<Transform> &encoder)
{
	auto created = std::make_unique<GzipEncoder>();
	Status status = created->Init(options.level);
	if (status.IsOk())
		encoder = std::move(created);
	return status;
}

Status CreateGzipDecoder(std::unique_ptr<Transform> &decoder)
{
	auto created = std::make_unique<GzipDecoder>();
	Status status = created->Init();
	if (status.IsOk())
		decoder = std::move(created);
	return status;
}

std::string GzipVersion()
{
	return std::string("zlib-ng ") + ZLIBNG_VERSION;
}

} // namespace codecs
} // namespace smarchive
