/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The natives of ArchiveResult, the handle a job's callback gets: how the
 * job ended, its entries and its memory output. Sizes beyond 2 GiB saturate
 * in the int getters; the ...64 ones give them exactly. */
#include "base64.h"
#include "natives_common.h"

#include <cstring>

using namespace smarchive;
using natives::ReadObject;

namespace {

/* The result's data, or nullptr after a native error. */
const ResultData *ReadResult(IPluginContext *context, cell_t handle)
{
	auto *result = ReadObject<ResultObject>(context, handle, g_SMArchive.resultType, "ArchiveResult");
	return result ? result->data.get() : nullptr;
}

/* The entry at params[2], or nullptr after a native error. */
const EntryRecord *ReadEntry(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	if (!data)
		return nullptr;
	const std::vector<EntryRecord> &entries = data->outcome.entries;
	if (params[2] < 0 || static_cast<size_t>(params[2]) >= entries.size()) {
		context->ReportError("Entry index %d is outside 0-%d", params[2], static_cast<int>(entries.size()) - 1);
		return nullptr;
	}
	return &entries[static_cast<size_t>(params[2])];
}

cell_t Result_Error(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	return data ? static_cast<cell_t>(data->outcome.status.code) : 0;
}

cell_t Result_LibraryCode(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	return data ? static_cast<cell_t>(data->outcome.status.libraryCode) : 0;
}

cell_t Result_Job(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	return data ? static_cast<cell_t>(data->id) : 0;
}

/* CodecFormat_None (1) when the job had no codec. */
cell_t Result_Codec(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	if (!data)
		return 0;
	return data->outcome.codec ? static_cast<cell_t>(*data->outcome.codec) : 1;
}

/* ArchiveFormat_Auto (0) when the job read or wrote no container. */
cell_t Result_Format(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	if (!data)
		return 0;
	return data->outcome.container ? static_cast<cell_t>(*data->outcome.container) : 0;
}

cell_t Result_GetMessage(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	return data ? natives::WriteString(context, params, params[2], params[3], data->outcome.status.message) : 0;
}

cell_t Result_EntryCount(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	return data ? static_cast<cell_t>(data->outcome.entries.size()) : 0;
}

cell_t Result_GetEntryPath(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry ? natives::WriteString(context, params, params[3], params[4], entry->path) : 0;
}

cell_t Result_GetEntryType(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry ? static_cast<cell_t>(entry->type) : 0;
}

cell_t Result_GetEntrySize(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry ? natives::Saturated(entry->size) : 0;
}

cell_t Result_GetEntrySize64(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	if (entry)
		natives::WriteInt64(context, params, params[3], entry->size);
	return 0;
}

cell_t Result_GetEntryPackedSize(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry ? natives::Saturated(entry->packedSize) : 0;
}

cell_t Result_GetEntryPackedSize64(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	if (entry)
		natives::WriteInt64(context, params, params[3], entry->packedSize);
	return 0;
}

cell_t Result_GetEntryTime(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry ? natives::Saturated(entry->mtime) : 0;
}

cell_t Result_IsEntryExecutable(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry && entry->isExecutable ? 1 : 0;
}

cell_t Result_GetEntryLinkTarget(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry ? natives::WriteString(context, params, params[3], params[4], entry->linkTarget) : 0;
}

cell_t Result_IsEntryWritten(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry && entry->isWritten ? 1 : 0;
}

cell_t Result_GetEntrySkip(IPluginContext *context, const cell_t *params)
{
	const EntryRecord *entry = ReadEntry(context, params);
	return entry ? static_cast<cell_t>(entry->skipReason) : 0;
}

cell_t Result_Size(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	return data ? natives::Saturated(static_cast<int64_t>(data->outcome.bytes.size())) : 0;
}

cell_t Result_GetSize64(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	if (data)
		natives::WriteInt64(context, params, params[2], static_cast<int64_t>(data->outcome.bytes.size()));
	return 0;
}

/* The bytes as a string: as many as fit before the terminating null; a
 * null byte in them ends the string early for SourcePawn. */
cell_t Result_GetString(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	if (!data || params[3] <= 0)
		return 0;
	auto *buffer = static_cast<char *>(natives::ResolveSpan(context, params, params[2],
	                                                        static_cast<uint64_t>(params[3]), "buffer"));
	if (!buffer)
		return 0;
	const std::vector<uint8_t> &bytes = data->outcome.bytes;
	size_t count = bytes.size() < static_cast<size_t>(params[3] - 1) ? bytes.size() : static_cast<size_t>(params[3] - 1);
	if (count > 0)
		memcpy(buffer, bytes.data(), count);
	buffer[count] = '\0';
	return static_cast<cell_t>(count);
}

// GetBytes(int[] bytes, int maxlength, int offset = 0): one byte per cell.
cell_t Result_GetBytes(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	if (!data)
		return 0;
	const std::vector<uint8_t> &bytes = data->outcome.bytes;
	if (params[3] < 0)
		return context->ThrowNativeError("Invalid maxlength %d", params[3]);
	cell_t offset = params[4];
	if (offset < 0 || static_cast<size_t>(offset) > bytes.size())
		return context->ThrowNativeError("Offset %d is outside 0-%d", offset, static_cast<int>(bytes.size()));
	auto *cells = static_cast<cell_t *>(natives::ResolveSpan(context, params, params[2],
	                                                         natives::CellBytes(params[3]), "bytes"));
	if (!cells)
		return 0;
	size_t left = bytes.size() - static_cast<size_t>(offset);
	size_t count = left < static_cast<size_t>(params[3]) ? left : static_cast<size_t>(params[3]);
	for (size_t i = 0; i < count; i++)
		cells[i] = bytes[static_cast<size_t>(offset) + i];
	return static_cast<cell_t>(count);
}

cell_t Result_Base64Size(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	return data ? natives::Saturated(static_cast<int64_t>(Base64Length(data->outcome.bytes.size()) + 1)) : 0;
}

cell_t Result_GetBase64(IPluginContext *context, const cell_t *params)
{
	const ResultData *data = ReadResult(context, params[1]);
	if (!data)
		return 0;
	size_t needed = Base64Length(data->outcome.bytes.size()) + 1;
	if (params[3] < 0 || static_cast<size_t>(params[3]) < needed)
		return context->ThrowNativeError("The buffer holds %d bytes, the base64 needs %d (Base64Size)", params[3],
		                                 static_cast<int>(needed));
	return natives::WriteString(context, params, params[2], params[3], EncodeBase64(data->outcome.bytes));
}

cell_t Result_Clone(IPluginContext *context, const cell_t *params)
{
	auto *result = ReadObject<ResultObject>(context, params[1], g_SMArchive.resultType, "ArchiveResult");
	if (!result)
		return 0;
	return natives::CreateHandle(context, g_SMArchive.resultType, new ResultObject{result->data});
}

} // namespace

