/* Copyright 2026 ETF2L Contributors, Dave */
#include "archive_io.h"

#include <archive.h>
#include <archive_entry.h>

#include <errno.h>
#include <stdio.h>

namespace smarchive {

namespace {

struct EntryDeleter {
	void operator()(struct archive_entry *entry) const { archive_entry_free(entry); }
};
using UniqueEntry = std::unique_ptr<struct archive_entry, EntryDeleter>;

constexpr int kExecutableBits = 0111;
constexpr int kFilePermissions = 0664;
constexpr int kExecutablePermissions = 0775;
constexpr int kDirectoryPermissions = 0775;

const char *ZipMethodName(ZipMethod method)
{
	switch (method) {
	case ZipMethod::Store:   return "store";
	case ZipMethod::Deflate: return "deflate";
	case ZipMethod::Bzip2:   return "bzip2";
	case ZipMethod::Lzma:    return "lzma";
	case ZipMethod::Xz:      return "xz";
	case ZipMethod::Zstd:    return "zstd";
	}
	return "deflate";
}

const char *SevenZipMethodName(SevenZipMethod method)
{
	switch (method) {
	case SevenZipMethod::Copy:    return "copy";
	case SevenZipMethod::Deflate: return "deflate";
	case SevenZipMethod::Bzip2:   return "bzip2";
	case SevenZipMethod::Lzma1:   return "lzma1";
	case SevenZipMethod::Lzma2:   return "lzma2";
	case SevenZipMethod::Ppmd:    return "ppmd";
	}
	return "lzma2";
}

Status LibarchiveFailure(struct archive *archive, ErrorCode code)
{
	const char *text = archive_error_string(archive);
	int number = archive_errno(archive);
	if (number == ENOMEM)
		code = ErrorCode::Memory;
	return Status::Fail(code, std::string("libarchive: ") + (text ? text : "failed"), number);
}

Status SetOption(struct archive *archive, const char *module, const char *key, const std::string &value)
{
	if (archive_write_set_format_option(archive, module, key, value.c_str()) == ARCHIVE_OK)
		return Status::Ok();
	return LibarchiveFailure(archive, ErrorCode::Unsupported);
}

Status SetFormat(struct archive *archive, const WriteOptions &options)
{
	if (std::holds_alternative<TarWriteOptions>(options)) {
		if (archive_write_set_format_pax_restricted(archive) != ARCHIVE_OK)
			return LibarchiveFailure(archive, ErrorCode::Internal);
		return Status::Ok();
	}
	if (const auto *zip = std::get_if<ZipWriteOptions>(&options)) {
		if (archive_write_set_format_zip(archive) != ARCHIVE_OK)
			return LibarchiveFailure(archive, ErrorCode::Internal);
		Status status = SetOption(archive, "zip", "compression", ZipMethodName(zip->method));
		if (status.IsOk() && zip->method != ZipMethod::Store)
			status = SetOption(archive, "zip", "compression-level", std::to_string(zip->level));
		return status;
	}
	const auto &sevenZip = std::get<SevenZipWriteOptions>(options);
	if (archive_write_set_format_7zip(archive) != ARCHIVE_OK)
		return LibarchiveFailure(archive, ErrorCode::Internal);
	Status status = SetOption(archive, "7zip", "compression", SevenZipMethodName(sevenZip.method));
	if (status.IsOk() && sevenZip.method != SevenZipMethod::Copy)
		status = SetOption(archive, "7zip", "compression-level", std::to_string(sevenZip.level));
	return status;
}

Status EnableFormat(struct archive *archive, Container container)
{
	int result;
	switch (container) {
	case Container::Tar:
		result = archive_read_support_format_tar(archive);
		break;
	case Container::Zip:
		result = archive_read_support_format_zip_seekable(archive);
		break;
	case Container::SevenZip:
		result = archive_read_support_format_7zip(archive);
		break;
	case Container::Rar:
		result = archive_read_support_format_rar(archive);
		if (result == ARCHIVE_OK)
			result = archive_read_support_format_rar5(archive);
		break;
	default:
		return Status::Fail(ErrorCode::Internal, "unknown container");
	}
	return result == ARCHIVE_OK ? Status::Ok() : LibarchiveFailure(archive, ErrorCode::Internal);
}

/* The name libarchive has for an entry's path or link: its UTF-8 form where
 * it can tell one, else the bytes as stored, which the checks then judge. */
std::string NameOf(const char *utf8, const char *stored)
{
	if (utf8)
		return utf8;
	return stored ? stored : "";
}

RawEntry ToRawEntry(struct archive_entry *entry)
{
	RawEntry raw;
	const char *utf8 = archive_entry_pathname_utf8(entry);
	const char *stored = archive_entry_pathname(entry);
	raw.hasName = utf8 || stored;
	raw.storedPath = NameOf(utf8, stored);
	raw.size = archive_entry_size_is_set(entry) ? archive_entry_size(entry) : -1;
	raw.mtime = archive_entry_mtime_is_set(entry) ? archive_entry_mtime(entry) : 0;
	raw.isExecutable = (archive_entry_perm(entry) & kExecutableBits) != 0;
	raw.isEncrypted = archive_entry_is_encrypted(entry) != 0;

	if (archive_entry_hardlink(entry) || archive_entry_hardlink_utf8(entry)) {
		raw.type = EntryType::Hardlink;
		raw.linkTarget = NameOf(archive_entry_hardlink_utf8(entry), archive_entry_hardlink(entry));
		return raw;
	}
	switch (archive_entry_filetype(entry)) {
	case AE_IFREG:
		raw.type = EntryType::File;
		break;
	case AE_IFDIR:
		raw.type = EntryType::Directory;
		break;
	case AE_IFLNK:
		raw.type = EntryType::Symlink;
		raw.linkTarget = NameOf(archive_entry_symlink_utf8(entry), archive_entry_symlink(entry));
		break;
	default:
		raw.type = EntryType::Other;
		break;
	}
	return raw;
}

} // namespace

struct ReaderCallbacks {
	static la_ssize_t Read(struct archive *, void *data, const void **buffer)
	{
		auto *reader = static_cast<ArchiveReader *>(data);
		size_t got = 0;
		Status status = reader->m_control.CheckAbort();
		if (status.IsOk())
			status = reader->m_source.Read(reader->m_buffer.data(), reader->m_buffer.size(), got);
		if (!status.IsOk()) {
			reader->m_callbackStatus = status;
			return ARCHIVE_FATAL;
		}
		*buffer = reader->m_buffer.data();
		return static_cast<la_ssize_t>(got);
	}

