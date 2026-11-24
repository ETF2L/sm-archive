/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The containers through libarchive, which only parses and writes the
 * formats here: it reads from and writes to our streams (so tar composes
 * with every codec) and touches no disk except for the 7z writer, which keeps
 * its compressed data in a file in the system's temp directory while it
 * writes; extraction, its checks and its limits are the core's (extract.h). */
#ifndef SMARCHIVE_CORE_ARCHIVE_IO_H
#define SMARCHIVE_CORE_ARCHIVE_IO_H

#include "codec.h"
#include "detect.h"
#include "entry.h"
#include "stream.h"

#include <memory>
#include <optional>
#include <string>
#include <variant>

struct archive;

namespace smarchive {

/* An entry's header as the archive stores it, before any check. */
struct RawEntry {
	std::string storedPath;
	/* False when libarchive has no form of the name at all, as for names it
	 * cannot convert without iconv. */
	bool hasName = true;
	EntryType type = EntryType::File;
	int64_t size = -1;
	int64_t mtime = 0;
	bool isExecutable = false;
	std::string linkTarget;
	bool isEncrypted = false;
};

class ArchiveReader {
public:
	/* Reads container from source; zip, 7z and RAR also seek, through
	 * seekable, which must be the same stream (tar never seeks). Every read
	 * libarchive asks for, also to skip an entry's data, first checks the
	 * job's abort flag in control. */
	static Status Open(Container container, Source &source, FileSource *seekable, JobControl &control,
	                   std::unique_ptr<ArchiveReader> &reader);
	~ArchiveReader();

	/* The next entry's header; isEnd at the archive's end. */
	Status Next(RawEntry &entry, bool &isEnd);

	/* The current entry's data; got is 0 at its end. */
	Status ReadData(uint8_t *buffer, size_t capacity, size_t &got);

private:
	ArchiveReader(Source &source, FileSource *seekable, JobControl &control)
		: m_source(source), m_seekable(seekable), m_control(control)
	{
	}

	/* The failure of the last libarchive call: ours from a callback, if one
	 * failed, else libarchive's. */
	Status Failure(ErrorCode code);

	friend struct ReaderCallbacks;

	struct archive *m_archive = nullptr;
	Source &m_source;
	FileSource *m_seekable;
	JobControl &m_control;
	std::vector<uint8_t> m_buffer = std::vector<uint8_t>(kChunkSize);
	Status m_callbackStatus;
	/* libarchive may only be asked about encryption once it is open. */
	bool m_isOpen = false;
};

/* Tars are written as pax (ustar headers, extended ones only where an entry
 * needs them). */
struct TarWriteOptions {
	/* The codec the tar is compressed with, none for a plain tar. */
	std::optional<CodecOptions> codec;
};

/* The values of the plugin API's ZipMethod. */
enum class ZipMethod : int {
	Store = 0,
	Deflate,
	Bzip2,
	Lzma,
	Xz,
	Zstd,
};

struct ZipWriteOptions {
	ZipMethod method = ZipMethod::Deflate;
	int level = 6;
};

/* The values of the plugin API's SevenZipMethod. */
enum class SevenZipMethod : int {
	Copy = 0,
	Deflate,
	Bzip2,
	Lzma1,
	Lzma2,
	Ppmd,
};

struct SevenZipWriteOptions {
	SevenZipMethod method = SevenZipMethod::Lzma2;
	int level = 5;
};

using WriteOptions = std::variant<TarWriteOptions, ZipWriteOptions, SevenZipWriteOptions>;

Container ContainerOf(const WriteOptions &options);

/* Why the options are out of range, empty when they are fine. */
std::string ValidateWriteOptions(const WriteOptions &options);

class ArchiveWriter {
public:
	/* Writes the container options name into sink; the codec of a tar is
	 * applied here. Finish() ends sink too. */
	static Status Open(const WriteOptions &options, Sink &sink, std::unique_ptr<ArchiveWriter> &writer);
	~ArchiveWriter();

	Status AddDirectory(const std::string &path, int64_t mtime);

	/* A file of size bytes, its data read from data. */
	Status AddFile(const std::string &path, int64_t size, bool isExecutable, int64_t mtime, Source &data,
	               JobControl &control);

	Status Finish();

private:
	explicit ArchiveWriter(Sink &sink) : m_sink(&sink) {}

	Status Failure();
	Status WriteHeader(const std::string &path, int type, int permissions, int64_t size, int64_t mtime);

	friend struct WriterCallbacks;

	struct archive *m_archive = nullptr;
	/* The sink libarchive writes to: the caller's, or a TransformSink over
	 * it for a compressed tar. */
	Sink *m_sink;
	std::unique_ptr<TransformSink> m_codecSink;
	Status m_callbackStatus;
};

/* libarchive and its version, e.g. "libarchive 3.8.9". */
std::string ArchiveLibraryVersion();

} // namespace smarchive

#endif // SMARCHIVE_CORE_ARCHIVE_IO_H
