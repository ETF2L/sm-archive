/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The tests on the fixtures directory: the hostile archives of hostile.py
 * (each named after the error it must end with), the RAR files of
 * libarchive's test suite, and interop with the real tools (interop.py):
 * what they packed is unpacked here, and what is packed here goes to
 * ours/<bits>/ for interop.py to unpack. Skipped without the directory. */
#include "check.h"
#include "paths.h"
#include "support.h"

#include <filesystem>

using namespace smarchive;
using tests::AutoArchive;
using tests::Fixtures;
using tests::kTestJobId;
using tests::Output;
using tests::PathOf;
using tests::ReadFile;
using tests::Run;
using tests::TreeOf;

namespace {

std::vector<std::string> FilesIn(const std::string &directory)
{
	std::vector<std::string> files;
	if (!std::filesystem::is_directory(directory))
		return files;
	for (const auto &entry : std::filesystem::directory_iterator(directory))
		if (entry.is_regular_file())
			files.push_back(entry.path().string());
	std::sort(files.begin(), files.end());
	return files;
}

ErrorCode CodeNamed(const std::string &name)
{
	for (int value = 0; value <= static_cast<int>(kLastErrorCode); value++) {
		ErrorCode code = static_cast<ErrorCode>(value);
		if (std::string("ArchiveError_") + name == ErrorCodeName(code))
			return code;
	}
	fprintf(stderr, "fixture names an unknown error: %s\n", name.c_str());
	tests::Failures()++;
	return ErrorCode::Internal;
}

ExtractJob Extraction(const std::string &archive, const std::string &target)
{
	ExtractJob job;
	job.archivePath = archive;
	job.targetPath = target;
	return job;
}

bool SameFiles(const std::string &expected, const std::string &actual)
{
	std::vector<std::string> tree = TreeOf(expected);
	if (tree != TreeOf(actual))
		return false;
	for (const std::string &path : tree)
		if (std::filesystem::is_regular_file(expected + "/" + path) &&
		    ReadFile(expected + "/" + path) != ReadFile(actual + "/" + path))
			return false;
	return true;
}

} // namespace

TEST(HostileArchivesFailAsNamed)
{
	std::vector<std::string> files = FilesIn(Fixtures() + "/hostile");
	if (Fixtures().empty())
		return;
	CHECK(files.size() >= 20);
	int n = 0;
	for (const std::string &file : files) {
		std::string name = std::filesystem::path(file).filename().string();
		std::string codes = name.substr(0, name.find('-'));
		size_t dot = codes.find('.');
		ErrorCode expected = CodeNamed(codes.substr(0, dot));
		ErrorCode expectedListing = dot == std::string::npos ? expected : CodeNamed(codes.substr(dot + 1));
		Status listed = Run(ListJob{file, AutoArchive()}).status;
		if (listed.code != expectedListing)
			fprintf(stderr, "%s: listing ended with %s (%s)\n", name.c_str(), ErrorCodeName(listed.code),
			        listed.message.c_str());
		CHECK(listed.code == expectedListing);
		std::string target = PathOf("out" + std::to_string(n++));
		Status extracted = Run(Extraction(file, target)).status;
		if (extracted.code != expected)
			fprintf(stderr, "%s: extraction ended with %s (%s)\n", name.c_str(), ErrorCodeName(extracted.code),
			        extracted.message.c_str());
		CHECK(extracted.code == expected);
		CHECK(expected == ErrorCode::None || !tests::Exists(target));
		CHECK(!tests::Exists(UniqueTempPath(target, kTestJobId)));
	}
	CHECK(!tests::Exists(PathOf("evil.txt")) && !tests::Exists(PathOf("../evil.txt")));
}

TEST(HostileLinksAreSkipped)
{
	if (Fixtures().empty())
		return;
	JobOutcome zip = Run(Extraction(Fixtures() + "/hostile/None-zip-symlink-out.zip", PathOf("zip")));
	CHECK_OK(zip.status);
	CHECK(TreeOf(PathOf("zip")) == std::vector<std::string>{"file.txt"});
	CHECK(zip.entries.size() == 2 && zip.entries[0].skipReason == SkipReason::LinkOutside);

	JobOutcome tar = Run(Extraction(Fixtures() + "/hostile/None-tar-links.tar.xz", PathOf("tar")));
	CHECK_OK(tar.status);
	CHECK(TreeOf(PathOf("tar")) == (std::vector<std::string>{"dir", "dir/file.txt", "hard", "inside"}));
	CHECK(ReadFile(PathOf("tar/inside")) == "file" && ReadFile(PathOf("tar/hard")) == "file");

	JobOutcome backslash = Run(Extraction(Fixtures() + "/hostile/None-zip-backslash.zip", PathOf("bs")));
	CHECK_OK(backslash.status);
	CHECK(ReadFile(PathOf("bs/addons/sourcemod/x.smx")) == "smx");

	JobOutcome v7 = Run(Extraction(Fixtures() + "/hostile/None-v7.tar", PathOf("v7")));
	CHECK_OK(v7.status);
	CHECK(ReadFile(PathOf("v7/old.txt")) == "old");
}

