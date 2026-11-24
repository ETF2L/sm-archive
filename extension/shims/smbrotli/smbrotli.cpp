/* Copyright 2026 ETF2L Contributors, Dave
 *
 * SMbrotli's natives (scripting/include/brotli.inc) on SMArchive. */
#include "shim.h"

namespace {

/* SMbrotli's BrotliError beyond the decoder's codes (-31 to -1). */
enum OldError : cell_t {
	BrotliError_Unreachable = -31,
	BrotliError_None = 0,
	BrotliError_OpenInput,
	BrotliError_OpenOutput,
	BrotliError_OutputExists,
	BrotliError_CreatePath,
	BrotliError_Read,
	BrotliError_Write,
	BrotliError_UnexpectedEOF,
	BrotliError_TrailingData,
	BrotliError_Memory,
	BrotliError_Rename,
};

struct NamedError {
	cell_t value;
	const char *name;
};

const NamedError kErrorNames[] = {
	{0, "BrotliError_None"}, {1, "BrotliError_OpenInput"}, {2, "BrotliError_OpenOutput"},
	{3, "BrotliError_OutputExists"}, {4, "BrotliError_CreatePath"}, {5, "BrotliError_Read"},
	{6, "BrotliError_Write"}, {7, "BrotliError_UnexpectedEOF"}, {8, "BrotliError_TrailingData"},
	{9, "BrotliError_Memory"}, {10, "BrotliError_Rename"},
	{-1, "BrotliError_FormatExuberantNibble"}, {-2, "BrotliError_FormatReserved"},
	{-3, "BrotliError_FormatExuberantMetaNibble"}, {-4, "BrotliError_FormatSimpleHuffmanAlphabet"},
	{-5, "BrotliError_FormatSimpleHuffmanSame"}, {-6, "BrotliError_FormatClSpace"},
	{-7, "BrotliError_FormatHuffmanSpace"}, {-8, "BrotliError_FormatContextMapRepeat"},
	{-9, "BrotliError_FormatBlockLength1"}, {-10, "BrotliError_FormatBlockLength2"},
	{-11, "BrotliError_FormatTransform"}, {-12, "BrotliError_FormatDictionary"},
	{-13, "BrotliError_FormatWindowBits"}, {-14, "BrotliError_FormatPadding1"},
	{-15, "BrotliError_FormatPadding2"}, {-16, "BrotliError_FormatDistance"},
	{-17, "BrotliError_FormatBlockSwitch"}, {-18, "BrotliError_CompoundDictionary"},
	{-19, "BrotliError_DictionaryNotSet"}, {-20, "BrotliError_InvalidArguments"},
	{-21, "BrotliError_AllocContextModes"}, {-22, "BrotliError_AllocTreeGroups"},
	{-25, "BrotliError_AllocContextMap"}, {-26, "BrotliError_AllocRingBuffer1"},
	{-27, "BrotliError_AllocRingBuffer2"}, {-30, "BrotliError_AllocBlockTypeTrees"},
	{-31, "BrotliError_Unreachable"},
};

const char *NameOf(cell_t value)
{
	for (const NamedError &named : kErrorNames)
		if (named.value == value)
			return named.name;
	return nullptr;
}

class BrotliShim : public Shim {
protected:
	const sp_nativeinfo_t *Natives() const override;
	const char *LibraryName() const override { return "brotli"; }

	cell_t OldError(const smarchive_api::Outcome &outcome) const override
	{
		/* A decoder failure carries brotli's own code. */
		bool isDecoderFailure = (outcome.error == 8 || outcome.error == 9) && outcome.libraryCode < 0;
		if (isDecoderFailure)
			return NameOf(outcome.libraryCode) ? outcome.libraryCode : BrotliError_Unreachable;
		/* SMArchive's ArchiveError values. */
		switch (outcome.error) {
		case 0:  return BrotliError_None;
		case 1:  return BrotliError_OpenInput;
		case 2:  return BrotliError_OpenOutput;
		case 3:  return BrotliError_OutputExists;
		case 4:  return BrotliError_CreatePath;
		case 5:  return BrotliError_Read;
		case 6:  return BrotliError_Write;
		case 7:  return BrotliError_Rename;
		case 8:  return BrotliError_Memory;
		case 10: return BrotliError_UnexpectedEOF;
		case 11: return BrotliError_TrailingData;
		/* The limits: the output was not written. */
		case 17: case 18: case 19: case 20:
			return BrotliError_Write;
		default: return BrotliError_Unreachable;
		}
	}
};

BrotliShim g_BrotliShim;

// Brotli.CompressFile(inFile, outFile, callback, data, quality, window, mode, overwrite, createPath)
cell_t Brotli_CompressFile(IPluginContext *context, const cell_t *params)
{
	int quality = params[5], window = params[6], mode = params[7];
	if (quality < 0 || quality > 11)
		return context->ThrowNativeError("Quality %d is outside 0-11", quality);
	if (window != 0 && (window < 10 || window > 24))
		return context->ThrowNativeError("Window %d is neither 0 (automatic) nor within 10-24", window);
	if (mode < 0 || mode > 2)
		return context->ThrowNativeError("Invalid BrotliMode %d", mode);
	smarchive_api::CodecFileRequest request{};
	request.isCompress = true;
	request.codec = smarchive_api::kCodecBrotli;
	request.quality = quality;
	request.window = window;
	request.mode = mode;
	request.overwrite = params[8] != 0;
	request.createPath = params[9] != 0;
	return g_BrotliShim.Queue(context, params, request);
}

// Brotli.DecompressFile(inFile, outFile, callback, data, overwrite, createPath)
cell_t Brotli_DecompressFile(IPluginContext *context, const cell_t *params)
{
	smarchive_api::CodecFileRequest request{};
	request.isCompress = false;
	request.codec = smarchive_api::kCodecBrotli;
	request.quality = 6;
	request.overwrite = params[5] != 0;
	request.createPath = params[6] != 0;
	return g_BrotliShim.Queue(context, params, request);
}

// Brotli.ErrorString(error, buffer, maxlength)
cell_t Brotli_ErrorString(IPluginContext *context, const cell_t *params)
{
	const char *name = NameOf(params[1]);
	if (!name)
		return context->ThrowNativeError("Invalid BrotliError %d", params[1]);
	size_t written;
	context->StringToLocalUTF8(params[2], params[3], name, &written);
	return static_cast<cell_t>(written);
}

const sp_nativeinfo_t kNatives[] = {
	{"Brotli.CompressFile",   Brotli_CompressFile},
	{"Brotli.DecompressFile", Brotli_DecompressFile},
	{"Brotli.ErrorString",    Brotli_ErrorString},
	{nullptr,                 nullptr},
};

const sp_nativeinfo_t *BrotliShim::Natives() const
{
	return kNatives;
}

} // namespace

SMEXT_LINK(&g_BrotliShim);
