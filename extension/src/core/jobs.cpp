/* Copyright 2026 ETF2L Contributors, Dave */
#include "jobs.h"
#include "extract.h"
#include "paths.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <functional>
#include <set>
#include <thread>

namespace smarchive {

namespace {

constexpr int kExecutableBits = 0111;
/* As fopen() creates files: read and write for all, less the umask. */
constexpr mode_t kTempFileMode = 0666;
constexpr mode_t kStagingDirectoryMode = 0775;
/* The compressed bytes the tar probe of Auto may read before it gives up.
 * A tar header comes from the first block of a stream; lz4 decodes a
 * compressed block only once it has all of it, and its largest (4 MiB, its
 * default) is read with its headers and room to spare. */
constexpr size_t kProbeInputLimit = 4 * 1024 * 1024 + 64 * 1024;

Status OpenInputFile(const std::string &path, JobControl &control, std::unique_ptr<FileSource> &source)
{
	UniqueFile file(fopen(path.c_str(), "rb"));
	if (!file)
		return Status::Fail(ErrorCode::OpenInput, "the input cannot be opened");
	source = std::make_unique<FileSource>(std::move(file), control);
	control.GetProgress().inputSize.store(source->Size(), std::memory_order_relaxed);
	return Status::Ok();
}

/* The checks before the input is opened: an existing output, its
 * directory. */
Status PrepareOutput(const OutputFile &output)
{
	if (!output.overwrite && fs::Exists(output.path))
		return Status::Fail(ErrorCode::OutputExists, "the output exists");
	if (output.createPath && !fs::CreateParentDirectories(output.path))
		return Status::Fail(ErrorCode::CreatePath, "the output's directory cannot be created");
	return Status::Ok();
}

Status Publish(const OutputFile &output, const std::string &tempPath)
{
	fs::RenameResult result = output.overwrite ? fs::RenameReplacing(tempPath, output.path)
	                                           : fs::RenameIfAbsent(tempPath, output.path);
	switch (result) {
	case fs::RenameResult::Renamed:      return Status::Ok();
	case fs::RenameResult::TargetExists: return Status::Fail(ErrorCode::OutputExists, "the output appeared meanwhile");
	case fs::RenameResult::Failed:       break;
	}
	return Status::Fail(ErrorCode::Rename, "the temp file cannot be renamed onto the output");
}

std::atomic<TempCreatedHook> g_tempCreatedHook{nullptr};

/* How often a temp file or staging directory is created before a job gives
 * up, and how long it waits for a sweeping job between two tries. */
constexpr int kCreateAttempts = 3;
constexpr std::chrono::milliseconds kSweepWait{10};

/* Clears path with clear, creates the entry there with create (a
 * descriptor, or -1 with errno set) and locks it. Another job's sweep may
 * lock and delete the entry between its creation and this job's lock; since
 * this job just created it, a lock found on it or a path naming something
 * else by then is such a sweep, and it is created again. Only a path that
 * stays held by another job, or fails to be created, fails the job. */
Status CreateLocked(const std::string &path, const std::function<bool()> &clear, const std::function<int()> &create,
                    ErrorCode code, const std::string &what, fs::TempLock &lock, int &descriptor)
{
	for (int attempt = 1;; attempt++) {
		bool isLastAttempt = attempt == kCreateAttempts;
		if (!clear()) {
			if (isLastAttempt)
				return Status::Fail(code, what + " is held by another job or is in the way");
			std::this_thread::sleep_for(kSweepWait);
			continue;
		}
		descriptor = create();
		if (descriptor < 0) {
			bool isRaced = errno == EEXIST || errno == ENOENT;
			if (isRaced && !isLastAttempt)
				continue;
			return Status::Fail(code, what + " cannot be created");
		}
		if (TempCreatedHook hook = g_tempCreatedHook.load())
			hook(path);
		fs::TempLock::Result taken = lock.Take(descriptor, path);
		if (taken == fs::TempLock::Result::Taken)
			return Status::Ok();
		close(descriptor);
		descriptor = -1;
		if (isLastAttempt)
			return Status::Fail(code, what + " was swept by another job each time it was created");
		if (taken == fs::TempLock::Result::Held)
			std::this_thread::sleep_for(kSweepWait);
	}
}

/* Creates the output's temp file anew, never through a symlink or into a
 * file already there, and locks it for the job; what interrupted jobs left
 * next to the output is deleted first. */
Status CreateTempFile(const OutputFile &output, const std::string &tempPath, fs::TempLock &lock, UniqueFile &file)
{
	fs::RemoveAbandonedTemps(output.path);
	int descriptor = -1;
	Status status = CreateLocked(
		tempPath, [&tempPath] { return fs::RemoveAbandonedFile(tempPath); },
		[&tempPath] {
			return open(tempPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, kTempFileMode);
		},
		ErrorCode::OpenOutput, "the temp file", lock, descriptor);
	if (!status.IsOk())
		return status;
	file.reset(fdopen(descriptor, "wb"));
	if (!file) {
		close(descriptor);
		/* Locked, so the path is still this job's. */
		unlink(tempPath.c_str());
		return Status::Fail(ErrorCode::OpenOutput, "the temp file cannot be created");
	}
	return Status::Ok();
}

/* Lets write fill the temp file of the job id through a sink, then puts it
 * in the output's place; on any failure the temp file is deleted. */
Status WriteThroughTemp(const OutputFile &output, JobId id, JobControl &control,
                        const std::function<Status(Sink &)> &write)
{
	std::string tempPath = TempPathOf(output, id);
	fs::TempLock lock;
	UniqueFile file;
	Status created = CreateTempFile(output, tempPath, lock, file);
	if (!created.IsOk())
		return created;
	FileSink sink(std::move(file), control);
	Status status = write(sink);
	if (status.IsOk())
		status = sink.Finish();
	if (status.IsOk())
		status = Publish(output, tempPath);
	if (!status.IsOk())
		unlink(tempPath.c_str());
	return status;
}

Status DetectCodec(FileSource &file, const std::string &path, Codec &codec)
{
	uint8_t head[kDetectHeadSize];
	size_t got = 0;
	Status status = file.Peek(head, sizeof(head), got);
	if (!status.IsOk())
		return status;
	std::optional<Codec> detected = CodecFromMagic(head, got);
	if (!detected)
		detected = CodecFromName(path);
	if (!detected)
		return Status::Fail(ErrorCode::UnknownFormat, "the input's format is unknown");
	codec = *detected;
	return Status::Ok();
}

/* The first kProbeInputLimit bytes of a file, each read after a look at
 * the job's abort flag. */
class ProbeSource : public Source {
public:
	ProbeSource(Source &file, const JobControl &job) : m_file(file), m_job(job) {}

