/* Copyright 2026 ETF2L Contributors, Dave
 *
 * Containers: creating tar (plain and with every codec), zip and 7z,
 * listing, extracting with filters, reading one entry, and the checks
 * against hostile archives, which are written here with libarchive. */
#include "check.h"
#include "paths.h"
#include "support.h"

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <memory>

using namespace smarchive;
using tests::AutoArchive;
using tests::Exists;
using tests::kTestJobId;
using tests::Output;
using tests::PathOf;
using tests::PathsOf;
using tests::ReadFile;
using tests::Run;
using tests::TreeOf;
using tests::WriteFile;

namespace {

/* An entry of a hand-made tar. */
struct RawTarEntry {
	std::string name;
	unsigned int type = AE_IFREG;
	std::string content;
	std::string link;
	bool isHardlink = false;
	int permissions = 0644;
};

/* A tar with exactly the given entries, whatever they are. */
std::string WriteRawTar(const std::string &name, const std::vector<RawTarEntry> &entries)
{
	std::string path = PathOf(name);
	struct archive *archive = archive_write_new();
	archive_write_set_format_pax_restricted(archive);
	archive_write_open_filename(archive, path.c_str());
	for (const RawTarEntry &raw : entries) {
		struct archive_entry *entry = archive_entry_new();
		archive_entry_copy_pathname(entry, raw.name.c_str());
		archive_entry_set_filetype(entry, raw.type);
		archive_entry_set_perm(entry, static_cast<mode_t>(raw.permissions));
		archive_entry_set_mtime(entry, 1700000000, 0);
		if (raw.isHardlink)
			archive_entry_copy_hardlink(entry, raw.link.c_str());
		else if (!raw.link.empty())
			archive_entry_copy_symlink(entry, raw.link.c_str());
		archive_entry_set_size(entry, static_cast<la_int64_t>(raw.content.size()));
		archive_write_header(archive, entry);
		if (!raw.content.empty())
			archive_write_data(archive, raw.content.data(), raw.content.size());
		archive_entry_free(entry);
	}
	archive_write_close(archive);
	archive_write_free(archive);
	return path;
}

/* A source tree: files, an executable, a nested directory and a symlink. */
std::string MakeSourceTree()
{
	std::string root = PathOf("source");
	WriteFile(root + "/addons/sourcemod/plugins/x.smx", "smx bytes");
	WriteFile(root + "/addons/sourcemod/scripting/x.sp", tests::SampleText(5000));
	WriteFile(root + "/README.md", "readme");
	WriteFile(root + "/tools/run.sh", "#!/bin/sh\n");
	chmod((root + "/tools/run.sh").c_str(), 0755);
	std::filesystem::create_directories(root + "/empty");
	symlink("README.md", (root + "/link").c_str());
	return root;
}

JobOutcome Create(const std::string &root, const WriteOptions &options, const std::string &output)
{
	return Run(CreateJob{{ArchiveSource{root, "release", true}}, options, Output(output)});
}

ExtractJob Extraction(const std::string &archive, const std::string &target)
{
	ExtractJob job;
	job.archivePath = archive;
	job.targetPath = target;
	return job;
}

const EntryRecord *Find(const std::vector<EntryRecord> &entries, const std::string &path)
{
	for (const EntryRecord &entry : entries)
		if (entry.path == path)
			return &entry;
	return nullptr;
}

const std::vector<std::string> kExtractedTree = {
	"release", "release/README.md", "release/addons", "release/addons/sourcemod",
	"release/addons/sourcemod/plugins", "release/addons/sourcemod/plugins/x.smx",
	"release/addons/sourcemod/scripting", "release/addons/sourcemod/scripting/x.sp", "release/empty",
	"release/tools", "release/tools/run.sh",
};

} // namespace

