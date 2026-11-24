/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The natives that queue jobs (Compression.* and Archive.*), cancel them
 * and name errors. Every argument is checked here; a bad one throws a
 * native error and nothing is queued. Each returns the job's id. */
#include "base64.h"
#include "natives_common.h"
#include "paths.h"

#include <cstring>

using namespace smarchive;
using natives::GamePath;
using natives::ReadObject;

namespace {

/* The plugin API's CodecFormat values beyond the codecs. */
constexpr cell_t kCodecFormatAuto = 0;
constexpr cell_t kCodecFormatNone = 1;
constexpr cell_t kArchiveFormatAuto = 0;

/* Arguments every job takes. */
struct CommonArguments {
	cell_t callback;
	cell_t data;
	cell_t serial;
	cell_t priority;
	cell_t limits;
	cell_t progress;
};

/* Fills what every request has from the common arguments; limits of 0
 * means the server's defaults. */
bool ReadCommon(IPluginContext *context, const CommonArguments &arguments, JobRequest &request)
{
	IPluginFunction *callback;
	if (!context->GetFunctionByIdOrNull(static_cast<funcid_t>(arguments.callback), &callback) || !callback) {
		context->ReportError("Invalid callback function %x", arguments.callback);
		return false;
	}
	request.callbacks.callback = static_cast<funcid_t>(arguments.callback);
	request.callbacks.data = arguments.data;
	request.isSerial = arguments.serial != 0;
	if (arguments.priority != static_cast<cell_t>(Priority::Normal) &&
	    arguments.priority != static_cast<cell_t>(Priority::High)) {
		context->ReportError("Invalid ArchivePriority %d", arguments.priority);
		return false;
	}
	request.priority = static_cast<Priority>(arguments.priority);
	request.job.limits = g_SMArchive.DefaultLimits();
	if (arguments.limits != 0) {
		auto *limits = ReadObject<Limits>(context, arguments.limits, g_SMArchive.limitsType, "ArchiveLimits");
		if (!limits)
			return false;
		request.job.limits = *limits;
	}
	return natives::ReadOptionalFunction(context, arguments.progress, request.callbacks.hasProgressCallback,
	                                     request.callbacks.progressCallback);
}

cell_t Queue(IPluginContext *context, JobRequest request)
{
	return static_cast<cell_t>(g_SMArchive.Submit(context, std::move(request)));
}

OutputFile OutputFor(const GamePath &path, cell_t overwrite, cell_t createPath)
{
	return OutputFile{path.resolved, std::nullopt, overwrite != 0, createPath != 0};
}

bool ReadCodecOptions(IPluginContext *context, cell_t handle, CodecOptions &options)
{
	auto *read = ReadObject<CodecOptions>(context, handle, g_SMArchive.codecOptionsType, "CodecOptions");
	if (read)
		options = *read;
	return read != nullptr;
}

/* A CodecFormat to decompress with: Auto detects, None is refused. */
bool ReadDecodeFormat(IPluginContext *context, cell_t value, std::optional<Codec> &codec)
{
	if (value == kCodecFormatAuto) {
		codec.reset();
		return true;
	}
	if (value < static_cast<cell_t>(Codec::Gzip) || value > static_cast<cell_t>(Codec::Lz4)) {
		context->ReportError("Invalid CodecFormat %d for decompression", value);
		return false;
	}
	codec = static_cast<Codec>(value);
	return true;
}

/* ArchiveFormat and, for tar, CodecFormat (Auto detects, None is plain). */
bool ReadArchiveChoice(IPluginContext *context, cell_t format, cell_t codec, ArchiveChoice &choice)
{
	bool isCodecGiven = codec != kCodecFormatAuto && codec != kCodecFormatNone;
	if (isCodecGiven && (codec < static_cast<cell_t>(Codec::Gzip) || codec > static_cast<cell_t>(Codec::Lz4))) {
		context->ReportError("Invalid CodecFormat %d", codec);
		return false;
	}
	if (format == kArchiveFormatAuto) {
		if (codec != kCodecFormatAuto) {
			context->ReportError("A CodecFormat other than Auto needs ArchiveFormat_Tar");
			return false;
		}
		choice = ArchiveChoice{};
		return true;
	}
	if (format < static_cast<cell_t>(Container::Tar) || format > static_cast<cell_t>(Container::Rar)) {
		context->ReportError("Invalid ArchiveFormat %d", format);
		return false;
	}
	Container container = static_cast<Container>(format);
	if (container != Container::Tar && isCodecGiven) {
		context->ReportError("Only ArchiveFormat_Tar is read through a codec");
		return false;
	}
	choice.container = container;
	choice.isCodecDetected = codec == kCodecFormatAuto;
	if (isCodecGiven)
		choice.codec = static_cast<Codec>(codec);
	return true;
}

bool RefuseSameFile(IPluginContext *context, const GamePath &input, const GamePath &output)
{
	if (input.resolved != output.resolved)
		return true;
	context->ReportError("inFile \"%s\" and outFile \"%s\" are the same file", input.relative.c_str(),
	                     output.relative.c_str());
	return false;
}

std::string Arrow(const std::string &from, const std::string &to)
{
	return from + " -> " + to;
}

// Compression.CompressFile(inFile, outFile, options, callback, data, overwrite, createPath, serial, priority,
//                          progress)
cell_t Compression_CompressFile(IPluginContext *context, const cell_t *params)
{
	GamePath input, output;
	JobRequest request;
	CodecOptions options;
	if (!natives::ReadGamePath(context, params[1], "inFile", input) ||
	    !natives::ReadGamePath(context, params[2], "outFile", output) || !RefuseSameFile(context, input, output) ||
	    !ReadCodecOptions(context, params[3], options) ||
	    !ReadCommon(context, CommonArguments{params[4], params[5], params[8], params[9], 0, params[10]}, request))
		return 0;
	request.job.spec = CodecFileJob{CodecChoice{true, options, std::nullopt}, input.resolved,
	                                OutputFor(output, params[6], params[7])};
	request.description = std::string("compress ") + CodecName(CodecOf(options)) + " " +
	                      Arrow(input.relative, output.relative);
	return Queue(context, std::move(request));
}

// Compression.DecompressFile(inFile, outFile, callback, data, format, overwrite, createPath, serial, priority,
//                            limits, progress)
cell_t Compression_DecompressFile(IPluginContext *context, const cell_t *params)
{
	GamePath input, output;
	JobRequest request;
	std::optional<Codec> codec;
	if (!natives::ReadGamePath(context, params[1], "inFile", input) ||
	    !natives::ReadGamePath(context, params[2], "outFile", output) || !RefuseSameFile(context, input, output) ||
	    !ReadDecodeFormat(context, params[5], codec) ||
	    !ReadCommon(context, CommonArguments{params[3], params[4], params[8], params[9], params[10], params[11]},
	                request))
		return 0;
	request.job.spec = CodecFileJob{CodecChoice{false, GzipOptions{}, codec}, input.resolved,
	                                OutputFor(output, params[6], params[7])};
	request.description = "decompress " + Arrow(input.relative, output.relative);
	return Queue(context, std::move(request));
}

/* Queues a memory job; output is the optional outFile with its flags. */
cell_t QueueMemoryJob(IPluginContext *context, CodecChoice choice, std::vector<uint8_t> input, cell_t outFile,
                      cell_t overwrite, cell_t createPath, const CommonArguments &arguments)
{
	GamePath output;
	bool isOutputGiven = false;
	JobRequest request;
	if (!natives::ReadOptionalGamePath(context, outFile, "outFile", output, isOutputGiven) ||
	    !ReadCommon(context, arguments, request))
		return 0;
	CodecMemoryJob job{choice, std::move(input), std::nullopt};
	if (isOutputGiven)
		job.output = OutputFor(output, overwrite, createPath);
	request.description = std::string(choice.isCompress ? "compress " : "decompress ") +
	                      std::to_string(job.input.size()) + " bytes" +
	                      (isOutputGiven ? " -> " + output.relative : std::string(" in memory"));
	request.job.spec = std::move(job);
	return Queue(context, std::move(request));
}

/* The bytes argument, one per cell; false after a native error. */
bool ReadBytes(IPluginContext *context, const cell_t *params, std::vector<uint8_t> &bytes)
{
	cell_t length = params[2];
	if (length < 0) {
		context->ReportError("Invalid length %d", length);
		return false;
	}
	auto *cells = static_cast<const cell_t *>(natives::ResolveSpan(context, params, params[1],
	                                                               natives::CellBytes(length), "bytes"));
	if (!cells)
		return false;
	size_t count = static_cast<size_t>(length);
	bytes.resize(count);
	for (size_t i = 0; i < count; i++)
		bytes[i] = static_cast<uint8_t>(cells[i] & 0xff);
	return true;
}

// Compression.CompressString(text, options, callback, data, outFile, overwrite, createPath, serial, priority,
//                            limits, progress)
cell_t Compression_CompressString(IPluginContext *context, const cell_t *params)
{
	char *text;
	context->LocalToString(params[1], &text);
	CodecOptions options;
	if (!ReadCodecOptions(context, params[2], options))
		return 0;
	std::vector<uint8_t> input(text, text + strlen(text));
	return QueueMemoryJob(context, CodecChoice{true, options, std::nullopt}, std::move(input), params[5], params[6],
	                      params[7], CommonArguments{params[3], params[4], params[8], params[9], params[10], params[11]});
}

// Compression.CompressBytes(bytes, length, options, callback, data, outFile, overwrite, createPath, serial,
//                           priority, limits, progress)
cell_t Compression_CompressBytes(IPluginContext *context, const cell_t *params)
{
	std::vector<uint8_t> input;
	if (!ReadBytes(context, params, input))
		return 0;
	CodecOptions options;
	if (!ReadCodecOptions(context, params[3], options))
		return 0;
	return QueueMemoryJob(context, CodecChoice{true, options, std::nullopt}, std::move(input), params[6], params[7],
	                      params[8], CommonArguments{params[4], params[5], params[9], params[10], params[11], params[12]});
}

// Compression.DecompressBytes(bytes, length, callback, data, format, outFile, overwrite, createPath, serial,
//                             priority, limits, progress)
cell_t Compression_DecompressBytes(IPluginContext *context, const cell_t *params)
{
	std::vector<uint8_t> input;
	if (!ReadBytes(context, params, input))
		return 0;
	std::optional<Codec> codec;
	if (!ReadDecodeFormat(context, params[5], codec))
		return 0;
	return QueueMemoryJob(context, CodecChoice{false, GzipOptions{}, codec}, std::move(input), params[6], params[7],
	                      params[8], CommonArguments{params[3], params[4], params[9], params[10], params[11], params[12]});
}

// Compression.DecompressBase64(base64, callback, data, format, outFile, overwrite, createPath, serial, priority,
//                              limits, progress)
cell_t Compression_DecompressBase64(IPluginContext *context, const cell_t *params)
{
	char *text;
	context->LocalToString(params[1], &text);
	std::vector<uint8_t> input;
	if (!DecodeBase64(text, input))
		return context->ThrowNativeError("base64 is no valid base64 (RFC 4648, padded, without whitespace)");
	std::optional<Codec> codec;
	if (!ReadDecodeFormat(context, params[4], codec))
		return 0;
	return QueueMemoryJob(context, CodecChoice{false, GzipOptions{}, codec}, std::move(input), params[5], params[6],
	                      params[7], CommonArguments{params[2], params[3], params[8], params[9], params[10], params[11]});
}

// Compression.Cancel(job)
cell_t Compression_Cancel(IPluginContext *context, const cell_t *params)
{
	return g_SMArchive.CancelOwnJob(context, static_cast<JobId>(params[1])) ? 1 : 0;
}

// Compression.ErrorString(error, buffer, maxlength)
cell_t Compression_ErrorString(IPluginContext *context, const cell_t *params)
{
	const char *name = params[1] < 0 ? nullptr : ErrorCodeName(static_cast<ErrorCode>(params[1]));
	if (!name)
		return context->ThrowNativeError("Invalid ArchiveError %d", params[1]);
	return natives::WriteString(context, params, params[2], params[3], name);
}

/* The archive path argument with its description. */
bool ReadArchivePath(IPluginContext *context, cell_t address, GamePath &path)
{
	return natives::ReadGamePath(context, address, "archive", path);
}

// Archive.List(archive, callback, data, format, codec, serial, priority, limits, progress)
cell_t Archive_List(IPluginContext *context, const cell_t *params)
{
	GamePath archive;
	ArchiveChoice choice;
	JobRequest request;
	if (!ReadArchivePath(context, params[1], archive) || !ReadArchiveChoice(context, params[4], params[5], choice) ||
	    !ReadCommon(context, CommonArguments{params[2], params[3], params[6], params[7], params[8], params[9]}, request))
		return 0;
	request.job.spec = ListJob{archive.resolved, choice};
	request.description = "list " + archive.relative;
	return Queue(context, std::move(request));
}

/* The prefixes of an ArchivePrefixes handle, checked when they were added;
 * none for a null handle. */
bool ReadPrefixes(IPluginContext *context, cell_t handle, std::vector<std::string> &prefixes)
{
	if (handle == 0)
		return true;
	auto *object = ReadObject<PrefixesObject>(context, handle, g_SMArchive.prefixesType, "ArchivePrefixes");
	if (object)
		prefixes = object->prefixes;
	return object != nullptr;
}

// Archive.Extract(archive, targetDir, callback, data, format, codec, prefixes, createPath, serial, priority,
//                 limits, progress)
cell_t Archive_Extract(IPluginContext *context, const cell_t *params)
{
	GamePath archive, target;
	ArchiveChoice choice;
	JobRequest request;
	ExtractJob job;
	if (!ReadArchivePath(context, params[1], archive) ||
	    !natives::ReadGamePath(context, params[2], "targetDir", target) ||
	    !ReadArchiveChoice(context, params[5], params[6], choice) || !ReadPrefixes(context, params[7], job.prefixes) ||
	    !ReadCommon(context, CommonArguments{params[3], params[4], params[9], params[10], params[11], params[12]},
	                request))
		return 0;
	job.archivePath = archive.resolved;
	job.choice = choice;
	job.targetPath = target.resolved;
	job.createPath = params[8] != 0;
	request.job.spec = std::move(job);
	request.description = "extract " + Arrow(archive.relative, target.relative);
	return Queue(context, std::move(request));
}

// Archive.ReadEntry(archive, entryPath, callback, data, format, codec, serial, priority, limits, progress)
cell_t Archive_ReadEntry(IPluginContext *context, const cell_t *params)
{
	GamePath archive;
	ArchiveChoice choice;
	JobRequest request;
	char *entryPath;
	context->LocalToString(params[2], &entryPath);
	if (entryPath[0] == '\0' || !IsCleanArchivePath(entryPath))
		return context->ThrowNativeError("entryPath \"%s\" must be a non-empty relative path with \"/\" between its "
		                                 "parts and no empty, \".\" or \"..\" parts", entryPath);
	if (!ReadArchivePath(context, params[1], archive) || !ReadArchiveChoice(context, params[5], params[6], choice) ||
	    !ReadCommon(context, CommonArguments{params[3], params[4], params[7], params[8], params[9], params[10]},
	                request))
		return 0;
	request.job.spec = ReadEntryJob{archive.resolved, choice, entryPath};
	request.description = "read " + std::string(entryPath) + " of " + archive.relative;
	return Queue(context, std::move(request));
}

// Archive.Create(outFile, sources, options, callback, data, overwrite, createPath, serial, priority, progress)
cell_t Archive_Create(IPluginContext *context, const cell_t *params)
{
	GamePath output;
	JobRequest request;
	if (!natives::ReadGamePath(context, params[1], "outFile", output))
		return 0;
	auto *sources = ReadObject<SourcesObject>(context, params[2], g_SMArchive.sourcesType, "ArchiveSources");
	auto *options = sources ? ReadObject<WriteOptions>(context, params[3], g_SMArchive.writeOptionsType,
	                                                   "ArchiveWriteOptions")
	                        : nullptr;
	if (!options ||
	    !ReadCommon(context, CommonArguments{params[4], params[5], params[8], params[9], 0, params[10]}, request))
		return 0;
	if (sources->sources.empty())
		return context->ThrowNativeError("sources is empty");
	request.job.spec = CreateJob{sources->sources, *options, OutputFor(output, params[6], params[7])};
	request.description = std::string("create ") + ContainerName(ContainerOf(*options)) + " " + output.relative;
	return Queue(context, std::move(request));
}

} // namespace

const sp_nativeinfo_t g_JobNatives[] = {
	{"Compression.CompressFile",     Compression_CompressFile},
	{"Compression.DecompressFile",   Compression_DecompressFile},
	{"Compression.CompressString",   Compression_CompressString},
	{"Compression.CompressBytes",    Compression_CompressBytes},
	{"Compression.DecompressBytes",  Compression_DecompressBytes},
	{"Compression.DecompressBase64", Compression_DecompressBase64},
	{"Compression.Cancel",           Compression_Cancel},
	{"Compression.ErrorString",      Compression_ErrorString},
	{"Archive.List",                 Archive_List},
	{"Archive.Extract",              Archive_Extract},
	{"Archive.ReadEntry",            Archive_ReadEntry},
	{"Archive.Create",               Archive_Create},
	{nullptr,                        nullptr},
};