	Status Read(uint8_t *buffer, size_t capacity, size_t &got) override
	{
		got = 0;
		Status status = m_job.CheckAbort();
		if (!status.IsOk() || m_left == 0)
			return status;
		status = m_file.Read(buffer, capacity < m_left ? capacity : m_left, got);
		m_left -= got;
		return status;
	}

private:
	Source &m_file;
	const JobControl &m_job;
	size_t m_left = kProbeInputLimit;
};

/* isTar: whether the start of path decompressed with codec is a tar header.
 * Fails only when the job is cancelled meanwhile. */
Status ProbeDecodesToTar(const std::string &path, Codec codec, const JobControl &job, bool &isTar)
{
	isTar = false;
	/* Its reads are no input of the job; the job's abort flag is looked at
	 * by ProbeSource. */
	std::atomic<bool> neverAborted{false};
	Progress unused;
	JobControl probe(neverAborted, unused, kNoLimits, false);
	std::unique_ptr<FileSource> file;
	std::unique_ptr<Transform> decoder;
	if (!OpenInputFile(path, probe, file).IsOk() || !CreateDecoder(codec, decoder).IsOk())
		return Status::Ok();
	ProbeSource limited(*file, job);
	TransformSource decoded(limited, std::move(decoder));
	uint8_t head[kDetectHeadSize];
	size_t length = 0;
	while (length < sizeof(head)) {
		size_t got = 0;
		Status status = decoded.Read(head + length, sizeof(head) - length, got);
		if (status.code == ErrorCode::Cancelled)
			return status;
		if (!status.IsOk() || got == 0)
			break;
		length += got;
	}
	isTar = IsTarHeader(head, length);
	return Status::Ok();
}

/* The container and codec an archive is read as; isGuessed when only the
 * file name speaks for a tar. */
Status ChooseArchiveFormat(FileSource &file, const std::string &path, const ArchiveChoice &choice,
                           const JobControl &control, Container &container, std::optional<Codec> &codec,
                           bool &isGuessed)
{
	isGuessed = false;
	uint8_t head[kDetectHeadSize];
	size_t got = 0;
	Status status = file.Peek(head, sizeof(head), got);
	if (!status.IsOk())
		return status;

	if (choice.container && *choice.container != Container::Tar) {
		container = *choice.container;
		codec.reset();
		return Status::Ok();
	}
	if (!choice.container) {
		if (std::optional<Container> magic = ContainerFromMagic(head, got)) {
			container = *magic;
			codec.reset();
			return Status::Ok();
		}
	}

	container = Container::Tar;
	if (choice.container && !choice.isCodecDetected) {
		codec = choice.codec;
		return Status::Ok();
	}
	codec = CodecFromMagic(head, got);
	if (!codec && !IsTarHeader(head, got))
		codec = CodecFromName(path);
	if (choice.container)
		return Status::Ok();

	bool isTar = IsTarHeader(head, got);
	if (codec) {
		status = ProbeDecodesToTar(path, *codec, control, isTar);
		if (!status.IsOk())
			return status;
	}
	isGuessed = !isTar;
	if (isTar || NameSaysTar(path))
		return Status::Ok();
	return Status::Fail(ErrorCode::UnknownFormat, codec ? "the input is compressed but no tar archive"
	                                                    : "the input's format is unknown");
}

/* The tar a codec decodes from an archive file. Every decoded byte counts
 * against the limits, the data of the entries libarchive skips (which the
 * core never writes) included. */
class DecodedArchiveSource : public Source {
public:
	DecodedArchiveSource(Source &file, std::unique_ptr<Transform> decoder, JobControl &control)
		: m_decoded(file, std::move(decoder)), m_control(control)
	{
	}