TEST(EveryContainerRoundTrips)
{
	std::string root = MakeSourceTree();
	std::vector<std::pair<std::string, WriteOptions>> formats = {
		{"plain.tar", TarWriteOptions{}},
		{"zip-store.zip", ZipWriteOptions{ZipMethod::Store, 0}},
		{"zip-deflate.zip", ZipWriteOptions{}},
		{"zip-bzip2.zip", ZipWriteOptions{ZipMethod::Bzip2, 9}},
		{"zip-lzma.zip", ZipWriteOptions{ZipMethod::Lzma, 6}},
		{"zip-xz.zip", ZipWriteOptions{ZipMethod::Xz, 6}},
		{"zip-zstd.zip", ZipWriteOptions{ZipMethod::Zstd, 3}},
		{"copy.7z", SevenZipWriteOptions{SevenZipMethod::Copy, 0}},
		{"deflate.7z", SevenZipWriteOptions{SevenZipMethod::Deflate, 5}},
		{"bzip2.7z", SevenZipWriteOptions{SevenZipMethod::Bzip2, 5}},
		{"lzma1.7z", SevenZipWriteOptions{SevenZipMethod::Lzma1, 5}},
		{"lzma2.7z", SevenZipWriteOptions{}},
		{"ppmd.7z", SevenZipWriteOptions{SevenZipMethod::Ppmd, 5}},
	};
	for (Codec codec : kAllCodecs)
		formats.push_back({std::string("codec.tar") + CodecExtension(codec), TarWriteOptions{DefaultOptionsFor(codec)}});

	for (const auto &[name, options] : formats) {
		CHECK(ValidateWriteOptions(options).empty());
		std::string archive = PathOf(name);
		JobOutcome created = Create(root, options, archive);
		CHECK_OK(created.status);
		const EntryRecord *link = Find(created.entries, "release/link");
		CHECK(link && !link->isWritten && link->skipReason == SkipReason::SourceSymlink);

		JobOutcome listed = Run(ListJob{archive, AutoArchive()});
		CHECK_OK(listed.status);
		CHECK(listed.container == ContainerOf(options));
		CHECK(listed.entries.size() == kExtractedTree.size());
		const EntryRecord *script = Find(listed.entries, "release/tools/run.sh");
		CHECK(script && script->isExecutable && script->size == 10);

		std::string target = PathOf(name + ".out");
		JobOutcome extracted = Run(Extraction(archive, target));
		CHECK_OK(extracted.status);
		CHECK(TreeOf(target) == kExtractedTree);
		CHECK(ReadFile(target + "/release/addons/sourcemod/scripting/x.sp") == tests::SampleText(5000));
		struct stat info;
		CHECK(stat((target + "/release/tools/run.sh").c_str(), &info) == 0 && (info.st_mode & 0100) != 0);
		CHECK(stat((target + "/release/README.md").c_str(), &info) == 0 && (info.st_mode & 0111) == 0);
	}
}

TEST(CreatedArchivesKeepTheSourceTimes)
{
	std::string root = PathOf("timed");
	WriteFile(root + "/a.txt", "a");
	struct timespec times[2] = {{1600000000, 0}, {1600000000, 0}};
	utimensat(AT_FDCWD, (root + "/a.txt").c_str(), times, 0);
	std::string archive = PathOf("timed.tar.gz");
	CHECK_OK(Run(CreateJob{{ArchiveSource{root + "/a.txt", "a.txt", false}}, TarWriteOptions{GzipOptions{}},
	                       Output(archive)}).status);
	std::string target = PathOf("timed.out");
	CHECK_OK(Run(Extraction(archive, target)).status);
	struct stat info;
	CHECK(stat((target + "/a.txt").c_str(), &info) == 0 && info.st_mtime == 1600000000);
}

TEST(TwoSourcesForOnePathFail)
{
	std::string root = MakeSourceTree();
	CreateJob job{{ArchiveSource{root + "/README.md", "x", false}, ArchiveSource{root + "/README.md", "x", false}},
	              TarWriteOptions{}, Output(PathOf("dup.tar"))};
	CHECK_CODE(Run(job).status, ErrorCode::UnsafeEntry);
	CHECK(!Exists(PathOf("dup.tar")));
	CHECK_CODE(Run(CreateJob{{ArchiveSource{PathOf("missing"), "m", false}}, TarWriteOptions{}, Output(PathOf("m.tar"))})
	           .status, ErrorCode::OpenInput);
}

