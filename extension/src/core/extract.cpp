/* Copyright 2026 ETF2L Contributors, Dave */
#include "extract.h"
#include "paths.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <set>

namespace smarchive {

namespace {

constexpr mode_t kFileMode = 0664;
constexpr mode_t kExecutableMode = 0775;
/* As the kernel's limit of symlinks followed in one lookup. */
constexpr int kMaxLinkHops = 40;

/* The checks every entry passes before anything is made of it. */
class EntryChecker {
public:
	explicit EntryChecker(JobControl &control) : m_control(control) {}

	/* The entry's normalized path in path; an empty path is the root
	 * directory, which only a directory entry may name. */
	Status Check(const RawEntry &raw, std::string &path)
	{
		Status status = m_control.CheckAbort();
		if (status.IsOk())
			status = m_control.CountEntry();
		if (!status.IsOk())
			return status;
		if (raw.isEncrypted)
			return Status::Fail(ErrorCode::Encrypted, "the entry \"" + raw.storedPath + "\" is encrypted");
		if (!raw.hasName)
			return Status::Fail(ErrorCode::EntryName, "an entry's name is not valid UTF-8");

		ErrorCode code = NormalizeEntryPath(raw.storedPath, path);
		if (code == ErrorCode::EntryName)
			return Status::Fail(code, "an entry's name is not valid UTF-8");
		if (code == ErrorCode::UnsafeEntry)
			return Status::Fail(code, "the entry \"" + raw.storedPath + "\" is absolute or contains \"..\"");
		if (path.empty()) {
			if (raw.type == EntryType::Directory)
				return Status::Ok();
			return Status::Fail(ErrorCode::UnsafeEntry, "an entry that is no directory names the archive's root");
		}
		if (!m_seen.insert(path).second)
			return Status::Fail(ErrorCode::UnsafeEntry, "the archive has \"" + path + "\" more than once");
		return Status::Ok();
	}

private:
	JobControl &m_control;
	std::set<std::string> m_seen;
};

EntryRecord RecordOf(const RawEntry &raw, const std::string &path)
{
	EntryRecord record;
	record.path = path;
	record.type = raw.type;
	record.size = raw.type == EntryType::File ? raw.size : (raw.size < 0 ? -1 : 0);
	record.mtime = raw.mtime;
	record.isExecutable = raw.type == EntryType::File && raw.isExecutable;
	record.linkTarget = raw.linkTarget;
	return record;
}

bool IsSelected(const std::string &path, const std::vector<std::string> &prefixes)
{
	if (prefixes.empty())
		return true;
	return std::any_of(prefixes.begin(), prefixes.end(), [&path](const std::string &prefix) {
		return path == prefix || (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 &&
		                          path[prefix.size()] == '/');
	});
}

std::vector<std::string> SplitOnSlash(const std::string &path)
{
	std::vector<std::string> parts;
	size_t start = 0;
	while (start <= path.size()) {
		size_t end = path.find('/', start);
		if (end == std::string::npos)
			end = path.size();
		parts.push_back(path.substr(start, end - start));
		start = end + 1;
	}
	return parts;
}

std::string JoinParts(const std::vector<std::string> &parts)
{
	std::string joined;
	for (const std::string &part : parts)
		joined = JoinPath(joined, part);
	return joined;
}

void SetTimes(int descriptor, const char *path, int64_t mtime, int flags)
{
	if (mtime == 0)
		return;
	time_t seconds = static_cast<time_t>(mtime);
	struct timespec times[2] = {{seconds, 0}, {seconds, 0}};
	if (descriptor >= 0)
		futimens(descriptor, times);
	else
		utimensat(AT_FDCWD, path, times, flags);
}

/* The tree being extracted, as the kernel will see it: which paths are
 * symlinks (to which resolved paths) and which are regular files this job
 * wrote. Paths are relative to the staging directory and never contain a
 * symlink once resolved. */
class StagingTree {
public:
	explicit StagingTree(std::string root) : m_root(std::move(root)) {}

	std::string OnDisk(const std::string &resolved) const { return JoinPath(m_root, resolved); }