	Status Read(uint8_t *buffer, size_t capacity, size_t &got) override
	{
		Status status = m_decoded.Read(buffer, capacity, got);
		return status.IsOk() ? m_control.CountDecoded(got) : status;
	}

private:
	TransformSource m_decoded;
	JobControl &m_control;
};

/* An archive opened for reading with everything it is read through. */
struct ArchiveInput {
	std::unique_ptr<FileSource> file;
	std::unique_ptr<DecodedArchiveSource> decoded;
	std::unique_ptr<ArchiveReader> reader;
	Container container = Container::Tar;
	std::optional<Codec> codec;
	bool isGuessed = false;
};

Status OpenArchive(const std::string &path, const ArchiveChoice &choice, JobControl &control, ArchiveInput &input)
{
	Status status = OpenInputFile(path, control, input.file);
	if (status.IsOk())
		status = ChooseArchiveFormat(*input.file, path, choice, control, input.container, input.codec,
		                             input.isGuessed);
	if (!status.IsOk())
		return status;

	if (input.container != Container::Tar)
		return ArchiveReader::Open(input.container, *input.file, input.file.get(), control, input.reader);
	Source *source = input.file.get();
	if (input.codec) {
		std::unique_ptr<Transform> decoder;
		status = CreateDecoder(*input.codec, decoder);
		if (!status.IsOk())
			return status;
		input.decoded = std::make_unique<DecodedArchiveSource>(*input.file, std::move(decoder), control);
		source = input.decoded.get();
	}
	status = ArchiveReader::Open(Container::Tar, *source, nullptr, control, input.reader);
	/* A tar only by its name that libarchive does not take is no tar. */
	if (status.code == ErrorCode::Corrupt && input.isGuessed)
		return Status::Fail(ErrorCode::UnknownFormat, "the input's format is unknown", status.libraryCode);
	return status;
}

/* Runs a codec over source into sink. */
Status Transcode(const CodecChoice &choice, Codec codec, int64_t inputSize, Source &source, Sink &sink,
                 JobControl &control)
{
	std::unique_ptr<Transform> transform;
	Status status = choice.isCompress ? CreateEncoder(choice.options, inputSize, transform)
	                                  : CreateDecoder(codec, transform);
	if (!status.IsOk())
		return status;
	TransformSource transformed(source, std::move(transform));
	return Pump(transformed, sink, control);
}

class JobRunner {
public:
	JobRunner(JobId id, const std::atomic<bool> &abort, Progress &progress)
		: m_id(id), m_abort(abort), m_progress(progress)
	{
	}

