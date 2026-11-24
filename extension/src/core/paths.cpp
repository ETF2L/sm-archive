/* Copyright 2026 ETF2L Contributors, Dave */
#include "paths.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <vector>

namespace smarchive {

namespace {

/* SourceMod's BuildPath() takes a path with this prefix as absolute. */
constexpr std::string_view kAbsolutePathPrefix = "file://";

constexpr mode_t kDirectoryMode = 0775;

bool IsDotPart(std::string_view part)
{
	return part == "." || part == "..";
}

/* The parts of path between its "/" (and, when backslashes count, "\"). */
std::vector<std::string_view> SplitParts(std::string_view path, bool isBackslashASeparator)
{
	std::vector<std::string_view> parts;
	size_t start = 0;
	for (size_t i = 0; i <= path.size(); i++) {
		bool isSeparator = i == path.size() || path[i] == '/' || (isBackslashASeparator && path[i] == '\\');
		if (!isSeparator)
			continue;
		parts.push_back(path.substr(start, i - start));
		start = i + 1;
	}
	return parts;
}

/* Whether path still names the file or directory open at descriptor: a
 * job may have deleted it and created its own there since it was opened. */
bool IsStillAt(int descriptor, const std::string &path)
{
	struct stat opened, named;
	return fstat(descriptor, &opened) == 0 && lstat(path.c_str(), &named) == 0 && opened.st_dev == named.st_dev &&
	       opened.st_ino == named.st_ino;
}

/* Opens what is at path and locks it, as a job deleting it holds it; -1
 * when it cannot be opened, a running job holds it or path names something
 * else by the time it is locked. */
int LockAbandoned(const std::string &path)
{
	int descriptor = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
	if (descriptor < 0)
		return -1;
	if (flock(descriptor, LOCK_EX | LOCK_NB) == 0 && IsStillAt(descriptor, path))
		return descriptor;
	close(descriptor);
	return -1;
}

int RemoveOne(const char *path, const struct stat *, int type, struct FTW *)
{
	if (type == FTW_DP)
		rmdir(path);
	else
		unlink(path);
	return 0;
}

} // namespace

const char *const kGamePathRule =
	"not start with \"/\" or \"file://\", have no \"\\\" and no empty, \".\" or \"..\" parts";

bool IsGameRelativePath(std::string_view path)
{
	if (path.empty() || path[0] == '/' || path.substr(0, kAbsolutePathPrefix.size()) == kAbsolutePathPrefix)
		return false;
	/* SourceMod's BuildPath() turns "\" into "/", so "..\x" would leave
	 * the game directory after this check. */
	if (path.find('\\') != std::string_view::npos)
		return false;
	for (std::string_view part : SplitParts(path, false))
		if (part.empty() || IsDotPart(part))
			return false;
	return true;
}

bool IsValidUtf8(std::string_view text)
{
	size_t i = 0;
	while (i < text.size()) {
		uint8_t lead = static_cast<uint8_t>(text[i]);
		size_t length;
		uint32_t point;
		if (lead < 0x80) {
			i++;
			continue;
		} else if ((lead & 0xe0) == 0xc0) {
			length = 2;
			point = lead & 0x1f;
		} else if ((lead & 0xf0) == 0xe0) {
			length = 3;
			point = lead & 0x0f;
		} else if ((lead & 0xf8) == 0xf0) {
			length = 4;
			point = lead & 0x07;
		} else {
			return false;
		}
		if (i + length > text.size())
			return false;
		for (size_t k = 1; k < length; k++) {
			uint8_t next = static_cast<uint8_t>(text[i + k]);
			if ((next & 0xc0) != 0x80)
				return false;
			point = (point << 6) | (next & 0x3f);
		}
		static const uint32_t kSmallest[] = {0, 0, 0x80, 0x800, 0x10000};
		bool isOverlong = point < kSmallest[length];
		bool isSurrogate = point >= 0xd800 && point <= 0xdfff;
		if (isOverlong || isSurrogate || point > 0x10ffff)
			return false;
		i += length;
	}
	return true;
}

ErrorCode NormalizeEntryPath(std::string_view stored, std::string &normalized)
{
	normalized.clear();
	if (!IsValidUtf8(stored))
		return ErrorCode::EntryName;
	if (!stored.empty() && (stored[0] == '/' || stored[0] == '\\'))
		return ErrorCode::UnsafeEntry;
	for (std::string_view part : SplitParts(stored, true)) {
		if (part.empty() || part == ".")
			continue;
		if (part == "..")
			return ErrorCode::UnsafeEntry;
		if (!normalized.empty())
			normalized += '/';
		normalized += part;
	}
	return ErrorCode::None;
}

bool IsCleanArchivePath(std::string_view path)
{
	std::string normalized;
	return NormalizeEntryPath(path, normalized) == ErrorCode::None && normalized == path;
}

std::string UniqueTempPath(const std::string &path, uint64_t jobId)
{
	return path + "." + std::to_string(jobId) + ".smarchive.tmp";
}

bool IsUniqueTempNameOf(std::string_view name, std::string_view outputName)
{
	constexpr std::string_view kSuffix = ".smarchive.tmp";
	size_t idStart = outputName.size() + 1;
	if (name.size() <= idStart + kSuffix.size() || name.substr(0, outputName.size()) != outputName ||
	    name[outputName.size()] != '.' || name.substr(name.size() - kSuffix.size()) != kSuffix)
		return false;
	std::string_view id = name.substr(idStart, name.size() - kSuffix.size() - idStart);
	return id.find_first_not_of("0123456789") == std::string_view::npos;
}

std::string JoinPath(const std::string &a, const std::string &b)
{
	if (a.empty())
		return b;
	if (b.empty())
		return a;
	return a + "/" + b;
}

std::string ParentOf(const std::string &path)
{
	size_t separator = path.rfind('/');
	return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

namespace fs {

bool Exists(const std::string &path)
{
	struct stat info;
	return lstat(path.c_str(), &info) == 0;
}

bool IsDirectory(const std::string &path)
{
	struct stat info;
	return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool CreateDirectories(const std::string &path)
{
	if (path.empty() || IsDirectory(path))
		return true;
	if (!CreateDirectories(ParentOf(path)))
		return false;
	return mkdir(path.c_str(), kDirectoryMode) == 0 || (errno == EEXIST && IsDirectory(path));
}

bool CreateParentDirectories(const std::string &filePath)
{
	return CreateDirectories(ParentOf(filePath));
}

void RemoveTree(const std::string &path)
{
	struct stat info;
	if (lstat(path.c_str(), &info) != 0)
		return;
	if (!S_ISDIR(info.st_mode)) {
		unlink(path.c_str());
		return;
	}
	constexpr int kOpenDirectories = 16;
	nftw(path.c_str(), RemoveOne, kOpenDirectories, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
}

TempLock::~TempLock()
{
	if (m_descriptor >= 0)
		close(m_descriptor);
}

TempLock::Result TempLock::Take(int descriptor, const std::string &path)
{
	if (m_descriptor >= 0)
		close(m_descriptor);
	m_descriptor = -1;
	if (flock(descriptor, LOCK_EX | LOCK_NB) != 0)
		return Result::Held;
	if (IsStillAt(descriptor, path))
		m_descriptor = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
	if (m_descriptor >= 0)
		return Result::Taken;
	flock(descriptor, LOCK_UN);
	return Result::Gone;
}

bool RemoveIfAbandoned(const std::string &path)
{
	struct stat info;
	if (lstat(path.c_str(), &info) != 0)
		return true;
	if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode)) {
		RemoveTree(path);
		return !Exists(path);
	}
	/* Held while deleting, so no job takes it meanwhile. */
	int descriptor = LockAbandoned(path);
	if (descriptor < 0)
		return false;
	RemoveTree(path);
	close(descriptor);
	return !Exists(path);
}

bool RemoveAbandonedFile(const std::string &path)
{
	struct stat info;
	if (lstat(path.c_str(), &info) != 0)
		return true;
	if (S_ISLNK(info.st_mode))
		return unlink(path.c_str()) == 0 || errno == ENOENT;
	if (!S_ISREG(info.st_mode))
		return false;
	/* Held while deleting, so no job takes it meanwhile. */
	int descriptor = LockAbandoned(path);
	if (descriptor < 0)
		return false;
	struct stat opened;
	bool isRemoved = fstat(descriptor, &opened) == 0 && S_ISREG(opened.st_mode) &&
	                 (unlink(path.c_str()) == 0 || errno == ENOENT);
	close(descriptor);
	return isRemoved;
}

void RemoveAbandonedTemps(const std::string &outputPath)
{
	size_t separator = outputPath.rfind('/');
	std::string directoryPath = separator == std::string::npos ? "." : outputPath.substr(0, separator);
	std::string outputName = separator == std::string::npos ? outputPath : outputPath.substr(separator + 1);
	DIR *directory = opendir(directoryPath.empty() ? "/" : directoryPath.c_str());
	if (!directory)
		return;
	std::vector<std::string> temps;
	while (struct dirent *entry = readdir(directory))
		if (IsUniqueTempNameOf(entry->d_name, outputName))
			temps.push_back(directoryPath + "/" + entry->d_name);
	closedir(directory);
	for (const std::string &temp : temps)
		RemoveIfAbandoned(temp);
}

RenameResult RenameReplacing(const std::string &from, const std::string &to)
{
	return rename(from.c_str(), to.c_str()) == 0 ? RenameResult::Renamed : RenameResult::Failed;
}

RenameResult RenameIfAbsent(const std::string &from, const std::string &to)
{
	if (renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0)
		return RenameResult::Renamed;
	if (errno == EEXIST)
		return RenameResult::TargetExists;
	if (errno != EINVAL && errno != ENOSYS)
		return RenameResult::Failed;

	/* A file system without RENAME_NOREPLACE: link() refuses an existing
	 * target for files; for a directory, rename() refuses a non-empty one
	 * and the check before it covers the rest. */
	struct stat info;
	if (lstat(from.c_str(), &info) != 0)
		return RenameResult::Failed;
	if (!S_ISDIR(info.st_mode)) {
		if (link(from.c_str(), to.c_str()) != 0)
			return errno == EEXIST ? RenameResult::TargetExists : RenameResult::Failed;
		unlink(from.c_str());
		return RenameResult::Renamed;
	}
	if (Exists(to))
		return RenameResult::TargetExists;
	return rename(from.c_str(), to.c_str()) == 0 ? RenameResult::Renamed : RenameResult::Failed;
}

} // namespace fs

} // namespace smarchive