	/* path with every created symlink among its parts replaced by where it
	 * leads; false when that leaves the tree or loops. The last part is
	 * resolved too unless isLastKept. */
	bool Resolve(const std::string &path, bool isLastKept, std::string &resolved) const
	{
		std::vector<std::string> pending = SplitOnSlash(path);
		std::reverse(pending.begin(), pending.end());
		std::vector<std::string> done;
		int hops = 0;
		while (!pending.empty()) {
			std::string part = pending.back();
			pending.pop_back();
			if (part.empty() || part == ".")
				continue;
			if (part == "..") {
				if (done.empty())
					return false;
				done.pop_back();
				continue;
			}
			done.push_back(part);
			bool isLast = pending.empty();
			auto link = m_symlinks.find(JoinParts(done));
			if (link == m_symlinks.end() || (isLast && isLastKept))
				continue;
			if (++hops > kMaxLinkHops)
				return false;
			/* Continue from the link's directory with its target. */
			done.pop_back();
			std::vector<std::string> target = SplitOnSlash(link->second);
			pending.insert(pending.end(), target.rbegin(), target.rend());
		}
		resolved = JoinParts(done);
		return true;
	}

	bool IsSymlink(const std::string &resolved) const { return m_symlinks.count(resolved) != 0; }

	/* A created symlink that, with every link of the finished tree, leads
	 * out of it or loops: a part of its target that was plain text when it
	 * was created may have become a link since. Empty when there is none. */
	std::string FindEscapingSymlink() const
	{
		std::string unused;
		for (const auto &[link, target] : m_symlinks)
			if (!Resolve(target, false, unused))
				return link;
		return "";
	}
	bool IsExtractedFile(const std::string &resolved) const { return m_files.count(resolved) != 0; }

	/* Notes a symlink at resolved whose target is relative to its
	 * directory. */
	void AddSymlink(const std::string &resolved, const std::string &target)
	{
		m_symlinks[resolved] = JoinPath(ParentOf(resolved), target);
	}

	void AddFile(const std::string &resolved) { m_files.insert(resolved); }
	void AddDirectory(const std::string &resolved, int64_t mtime) { m_directoryTimes[resolved] = mtime; }

	/* The directories' times, set last since writing inside changes them;
	 * deepest first. */
	void ApplyDirectoryTimes() const
	{
		for (auto it = m_directoryTimes.rbegin(); it != m_directoryTimes.rend(); ++it)
			SetTimes(-1, OnDisk(it->first).c_str(), it->second, 0);
	}

private:
	std::string m_root;
	std::map<std::string, std::string> m_symlinks;
	std::set<std::string> m_files;
	std::map<std::string, int64_t> m_directoryTimes;
};

Status Collision(const std::string &path)
{
	return Status::Fail(ErrorCode::UnsafeEntry, "the entry \"" + path + "\" collides with another entry");
}


class Extractor {
public:
	Extractor(ArchiveReader &reader, const ExtractRequest &request, JobControl &control)
		: m_reader(reader), m_request(request), m_control(control), m_tree(request.stagingPath)
	{
	}

	Status Run(std::vector<EntryRecord> &records)
	{
		EntryChecker checker(m_control);
		for (;;) {
			RawEntry raw;
			bool isEnd = false;
			Status status = m_reader.Next(raw, isEnd);
			if (!status.IsOk())
				return status;
			if (isEnd)
				return Finish();
			std::string path;
			status = checker.Check(raw, path);
			if (!status.IsOk())
				return status;
			if (path.empty() || !IsSelected(path, m_request.prefixes))
				continue;

			EntryRecord record = RecordOf(raw, path);
			status = Write(raw, record);
			if (!status.IsOk())
				return status;
			records.push_back(std::move(record));
		}
	}

private:
	/* After the last entry: every symlink is judged again against the
	 * finished tree, since a later link may have retargeted a part of an
	 * earlier one's target (esc -> "a/.." and then a -> "."). */
	Status Finish()
	{
		std::string escaping = m_tree.FindEscapingSymlink();
		if (!escaping.empty())
			return Status::Fail(ErrorCode::UnsafeEntry, "the symlink \"" + escaping +
			                                                "\" leads out of the target or loops through a later link");
		m_tree.ApplyDirectoryTimes();
		return Status::Ok();
	}