	JobOutcome operator()(const Job &job)
	{
		m_limits = job.limits;
		JobOutcome outcome;
		outcome.status = std::visit([this, &outcome](const auto &spec) { return Run(spec, outcome); }, job.spec);
		if (!outcome.status.IsOk()) {
			outcome.entries.clear();
			outcome.bytes.clear();
		}
		return outcome;
	}

private:
	JobControl Control(bool enforceOutputLimits) const
	{
		return JobControl(m_abort, m_progress, m_limits, enforceOutputLimits);
	}

	Status Run(const CodecFileJob &job, JobOutcome &outcome)
	{
		JobControl control = Control(!job.choice.isCompress);
		Status status = PrepareOutput(job.output);
		std::unique_ptr<FileSource> input;
		if (status.IsOk())
			status = OpenInputFile(job.inputPath, control, input);
		Codec codec = job.choice.isCompress ? CodecOf(job.choice.options) : job.choice.codec.value_or(Codec::Gzip);
		if (status.IsOk() && !job.choice.isCompress && !job.choice.codec)
			status = DetectCodec(*input, job.inputPath, codec);
		if (!status.IsOk())
			return status;
		outcome.codec = codec;
		int64_t inputSize = input->Size();
		return WriteThroughTemp(job.output, m_id, control, [&](Sink &sink) {
			return Transcode(job.choice, codec, inputSize, *input, sink, control);
		});
	}

	Status Run(const CodecMemoryJob &job, JobOutcome &outcome)
	{
		JobControl control = Control(!job.choice.isCompress);
		m_progress.inputSize.store(static_cast<int64_t>(job.input.size()), std::memory_order_relaxed);
		Codec codec;
		if (job.choice.isCompress) {
			codec = CodecOf(job.choice.options);
		} else if (job.choice.codec) {
			codec = *job.choice.codec;
		} else {
			std::optional<Codec> detected = CodecFromMagic(job.input.data(), job.input.size());
			if (!detected)
				return Status::Fail(ErrorCode::UnknownFormat, "the input's format is unknown");
			codec = *detected;
		}
		outcome.codec = codec;
		MemorySource source(job.input, control);
		int64_t inputSize = static_cast<int64_t>(job.input.size());
		if (!job.output) {
			MemorySink sink(outcome.bytes, control);
			return Transcode(job.choice, codec, inputSize, source, sink, control);
		}
		Status status = PrepareOutput(*job.output);
		if (!status.IsOk())
			return status;
		return WriteThroughTemp(*job.output, m_id, control, [&](Sink &sink) {
			return Transcode(job.choice, codec, inputSize, source, sink, control);
		});
	}

	Status Run(const ListJob &job, JobOutcome &outcome)
	{
		JobControl control = Control(true);
		ArchiveInput input;
		Status status = OpenArchive(job.archivePath, job.choice, control, input);
		if (!status.IsOk())
			return status;
		outcome.container = input.container;
		outcome.codec = input.codec;
		return ListEntries(*input.reader, control, outcome.entries);
	}

	Status Run(const ExtractJob &job, JobOutcome &outcome)
	{
		JobControl control = Control(true);
		if (fs::Exists(job.targetPath))
			return Status::Fail(ErrorCode::OutputExists, "the target directory exists");
		if (job.createPath && !fs::CreateParentDirectories(job.targetPath))
			return Status::Fail(ErrorCode::CreatePath, "the target's parent directory cannot be created");
		ArchiveInput input;
		Status status = OpenArchive(job.archivePath, job.choice, control, input);
		if (!status.IsOk())
			return status;
		outcome.container = input.container;
		outcome.codec = input.codec;

		std::string stagingPath = UniqueTempPath(job.targetPath, m_id);
		fs::TempLock lock;
		status = CreateStagingDirectory(stagingPath, job.targetPath, lock);
		if (!status.IsOk())
			return status;
		ExtractRequest request{stagingPath, job.prefixes,
		                       input.container == Container::Tar || input.container == Container::Zip};
		status = ExtractEntries(*input.reader, request, control, outcome.entries);
		if (status.IsOk()) {
			fs::RenameResult renamed = fs::RenameIfAbsent(stagingPath, job.targetPath);
			if (renamed == fs::RenameResult::TargetExists)
				status = Status::Fail(ErrorCode::OutputExists, "the target directory appeared meanwhile");
			else if (renamed == fs::RenameResult::Failed)
				status = Status::Fail(ErrorCode::Rename, "the staging directory cannot be renamed onto the target");
		}
		if (!status.IsOk())
			fs::RemoveTree(stagingPath);
		return status;
	}

