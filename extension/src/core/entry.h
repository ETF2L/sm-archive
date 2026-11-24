/* Copyright 2026 ETF2L Contributors, Dave
 *
 * An archive entry as a job reports it back: what it is, and for
 * extraction and creation what became of it. */
#ifndef SMARCHIVE_CORE_ENTRY_H
#define SMARCHIVE_CORE_ENTRY_H

#include <cstdint>
#include <string>

namespace smarchive {

/* The values of the plugin API's ArchiveEntryType. */
enum class EntryType : int {
	File = 0,
	Directory,
	Symlink,
	Hardlink,
	/* Devices, FIFOs, sockets. */
	Other,
};

/* The values of the plugin API's ArchiveSkip: why an entry was not written. */
enum class SkipReason : int {
	None = 0,
	/* A symlink that is absolute or leads out of the target. */
	LinkOutside,
	/* A hardlink to a file this job did not extract. */
	LinkTargetMissing,
	/* A device, FIFO or socket. */
	SpecialFile,
	/* Creating: a symlink among the sources. */
	SourceSymlink,
};

struct EntryRecord {
	/* Normalized, relative, "/" between parts. */
	std::string path;
	EntryType type = EntryType::File;
	/* Unpacked size in bytes, -1 when the archive does not tell it. */
	int64_t size = -1;
	/* Archive bytes read for the entry while extracting it, -1 when unknown
	 * (listing, solid archives). */
	int64_t packedSize = -1;
	/* Seconds since 1970, 0 when the archive has none. */
	int64_t mtime = 0;
	bool isExecutable = false;
	/* For symlinks and hardlinks as stored. */
	std::string linkTarget;
	/* Extraction and creation: whether the entry was written. */
	bool isWritten = false;
	SkipReason skipReason = SkipReason::None;
};

} // namespace smarchive

#endif // SMARCHIVE_CORE_ENTRY_H
