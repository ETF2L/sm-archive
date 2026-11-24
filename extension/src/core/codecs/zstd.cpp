/* Copyright 2026 ETF2L Contributors, Dave
 *
 * zstd with Facebook's library. Files of several frames are decompressed
 * whole; frames whose window exceeds 2^27 bytes (the zstd tool's limit
 * without --memory) are refused. */
#include "codecs.h"

#include <zstd.h>
#include <zstd_errors.h>

namespace smarchive {
namespace codecs {

namespace {

constexpr int kMaxDecoderWindowLog = 27;

Status ZstdFailure(size_t result)
{
	ZSTD_ErrorCode code = ZSTD_getErrorCode(result);
	std::string message = std::string("zstd: ") + ZSTD_getErrorName(result);
	switch (code) {
	case ZSTD_error_memory_allocation:
		return Status::Fail(ErrorCode::Memory, message, code);
	case ZSTD_error_frameParameter_windowTooLarge:
		return Status::Fail(ErrorCode::Unsupported, message, code);
	default:
		return Status::Fail(ErrorCode::Corrupt, message, code);
	}
}

class ZstdEncoder : public Transform {
public:
	~ZstdEncoder() override { ZSTD_freeCCtx(m_context); }

	Status Init(const ZstdOptions &options)
	{
		m_context = ZSTD_createCCtx();
		if (!m_context)
			return Status::Fail(ErrorCode::Memory, "zstd cannot create its encoder");
		size_t result = ZSTD_CCtx_setParameter(m_context, ZSTD_c_compressionLevel, options.level);
		if (!ZSTD_isError(result))
			result = ZSTD_CCtx_setParameter(m_context, ZSTD_c_checksumFlag, options.hasChecksum ? 1 : 0);
		if (!ZSTD_isError(result) && options.longWindow != kZstdNoLongWindow) {
			result = ZSTD_CCtx_setParameter(m_context, ZSTD_c_enableLongDistanceMatching, 1);
			if (!ZSTD_isError(result))
				result = ZSTD_CCtx_setParameter(m_context, ZSTD_c_windowLog, options.longWindow);
		}
		return ZSTD_isError(result) ? ZstdFailure(result) : Status::Ok();
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		ZSTD_inBuffer in{input, inputLength, 0};
		ZSTD_outBuffer out{output, outputLength, 0};
		size_t remaining = ZSTD_compressStream2(m_context, &out, &in, isLastInput ? ZSTD_e_end : ZSTD_e_continue);
		if (ZSTD_isError(remaining))
			return ZstdFailure(remaining);
		step.consumed = in.pos;
		step.produced = out.pos;
		step.isDone = isLastInput && remaining == 0;
		return Status::Ok();
	}

private:
	ZSTD_CCtx *m_context = nullptr;
};

class ZstdDecoder : public Transform {
public:
	~ZstdDecoder() override { ZSTD_freeDCtx(m_context); }

	Status Init()
	{
		m_context = ZSTD_createDCtx();
		if (!m_context)
			return Status::Fail(ErrorCode::Memory, "zstd cannot create its decoder");
		size_t result = ZSTD_DCtx_setParameter(m_context, ZSTD_d_windowLogMax, kMaxDecoderWindowLog);
		return ZSTD_isError(result) ? ZstdFailure(result) : Status::Ok();
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		if (m_isAtFrameEnd && inputLength == 0) {
			step.isDone = isLastInput;
			return Status::Ok();
		}
		ZSTD_inBuffer in{input, inputLength, 0};
		ZSTD_outBuffer out{output, outputLength, 0};
		size_t hint = ZSTD_decompressStream(m_context, &out, &in);
		if (ZSTD_isError(hint)) {
			if (m_hasFinishedAFrame && m_isAtFrameEnd)
				return Status::Fail(ErrorCode::TrailingData, "data that is no zstd frame follows the last frame",
				                    ZSTD_getErrorCode(hint));
			return ZstdFailure(hint);
		}
		step.consumed = in.pos;
		step.produced = out.pos;
		m_isAtFrameEnd = hint == 0;
		m_hasFinishedAFrame = m_hasFinishedAFrame || m_isAtFrameEnd;
		step.isDone = m_isAtFrameEnd && isLastInput && in.pos == inputLength;
		return Status::Ok();
	}

private:
	ZSTD_DCtx *m_context = nullptr;
	bool m_isAtFrameEnd = false;
	bool m_hasFinishedAFrame = false;
};

} // namespace

Status CreateZstdEncoder(const ZstdOptions &options, std::unique_ptr<Transform> &encoder)
{
	auto created = std::make_unique<ZstdEncoder>();
	Status status = created->Init(options);
	if (status.IsOk())
		encoder = std::move(created);
	return status;
}

Status CreateZstdDecoder(std::unique_ptr<Transform> &decoder)
{
	auto created = std::make_unique<ZstdDecoder>();
	Status status = created->Init();
	if (status.IsOk())
		decoder = std::move(created);
	return status;
}

std::string ZstdVersion()
{
	return std::string("zstd ") + ZSTD_versionString();
}

} // namespace codecs
} // namespace smarchive
