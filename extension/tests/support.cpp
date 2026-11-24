/* Copyright 2026 ETF2L Contributors, Dave */
#include "support.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

namespace tests {

namespace {

std::string g_workDirectory;
std::string g_fixtures;

} // namespace

void SetWorkDirectory(const std::string &directory)
{
	g_workDirectory = directory;
}

void SetFixtures(const std::string &directory)
{
	g_fixtures = directory;
}

const std::string &Fixtures()
{
	return g_fixtures;
}

std::string PathOf(const std::string &name)
{
	return g_workDirectory + "/" + name;
}

std::string ReadFile(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFile(const std::string &path, const std::string &content)
{
	std::filesystem::create_directories(std::filesystem::path(path).parent_path());
	std::ofstream(path, std::ios::binary) << content;
}

bool Exists(const std::string &path)
{
	std::error_code error;
	return std::filesystem::exists(std::filesystem::symlink_status(path, error));
}

std::string SampleText(size_t length)
{
	std::string text;
	for (unsigned line = 0; text.size() < length; line++)
		text += "L 11/21/2026 - 12:00:" + std::to_string(line % 60) + ": \"player<" + std::to_string(line * 7 % 97) +
		        ">\" triggered \"damage\" (damage \"" + std::to_string(line % 113) + "\")\n";
	text.resize(length);
	return text;
}

std::string RandomBytes(size_t length, unsigned seed)
{
	std::mt19937 random(seed);
	std::string bytes(length, '\0');
	for (char &c : bytes)
		c = static_cast<char>(random() & 0xff);
	return bytes;
}

smarchive::JobOutcome Run(const smarchive::JobSpec &spec, const smarchive::Limits &limits)
{
	std::atomic<bool> abort{false};
	smarchive::Progress progress;
	return smarchive::RunJob(kTestJobId, smarchive::Job{spec, limits}, abort, progress);
}

smarchive::OutputFile Output(const std::string &path, bool overwrite)
{
	return smarchive::OutputFile{path, std::nullopt, overwrite, true};
}

smarchive::CodecChoice Compress(const smarchive::CodecOptions &options)
{
	return smarchive::CodecChoice{true, options, std::nullopt};
}

smarchive::CodecChoice Decompress(std::optional<smarchive::Codec> codec)
{
	return smarchive::CodecChoice{false, smarchive::GzipOptions{}, codec};
}

smarchive::ArchiveChoice AutoArchive()
{
	return smarchive::ArchiveChoice{};
}

bool HoldTemp(smarchive::fs::TempLock &lock, const std::string &path)
{
	int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
	bool isTaken = descriptor >= 0 && lock.Take(descriptor, path) == smarchive::fs::TempLock::Result::Taken;
	if (descriptor >= 0)
		close(descriptor);
	return isTaken;
}

std::vector<std::string> PathsOf(const std::vector<smarchive::EntryRecord> &entries)
{
	std::vector<std::string> paths;
	for (const smarchive::EntryRecord &entry : entries)
		paths.push_back(entry.path);
	return paths;
}

std::vector<std::string> TreeOf(const std::string &directory)
{
	std::vector<std::string> paths;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(directory))
		paths.push_back(entry.path().lexically_relative(directory).string());
	std::sort(paths.begin(), paths.end());
	return paths;
}

} // namespace tests
