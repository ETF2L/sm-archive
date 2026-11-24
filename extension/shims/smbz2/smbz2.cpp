/* Copyright 2026 ETF2L Contributors, Dave
 *
 * SMbz2's natives (scripting/include/bzip2.inc) on SMArchive. */
#include "shim.h"

namespace {

/* SMbz2's BZ2Error. */
enum OldError : cell_t {
	BZ2Error_None = 0,
	BZ2Error_OpenInput,
	BZ2Error_OpenOutput,
	BZ2Error_OutputExists,
	BZ2Error_CreatePath,
	BZ2Error_Read,
	BZ2Error_Write,
	BZ2Error_Data,
	BZ2Error_DataMagic,
	BZ2Error_UnexpectedEOF,
	BZ2Error_Memory,
	BZ2Error_Rename,
};

const char *const kErrorNames[] = {
	"BZ2Error_None", "BZ2Error_OpenInput", "BZ2Error_OpenOutput", "BZ2Error_OutputExists", "BZ2Error_CreatePath",
	"BZ2Error_Read", "BZ2Error_Write", "BZ2Error_Data", "BZ2Error_DataMagic", "BZ2Error_UnexpectedEOF",
	"BZ2Error_Memory", "BZ2Error_Rename",
};

/* libbzip2's BZ_DATA_ERROR_MAGIC: the input is no bzip2 stream. */
constexpr int kDataErrorMagic = -5;

class Bzip2Shim : public Shim {
protected:
	const sp_nativeinfo_t *Natives() const override;
	const char *LibraryName() const override { return "bzip2"; }

	cell_t OldError(const smarchive_api::Outcome &outcome) const override
	{
		/* SMArchive's ArchiveError values. */
		switch (outcome.error) {
		case 0:  return BZ2Error_None;
		case 1:  return BZ2Error_OpenInput;
		case 2:  return BZ2Error_OpenOutput;
		case 3:  return BZ2Error_OutputExists;
		case 4:  return BZ2Error_CreatePath;
		case 5:  return BZ2Error_Read;
		case 6:  return BZ2Error_Write;
		case 7:  return BZ2Error_Rename;
		case 8:  return BZ2Error_Memory;
		case 9:  return outcome.libraryCode == kDataErrorMagic ? BZ2Error_DataMagic : BZ2Error_Data;
		case 10: return BZ2Error_UnexpectedEOF;
		case 11: return BZ2Error_DataMagic;
		/* The limits: the output was not written. */
		case 17: case 18: case 19: case 20:
			return BZ2Error_Write;
		default: return BZ2Error_Data;
		}
	}
};

Bzip2Shim g_Bzip2Shim;

// BZ2.CompressFile(inFile, outFile, callback, data, level, overwrite, createPath)
cell_t BZ2_CompressFile(IPluginContext *context, const cell_t *params)
{
	if (params[5] < 1 || params[5] > 9)
		return context->ThrowNativeError("Level %d is outside 1-9", params[5]);
	smarchive_api::CodecFileRequest request{};
	request.isCompress = true;
	request.codec = smarchive_api::kCodecBzip2;
	request.level = params[5];
	request.overwrite = params[6] != 0;
	request.createPath = params[7] != 0;
	return g_Bzip2Shim.Queue(context, params, request);
}

// BZ2.DecompressFile(inFile, outFile, callback, data, overwrite, createPath)
cell_t BZ2_DecompressFile(IPluginContext *context, const cell_t *params)
{
	smarchive_api::CodecFileRequest request{};
	request.isCompress = false;
	request.codec = smarchive_api::kCodecBzip2;
	request.level = 9;
	request.overwrite = params[5] != 0;
	request.createPath = params[6] != 0;
	return g_Bzip2Shim.Queue(context, params, request);
}

// BZ2.ErrorString(error, buffer, maxlength)
cell_t BZ2_ErrorString(IPluginContext *context, const cell_t *params)
{
	if (params[1] < BZ2Error_None || params[1] > BZ2Error_Rename)
		return context->ThrowNativeError("Invalid BZ2Error %d", params[1]);
	size_t written;
	context->StringToLocalUTF8(params[2], params[3], kErrorNames[params[1]], &written);
	return static_cast<cell_t>(written);
}

const sp_nativeinfo_t kNatives[] = {
	{"BZ2.CompressFile",   BZ2_CompressFile},
	{"BZ2.DecompressFile", BZ2_DecompressFile},
	{"BZ2.ErrorString",    BZ2_ErrorString},
	{nullptr,              nullptr},
};

const sp_nativeinfo_t *Bzip2Shim::Natives() const
{
	return kNatives;
}

} // namespace

SMEXT_LINK(&g_Bzip2Shim);
