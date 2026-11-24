/* Copyright 2026 ETF2L Contributors, Dave
 *
 * brotli with Google's library, set up like the brotli tool: the automatic
 * window from the input's size and the size as a hint, so the output is
 * the tool's at the same quality. Brotli streams cannot be joined; any byte
 * after the stream fails decompression. */
#include "codecs.h"

#include <brotli/decode.h>
#include <brotli/encode.h>

namespace smarchive {
namespace codecs {

namespace {

constexpr int kMinWindow = BROTLI_MIN_WINDOW_BITS;
constexpr int kMaxWindow = BROTLI_MAX_WINDOW_BITS;

/* The most bytes a window of this many bits reaches back: brotli's
 * BROTLI_MAX_BACKWARD_LIMIT, from an internal header. */
constexpr int64_t MaxBackwardDistance(int window)
{
	return (int64_t{1} << window) - 16;
}

static_assert(static_cast<int>(BrotliMode::Generic) == BROTLI_MODE_GENERIC &&
              static_cast<int>(BrotliMode::Text) == BROTLI_MODE_TEXT &&
              static_cast<int>(BrotliMode::Font) == BROTLI_MODE_FONT, "brotli's encoder modes");

class BrotliEncoder : public Transform {
public:
	~BrotliEncoder() override
	{
		if (m_state)
			BrotliEncoderDestroyInstance(m_state);
	}

	Status Init(const BrotliOptions &options, int64_t inputSize)
	{
		m_state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
		if (!m_state)
			return Status::Fail(ErrorCode::Memory, "brotli cannot create its encoder");
		int window = options.window == kBrotliAutomaticWindow ? BrotliAutomaticWindow(inputSize) : options.window;
		BrotliEncoderSetParameter(m_state, BROTLI_PARAM_QUALITY, static_cast<uint32_t>(options.quality));
		BrotliEncoderSetParameter(m_state, BROTLI_PARAM_LGWIN, static_cast<uint32_t>(window));
		BrotliEncoderSetParameter(m_state, BROTLI_PARAM_MODE, static_cast<uint32_t>(options.mode));
		if (inputSize > 0) {
			constexpr int64_t kMaxSizeHint = int64_t{1} << 30;
			uint32_t sizeHint = static_cast<uint32_t>(inputSize < kMaxSizeHint ? inputSize : kMaxSizeHint);
			BrotliEncoderSetParameter(m_state, BROTLI_PARAM_SIZE_HINT, sizeHint);
		}
		return Status::Ok();
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		const uint8_t *nextIn = input;
		size_t availableIn = inputLength;
		uint8_t *nextOut = output;
		size_t availableOut = outputLength;
		BrotliEncoderOperation operation = isLastInput ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS;
		/* The encoder fails only when it cannot allocate its memory. */
		if (!BrotliEncoderCompressStream(m_state, operation, &availableIn, &nextIn, &availableOut, &nextOut, nullptr))
			return Status::Fail(ErrorCode::Memory, "brotli's encoder failed");
		step.consumed = inputLength - availableIn;
		step.produced = outputLength - availableOut;
		step.isDone = isLastInput && BrotliEncoderIsFinished(m_state) && !BrotliEncoderHasMoreOutput(m_state);
		return Status::Ok();
	}

private:
	BrotliEncoderState *m_state = nullptr;
};

class BrotliDecoder : public Transform {
public:
	~BrotliDecoder() override
	{
		if (m_state)
			BrotliDecoderDestroyInstance(m_state);
	}

	Status Init()
	{
		m_state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
		if (!m_state)
			return Status::Fail(ErrorCode::Memory, "brotli cannot create its decoder");
		return Status::Ok();
	}

	Status Process(const uint8_t *input, size_t inputLength, uint8_t *output, size_t outputLength,
	               bool isLastInput, TransformStep &step) override
	{
		const uint8_t *nextIn = input;
		size_t availableIn = inputLength;
		uint8_t *nextOut = output;
		size_t availableOut = outputLength;
		BrotliDecoderResult result =
			BrotliDecoderDecompressStream(m_state, &availableIn, &nextIn, &availableOut, &nextOut, nullptr);
		step.consumed = inputLength - availableIn;
		step.produced = outputLength - availableOut;
		step.isDone = result == BROTLI_DECODER_RESULT_SUCCESS;
		if (result != BROTLI_DECODER_RESULT_ERROR)
			return Status::Ok();

		BrotliDecoderErrorCode code = BrotliDecoderGetErrorCode(m_state);
		std::string message = std::string("brotli: ") + BrotliDecoderErrorString(code);
		/* The allocation failures are -30 to -21. */
		bool isAllocation = code <= BROTLI_DECODER_ERROR_ALLOC_CONTEXT_MODES &&
		                    code >= BROTLI_DECODER_ERROR_ALLOC_BLOCK_TYPE_TREES;
		return Status::Fail(isAllocation ? ErrorCode::Memory : ErrorCode::Corrupt, message, code);
	}

private:
	BrotliDecoderState *m_state = nullptr;
};

} // namespace

int BrotliAutomaticWindow(int64_t inputSize)
{
	if (inputSize < 0)
		return kMaxWindow;
	int window = kMinWindow;
	while (window < kMaxWindow && MaxBackwardDistance(window) < inputSize)
		window++;
	return window;
}

Status CreateBrotliEncoder(const BrotliOptions &options, int64_t inputSize, std::unique_ptr<Transform> &encoder)
{
	auto created = std::make_unique<BrotliEncoder>();
	Status status = created->Init(options, inputSize);
	if (status.IsOk())
		encoder = std::move(created);
	return status;
}

Status CreateBrotliDecoder(std::unique_ptr<Transform> &decoder)
{
	auto created = std::make_unique<BrotliDecoder>();
	Status status = created->Init();
	if (status.IsOk())
		decoder = std::move(created);
	return status;
}

std::string BrotliVersion()
{
	uint32_t version = BrotliEncoderVersion();
	return "brotli " + std::to_string(version >> 24) + "." + std::to_string((version >> 12) & 0xfff) + "." +
	       std::to_string(version & 0xfff);
}

} // namespace codecs
} // namespace smarchive