	Status Write(const RawEntry &raw, EntryRecord &record)
	{
		switch (raw.type) {
		case EntryType::File:      return WriteFile(record);
		case EntryType::Directory: return WriteDirectory(record);
		case EntryType::Symlink:   return WriteSymlink(record);
		case EntryType::Hardlink:  return WriteHardlink(record);
		case EntryType::Other:
			record.skipReason = SkipReason::SpecialFile;
			return Status::Ok();
		}
		return Status::Ok();
	}

	/* Where a new entry goes: its parent resolved, its own name kept; it
	 * must not exist yet. */
	Status PlaceNew(const std::string &path, std::string &resolved)
	{
		if (!m_tree.Resolve(path, true, resolved))
			return Status::Fail(ErrorCode::UnsafeEntry, "the entry \"" + path + "\" leads out of the target");
		if (m_tree.IsSymlink(resolved) || fs::Exists(m_tree.OnDisk(resolved)))
			return Collision(path);
		if (!fs::CreateParentDirectories(m_tree.OnDisk(resolved)))
			return Status::Fail(ErrorCode::CreatePath, "the directory of \"" + path + "\" cannot be created");
		return Status::Ok();
	}

	Status WriteFile(EntryRecord &record)
	{
		std::string resolved;
		Status status = PlaceNew(record.path, resolved);
		if (!status.IsOk())
			return status;
		std::string disk = m_tree.OnDisk(resolved);
		mode_t mode = record.isExecutable ? kExecutableMode : kFileMode;
		int descriptor = open(disk.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
		if (descriptor < 0)
			return Status::Fail(ErrorCode::OpenOutput, "\"" + record.path + "\" cannot be created");
		m_tree.AddFile(resolved);

		m_control.BeginEntryData();
		int64_t written = 0;
		status = CopyData(descriptor, record.path, written);
		m_control.FinishEntry();
		if (status.IsOk())
			SetTimes(descriptor, nullptr, record.mtime, 0);
		if (close(descriptor) != 0 && status.IsOk())
			status = Status::Fail(ErrorCode::Write, "writing \"" + record.path + "\" failed");
		if (!status.IsOk())
			return status;

		record.size = written;
		record.packedSize = m_request.hasPackedSizes ? static_cast<int64_t>(m_control.EntryBytesRead()) : -1;
		record.isWritten = true;
		return Status::Ok();
	}

	Status CopyData(int descriptor, const std::string &path, int64_t &written)
	{
		std::vector<uint8_t> buffer(kChunkSize);
		for (;;) {
			Status status = m_control.CheckAbort();
			if (!status.IsOk())
				return status;
			size_t got = 0;
			status = m_reader.ReadData(buffer.data(), buffer.size(), got);
			if (!status.IsOk() || got == 0)
				return status;
			status = m_control.CountWritten(got);
			if (!status.IsOk())
				return status;
			const uint8_t *data = buffer.data();
			while (got > 0) {
				ssize_t result = write(descriptor, data, got);
				if (result < 0 && errno == EINTR)
					continue;
				if (result <= 0)
					return Status::Fail(ErrorCode::Write, "writing \"" + path + "\" failed");
				data += result;
				got -= static_cast<size_t>(result);
				written += result;
			}
		}
	}

	Status WriteDirectory(EntryRecord &record)
	{
		std::string resolved;
		if (!m_tree.Resolve(record.path, false, resolved))
			return Status::Fail(ErrorCode::UnsafeEntry, "the entry \"" + record.path + "\" leads out of the target");
		if (m_tree.IsExtractedFile(resolved))
			return Collision(record.path);
		std::string disk = m_tree.OnDisk(resolved);
		if (!fs::CreateDirectories(disk))
			return Status::Fail(ErrorCode::CreatePath, "the directory \"" + record.path + "\" cannot be created");
		m_tree.AddDirectory(resolved, record.mtime);
		record.isWritten = true;
		return Status::Ok();
	}

	/* A symlink is written when its target is relative and, resolved from
	 * the link's directory as the kernel would, stays inside the tree. */
	bool IsTargetInside(const std::string &resolvedLink, const std::string &target) const
	{
		if (target.empty() || target[0] == '/' || !IsValidUtf8(target))
			return false;
		std::string resolvedTarget;
		return m_tree.Resolve(JoinPath(ParentOf(resolvedLink), target), false, resolvedTarget);
	}

	Status WriteSymlink(EntryRecord &record)
	{
		std::string resolved;
		Status status = PlaceNew(record.path, resolved);
		if (!status.IsOk())
			return status;
		if (!IsTargetInside(resolved, record.linkTarget)) {
			record.skipReason = SkipReason::LinkOutside;
			return Status::Ok();
		}
		std::string disk = m_tree.OnDisk(resolved);
		if (symlink(record.linkTarget.c_str(), disk.c_str()) != 0)
			return Status::Fail(ErrorCode::Write, "the symlink \"" + record.path + "\" cannot be created");
		m_tree.AddSymlink(resolved, record.linkTarget);
		SetTimes(-1, disk.c_str(), record.mtime, AT_SYMLINK_NOFOLLOW);
		record.isWritten = true;
		return Status::Ok();
	}

	Status WriteHardlink(EntryRecord &record)
	{
		std::string resolved;
		Status status = PlaceNew(record.path, resolved);
		if (!status.IsOk())
			return status;
		std::string targetPath, resolvedTarget;
		bool isTargetKnown = NormalizeEntryPath(record.linkTarget, targetPath) == ErrorCode::None &&
		                     m_tree.Resolve(targetPath, false, resolvedTarget) &&
		                     m_tree.IsExtractedFile(resolvedTarget);
		if (!isTargetKnown) {
			record.skipReason = SkipReason::LinkTargetMissing;
			return Status::Ok();
		}
		if (link(m_tree.OnDisk(resolvedTarget).c_str(), m_tree.OnDisk(resolved).c_str()) != 0)
			return Status::Fail(ErrorCode::Write, "the hardlink \"" + record.path + "\" cannot be created");
		m_tree.AddFile(resolved);
		record.isWritten = true;
		return Status::Ok();
	}

	ArchiveReader &m_reader;
	const ExtractRequest &m_request;
	JobControl &m_control;
	StagingTree m_tree;
};

} // namespace

Status ListEntries(ArchiveReader &reader, JobControl &control, std::vector<EntryRecord> &records)
{
	EntryChecker checker(control);
	for (;;) {
		RawEntry raw;
		bool isEnd = false;
		Status status = reader.Next(raw, isEnd);
		if (!status.IsOk() || isEnd)
			return status;
		std::string path;
		status = checker.Check(raw, path);
		if (!status.IsOk())
			return status;
		if (!path.empty())
			records.push_back(RecordOf(raw, path));
	}
}

Status ExtractEntries(ArchiveReader &reader, const ExtractRequest &request, JobControl &control,
                      std::vector<EntryRecord> &records)
{
	Extractor extractor(reader, request, control);
	return extractor.Run(records);
}

Status ReadOneEntry(ArchiveReader &reader, const std::string &entryPath, JobControl &control,
                    std::vector<uint8_t> &bytes, EntryRecord &record)
{
	EntryChecker checker(control);
	for (;;) {
		RawEntry raw;
		bool isEnd = false;
		Status status = reader.Next(raw, isEnd);
		if (!status.IsOk())
			return status;
		if (isEnd)
			return Status::Fail(ErrorCode::EntryNotFound, "the archive has no entry \"" + entryPath + "\"");
		std::string path;
		status = checker.Check(raw, path);
		if (!status.IsOk())
			return status;
		if (path != entryPath)
			continue;
		if (raw.type != EntryType::File)
			return Status::Fail(ErrorCode::Unsupported, "the entry \"" + entryPath + "\" is no regular file");

		record = RecordOf(raw, path);
		MemorySink sink(bytes, control);
		control.BeginEntryData();
		std::vector<uint8_t> buffer(kChunkSize);
		for (;;) {
			status = control.CheckAbort();
			size_t got = 0;
			if (status.IsOk())
				status = reader.ReadData(buffer.data(), buffer.size(), got);
			if (status.IsOk() && got > 0)
				status = sink.Write(buffer.data(), got);
			if (!status.IsOk() || got == 0)
				break;
		}
		control.FinishEntry();
		record.size = static_cast<int64_t>(bytes.size());
		record.isWritten = status.IsOk();
		return status;
	}
}

} // namespace smarchive
