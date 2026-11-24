/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The natives of the option handles: CodecOptions and its six codecs,
 * ArchiveWriteOptions with tar, zip and 7z, ArchiveLimits, ArchiveSources and
 * ArchivePrefixes. Every setter checks the new value against the format's
 * range and throws a native error outside it, leaving the handle as it
 * was. */
#include "natives_common.h"
#include "paths.h"

using namespace smarchive;
using natives::CreateHandle;
using natives::ReadObject;

namespace {

/* The typed options of a CodecOptions handle, or nullptr after a native
 * error (also for a handle of another codec). */
template <typename T>
T *ReadCodecOptions(IPluginContext *context, cell_t handle, const char *typeName)
{
	auto *options = ReadObject<CodecOptions>(context, handle, g_SMArchive.codecOptionsType, "CodecOptions");
	if (!options)
		return nullptr;
	T *typed = std::get_if<T>(options);
	if (!typed)
		context->ReportError("Handle %x is no %s but %s options", handle, typeName, CodecName(CodecOf(*options)));
	return typed;
}

template <typename T>
T *ReadWriteOptions(IPluginContext *context, cell_t handle, const char *typeName)
{
	auto *options = ReadObject<WriteOptions>(context, handle, g_SMArchive.writeOptionsType, "ArchiveWriteOptions");
	if (!options)
		return nullptr;
	T *typed = std::get_if<T>(options);
	if (!typed)
		context->ReportError("Handle %x is no %s but %s options", handle, typeName,
		                     ContainerName(ContainerOf(*options)));
	return typed;
}

/* The names of the typed handles, for errors. */
template <typename T> struct TypeName;
template <> struct TypeName<GzipOptions> { static constexpr const char *value = "GzipOptions"; };
template <> struct TypeName<Bzip2Options> { static constexpr const char *value = "Bzip2Options"; };
template <> struct TypeName<BrotliOptions> { static constexpr const char *value = "BrotliOptions"; };
template <> struct TypeName<XzOptions> { static constexpr const char *value = "XzOptions"; };
template <> struct TypeName<ZstdOptions> { static constexpr const char *value = "ZstdOptions"; };
template <> struct TypeName<Lz4Options> { static constexpr const char *value = "Lz4Options"; };
template <> struct TypeName<ZipWriteOptions> { static constexpr const char *value = "ZipOptions"; };
template <> struct TypeName<SevenZipWriteOptions> { static constexpr const char *value = "SevenZipOptions"; };

template <typename T>
cell_t NewCodecOptions(IPluginContext *context, const cell_t *params)
{
	return CreateHandle(context, g_SMArchive.codecOptionsType, new CodecOptions(T{}));
}

template <typename T, typename V, V T::*Member>
cell_t GetCodecMember(IPluginContext *context, const cell_t *params)
{
	T *options = ReadCodecOptions<T>(context, params[1], TypeName<T>::value);
	return options ? static_cast<cell_t>(options->*Member) : 0;
}

template <typename T, typename V, V T::*Member>
cell_t SetCodecMember(IPluginContext *context, const cell_t *params)
{
	T *options = ReadCodecOptions<T>(context, params[1], TypeName<T>::value);
	if (!options)
		return 0;
	T changed = *options;
	changed.*Member = static_cast<V>(params[2]);
	std::string reason = ValidateOptions(CodecOptions(changed));
	if (!reason.empty())
		return context->ThrowNativeError("%s", reason.c_str());
	*options = changed;
	return 0;
}

template <typename T>
cell_t NewWriteOptions(IPluginContext *context, const cell_t *params)
{
	return CreateHandle(context, g_SMArchive.writeOptionsType, new WriteOptions(T{}));
}

template <typename T, typename V, V T::*Member>
cell_t GetWriteMember(IPluginContext *context, const cell_t *params)
{
	T *options = ReadWriteOptions<T>(context, params[1], TypeName<T>::value);
	return options ? static_cast<cell_t>(options->*Member) : 0;
}

template <typename T, typename V, V T::*Member>
cell_t SetWriteMember(IPluginContext *context, const cell_t *params)
{
	T *options = ReadWriteOptions<T>(context, params[1], TypeName<T>::value);
	if (!options)
		return 0;
	T changed = *options;
	changed.*Member = static_cast<V>(params[2]);
	std::string reason = ValidateWriteOptions(WriteOptions(changed));
	if (!reason.empty())
		return context->ThrowNativeError("%s", reason.c_str());
	*options = changed;
	return 0;
}

// CodecOptions.Format.get
cell_t CodecOptions_Format(IPluginContext *context, const cell_t *params)
{
	auto *options = ReadObject<CodecOptions>(context, params[1], g_SMArchive.codecOptionsType, "CodecOptions");
	return options ? static_cast<cell_t>(CodecOf(*options)) : 0;
}

// ArchiveWriteOptions.Format.get
cell_t WriteOptions_Format(IPluginContext *context, const cell_t *params)
{
	auto *options = ReadObject<WriteOptions>(context, params[1], g_SMArchive.writeOptionsType, "ArchiveWriteOptions");
	return options ? static_cast<cell_t>(ContainerOf(*options)) : 0;
}

// TarOptions(CodecOptions codec = null): a copy of codec's options.
cell_t TarOptions_New(IPluginContext *context, const cell_t *params)
{
	TarWriteOptions tar;
	if (params[1] != 0) {
		auto *codec = ReadObject<CodecOptions>(context, params[1], g_SMArchive.codecOptionsType, "CodecOptions");
		if (!codec)
			return 0;
		tar.codec = *codec;
	}
	return CreateHandle(context, g_SMArchive.writeOptionsType, new WriteOptions(tar));
}

// TarOptions.Codec.get: CodecFormat_None for a plain tar.
cell_t TarOptions_Codec(IPluginContext *context, const cell_t *params)
{
	auto *tar = ReadWriteOptions<TarWriteOptions>(context, params[1], "TarOptions");
	if (!tar)
		return 0;
	constexpr cell_t kCodecFormatNone = 1;
	return tar->codec ? static_cast<cell_t>(CodecOf(*tar->codec)) : kCodecFormatNone;
}

Limits *ReadLimits(IPluginContext *context, cell_t handle)
{
	return ReadObject<Limits>(context, handle, g_SMArchive.limitsType, "ArchiveLimits");
}

// ArchiveLimits(): the defaults the server set.
cell_t Limits_New(IPluginContext *context, const cell_t *params)
{
	return CreateHandle(context, g_SMArchive.limitsType, new Limits(g_SMArchive.DefaultLimits()));
}

template <uint64_t Limits::*Member>
cell_t GetLimit(IPluginContext *context, const cell_t *params)
{
	Limits *limits = ReadLimits(context, params[1]);
	return limits ? natives::Saturated(static_cast<int64_t>(limits->*Member)) : 0;
}

template <uint64_t Limits::*Member>
cell_t SetLimit(IPluginContext *context, const cell_t *params)
{
	Limits *limits = ReadLimits(context, params[1]);
	if (!limits)
		return 0;
	if (params[2] < 0)
		return context->ThrowNativeError("A limit cannot be negative (%d); 0 is no limit", params[2]);
	limits->*Member = static_cast<uint64_t>(params[2]);
	return 0;
}

template <uint64_t Limits::*Member>
cell_t GetLimit64(IPluginContext *context, const cell_t *params)
{
	Limits *limits = ReadLimits(context, params[1]);
	if (limits)
		natives::WriteInt64(context, params, params[2], static_cast<int64_t>(limits->*Member));
	return 0;
}

template <uint64_t Limits::*Member>
cell_t SetLimit64(IPluginContext *context, const cell_t *params)
{
	Limits *limits = ReadLimits(context, params[1]);
	if (!limits)
		return 0;
	int64_t value = 0;
	if (!natives::ReadInt64(context, params, params[2], value))
		return 0;
	if (value < 0)
		return context->ThrowNativeError("A limit cannot be negative; 0 is no limit");
	limits->*Member = static_cast<uint64_t>(value);
	return 0;
}

SourcesObject *ReadSources(IPluginContext *context, cell_t handle)
{
	return ReadObject<SourcesObject>(context, handle, g_SMArchive.sourcesType, "ArchiveSources");
}

cell_t Sources_New(IPluginContext *context, const cell_t *params)
{
	return CreateHandle(context, g_SMArchive.sourcesType, new SourcesObject());
}

/* Adds a game path under an archive path; a file needs a non-empty one. */
cell_t AddSource(IPluginContext *context, const cell_t *params, bool isDirectory)
{
	SourcesObject *sources = ReadSources(context, params[1]);
	natives::GamePath path;
	if (!sources || !natives::ReadGamePath(context, params[2], "gamePath", path))
		return 0;
	char *archivePath;
	context->LocalToString(params[3], &archivePath);
	if (!IsCleanArchivePath(archivePath))
		return context->ThrowNativeError("archivePath \"%s\" must be a relative path with \"/\" between its parts "
		                                 "and no empty, \".\" or \"..\" parts", archivePath);
	if (!isDirectory && archivePath[0] == '\0')
		return context->ThrowNativeError("A file needs a non-empty archivePath");
	sources->sources.push_back(ArchiveSource{path.resolved, archivePath, isDirectory});
	return 0;
}

// ArchiveSources.AddFile(gamePath, archivePath)
cell_t Sources_AddFile(IPluginContext *context, const cell_t *params)
{
	return AddSource(context, params, false);
}

// ArchiveSources.AddDirectory(gamePath, archivePath = "")
cell_t Sources_AddDirectory(IPluginContext *context, const cell_t *params)
{
	return AddSource(context, params, true);
}

cell_t Sources_Length(IPluginContext *context, const cell_t *params)
{
	SourcesObject *sources = ReadSources(context, params[1]);
	return sources ? static_cast<cell_t>(sources->sources.size()) : 0;
}

cell_t Prefixes_New(IPluginContext *context, const cell_t *params)
{
	return CreateHandle(context, g_SMArchive.prefixesType, new PrefixesObject());
}

// ArchivePrefixes.Add(prefix)
cell_t Prefixes_Add(IPluginContext *context, const cell_t *params)
{
	auto *object = ReadObject<PrefixesObject>(context, params[1], g_SMArchive.prefixesType, "ArchivePrefixes");
	if (!object)
		return 0;
	char *prefix;
	context->LocalToString(params[2], &prefix);
	if (prefix[0] == '\0' || !IsCleanArchivePath(prefix))
		return context->ThrowNativeError("prefix \"%s\" must be a non-empty relative path with \"/\" between its "
		                                 "parts and no empty, \".\" or \"..\" parts", prefix);
	object->prefixes.push_back(prefix);
	return 0;
}

cell_t Prefixes_Length(IPluginContext *context, const cell_t *params)
{
	auto *object = ReadObject<PrefixesObject>(context, params[1], g_SMArchive.prefixesType, "ArchivePrefixes");
	return object ? static_cast<cell_t>(object->prefixes.size()) : 0;
}

} // namespace