	static la_int64_t Seek(struct archive *, void *data, la_int64_t offset, int whence)
	{
		auto *reader = static_cast<ArchiveReader *>(data);
		int64_t position = 0;
		Status status = reader->m_seekable->Seek(offset, whence, position);
		if (!status.IsOk()) {
			reader->m_callbackStatus = status;
			return ARCHIVE_FATAL;
		}
		return position;
	}

	/* A failed seek skips nothing, and libarchive reads instead; it is no
	 * failure of the job, so it is not kept. */
	static la_int64_t Skip(struct archive *, void *data, la_int64_t request)
	{
		auto *reader = static_cast<ArchiveReader *>(data);
		int64_t before = 0, after = 0;
		Status status = reader->m_seekable->Seek(0, SEEK_CUR, before);
		if (status.IsOk())
			status = reader->m_seekable->Seek(request, SEEK_CUR, after);
		return status.IsOk() ? after - before : 0;
	}
};

Status ArchiveReader::Open(Container container, Source &source, FileSource *seekable, JobControl &control,
                           std::unique_ptr<ArchiveReader> &reader)
{
	std::unique_ptr<ArchiveReader> created(new ArchiveReader(source, seekable, control));
	created->m_archive = archive_read_new();
	if (!created->m_archive)
		return Status::Fail(ErrorCode::Memory, "libarchive cannot create a reader");
	struct archive *archive = created->m_archive;

	Status status = EnableFormat(archive, container);
	if (!status.IsOk())
		return status;
	archive_read_set_read_callback(archive, ReaderCallbacks::Read);
	if (seekable) {
		archive_read_set_seek_callback(archive, ReaderCallbacks::Seek);
		archive_read_set_skip_callback(archive, ReaderCallbacks::Skip);
	}
	archive_read_set_callback_data(archive, created.get());
	if (archive_read_open1(archive) != ARCHIVE_OK)
		return created->Failure(ErrorCode::Corrupt);
	created->m_isOpen = true;
	reader = std::move(created);
	return Status::Ok();
}

ArchiveReader::~ArchiveReader()
{
	if (m_archive)
		archive_read_free(m_archive);
}

Status ArchiveReader::Failure(ErrorCode code)
{
	if (!m_callbackStatus.IsOk())
		return m_callbackStatus;
	if (m_isOpen && archive_read_has_encrypted_entries(m_archive) > 0)
		return Status::Fail(ErrorCode::Encrypted, "the archive is encrypted");
	return LibarchiveFailure(m_archive, code);
}

Status ArchiveReader::Next(RawEntry &entry, bool &isEnd)
{
	struct archive_entry *header = nullptr;
	int result = archive_read_next_header(m_archive, &header);
	isEnd = result == ARCHIVE_EOF;
	/* An end that a failed read callback left behind is that failure. */
	if (isEnd)
		return m_callbackStatus;
	if (result < ARCHIVE_WARN)
		return Failure(ErrorCode::Corrupt);
	entry = ToRawEntry(header);
	return Status::Ok();
}

Status ArchiveReader::ReadData(uint8_t *buffer, size_t capacity, size_t &got)
{
	la_ssize_t result = archive_read_data(m_archive, buffer, capacity);
	if (result < 0)
		return Failure(ErrorCode::Corrupt);
	got = static_cast<size_t>(result);
	return Status::Ok();
}

Container ContainerOf(const WriteOptions &options)
{
	if (std::holds_alternative<TarWriteOptions>(options))
		return Container::Tar;
	if (std::holds_alternative<ZipWriteOptions>(options))
		return Container::Zip;
	return Container::SevenZip;
}

std::string ValidateWriteOptions(const WriteOptions &options)
{
	if (const auto *tar = std::get_if<TarWriteOptions>(&options))
		return tar->codec ? ValidateOptions(*tar->codec) : "";
	if (const auto *zip = std::get_if<ZipWriteOptions>(&options)) {
		if (zip->method < ZipMethod::Store || zip->method > ZipMethod::Zstd)
			return "invalid zip method " + std::to_string(static_cast<int>(zip->method));
		if (zip->level < 0 || zip->level > 9)
			return "zip level " + std::to_string(zip->level) + " is outside 0-9";
		return "";
	}
	const auto &sevenZip = std::get<SevenZipWriteOptions>(options);
	if (sevenZip.method < SevenZipMethod::Copy || sevenZip.method > SevenZipMethod::Ppmd)
		return "invalid 7z method " + std::to_string(static_cast<int>(sevenZip.method));
	if (sevenZip.level < 0 || sevenZip.level > 9)
		return "7z level " + std::to_string(sevenZip.level) + " is outside 0-9";
	return "";
}

struct WriterCallbacks {
	static la_ssize_t Write(struct archive *, void *data, const void *buffer, size_t length)
	{
		auto *writer = static_cast<ArchiveWriter *>(data);
		Status status = writer->m_sink->Write(static_cast<const uint8_t *>(buffer), length);
		if (!status.IsOk()) {
			writer->m_callbackStatus = status;
			return ARCHIVE_FATAL;
		}
		return static_cast<la_ssize_t>(length);
	}
};

Status ArchiveWriter::Open(const WriteOptions &options, Sink &sink, std::unique_ptr<ArchiveWriter> &writer)
{
	std::unique_ptr<ArchiveWriter> created(new ArchiveWriter(sink));
	const auto *tar = std::get_if<TarWriteOptions>(&options);
	if (tar && tar->codec) {
		std::unique_ptr<Transform> encoder;
		Status status = CreateEncoder(*tar->codec, -1, encoder);
		if (!status.IsOk())
			return status;
		created->m_codecSink = std::make_unique<TransformSink>(sink, std::move(encoder));
		created->m_sink = created->m_codecSink.get();
	}

	created->m_archive = archive_write_new();
	if (!created->m_archive)
		return Status::Fail(ErrorCode::Memory, "libarchive cannot create a writer");
	struct archive *archive = created->m_archive;
	Status status = SetFormat(archive, options);
	if (!status.IsOk())
		return status;
	if (archive_write_add_filter_none(archive) != ARCHIVE_OK)
		return LibarchiveFailure(archive, ErrorCode::Internal);
	/* Written through callbacks, libarchive pads the last block to 10 KiB;
	 * tar keeps that as tar writes it, zip and 7z end where they end. */
	if (!tar)
		archive_write_set_bytes_in_last_block(archive, 1);
	if (archive_write_open2(archive, created.get(), nullptr, WriterCallbacks::Write, nullptr, nullptr) != ARCHIVE_OK)
		return created->Failure();
	writer = std::move(created);
	return Status::Ok();
}

ArchiveWriter::~ArchiveWriter()
{
	if (m_archive)
		archive_write_free(m_archive);
}

Status ArchiveWriter::Failure()
{
	if (!m_callbackStatus.IsOk())
		return m_callbackStatus;
	return LibarchiveFailure(m_archive, ErrorCode::Write);
}

Status ArchiveWriter::WriteHeader(const std::string &path, int type, int permissions, int64_t size, int64_t mtime)
{
	UniqueEntry entry(archive_entry_new());
	if (!entry)
		return Status::Fail(ErrorCode::Memory, "libarchive cannot create an entry");
	archive_entry_set_pathname_utf8(entry.get(), path.c_str());
	archive_entry_set_filetype(entry.get(), static_cast<unsigned int>(type));
	archive_entry_set_perm(entry.get(), static_cast<mode_t>(permissions));
	archive_entry_set_size(entry.get(), size);
	archive_entry_set_mtime(entry.get(), static_cast<time_t>(mtime), 0);
	if (archive_write_header(m_archive, entry.get()) < ARCHIVE_WARN)
		return Failure();
	return Status::Ok();
}

Status ArchiveWriter::AddDirectory(const std::string &path, int64_t mtime)
{
	return WriteHeader(path, AE_IFDIR, kDirectoryPermissions, 0, mtime);
}

Status ArchiveWriter::AddFile(const std::string &path, int64_t size, bool isExecutable, int64_t mtime, Source &data,
                              JobControl &control)
{
	Status status = WriteHeader(path, AE_IFREG, isExecutable ? kExecutablePermissions : kFilePermissions, size, mtime);
	if (!status.IsOk())
		return status;

	std::vector<uint8_t> buffer(kChunkSize);
	int64_t written = 0;
	for (;;) {
		status = control.CheckAbort();
		if (!status.IsOk())
			return status;
		size_t got = 0;
		status = data.Read(buffer.data(), buffer.size(), got);
		if (!status.IsOk())
			return status;
		if (got == 0)
			break;
		written += static_cast<int64_t>(got);
		if (written > size)
			break;
		if (archive_write_data(m_archive, buffer.data(), got) < 0)
			return Failure();
	}
	if (written != size)
		return Status::Fail(ErrorCode::Read, path + " changed its size while it was archived");
	return Status::Ok();
}

Status ArchiveWriter::Finish()
{
	if (archive_write_close(m_archive) != ARCHIVE_OK)
		return Failure();
	return m_sink->Finish();
}

std::string ArchiveLibraryVersion()
{
	return archive_version_string();
}

} // namespace smarchive
