/* Copyright 2026 ETF2L Contributors, Dave
 *
 * Listing, extracting and reading entries of an opened archive, with every
 * check against hostile archives: names must be UTF-8 and stay inside the
 * archive (no absolute paths, no ".."), no path may come twice, encrypted
 * entries and the entry limit fail the job. Extraction writes only below a
 * fresh staging directory; symlinks are created only when they resolve
 * inside it, hardlinks only to files this job extracted, special files
 * never, and every path is resolved through the links created so far, so
 * nothing is written outside. After the last entry every symlink is judged
 * again against the finished tree; one that a later link made lead out
 * fails the job. */
#ifndef SMARCHIVE_CORE_EXTRACT_H
#define SMARCHIVE_CORE_EXTRACT_H

#include "archive_io.h"
#include "entry.h"

#include <string>
#include <vector>

namespace smarchive {

/* Every entry, checked. */
Status ListEntries(ArchiveReader &reader, JobControl &control, std::vector<EntryRecord> &records);

struct ExtractRequest {
	/* The empty directory to extract into, which the caller created. */
	std::string stagingPath;
	/* Clean archive paths; only entries at or below one of them are written
	 * and reported. Empty: every entry. */
	std::vector<std::string> prefixes;
	/* Whether packed sizes can be told per entry (not for solid formats). */
	bool hasPackedSizes = true;
};

/* Extracts into request.stagingPath, which the caller deletes on failure;
 * records are the selected entries. */
Status ExtractEntries(ArchiveReader &reader, const ExtractRequest &request, JobControl &control,
                      std::vector<EntryRecord> &records);

/* Reads the regular file at the clean archive path entryPath into bytes
 * (within maxMemory); EntryNotFound when the archive has none. */
Status ReadOneEntry(ArchiveReader &reader, const std::string &entryPath, JobControl &control,
                    std::vector<uint8_t> &bytes, EntryRecord &record);

} // namespace smarchive

#endif // SMARCHIVE_CORE_EXTRACT_H