	/* Creates the extraction's staging directory at path anew and locks it
	 * for the job; what interrupted jobs left next to the target is deleted
	 * first. */
	static Status CreateStagingDirectory(const std::string &path, const std::string &targetPath, fs::TempLock &lock)
	{
		fs::RemoveAbandonedTemps(targetPath);
		int descriptor = -1;
		Status status = CreateLocked(
			path, [&path] { return fs::RemoveIfAbandoned(path); },
			[&path] {
				if (mkdir(path.c_str(), kStagingDirectoryMode) != 0)
					return -1;
				int descriptor = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
				if (descriptor < 0) {
					/* Not left behind unlocked; the failure is the open's. */
					int openError = errno;
					rmdir(path.c_str());
					errno = openError;
				}
				return descriptor;
			},
			ErrorCode::CreatePath, "the staging directory", lock, descriptor);
		if (descriptor >= 0)
			close(descriptor);
		return status;
	}

	Status Run(const ReadEntryJob &job, JobOutcome &outcome)
	{
		JobControl control = Control(true);
		ArchiveInput input;
		Status status = OpenArchive(job.archivePath, job.choice, control, input);
		if (!status.IsOk())
			return status;
		outcome.container = input.container;
		outcome.codec = input.codec;
		EntryRecord record;
		status = ReadOneEntry(*input.reader, job.entryPath, control, outcome.bytes, record);
		if (status.IsOk())
			outcome.entries.push_back(std::move(record));
		return status;
	}

	Status Run(const CreateJob &job, JobOutcome &outcome)
	{
		JobControl control = Control(false);
		Status status = PrepareOutput(job.output);
		std::vector<PlannedEntry> plan;
		if (status.IsOk())
			status = PlanSources(job.sources, plan, outcome.entries);
		if (!status.IsOk())
			return status;
		outcome.container = ContainerOf(job.options);
		if (const auto *tar = std::get_if<TarWriteOptions>(&job.options); tar && tar->codec)
			outcome.codec = CodecOf(*tar->codec);

		return WriteThroughTemp(job.output, m_id, control, [&](Sink &sink) {
			std::unique_ptr<ArchiveWriter> writer;
			Status written = ArchiveWriter::Open(job.options, sink, writer);
			for (size_t i = 0; written.IsOk() && i < plan.size(); i++)
				written = AddPlanned(*writer, plan[i], control);
			if (written.IsOk())
				written = writer->Finish();
			return written;
		});
	}

	/* A file or directory to put in the archive, found before writing. */
	struct PlannedEntry {
		std::string diskPath;
		std::string archivePath;
		bool isDirectory = false;
		int64_t size = 0;
		bool isExecutable = false;
		int64_t mtime = 0;
	};

	Status AddPlanned(ArchiveWriter &writer, const PlannedEntry &entry, JobControl &control)
	{
		Status status = control.CheckAbort();
		if (!status.IsOk())
			return status;
		if (entry.isDirectory)
			return writer.AddDirectory(entry.archivePath, entry.mtime);
		std::unique_ptr<FileSource> data;
		status = OpenInputFile(entry.diskPath, control, data);
		if (!status.IsOk())
			return status;
		status = writer.AddFile(entry.archivePath, entry.size, entry.isExecutable, entry.mtime, *data, control);
		control.GetProgress().entriesDone.fetch_add(1, std::memory_order_relaxed);
		return status;
	}

	/* Walks the sources; symlinks and special files are reported as
	 * skipped. Two sources for one archive path fail the job. */
	Status PlanSources(const std::vector<ArchiveSource> &sources, std::vector<PlannedEntry> &plan,
	                   std::vector<EntryRecord> &records)
	{
		std::set<std::string> archivePaths;
		for (const ArchiveSource &source : sources) {
			Status status = PlanPath(source.path, source.archivePath, source.isDirectory, true, plan, records,
			                         archivePaths);
			if (!status.IsOk())
				return status;
		}
		return Status::Ok();
	}