TEST(CreatedArchivesHoldOnlyNamesThatComeBack)
{
	/* "\\" separates parts when an archive is read; a name that is not
	 * UTF-8 fails every read. */
	const char *names[] = {"back\\slash.txt", "caf\xe9.txt"};
	for (size_t i = 0; i < 2; i++) {
		std::string root = PathOf("odd" + std::to_string(i));
		WriteFile(root + "/" + names[i], "odd");
		std::string archive = PathOf("odd" + std::to_string(i) + ".tar");
		CHECK_CODE(Run(CreateJob{{ArchiveSource{root, "", true}}, TarWriteOptions{}, Output(archive)}).status,
		           ErrorCode::EntryName);
		CHECK(!Exists(archive));
	}
}

namespace {

/* The paths a test hook saw, and the locks it holds on them. */
std::vector<std::string> g_created;
std::vector<std::unique_ptr<fs::TempLock>> g_heldBySweeper;

/* A concurrent job's sweep of the first fresh entry, before its job locks
 * it. */
void SweepTheFirst(const std::string &path)
{
	g_created.push_back(path);
	if (g_created.size() == 1)
		fs::RemoveTree(path);
}

/* A concurrent job that locks every fresh entry and never lets go. */
void HoldEvery(const std::string &path)
{
	g_created.push_back(path);
	g_heldBySweeper.push_back(std::make_unique<fs::TempLock>());
	tests::HoldTemp(*g_heldBySweeper.back(), path);
}

} // namespace

TEST(ASweptFreshTempIsCreatedAgain)
{
	std::string input = PathOf("sweep.txt");
	WriteFile(input, "data");
	SetTempCreatedHook(SweepTheFirst);
	CHECK_OK(Run(CodecFileJob{tests::Compress(GzipOptions{}), input, Output(PathOf("sweep.gz"))}).status);
	CHECK(g_created.size() == 2 && Exists(PathOf("sweep.gz")));

	g_created.clear();
	std::string archive = PathOf("sweep.tar");
	CHECK_OK(Create(MakeSourceTree(), TarWriteOptions{}, archive).status);
	g_created.clear();
	CHECK_OK(Run(Extraction(archive, PathOf("sweep.out"))).status);
	CHECK(g_created.size() == 2 && TreeOf(PathOf("sweep.out")) == kExtractedTree);

	/* Held by another job each time: a bounded number of tries, then the job
	 * fails and leaves the held entry alone. */
	g_created.clear();
	SetTempCreatedHook(HoldEvery);
	CHECK_CODE(Run(CodecFileJob{tests::Compress(GzipOptions{}), input, Output(PathOf("held.gz"))}).status,
	           ErrorCode::OpenOutput);
	CHECK(g_created.size() == 1 && Exists(g_created[0]) && !Exists(PathOf("held.gz")));
	SetTempCreatedHook(nullptr);
	g_heldBySweeper.clear();
	g_created.clear();
}

namespace {

void NoteCreated(const std::string &path)
{
	g_created.push_back(path);
}

} // namespace

TEST(TempsAreNamedAfterTheRunningJob)
{
	std::string input = PathOf("named.txt"), archive = PathOf("named.tar");
	WriteFile(input, "data");
	CHECK_OK(Create(MakeSourceTree(), TarWriteOptions{}, archive).status);
	std::atomic<bool> abort{false};
	Progress progress;
	SetTempCreatedHook(NoteCreated);
	CHECK_OK(RunJob(42, Job{CodecFileJob{tests::Compress(GzipOptions{}), input, Output(PathOf("named.gz"))}}, abort,
	                progress).status);
	CHECK_OK(RunJob(43, Job{Extraction(archive, PathOf("named.out"))}, abort, progress).status);
	/* A fixed temp path, as the shims give, is taken as it is. */
	OutputFile fixed{PathOf("fixed.gz"), PathOf("fixed.gz.tmp"), false, true};
	CHECK_OK(RunJob(44, Job{CodecFileJob{tests::Compress(GzipOptions{}), input, fixed}}, abort, progress).status);
	SetTempCreatedHook(nullptr);
	CHECK(g_created == (std::vector<std::string>{UniqueTempPath(PathOf("named.gz"), 42),
	                                             UniqueTempPath(PathOf("named.out"), 43), PathOf("fixed.gz.tmp")}));
	CHECK(Exists(PathOf("named.gz")) && Exists(PathOf("named.out")) && Exists(PathOf("fixed.gz")));
	g_created.clear();
}