TEST(RarArchivesAreRead)
{
	std::vector<std::string> files = FilesIn(Fixtures() + "/rar");
	if (Fixtures().empty())
		return;
	int readable = 0, n = 0;
	for (const std::string &file : files) {
		std::string name = std::filesystem::path(file).filename().string();
		if (name.find(".rar") == std::string::npos)
			continue;
		bool isEncrypted = name.find("encrypted") != std::string::npos;
		JobOutcome listed = Run(ListJob{file, AutoArchive()});
		JobOutcome extracted = Run(Extraction(file, PathOf("rar" + std::to_string(n++))));
		if (isEncrypted) {
			CHECK_CODE(listed.status.code == ErrorCode::Encrypted ? listed.status : extracted.status,
			           ErrorCode::Encrypted);
			CHECK_CODE(extracted.status, ErrorCode::Encrypted);
			continue;
		}
		if (!listed.status.IsOk() || !extracted.status.IsOk())
			fprintf(stderr, "%s: %s / %s\n", name.c_str(), listed.status.message.c_str(),
			        extracted.status.message.c_str());
		CHECK_OK(listed.status);
		CHECK_OK(extracted.status);
		CHECK(listed.container == Container::Rar);
		CHECK(!listed.entries.empty());
		readable++;
	}
	CHECK(readable >= 6);

	JobOutcome rar4 = Run(ReadEntryJob{Fixtures() + "/rar/test_read_format_rar.rar", AutoArchive(), "test.txt"});
	CHECK_OK(rar4.status);
	CHECK(std::string(rar4.bytes.begin(), rar4.bytes.end()) == "test text document\r\n");
}

TEST(ToolOutputIsUnpacked)
{
	std::string tool = Fixtures() + "/tool";
	if (Fixtures().empty())
		return;
	std::string sample = ReadFile(Fixtures() + "/sample.bin");
	CHECK(!sample.empty());
	int unpacked = 0;
	for (const std::string &file : FilesIn(tool)) {
		std::string name = std::filesystem::path(file).filename().string();
		std::string target = PathOf(name + ".out");
		Status status;
		if (name.rfind("sample.bin.", 0) == 0) {
			status = Run(CodecFileJob{tests::Decompress(std::nullopt), file, Output(target)}).status;
			CHECK(ReadFile(target) == sample);
		} else {
			status = Run(Extraction(file, target)).status;
			CHECK(SameFiles(Fixtures() + "/tree", target));
		}
		if (!status.IsOk())
			fprintf(stderr, "%s: %s\n", name.c_str(), status.message.c_str());
		CHECK_OK(status);
		unpacked++;
	}
	CHECK(unpacked == 15);
}

TEST(OurOutputGoesToTheTools)
{
	if (Fixtures().empty() || !std::filesystem::is_directory(Fixtures() + "/tool"))
		return;
	std::string ours = Fixtures() + "/ours/" + std::to_string(sizeof(void *) * 8);
	std::filesystem::create_directories(ours);
	std::string sample = Fixtures() + "/sample.bin";
	for (Codec codec : kAllCodecs) {
		std::string target = ours + "/sample.bin" + CodecExtension(codec);
		CHECK_OK(Run(CodecFileJob{tests::Compress(DefaultOptionsFor(codec)), sample, Output(target)}).status);
	}
	std::vector<std::pair<std::string, WriteOptions>> archives = {
		{"tree.tar", TarWriteOptions{}},
		{"tree.zip", ZipWriteOptions{}},
		{"tree.7z", SevenZipWriteOptions{}},
	};
	for (Codec codec : kAllCodecs)
		archives.push_back({std::string("tree.tar") + CodecExtension(codec), TarWriteOptions{DefaultOptionsFor(codec)}});
	for (const auto &[name, options] : archives) {
		CreateJob job{{ArchiveSource{Fixtures() + "/tree", "", true}}, options, Output(ours + "/" + name)};
		CHECK_OK(Run(job).status);
	}
}
