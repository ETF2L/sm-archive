/* Copyright 2026 ETF2L Contributors, Dave
 *
 * Path rules and the file system calls of the jobs (Linux). Paths a plugin
 * passes must stay below the game directory; paths inside archives are
 * normalized before anything is made of them. */
#ifndef SMARCHIVE_CORE_PATHS_H
#define SMARCHIVE_CORE_PATHS_H

#include "status.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace smarchive {

/* A path a plugin passes: relative to the game directory, not starting with
 * "/" or "file://", without "\" (which SourceMod's BuildPath() turns into
 * "/") and with no empty, "." or ".." parts. */
bool IsGameRelativePath(std::string_view path);

/* The text of the rule IsGameRelativePath() checks, for native errors. */
extern const char *const kGamePathRule;

/* Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF. */
bool IsValidUtf8(std::string_view text);

/* Turns an archive entry's stored name into a clean relative path: "\" and
 * "/" separate parts, empty and "." parts are dropped (so "./a//b/" is
 * "a/b"). EntryName for invalid UTF-8, UnsafeEntry for an absolute path or a
 * ".." part. An empty result names the archive's root. */
ErrorCode NormalizeEntryPath(std::string_view stored, std::string &normalized);

/* A path inside an archive a plugin names when creating one: what
 * NormalizeEntryPath() leaves unchanged; empty for the archive's root. */
bool IsCleanArchivePath(std::string_view path);

/* "<path>.<jobId>.smarchive.tmp": unique per job, next to path. */
std::string UniqueTempPath(const std::string &path, uint64_t jobId);

/* Whether name is the last part of a UniqueTempPath() of a file named
 * outputName, for any job id. */
bool IsUniqueTempNameOf(std::string_view name, std::string_view outputName);

/* a + "/" + b, or the one that is not empty. */
std::string JoinPath(const std::string &a, const std::string &b);

/* Everything before the last "/", empty for a single part. */
std::string ParentOf(const std::string &path);

namespace fs {

/* Whether anything (a dangling symlink too) is at path. */
bool Exists(const std::string &path);

bool IsDirectory(const std::string &path);

/* mkdir -p of the directory the file is in (mode 0775 less the umask). */
bool CreateParentDirectories(const std::string &filePath);

/* mkdir -p of path itself. */
bool CreateDirectories(const std::string &path);

/* Deletes path and, for a directory, everything in it, never following a
 * symlink. Missing paths are fine. */
void RemoveTree(const std::string &path);

/* What a running job holds on its temp file or staging directory: a
 * flock(), so that other jobs, of other server processes too, can tell it
 * from one an interrupted job left behind. Released when destroyed. */
class TempLock {
public:
	TempLock() = default;
	~TempLock();
	TempLock(const TempLock &) = delete;
	TempLock &operator=(const TempLock &) = delete;

	enum class Result {
		Taken,
		/* Another job holds a lock on it. */
		Held,
		/* path names something else by now (or the lock cannot be kept). */
		Gone,
	};

	/* Locks the file or directory open at descriptor, which the job just
	 * created at path, keeping the lock on a duplicate of descriptor so it
	 * outlives the caller's close. Nothing is deleted when it is not
	 * Taken. */
	Result Take(int descriptor, const std::string &path);

private:
	int m_descriptor = -1;
};

/* Deletes the temp file or staging directory at path (everything in it,
 * never following a symlink) unless a running job holds its TempLock or it
 * cannot be opened to be locked; anything else at path, a symlink too, is
 * deleted outright. Whether nothing is at path now. */
bool RemoveIfAbandoned(const std::string &path);

/* Clears path for a temp file: deletes a regular file there unless a
 * running job holds its TempLock, or a symlink. False when that file is
 * held or anything else is there, a directory too, which is left alone. */
bool RemoveAbandonedFile(const std::string &path);

/* Deletes every UniqueTempPath() of outputPath, of any job id, that no
 * running job holds: what interrupted jobs (a crash, a server stopped
 * meanwhile) left behind. */
void RemoveAbandonedTemps(const std::string &outputPath);

enum class RenameResult {
	Renamed,
	TargetExists,
	Failed,
};

/* Renames from onto to, replacing a file at to. */
RenameResult RenameReplacing(const std::string &from, const std::string &to);

/* Renames a file or directory onto to only while nothing is at to, checked
 * atomically. */
RenameResult RenameIfAbsent(const std::string &from, const std::string &to);

} // namespace fs

} // namespace smarchive

#endif // SMARCHIVE_CORE_PATHS_H