TEST(ExtractionNeedsAFreshTarget)
{
	std::string archive = PathOf("a.tar");
	CHECK_OK(Create(MakeSourceTree(), TarWriteOptions{}, archive).status);
	std::string target = PathOf("existing");
	std::filesystem::create_directories(target);
	CHECK_CODE(Run(Extraction(archive, target)).status, ErrorCode::OutputExists);
	CHECK(TreeOf(target).empty());

	/* An interrupted extraction's staging directory goes, a running one's
	 * stays. */
	std::string fresh = PathOf("fresh");
	std::string abandoned = UniqueTempPath(fresh, 7), held = UniqueTempPath(fresh, 8);
	WriteFile(abandoned + "/a/b.txt", "abandoned");
	WriteFile(held + "/c.txt", "held");
	fs::TempLock lock;
	CHECK(tests::HoldTemp(lock, held));
	CHECK_OK(Run(Extraction(archive, fresh)).status);
	CHECK(!Exists(abandoned));
	CHECK(ReadFile(held + "/c.txt") == "held");
}

TEST(PrefixesSelectEntries)
{
	std::string archive = PathOf("a.zip");
	CHECK_OK(Create(MakeSourceTree(), ZipWriteOptions{}, archive).status);
	ExtractJob job = Extraction(archive, PathOf("filtered"));
	job.prefixes = {"release/addons/sourcemod/plugins", "release/README.md", "release/tool"};
	JobOutcome outcome = Run(job);
	CHECK_OK(outcome.status);
	CHECK(PathsOf(outcome.entries) ==
	      (std::vector<std::string>{"release/README.md", "release/addons/sourcemod/plugins",
	                                "release/addons/sourcemod/plugins/x.smx"}));
	CHECK(TreeOf(PathOf("filtered")) ==
	      (std::vector<std::string>{"release", "release/README.md", "release/addons", "release/addons/sourcemod",
	                                "release/addons/sourcemod/plugins", "release/addons/sourcemod/plugins/x.smx"}));
	const EntryRecord *smx = Find(outcome.entries, "release/addons/sourcemod/plugins/x.smx");
	CHECK(smx && smx->isWritten && smx->size == 9 && smx->packedSize >= 0);
}

TEST(OneEntryIsReadIntoMemory)
{
	std::string archive = PathOf("a.tar.xz");
	CHECK_OK(Create(MakeSourceTree(), TarWriteOptions{XzOptions{}}, archive).status);
	JobOutcome outcome = Run(ReadEntryJob{archive, AutoArchive(), "release/README.md"});
	CHECK_OK(outcome.status);
	CHECK(std::string(outcome.bytes.begin(), outcome.bytes.end()) == "readme");
	CHECK(outcome.codec == Codec::Xz);
	CHECK_CODE(Run(ReadEntryJob{archive, AutoArchive(), "release/nothing"}).status, ErrorCode::EntryNotFound);
	CHECK_CODE(Run(ReadEntryJob{archive, AutoArchive(), "release/tools"}).status, ErrorCode::Unsupported);
}