const sp_nativeinfo_t g_ResultNatives[] = {
	{"ArchiveResult.Error.get",              Result_Error},
	{"ArchiveResult.LibraryCode.get",        Result_LibraryCode},
	{"ArchiveResult.Job.get",                Result_Job},
	{"ArchiveResult.Codec.get",              Result_Codec},
	{"ArchiveResult.Format.get",             Result_Format},
	{"ArchiveResult.GetMessage",             Result_GetMessage},
	{"ArchiveResult.EntryCount.get",         Result_EntryCount},
	{"ArchiveResult.GetEntryPath",           Result_GetEntryPath},
	{"ArchiveResult.GetEntryType",           Result_GetEntryType},
	{"ArchiveResult.GetEntrySize",           Result_GetEntrySize},
	{"ArchiveResult.GetEntrySize64",         Result_GetEntrySize64},
	{"ArchiveResult.GetEntryPackedSize",     Result_GetEntryPackedSize},
	{"ArchiveResult.GetEntryPackedSize64",   Result_GetEntryPackedSize64},
	{"ArchiveResult.GetEntryTime",           Result_GetEntryTime},
	{"ArchiveResult.IsEntryExecutable",      Result_IsEntryExecutable},
	{"ArchiveResult.GetEntryLinkTarget",     Result_GetEntryLinkTarget},
	{"ArchiveResult.IsEntryWritten",         Result_IsEntryWritten},
	{"ArchiveResult.GetEntrySkip",           Result_GetEntrySkip},
	{"ArchiveResult.Size.get",               Result_Size},
	{"ArchiveResult.GetSize64",              Result_GetSize64},
	{"ArchiveResult.GetString",              Result_GetString},
	{"ArchiveResult.GetBytes",               Result_GetBytes},
	{"ArchiveResult.Base64Size.get",         Result_Base64Size},
	{"ArchiveResult.GetBase64",              Result_GetBase64},
	{"ArchiveResult.Clone",                  Result_Clone},
	{nullptr,                                nullptr},
};
