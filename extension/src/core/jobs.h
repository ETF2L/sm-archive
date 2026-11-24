/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The jobs: one description per operation, built and checked by whoever
 * queues it (paths absolute and allowed, options valid), and RunJob(),
 * which runs one on the calling thread. Every output appears only on
 * success, through a temp file or staging directory renamed onto it; on
 * any failure the temp is gone and the output untouched. RunJob() names
 * the temp files and staging directories after the id of the job it runs,
 * unless the queuer fixed the temp path (the shims). */
#ifndef SMARCHIVE_CORE_JOBS_H
#define SMARCHIVE_CORE_JOBS_H

#include "archive_io.h"
#include "codec.h"
#include "control.h"
#include "detect.h"
#include "entry.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace smarchive {

using JobId = uint64_t;

/* A file a job writes and how. */
struct OutputFile {
	std::string path;
	/* Where it is written first, renamed onto path at the end: nullopt for
	 * the job's own UniqueTempPath(path, id), or a path the queuer fixed
	 * (the shims' "<outFile>.tmp"). */
	std::optional<std::string> fixedTempPath;
	bool overwrite = false;
	bool createPath = true;
};

/* Compress with options, or decompress with codec (nullopt: detect). */
struct CodecChoice {
	bool isCompress = false;
	CodecOptions options;
	std::optional<Codec> codec;
};

struct CodecFileJob {
	CodecChoice choice;
	std::string inputPath;
	OutputFile output;
};

struct CodecMemoryJob {
	CodecChoice choice;
	std::vector<uint8_t> input;
	/* Written here instead of kept in memory when set. */
	std::optional<OutputFile> output;
};

/* Which container to read: container nullopt detects everything; for a
 * tar, isCodecDetected detects its codec, else codec (nullopt: plain). */
struct ArchiveChoice {
	std::optional<Container> container;
	bool isCodecDetected = true;
	std::optional<Codec> codec;
};

struct ListJob {
	std::string archivePath;
	ArchiveChoice choice;
};

struct ExtractJob {
	std::string archivePath;
	ArchiveChoice choice;
	/* The directory to create; must not exist. It is written as the
	 * staging directory UniqueTempPath(targetPath, id) first. */
	std::string targetPath;
	bool createPath = true;
	std::vector<std::string> prefixes;
};

struct ReadEntryJob {
	std::string archivePath;
	ArchiveChoice choice;
	/* A clean archive path. */
	std::string entryPath;
};

/* A file or directory below the game directory and where it goes in the
 * archive; directories are added with everything in them. */
struct ArchiveSource {
	std::string path;
	std::string archivePath;
	bool isDirectory = false;
};

struct CreateJob {
	std::vector<ArchiveSource> sources;
	WriteOptions options;
	OutputFile output;
};

using JobSpec = std::variant<CodecFileJob, CodecMemoryJob, ListJob, ExtractJob, ReadEntryJob, CreateJob>;

struct Job {
	JobSpec spec;
	Limits limits = kDefaultLimits;
};

struct JobOutcome {
	Status status;
	/* List, Extract, ReadEntry and Create: the entries. */
	std::vector<EntryRecord> entries;
	/* Memory output and ReadEntry: the bytes. */
	std::vector<uint8_t> bytes;
	/* The formats read or written, where they apply. */
	std::optional<Container> container;
	std::optional<Codec> codec;
};

/* Runs job as the job id, the id its temp file or staging directory is
 * named after. */
JobOutcome RunJob(JobId id, const Job &job, const std::atomic<bool> &abort, Progress &progress);

/* The temp file a job of id writes output through. */
std::string TempPathOf(const OutputFile &output, JobId id);

/* For the unit tests only: called with the path of every temp file and
 * staging directory a job created, before it locks it, so a test can play
 * a concurrent job's sweep there. nullptr (the default) calls nothing. */
using TempCreatedHook = void (*)(const std::string &path);
void SetTempCreatedHook(TempCreatedHook hook);

} // namespace smarchive

#endif // SMARCHIVE_CORE_JOBS_H