TEST(ExplicitFormatsAreUsed)
{
	std::string archive = PathOf("a.tar.zst");
	CHECK_OK(Create(MakeSourceTree(), TarWriteOptions{ZstdOptions{}}, archive).status);
	std::string renamed = PathOf("a.bin");
	std::filesystem::rename(archive, renamed);
	CHECK_OK(Run(ListJob{renamed, AutoArchive()}).status);
	ArchiveChoice tarZstd{Container::Tar, false, Codec::Zstd};
	CHECK_OK(Run(ListJob{renamed, tarZstd}).status);
	ArchiveChoice plainTar{Container::Tar, false, std::nullopt};
	CHECK(!Run(ListJob{renamed, plainTar}).status.IsOk());
	ArchiveChoice zip{Container::Zip, false, std::nullopt};
	CHECK(!Run(ListJob{renamed, zip}).status.IsOk());

	std::string brotliTar = PathOf("b.tar.br");
	CHECK_OK(Create(MakeSourceTree(), TarWriteOptions{BrotliOptions{}}, brotliTar).status);
	JobOutcome listed = Run(ListJob{brotliTar, AutoArchive()});
	CHECK_OK(listed.status);
	CHECK(listed.codec == Codec::Brotli);

	std::string text = PathOf("notes.gz");
	WriteFile(PathOf("notes"), "notes");
	CHECK_OK(Run(CodecFileJob{tests::Compress(GzipOptions{}), PathOf("notes"), Output(text)}).status);
	CHECK_CODE(Run(ListJob{text, AutoArchive()}).status, ErrorCode::UnknownFormat);
	WriteFile(PathOf("junk.bin"), "nothing to see");
	CHECK_CODE(Run(ListJob{PathOf("junk.bin"), AutoArchive()}).status, ErrorCode::UnknownFormat);
}

TEST(AutoTellsACompressedTarByItsDecodedStart)
{
	/* Random 64-byte chunks with every tenth one repeated compress a little:
	 * lz4's first block (its default 4 MiB) becomes a compressed block of
	 * nearly 4 MiB, decoded only once it is read whole. The name does not
	 * say tar. */
	std::string mostlyRandom;
	for (unsigned seed = 0; mostlyRandom.size() < 5 * kMiB; seed++) {
		std::string chunk = tests::RandomBytes(64, seed);
		mostlyRandom += seed % 10 == 0 ? chunk + chunk : chunk;
	}
	std::string tar = WriteRawTar("backup.tar", {{"mostly-random.bin", AE_IFREG, mostlyRandom}});
	std::string packed = PathOf("backup.lz4");
	CHECK_OK(Run(CodecFileJob{tests::Compress(Lz4Options{}), tar, Output(packed)}).status);
	JobOutcome listed = Run(ListJob{packed, AutoArchive()});
	CHECK_OK(listed.status);
	CHECK(listed.container == Container::Tar && listed.codec == Codec::Lz4);
}

TEST(UnsafePathsFail)
{
	for (const char *name : {"../evil", "a/../../evil", "/etc/evil", "a\\..\\..\\evil"}) {
		std::string archive = WriteRawTar("unsafe.tar", {{"ok.txt", AE_IFREG, "ok"}, {name, AE_IFREG, "evil"}});
		CHECK_CODE(Run(ListJob{archive, AutoArchive()}).status, ErrorCode::UnsafeEntry);
		std::string target = PathOf("unsafe.out");
		CHECK_CODE(Run(Extraction(archive, target)).status, ErrorCode::UnsafeEntry);
		CHECK(!Exists(target));
		CHECK(!Exists(UniqueTempPath(target, kTestJobId)));
		CHECK(!Exists(PathOf("evil")));
	}
}

TEST(BackslashesSeparateParts)
{
	std::string archive = WriteRawTar("windows.tar", {{"addons\\sourcemod\\x.smx", AE_IFREG, "smx"}});
	std::string target = PathOf("windows.out");
	JobOutcome outcome = Run(Extraction(archive, target));
	CHECK_OK(outcome.status);
	CHECK(ReadFile(target + "/addons/sourcemod/x.smx") == "smx");
}

