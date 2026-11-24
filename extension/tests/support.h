/* Copyright 2026 ETF2L Contributors, Dave
 *
 * Helpers of the tests: files in the test's directory, running jobs. */
#ifndef SMARCHIVE_TESTS_SUPPORT_H
#define SMARCHIVE_TESTS_SUPPORT_H

#include "jobs.h"
#include "paths.h"

#include <string>
#include <vector>

namespace tests {

void SetWorkDirectory(const std::string &directory);
void SetFixtures(const std::string &directory);

/* The fixtures directory, empty when none was given. */
const std::string &Fixtures();

/* A path in the test's directory. */
std::string PathOf(const std::string &name);

std::string ReadFile(const std::string &path);
void WriteFile(const std::string &path, const std::string &content);
bool Exists(const std::string &path);

/* Bytes that compress somewhat, like a log: repeated lines with numbers. */
std::string SampleText(size_t length);

/* Bytes that do not compress. */
std::string RandomBytes(size_t length, unsigned seed);

/* The id Run() runs every job as, which its temp names carry. */
constexpr smarchive::JobId kTestJobId = 1;

smarchive::JobOutcome Run(const smarchive::JobSpec &spec, const smarchive::Limits &limits = smarchive::kDefaultLimits);

/* An output written through the job's own temp file. */
smarchive::OutputFile Output(const std::string &path, bool overwrite = false);

smarchive::CodecChoice Compress(const smarchive::CodecOptions &options);
smarchive::CodecChoice Decompress(std::optional<smarchive::Codec> codec);

smarchive::ArchiveChoice AutoArchive();

/* Takes the TempLock of the file or directory at path, as a running job
 * holds it. */
bool HoldTemp(smarchive::fs::TempLock &lock, const std::string &path);

/* The paths of the entries, in order. */
std::vector<std::string> PathsOf(const std::vector<smarchive::EntryRecord> &entries);

/* Every file and directory below directory, relative and sorted. */
std::vector<std::string> TreeOf(const std::string &directory);

} // namespace tests

#endif // SMARCHIVE_TESTS_SUPPORT_H
