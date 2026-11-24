/* Copyright 2026 ETF2L Contributors, Dave
 *
 * bzip2 with libbzip2. Files of several bzip2 streams one after the other
 * (pbzip2, joined with cat) are decompressed whole. */
#include "codecs.h"

#include <bzlib.h>

namespace smarchive {
namespace codecs {

namespace {

void Bind(bz_stream &stream, const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength)
{
	stream.next_in = reinterpret_cast<char *>(const_cast<uint8_t *>(input));
	stream.avail_in = static_cast<unsigned int>(inputLength);
	stream.next_out = reinterpret_cast<char *>(output);
	stream.avail_out = static_cast<unsigned int>(outputLength);
}

Status Bzip2Failure(int result)
{
	switch (result) {
	case BZ_MEM_ERROR:
		return Status::Fail(ErrorCode::Memory, "libbzip2 ran out of memory", result);
	case BZ_DATA_ERROR:
		return Status::Fail(ErrorCode::Corrupt, "the bzip2 data is corrupt", result);
	case BZ_DATA_ERROR_MAGIC:
		return Status::Fail(ErrorCode::Corrupt, "the input is no bzip2 stream", result);
	default:
		return Status::Fail(ErrorCode::Internal, "libbzip2 failed", result);
	}
}

class Bzip2Encoder : public Transform {
public:
	~Bzip2Encoder() override { BZ2_bzCompressEnd(&m_stream); }

	Status Init(int level)
	{
		int result = BZ2_bzCompressInit(&m_stream, level, 0, 0);
		return result == BZ_OK ? Status::Ok() : Bzip2Failure(result);
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		Bind(m_stream, input, inputLength, output, outputLength);
		int result = BZ2_bzCompress(&m_stream, isLastInput ? BZ_FINISH : BZ_RUN);
		step.consumed = inputLength - m_stream.avail_in;
		step.produced = outputLength - m_stream.avail_out;
		step.isDone = result == BZ_STREAM_END;
		if (result == BZ_RUN_OK || result == BZ_FINISH_OK || result == BZ_STREAM_END)
			return Status::Ok();
		return Bzip2Failure(result);
	}

private:
	bz_stream m_stream{};
};

class Bzip2Decoder : public Transform {
public:
	~Bzip2Decoder() override
	{
		if (m_isInitialized)
			BZ2_bzDecompressEnd(&m_stream);
	}

	Status Init()
	{
		int result = BZ2_bzDecompressInit(&m_stream, 0, 0);
		m_isInitialized = result == BZ_OK;
		return m_isInitialized ? Status::Ok() : Bzip2Failure(result);
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		if (m_isAtStreamEnd) {
			if (inputLength == 0) {
				step.isDone = isLastInput;
				return Status::Ok();
			}
			if (input[0] != 'B')
				return Status::Fail(ErrorCode::TrailingData, "data that is no bzip2 stream follows the last stream");
			BZ2_bzDecompressEnd(&m_stream);
			m_isInitialized = false;
			Status status = Init();
			if (!status.IsOk())
				return status;
			m_isAtStreamEnd = false;
		}

		Bind(m_stream, input, inputLength, output, outputLength);
		int result = BZ2_bzDecompress(&m_stream);
		step.consumed = inputLength - m_stream.avail_in;
		step.produced = outputLength - m_stream.avail_out;
		if (result == BZ_STREAM_END) {
			m_isAtStreamEnd = true;
			step.isDone = isLastInput && step.consumed == inputLength;
			return Status::Ok();
		}
		if (result == BZ_OK)
			return Status::Ok();
		return Bzip2Failure(result);
	}

private:
	bz_stream m_stream{};
	bool m_isInitialized = false;
	bool m_isAtStreamEnd = false;
};

} // namespace

Status CreateBzip2Encoder(const Bzip2Options &options, std::unique_ptr<Transform> &encoder)
{
	auto created = std::make_unique<Bzip2Encoder>();
	Status status = created->Init(options.level);
	if (status.IsOk())
		encoder = std::move(created);
	return status;
}

Status CreateBzip2Decoder(std::unique_ptr<Transform> &decoder)
{
	auto created = std::make_unique<Bzip2Decoder>();
	Status status = created->Init();
	if (status.IsOk())
		decoder = std::move(created);
	return status;
}

std::string Bzip2Version()
{
	std::string version = BZ2_bzlibVersion();
	return "libbzip2 " + version.substr(0, version.find(','));
}

} // namespace codecs
} // namespace smarchive