TEST(InvalidNamesFail)
{
	std::string archive = WriteRawTar("latin1.tar", {{"caf\xe9.txt", AE_IFREG, "x"}});
	CHECK_CODE(Run(ListJob{archive, AutoArchive()}).status, ErrorCode::EntryName);
	CHECK_CODE(Run(Extraction(archive, PathOf("latin1.out"))).status, ErrorCode::EntryName);
	CHECK(!Exists(PathOf("latin1.out")));
}

TEST(DuplicatePathsFail)
{
	std::string archive = WriteRawTar("dup.tar", {{"a/x", AE_IFREG, "1"}, {"./a//x", AE_IFREG, "2"}});
	CHECK_CODE(Run(ListJob{archive, AutoArchive()}).status, ErrorCode::UnsafeEntry);
	CHECK_CODE(Run(Extraction(archive, PathOf("dup.out"))).status, ErrorCode::UnsafeEntry);
	CHECK(!Exists(PathOf("dup.out")));
}

TEST(SymlinksStayInside)
{
	std::string archive = WriteRawTar("links.tar", {
		{"docs/README", AE_IFREG, "readme"},
		{"README", AE_IFLNK, "", "docs/README"},
		{"docslink", AE_IFLNK, "", "docs"},
		{"up", AE_IFLNK, "", "../outside"},
		{"absolute", AE_IFLNK, "", "/etc/passwd"},
		{"sneaky", AE_IFLNK, "", "docslink/../../outside"},
		{"dangling", AE_IFLNK, "", "later/file"},
		{"docslink/through.txt", AE_IFREG, "through"},
	});
	std::string target = PathOf("links.out");
	JobOutcome outcome = Run(Extraction(archive, target));
	CHECK_OK(outcome.status);
	CHECK(ReadFile(target + "/README") == "readme");
	CHECK(ReadFile(target + "/docs/through.txt") == "through");
	char buffer[64] = {};
	CHECK(readlink((target + "/dangling").c_str(), buffer, sizeof(buffer) - 1) > 0 &&
	      std::string(buffer) == "later/file");
	for (const char *name : {"up", "absolute", "sneaky"}) {
		const EntryRecord *link = Find(outcome.entries, name);
		CHECK(link && !link->isWritten && link->skipReason == SkipReason::LinkOutside);
		CHECK(!Exists(target + "/" + name));
	}
}

TEST(ALaterLinkCannotLeadAnEarlierOneOut)
{
	/* Each first link stays inside when it is created; the later ones make
	 * the kernel's ".." after them step out of the target. */
	const std::vector<std::vector<RawTarEntry>> archives = {
		{{"esc", AE_IFLNK, "", "a/.."}, {"a", AE_IFLNK, "", "."}},
		{{"esc", AE_IFLNK, "", "a/b/../../x"}, {"a", AE_IFLNK, "", "."}, {"a/b", AE_IFLNK, "", "."}},
		{{"loop", AE_IFLNK, "", "back"}, {"back", AE_IFLNK, "", "loop"}},
	};
	for (const std::vector<RawTarEntry> &entries : archives) {
		std::string archive = WriteRawTar("retarget.tar", entries);
		CHECK_OK(Run(ListJob{archive, AutoArchive()}).status);
		std::string target = PathOf("retarget.out");
		CHECK_CODE(Run(Extraction(archive, target)).status, ErrorCode::UnsafeEntry);
		CHECK(!Exists(target));
		CHECK(!Exists(UniqueTempPath(target, kTestJobId)));
	}
}

TEST(WritingThroughALinkOutsideIsImpossible)
{
	/* The link is skipped, so the file goes into a real directory "out". */
	std::string archive = WriteRawTar("escape.tar", {
		{"out", AE_IFLNK, "", "../../"},
		{"out/evil.txt", AE_IFREG, "evil"},
	});
	std::string target = PathOf("escape.out");
	CHECK_OK(Run(Extraction(archive, target)).status);
	CHECK(ReadFile(target + "/out/evil.txt") == "evil");
	CHECK(!Exists(PathOf("evil.txt")));

	/* A file where a created link is: a collision. */
	std::string colliding = WriteRawTar("collide.tar", {
		{"real/x", AE_IFREG, "x"},
		{"alias", AE_IFLNK, "", "real"},
		{"alias/x", AE_IFREG, "again"},
	});
	CHECK_CODE(Run(Extraction(colliding, PathOf("collide.out"))).status, ErrorCode::UnsafeEntry);
	CHECK(!Exists(PathOf("collide.out")));
}

