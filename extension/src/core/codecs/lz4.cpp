/* Copyright 2026 ETF2L Contributors, Dave
 *
 * lz4 in its frame format (.lz4, as the lz4 tool writes it) with lz4's
 * library. Levels below 3 use the fast compressor, 3 to 12 the high
 * compression one. Files of several frames are decompressed whole. */
#include "codecs.h"

#include <lz4.h>
#include <lz4frame.h>

#include <cstring>
#include <vector>

namespace smarchive {
namespace codecs {

namespace {

/* The input taken per LZ4F_compressUpdate(); its bound sizes the buffer. */
constexpr size_t kEncoderInputStep = 64 * 1024;

Status Lz4Failure(size_t result, ErrorCode code)
{
	return Status::Fail(code, std::string("lz4: ") + LZ4F_getErrorName(result), static_cast<int>(-result));
}

/* LZ4F's compressor writes whole blocks and needs room for its worst case,
 * so its output goes through a buffer of its own first. */
class Lz4Encoder : public Transform {
public:
	~Lz4Encoder() override { LZ4F_freeCompressionContext(m_context); }

	Status Init(const Lz4Options &options)
	{
		size_t result = LZ4F_createCompressionContext(&m_context, LZ4F_VERSION);
		if (LZ4F_isError(result))
			return Lz4Failure(result, ErrorCode::Memory);
		m_preferences.frameInfo.blockSizeID =
			static_cast<LZ4F_blockSizeID_t>(LZ4F_max64KB + static_cast<int>(options.blockSize));
		m_preferences.frameInfo.contentChecksumFlag =
			options.hasContentChecksum ? LZ4F_contentChecksumEnabled : LZ4F_noContentChecksum;
		m_preferences.compressionLevel = options.level;
		m_pending.resize(LZ4F_compressBound(kEncoderInputStep, &m_preferences) + LZ4F_HEADER_SIZE_MAX);
		return Status::Ok();
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		if (m_pendingStart == m_pendingEnd) {
			Status status = Refill(input, inputLength, isLastInput, step);
			if (!status.IsOk())
				return status;
		}
		size_t pending = m_pendingEnd - m_pendingStart;
		step.produced = pending < outputLength ? pending : outputLength;
		memcpy(output, m_pending.data() + m_pendingStart, step.produced);
		m_pendingStart += step.produced;
		step.isDone = m_hasEnded && m_pendingStart == m_pendingEnd;
		return Status::Ok();
	}

private:
	/* Puts the next piece of the frame into the empty buffer. */
	Status Refill(const uint8_t *input, size_t inputLength, bool isLastInput, TransformStep &step)
	{
		size_t written;
		if (!m_hasBegun) {
			written = LZ4F_compressBegin(m_context, m_pending.data(), m_pending.size(), &m_preferences);
			m_hasBegun = true;
		} else if (inputLength > 0) {
			step.consumed = inputLength < kEncoderInputStep ? inputLength : kEncoderInputStep;
			written = LZ4F_compressUpdate(m_context, m_pending.data(), m_pending.size(), input, step.consumed,
			                              nullptr);
		} else if (isLastInput && !m_hasEnded) {
			written = LZ4F_compressEnd(m_context, m_pending.data(), m_pending.size(), nullptr);
			m_hasEnded = true;
		} else {
			written = 0;
		}
		if (LZ4F_isError(written))
			return Lz4Failure(written, ErrorCode::Internal);
		m_pendingStart = 0;
		m_pendingEnd = written;
		return Status::Ok();
	}

	LZ4F_cctx *m_context = nullptr;
	LZ4F_preferences_t m_preferences{};
	std::vector<uint8_t> m_pending;
	size_t m_pendingStart = 0;
	size_t m_pendingEnd = 0;
	bool m_hasBegun = false;
	bool m_hasEnded = false;
};

class Lz4Decoder : public Transform {
public:
	~Lz4Decoder() override { LZ4F_freeDecompressionContext(m_context); }

	Status Init()
	{
		size_t result = LZ4F_createDecompressionContext(&m_context, LZ4F_VERSION);
		return LZ4F_isError(result) ? Lz4Failure(result, ErrorCode::Memory) : Status::Ok();
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		if (m_isAtFrameEnd && inputLength == 0) {
			step.isDone = isLastInput;
			return Status::Ok();
		}
		size_t produced = outputLength;
		size_t consumed = inputLength;
		size_t hint = LZ4F_decompress(m_context, output, &produced, input, &consumed, nullptr);
		if (LZ4F_isError(hint)) {
			if (m_hasFinishedAFrame && m_isAtFrameEnd)
				return Status::Fail(ErrorCode::TrailingData, "data that is no lz4 frame follows the last frame",
				                    static_cast<int>(-hint));
			return Lz4Failure(hint, ErrorCode::Corrupt);
		}
		step.consumed = consumed;
		step.produced = produced;
		m_isAtFrameEnd = hint == 0;
		m_hasFinishedAFrame = m_hasFinishedAFrame || m_isAtFrameEnd;
		step.isDone = m_isAtFrameEnd && isLastInput && consumed == inputLength;
		return Status::Ok();
	}

private:
	LZ4F_dctx *m_context = nullptr;
	bool m_isAtFrameEnd = false;
	bool m_hasFinishedAFrame = false;
};

} // namespace

Status CreateLz4Encoder(const Lz4Options &options, std::unique_ptr<Transform> &encoder)
{
	auto created = std::make_unique<Lz4Encoder>();
	Status status = created->Init(options);
	if (status.IsOk())
		encoder = std::move(created);
	return status;
}

Status CreateLz4Decoder(std::unique_ptr<Transform> &decoder)
{
	auto created = std::make_unique<Lz4Decoder>();
	Status status = created->Init();
	if (status.IsOk())
		decoder = std::move(created);
	return status;
}

std::string Lz4Version()
{
	return std::string("lz4 ") + LZ4_versionString();
}

} // namespace codecs
} // namespace smarchive