	Status PlanPath(const std::string &diskPath, const std::string &archivePath, bool isDirectoryExpected,
	                bool isNamedByPlugin, std::vector<PlannedEntry> &plan, std::vector<EntryRecord> &records,
	                std::set<std::string> &archivePaths)
	{
		struct stat info;
		if (lstat(diskPath.c_str(), &info) != 0)
			return Status::Fail(ErrorCode::OpenInput, "the source \"" + archivePath + "\" does not exist");
		bool isDirectory = S_ISDIR(info.st_mode);
		if (isNamedByPlugin && isDirectory != isDirectoryExpected && !S_ISLNK(info.st_mode))
			return Status::Fail(ErrorCode::OpenInput, isDirectoryExpected ? "a source directory is no directory"
			                                                              : "a source file is a directory");

		EntryRecord record;
		record.path = archivePath;
		record.mtime = static_cast<int64_t>(info.st_mtime);
		if (S_ISLNK(info.st_mode) || (!isDirectory && !S_ISREG(info.st_mode))) {
			record.type = S_ISLNK(info.st_mode) ? EntryType::Symlink : EntryType::Other;
			record.skipReason = S_ISLNK(info.st_mode) ? SkipReason::SourceSymlink : SkipReason::SpecialFile;
			records.push_back(std::move(record));
			return Status::Ok();
		}
		if (!archivePath.empty() && !archivePaths.insert(archivePath).second)
			return Status::Fail(ErrorCode::UnsafeEntry, "two sources go to \"" + archivePath + "\" in the archive");

		PlannedEntry planned{diskPath, archivePath, isDirectory, 0, false, record.mtime};
		record.isWritten = true;
		if (!isDirectory) {
			planned.size = static_cast<int64_t>(info.st_size);
			planned.isExecutable = (info.st_mode & kExecutableBits) != 0;
			record.size = planned.size;
			record.isExecutable = planned.isExecutable;
			plan.push_back(planned);
			records.push_back(std::move(record));
			return Status::Ok();
		}

		record.type = EntryType::Directory;
		record.size = 0;
		if (!archivePath.empty()) {
			plan.push_back(planned);
			records.push_back(std::move(record));
		}
		std::vector<std::string> names;
		Status status = ListDirectory(diskPath, names);
		for (size_t i = 0; status.IsOk() && i < names.size(); i++) {
			status = CheckFoundName(names[i], archivePath);
			if (status.IsOk())
				status = PlanPath(JoinPath(diskPath, names[i]), JoinPath(archivePath, names[i]), false, false, plan,
				                  records, archivePaths);
		}
		return status;
	}

	/* A name found in a source directory becomes one part of an archive
	 * path; one that is not UTF-8 or holds "\" (a separator when archives
	 * are read) would not come back as it went in. */
	static Status CheckFoundName(const std::string &name, const std::string &directoryArchivePath)
	{
		std::string where = directoryArchivePath.empty() ? "the archive's root" : "\"" + directoryArchivePath + "\"";
		if (!IsValidUtf8(name))
			return Status::Fail(ErrorCode::EntryName, "a file for " + where + " has a name that is not valid UTF-8");
		if (name.find('\\') != std::string::npos)
			return Status::Fail(ErrorCode::EntryName,
			                    "the file \"" + name + "\" for " + where + " has \"\\\" in its name");
		return Status::Ok();
	}

	/* The names in a directory, sorted, so archives come out the same. */
	static Status ListDirectory(const std::string &path, std::vector<std::string> &names)
	{
		DIR *directory = opendir(path.c_str());
		if (!directory)
			return Status::Fail(ErrorCode::OpenInput, "a source directory cannot be read");
		while (struct dirent *entry = readdir(directory)) {
			std::string name = entry->d_name;
			if (name != "." && name != "..")
				names.push_back(name);
		}
		closedir(directory);
		std::sort(names.begin(), names.end());
		return Status::Ok();
	}

	JobId m_id;
	const std::atomic<bool> &m_abort;
	Progress &m_progress;
	Limits m_limits = kDefaultLimits;
};

} // namespace

void SetTempCreatedHook(TempCreatedHook hook)
{
	g_tempCreatedHook.store(hook);
}

std::string TempPathOf(const OutputFile &output, JobId id)
{
	return output.fixedTempPath ? *output.fixedTempPath : UniqueTempPath(output.path, id);
}

JobOutcome RunJob(JobId id, const Job &job, const std::atomic<bool> &abort, Progress &progress)
{
	JobRunner runner(id, abort, progress);
	return runner(job);
}

} // namespace smarchive