TEST(HardlinksOnlyToExtractedFiles)
{
	std::string archive = WriteRawTar("hard.tar", {
		{"a.txt", AE_IFREG, "shared"},
		{"b.txt", AE_IFREG, "", "a.txt", true},
		{"c.txt", AE_IFREG, "", "/etc/passwd", true},
		{"d.txt", AE_IFREG, "", "missing.txt", true},
	});
	std::string target = PathOf("hard.out");
	JobOutcome outcome = Run(Extraction(archive, target));
	CHECK_OK(outcome.status);
	CHECK(ReadFile(target + "/b.txt") == "shared");
	for (const char *name : {"c.txt", "d.txt"}) {
		const EntryRecord *link = Find(outcome.entries, name);
		CHECK(link && link->type == EntryType::Hardlink && link->skipReason == SkipReason::LinkTargetMissing);
		CHECK(!Exists(target + "/" + name));
	}
}

TEST(SpecialFilesAreSkipped)
{
	std::string archive = WriteRawTar("special.tar", {{"fifo", AE_IFIFO, ""}, {"file", AE_IFREG, "f"}});
	std::string target = PathOf("special.out");
	JobOutcome outcome = Run(Extraction(archive, target));
	CHECK_OK(outcome.status);
	const EntryRecord *fifo = Find(outcome.entries, "fifo");
	CHECK(fifo && fifo->type == EntryType::Other && fifo->skipReason == SkipReason::SpecialFile);
	CHECK(TreeOf(target) == std::vector<std::string>{"file"});
}

TEST(ExtractionLimitsHold)
{
	std::vector<RawTarEntry> many;
	for (int i = 0; i < 30; i++)
		many.push_back({"f" + std::to_string(i), AE_IFREG, "x"});
	std::string archive = WriteRawTar("many.tar", many);
	Limits limits = kDefaultLimits;
	limits.maxEntries = 20;
	CHECK_CODE(Run(ListJob{archive, AutoArchive()}, limits).status, ErrorCode::LimitEntries);
	CHECK_CODE(Run(Extraction(archive, PathOf("many.out")), limits).status, ErrorCode::LimitEntries);
	CHECK(!Exists(PathOf("many.out")));

	std::string zeros(30 * kMiB, '\0');
	std::string bomb = WriteRawTar("bomb.tar", {{"zeros", AE_IFREG, zeros}});
	std::string packed = PathOf("bomb.tar.xz");
	CHECK_OK(Run(CodecFileJob{tests::Compress(XzOptions{9, true, XzCheck::Crc64}), bomb, Output(packed)}).status);
	CHECK_CODE(Run(Extraction(packed, PathOf("bomb.out"))).status, ErrorCode::LimitRatio);
	limits = kDefaultLimits;
	limits.maxSize = 10 * kMiB;
	CHECK_CODE(Run(Extraction(bomb, PathOf("bomb2.out")), limits).status, ErrorCode::LimitSize);
	CHECK(!Exists(PathOf("bomb.out")) && !Exists(PathOf("bomb2.out")));
	CHECK_OK(Run(Extraction(packed, PathOf("bomb3.out")), kNoLimits).status);
}