const sp_nativeinfo_t g_OptionNatives[] = {
	{"CodecOptions.Format.get", CodecOptions_Format},

	{"GzipOptions.GzipOptions", NewCodecOptions<GzipOptions>},
	{"GzipOptions.Level.get", GetCodecMember<GzipOptions, int, &GzipOptions::level>},
	{"GzipOptions.Level.set", SetCodecMember<GzipOptions, int, &GzipOptions::level>},

	{"Bzip2Options.Bzip2Options", NewCodecOptions<Bzip2Options>},
	{"Bzip2Options.Level.get", GetCodecMember<Bzip2Options, int, &Bzip2Options::level>},
	{"Bzip2Options.Level.set", SetCodecMember<Bzip2Options, int, &Bzip2Options::level>},

	{"BrotliOptions.BrotliOptions", NewCodecOptions<BrotliOptions>},
	{"BrotliOptions.Quality.get", GetCodecMember<BrotliOptions, int, &BrotliOptions::quality>},
	{"BrotliOptions.Quality.set", SetCodecMember<BrotliOptions, int, &BrotliOptions::quality>},
	{"BrotliOptions.Window.get", GetCodecMember<BrotliOptions, int, &BrotliOptions::window>},
	{"BrotliOptions.Window.set", SetCodecMember<BrotliOptions, int, &BrotliOptions::window>},
	{"BrotliOptions.Mode.get", GetCodecMember<BrotliOptions, BrotliMode, &BrotliOptions::mode>},
	{"BrotliOptions.Mode.set", SetCodecMember<BrotliOptions, BrotliMode, &BrotliOptions::mode>},

	{"XzOptions.XzOptions", NewCodecOptions<XzOptions>},
	{"XzOptions.Preset.get", GetCodecMember<XzOptions, int, &XzOptions::preset>},
	{"XzOptions.Preset.set", SetCodecMember<XzOptions, int, &XzOptions::preset>},
	{"XzOptions.Extreme.get", GetCodecMember<XzOptions, bool, &XzOptions::isExtreme>},
	{"XzOptions.Extreme.set", SetCodecMember<XzOptions, bool, &XzOptions::isExtreme>},
	{"XzOptions.Check.get", GetCodecMember<XzOptions, XzCheck, &XzOptions::check>},
	{"XzOptions.Check.set", SetCodecMember<XzOptions, XzCheck, &XzOptions::check>},

	{"ZstdOptions.ZstdOptions", NewCodecOptions<ZstdOptions>},
	{"ZstdOptions.Level.get", GetCodecMember<ZstdOptions, int, &ZstdOptions::level>},
	{"ZstdOptions.Level.set", SetCodecMember<ZstdOptions, int, &ZstdOptions::level>},
	{"ZstdOptions.LongWindow.get", GetCodecMember<ZstdOptions, int, &ZstdOptions::longWindow>},
	{"ZstdOptions.LongWindow.set", SetCodecMember<ZstdOptions, int, &ZstdOptions::longWindow>},
	{"ZstdOptions.Checksum.get", GetCodecMember<ZstdOptions, bool, &ZstdOptions::hasChecksum>},
	{"ZstdOptions.Checksum.set", SetCodecMember<ZstdOptions, bool, &ZstdOptions::hasChecksum>},

	{"Lz4Options.Lz4Options", NewCodecOptions<Lz4Options>},
	{"Lz4Options.Level.get", GetCodecMember<Lz4Options, int, &Lz4Options::level>},
	{"Lz4Options.Level.set", SetCodecMember<Lz4Options, int, &Lz4Options::level>},
	{"Lz4Options.BlockSize.get", GetCodecMember<Lz4Options, Lz4BlockSize, &Lz4Options::blockSize>},
	{"Lz4Options.BlockSize.set", SetCodecMember<Lz4Options, Lz4BlockSize, &Lz4Options::blockSize>},
	{"Lz4Options.ContentChecksum.get", GetCodecMember<Lz4Options, bool, &Lz4Options::hasContentChecksum>},
	{"Lz4Options.ContentChecksum.set", SetCodecMember<Lz4Options, bool, &Lz4Options::hasContentChecksum>},

	{"ArchiveWriteOptions.Format.get", WriteOptions_Format},
	{"TarOptions.TarOptions", TarOptions_New},
	{"TarOptions.Codec.get", TarOptions_Codec},
	{"ZipOptions.ZipOptions", NewWriteOptions<ZipWriteOptions>},
	{"ZipOptions.Method.get", GetWriteMember<ZipWriteOptions, ZipMethod, &ZipWriteOptions::method>},
	{"ZipOptions.Method.set", SetWriteMember<ZipWriteOptions, ZipMethod, &ZipWriteOptions::method>},
	{"ZipOptions.Level.get", GetWriteMember<ZipWriteOptions, int, &ZipWriteOptions::level>},
	{"ZipOptions.Level.set", SetWriteMember<ZipWriteOptions, int, &ZipWriteOptions::level>},
	{"SevenZipOptions.SevenZipOptions", NewWriteOptions<SevenZipWriteOptions>},
	{"SevenZipOptions.Method.get", GetWriteMember<SevenZipWriteOptions, SevenZipMethod, &SevenZipWriteOptions::method>},
	{"SevenZipOptions.Method.set", SetWriteMember<SevenZipWriteOptions, SevenZipMethod, &SevenZipWriteOptions::method>},
	{"SevenZipOptions.Level.get", GetWriteMember<SevenZipWriteOptions, int, &SevenZipWriteOptions::level>},
	{"SevenZipOptions.Level.set", SetWriteMember<SevenZipWriteOptions, int, &SevenZipWriteOptions::level>},

	{"ArchiveLimits.ArchiveLimits", Limits_New},
	{"ArchiveLimits.MaxSize.get", GetLimit<&Limits::maxSize>},
	{"ArchiveLimits.MaxSize.set", SetLimit<&Limits::maxSize>},
	{"ArchiveLimits.GetMaxSize64", GetLimit64<&Limits::maxSize>},
	{"ArchiveLimits.SetMaxSize64", SetLimit64<&Limits::maxSize>},
	{"ArchiveLimits.MaxEntries.get", GetLimit<&Limits::maxEntries>},
	{"ArchiveLimits.MaxEntries.set", SetLimit<&Limits::maxEntries>},
	{"ArchiveLimits.MaxRatio.get", GetLimit<&Limits::maxRatio>},
	{"ArchiveLimits.MaxRatio.set", SetLimit<&Limits::maxRatio>},
	{"ArchiveLimits.MaxMemory.get", GetLimit<&Limits::maxMemory>},
	{"ArchiveLimits.MaxMemory.set", SetLimit<&Limits::maxMemory>},
	{"ArchiveLimits.GetMaxMemory64", GetLimit64<&Limits::maxMemory>},
	{"ArchiveLimits.SetMaxMemory64", SetLimit64<&Limits::maxMemory>},

	{"ArchiveSources.ArchiveSources", Sources_New},
	{"ArchiveSources.AddFile", Sources_AddFile},
	{"ArchiveSources.AddDirectory", Sources_AddDirectory},
	{"ArchiveSources.Length.get", Sources_Length},

	{"ArchivePrefixes.ArchivePrefixes", Prefixes_New},
	{"ArchivePrefixes.Add", Prefixes_Add},
	{"ArchivePrefixes.Length.get", Prefixes_Length},

	{nullptr, nullptr},
};