TEST(SkippedDataCountsAgainstTheLimits)
{
	/* The bomb comes first, so every job decodes it to reach small.txt. */
	std::string zeros(30 * kMiB, '\0');
	std::string tar = WriteRawTar("skipped.tar", {{"zeros", AE_IFREG, zeros}, {"small.txt", AE_IFREG, "small"}});
	std::string packed = PathOf("skipped.tar.xz");
	CHECK_OK(Run(CodecFileJob{tests::Compress(XzOptions{9, true, XzCheck::Crc64}), tar, Output(packed)}).status);

	CHECK_CODE(Run(ListJob{packed, AutoArchive()}).status, ErrorCode::LimitRatio);
	CHECK_CODE(Run(ReadEntryJob{packed, AutoArchive(), "small.txt"}).status, ErrorCode::LimitRatio);
	ExtractJob selected = Extraction(packed, PathOf("skipped.out"));
	selected.prefixes = {"small.txt"};
	CHECK_CODE(Run(selected).status, ErrorCode::LimitRatio);
	CHECK(!Exists(PathOf("skipped.out")));
	Limits sizeOnly = kNoLimits;
	sizeOnly.maxSize = 10 * kMiB;
	CHECK_CODE(Run(ListJob{packed, AutoArchive()}, sizeOnly).status, ErrorCode::LimitSize);
	CHECK_OK(Run(ListJob{packed, AutoArchive()}, kNoLimits).status);
	/* A plain tar decodes nothing: only what is written counts. */
	CHECK_OK(Run(ListJob{tar, AutoArchive()}, sizeOnly).status);
}

TEST(CancelledArchiveJobsStopReading)
{
	std::string packed = PathOf("cancel.tar.gz");
	CHECK_OK(Create(MakeSourceTree(), TarWriteOptions{GzipOptions{}}, packed).status);
	std::string zip = PathOf("cancel.zip");
	CHECK_OK(Create(MakeSourceTree(), ZipWriteOptions{}, zip).status);
	std::atomic<bool> abort{true};
	Progress progress;
	/* Auto probes the decoded start first; a named codec goes to libarchive
	 * at once, which reads through the core's callback. */
	const std::vector<ListJob> jobs = {
		{packed, AutoArchive()},
		{packed, ArchiveChoice{Container::Tar, false, Codec::Gzip}},
		{zip, AutoArchive()},
	};
	for (const ListJob &job : jobs)
		CHECK_CODE(RunJob(kTestJobId, Job{job, kDefaultLimits}, abort, progress).status, ErrorCode::Cancelled);
}

TEST(ARootEntryIsFine)
{
	std::string archive = WriteRawTar("root.tar", {{"./", AE_IFDIR, ""}, {"./a", AE_IFREG, "a"}});
	JobOutcome outcome = Run(Extraction(archive, PathOf("root.out")));
	CHECK_OK(outcome.status);
	CHECK(PathsOf(outcome.entries) == std::vector<std::string>{"a"});
}

TEST(ZipAndSevenZipAreNotPadded)
{
	std::string root = MakeSourceTree();
	for (const char *name : {"small.zip", "small.7z"}) {
		WriteOptions options = std::string(name) == "small.zip" ? WriteOptions{ZipWriteOptions{}}
		                                                         : WriteOptions{SevenZipWriteOptions{}};
		CHECK_OK(Create(root, options, PathOf(name)).status);
		std::string bytes = ReadFile(PathOf(name));
		CHECK(bytes.size() < 10240);
	}
}

TEST(TruncatedArchivesFail)
{
	std::string archive = PathOf("t.tar.gz");
	CHECK_OK(Create(MakeSourceTree(), TarWriteOptions{GzipOptions{}}, archive).status);
	std::string bytes = ReadFile(archive);
	std::string truncated = PathOf("truncated.tar.gz");
	WriteFile(truncated, bytes.substr(0, bytes.size() / 2));
	CHECK_CODE(Run(Extraction(truncated, PathOf("t.out"))).status, ErrorCode::UnexpectedEOF);
	CHECK(!Exists(PathOf("t.out")));

	std::string zip = PathOf("t.zip");
	CHECK_OK(Create(MakeSourceTree(), ZipWriteOptions{}, zip).status);
	bytes = ReadFile(zip);
	WriteFile(PathOf("truncated.zip"), bytes.substr(0, bytes.size() - 30));
	CHECK_CODE(Run(Extraction(PathOf("truncated.zip"), PathOf("z.out"))).status, ErrorCode::Corrupt);
	CHECK(!Exists(PathOf("z.out")));
}
