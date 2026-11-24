/* Copyright 2026 ETF2L Contributors, Dave
 *
 * The codecs through the jobs: round trips of files and memory for every
 * codec and option, joined streams, broken and trailing data, the output
 * rules and the limits. */
#include "check.h"
#include "codecs/codecs.h"
#include "support.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>

using namespace smarchive;
using tests::Compress;
using tests::Decompress;
using tests::kTestJobId;
using tests::Output;
using tests::PathOf;
using tests::ReadFile;
using tests::Run;
using tests::WriteFile;

namespace {

/* Every codec with options that differ from the defaults where it has some. */
std::vector<CodecOptions> AllOptionSets()
{
	std::vector<CodecOptions> sets;
	for (Codec codec : kAllCodecs)
		sets.push_back(DefaultOptionsFor(codec));
	sets.push_back(GzipOptions{1});
	sets.push_back(GzipOptions{9});
	sets.push_back(Bzip2Options{1});
	sets.push_back(BrotliOptions{0, kBrotliAutomaticWindow, BrotliMode::Generic});
	sets.push_back(BrotliOptions{11, 16, BrotliMode::Text});
	sets.push_back(BrotliOptions{5, 24, BrotliMode::Font});
	sets.push_back(XzOptions{0, false, XzCheck::None});
	sets.push_back(XzOptions{3, true, XzCheck::Crc32});
	sets.push_back(XzOptions{6, false, XzCheck::Sha256});
	sets.push_back(ZstdOptions{1, kZstdNoLongWindow, false});
	sets.push_back(ZstdOptions{19, 27, true});
	sets.push_back(Lz4Options{1, Lz4BlockSize::Size64KiB, false});
	sets.push_back(Lz4Options{9, Lz4BlockSize::Size1MiB, true});
	sets.push_back(Lz4Options{12, Lz4BlockSize::Size256KiB, true});
	return sets;
}

std::string CompressedFile(const std::string &name, const std::string &content, const CodecOptions &options)
{
	std::string input = PathOf(name + ".in"), output = PathOf(name + CodecExtension(CodecOf(options)));
	WriteFile(input, content);
	CHECK_OK(Run(CodecFileJob{Compress(options), input, Output(output)}).status);
	return output;
}

std::string DecompressedBytes(const std::string &compressed, std::optional<Codec> codec, Status &status)
{
	std::string output = compressed + ".out";
	status = Run(CodecFileJob{Decompress(codec), compressed, Output(output)}).status;
	return ReadFile(output);
}

} // namespace

TEST(EveryCodecRoundTripsFiles)
{
	std::string content = tests::SampleText(300000) + tests::RandomBytes(70000, 1);
	int n = 0;
	for (const CodecOptions &options : AllOptionSets()) {
		CHECK(ValidateOptions(options).empty());
		std::string compressed = CompressedFile("set" + std::to_string(n++), content, options);
		Status status;
		CHECK(DecompressedBytes(compressed, CodecOf(options), status) == content);
		CHECK_OK(status);
		std::filesystem::remove(compressed + ".out");
		CHECK(DecompressedBytes(compressed, std::nullopt, status) == content);
		CHECK_OK(status);
	}
}

TEST(EveryCodecRoundTripsEmptyInput)
{
	for (Codec codec : kAllCodecs) {
		std::string compressed = CompressedFile(CodecName(codec), "", DefaultOptionsFor(codec));
		Status status;
		CHECK(DecompressedBytes(compressed, codec, status).empty());
		CHECK_OK(status);
	}
}

TEST(EveryCodecRoundTripsMemory)
{
	std::string text = tests::SampleText(100000);
	std::vector<uint8_t> input(text.begin(), text.end());
	for (Codec codec : kAllCodecs) {
		JobOutcome packed = Run(CodecMemoryJob{Compress(DefaultOptionsFor(codec)), input, std::nullopt});
		CHECK_OK(packed.status);
		CHECK(packed.codec == codec);
		CHECK(packed.bytes.size() < input.size());
		std::optional<Codec> detected = codec == Codec::Brotli ? std::optional<Codec>(codec) : std::nullopt;
		JobOutcome unpacked = Run(CodecMemoryJob{Decompress(detected), packed.bytes, std::nullopt});
		CHECK_OK(unpacked.status);
		CHECK(unpacked.bytes == input);
	}
}

TEST(MemoryOutputCanGoToAFile)
{
	std::vector<uint8_t> input{'h', 'e', 'l', 'l', 'o'};
	std::string output = PathOf("hello.zst");
	CHECK_OK(Run(CodecMemoryJob{Compress(ZstdOptions{}), input, Output(output)}).status);
	Status status;
	CHECK(DecompressedBytes(output, Codec::Zstd, status) == "hello");
	CHECK_OK(status);
}

TEST(BrotliWithoutMagicNeedsItsName)
{
	std::string compressed = CompressedFile("text", "some text", BrotliOptions{});
	std::string renamed = PathOf("text.bin");
	std::filesystem::rename(compressed, renamed);
	Status status;
	DecompressedBytes(renamed, std::nullopt, status);
	CHECK_CODE(status, ErrorCode::UnknownFormat);
	CHECK(DecompressedBytes(renamed, Codec::Brotli, status) == "some text");
	CHECK_OK(status);
}

TEST(BrotliAutomaticWindowFollowsTheTool)
{
	CHECK(codecs::BrotliAutomaticWindow(-1) == 24);
	CHECK(codecs::BrotliAutomaticWindow(0) == 10);
	CHECK(codecs::BrotliAutomaticWindow(1008) == 10);
	CHECK(codecs::BrotliAutomaticWindow(1009) == 11);
	CHECK(codecs::BrotliAutomaticWindow(int64_t{1} << 40) == 24);
}

TEST(JoinedStreamsDecompressWhole)
{
	for (Codec codec : {Codec::Gzip, Codec::Bzip2, Codec::Xz, Codec::Zstd, Codec::Lz4}) {
		std::string first = ReadFile(CompressedFile(std::string(CodecName(codec)) + "1", "first ", DefaultOptionsFor(codec)));
		std::string second = ReadFile(CompressedFile(std::string(CodecName(codec)) + "2", "second", DefaultOptionsFor(codec)));
		std::string joined = PathOf(std::string("joined") + CodecExtension(codec));
		WriteFile(joined, first + second);
		Status status;
		CHECK(DecompressedBytes(joined, codec, status) == "first second");
		CHECK_OK(status);
	}
}

TEST(TrailingDataFails)
{
	for (Codec codec : kAllCodecs) {
		std::string compressed = ReadFile(CompressedFile(CodecName(codec), "payload", DefaultOptionsFor(codec)));
		std::string withGarbage = PathOf(std::string("garbage") + CodecExtension(codec));
		WriteFile(withGarbage, compressed + "garbage!");
		Status status;
		DecompressedBytes(withGarbage, codec, status);
		CHECK_CODE(status, ErrorCode::TrailingData);
		CHECK(!tests::Exists(withGarbage + ".out"));
	}
}

TEST(TruncatedStreamsFail)
{
	std::string content = tests::RandomBytes(200000, 2);
	for (Codec codec : kAllCodecs) {
		std::string compressed = ReadFile(CompressedFile(CodecName(codec), content, DefaultOptionsFor(codec)));
		std::string truncated = PathOf(std::string("truncated") + CodecExtension(codec));
		WriteFile(truncated, compressed.substr(0, compressed.size() / 2));
		Status status;
		DecompressedBytes(truncated, codec, status);
		CHECK_CODE(status, ErrorCode::UnexpectedEOF);
		CHECK(!tests::Exists(truncated + ".out"));

		std::string empty = PathOf(std::string("empty") + CodecExtension(codec));
		WriteFile(empty, "");
		DecompressedBytes(empty, codec, status);
		CHECK_CODE(status, ErrorCode::UnexpectedEOF);
	}
}

TEST(CorruptDataFails)
{
	std::string content = tests::SampleText(200000);
	for (Codec codec : {Codec::Gzip, Codec::Bzip2, Codec::Xz, Codec::Zstd, Codec::Lz4}) {
		std::string compressed = ReadFile(CompressedFile(CodecName(codec), content, DefaultOptionsFor(codec)));
		for (size_t i = 20; i < compressed.size() - 20; i += 7)
			compressed[i] = static_cast<char>(compressed[i] ^ 0x55);
		std::string corrupt = PathOf(std::string("corrupt") + CodecExtension(codec));
		WriteFile(corrupt, compressed);
		Status status;
		DecompressedBytes(corrupt, codec, status);
		CHECK_CODE(status, ErrorCode::Corrupt);
	}
}

TEST(WrongCodecFails)
{
	std::string gzip = CompressedFile("text", "text", GzipOptions{});
	Status status;
	DecompressedBytes(gzip, Codec::Bzip2, status);
	CHECK_CODE(status, ErrorCode::Corrupt);
	CHECK(status.libraryCode == -5);
	std::filesystem::remove(gzip + ".out");
	DecompressedBytes(gzip, Codec::Xz, status);
	CHECK_CODE(status, ErrorCode::Corrupt);
}

TEST(OutputsAppearOnlyOnSuccess)
{
	std::string input = PathOf("in.txt"), output = PathOf("out.gz");
	WriteFile(input, "data");
	WriteFile(output, "old");
	CHECK_CODE(Run(CodecFileJob{Compress(GzipOptions{}), input, Output(output)}).status, ErrorCode::OutputExists);
	CHECK(ReadFile(output) == "old");
	CHECK_OK(Run(CodecFileJob{Compress(GzipOptions{}), input, Output(output, true)}).status);
	CHECK(ReadFile(output) != "old");
	CHECK(!tests::Exists(UniqueTempPath(output, kTestJobId)));

	CHECK_CODE(Run(CodecFileJob{Compress(GzipOptions{}), PathOf("missing"), Output(PathOf("x.gz"))}).status,
	           ErrorCode::OpenInput);
	CHECK(!tests::Exists(PathOf("x.gz")));

	OutputFile deep = Output(PathOf("a/b/c.gz"));
	CHECK_OK(Run(CodecFileJob{Compress(GzipOptions{}), input, deep}).status);
	OutputFile noPath = Output(PathOf("d/e.gz"));
	noPath.createPath = false;
	CHECK_CODE(Run(CodecFileJob{Compress(GzipOptions{}), input, noPath}).status, ErrorCode::OpenOutput);
}

TEST(TempFilesAreCreatedAnew)
{
	std::string input = PathOf("swept.txt"), output = PathOf("swept.gz");
	WriteFile(input, "data");
	/* An interrupted job's temp file goes, a running job's stays. */
	std::string abandoned = UniqueTempPath(output, 7), held = UniqueTempPath(output, 8);
	WriteFile(abandoned, "abandoned");
	WriteFile(held, "held");
	fs::TempLock lock, again;
	CHECK(tests::HoldTemp(lock, held));
	CHECK(!tests::HoldTemp(again, held));
	CHECK_OK(Run(CodecFileJob{Compress(GzipOptions{}), input, Output(output)}).status);
	CHECK(!tests::Exists(abandoned));
	CHECK(ReadFile(held) == "held");

	/* A lock is only taken on what the path still names. */
	std::string replaced = PathOf("replaced.tmp");
	WriteFile(replaced, "first");
	int descriptor = open(replaced.c_str(), O_RDONLY | O_CLOEXEC);
	WriteFile(replaced + ".new", "second");
	CHECK(rename((replaced + ".new").c_str(), replaced.c_str()) == 0);
	fs::TempLock stale;
	CHECK(descriptor >= 0 && stale.Take(descriptor, replaced) == fs::TempLock::Result::Gone);
	close(descriptor);

	/* A directory at a temp file's path is left alone and fails the job. */
	std::string legacy = PathOf("legacy.bz2");
	WriteFile(legacy + ".tmp/keep.txt", "keep");
	OutputFile legacyOutput{legacy, legacy + ".tmp", false, true};
	CHECK_CODE(Run(CodecFileJob{Compress(GzipOptions{}), input, legacyOutput}).status, ErrorCode::OpenOutput);
	CHECK(ReadFile(legacy + ".tmp/keep.txt") == "keep");

	/* A symlink planted at the job's own temp path is not written through. */
	std::string victim = PathOf("victim.txt"), planted = PathOf("planted.gz");
	WriteFile(victim, "victim");
	CHECK(symlink(victim.c_str(), UniqueTempPath(planted, kTestJobId).c_str()) == 0);
	CHECK_OK(Run(CodecFileJob{Compress(GzipOptions{}), input, Output(planted)}).status);
	CHECK(ReadFile(victim) == "victim");
	CHECK(!tests::Exists(UniqueTempPath(planted, kTestJobId)));
}

TEST(GzipOutputIsReproducible)
{
	std::string first = ReadFile(CompressedFile("a", "same", GzipOptions{}));
	std::string second = ReadFile(CompressedFile("b", "same", GzipOptions{}));
	CHECK(first == second);
	CHECK(first[4] == 0 && first[5] == 0 && first[6] == 0 && first[7] == 0);
	CHECK((static_cast<uint8_t>(first[3]) & 0x08) == 0);
}

TEST(OptionsOutsideTheirRangesAreRefused)
{
	CHECK(!ValidateOptions(GzipOptions{0}).empty());
	CHECK(!ValidateOptions(GzipOptions{10}).empty());
	CHECK(!ValidateOptions(Bzip2Options{0}).empty());
	CHECK(!ValidateOptions(BrotliOptions{12, 0, BrotliMode::Generic}).empty());
	CHECK(!ValidateOptions(BrotliOptions{6, 9, BrotliMode::Generic}).empty());
	CHECK(!ValidateOptions(BrotliOptions{6, 25, BrotliMode::Generic}).empty());
	CHECK(!ValidateOptions(BrotliOptions{6, 0, static_cast<BrotliMode>(3)}).empty());
	CHECK(!ValidateOptions(XzOptions{10, false, XzCheck::Crc64}).empty());
	CHECK(!ValidateOptions(XzOptions{6, false, static_cast<XzCheck>(4)}).empty());
	CHECK(!ValidateOptions(ZstdOptions{0, 0, true}).empty());
	CHECK(!ValidateOptions(ZstdOptions{23, 0, true}).empty());
	CHECK(!ValidateOptions(ZstdOptions{3, 9, true}).empty());
	CHECK(!ValidateOptions(ZstdOptions{3, 28, true}).empty());
	CHECK(!ValidateOptions(Lz4Options{0, Lz4BlockSize::Size4MiB, true}).empty());
	CHECK(!ValidateOptions(Lz4Options{13, Lz4BlockSize::Size4MiB, true}).empty());
	CHECK(!ValidateOptions(Lz4Options{1, static_cast<Lz4BlockSize>(4), true}).empty());
}

TEST(DecompressionLimitsStopBombs)
{
	std::string zeros(40 * kMiB, '\0');
	std::string bomb = CompressedFile("zeros", zeros, GzipOptions{9});
	Status status;
	DecompressedBytes(bomb, Codec::Gzip, status);
	CHECK_CODE(status, ErrorCode::LimitRatio);
	CHECK(!tests::Exists(bomb + ".out"));

	Limits small = kDefaultLimits;
	small.maxRatio = 0;
	small.maxSize = 1 * kMiB;
	CHECK_CODE(Run(CodecFileJob{Decompress(Codec::Gzip), bomb, Output(PathOf("sized"))}, small).status,
	           ErrorCode::LimitSize);
	CHECK_OK(Run(CodecFileJob{Decompress(Codec::Gzip), bomb, Output(PathOf("free"))}, kNoLimits).status);
	CHECK(ReadFile(PathOf("free")) == zeros);

	std::string packedText = ReadFile(bomb);
	std::vector<uint8_t> packed(packedText.begin(), packedText.end());
	Limits memory = kNoLimits;
	memory.maxMemory = 1 * kMiB;
	CHECK_CODE(Run(CodecMemoryJob{Decompress(std::nullopt), packed, std::nullopt}, memory).status,
	           ErrorCode::LimitMemory);

	/* Compression is not limited by size or ratio. */
	std::string zerosFile = PathOf("zeros.raw");
	WriteFile(zerosFile, zeros);
	CHECK_OK(Run(CodecFileJob{Compress(GzipOptions{}), zerosFile, Output(PathOf("again.gz"))}, small).status);
}

TEST(CancelledJobsLeaveNothing)
{
	std::string input = PathOf("big");
	WriteFile(input, tests::RandomBytes(4 * kMiB, 3));
	std::atomic<bool> abort{true};
	Progress progress;
	OutputFile output = Output(PathOf("big.xz"));
	JobOutcome outcome =
		RunJob(kTestJobId, Job{CodecFileJob{Compress(XzOptions{}), input, output}, kDefaultLimits}, abort, progress);
	CHECK_CODE(outcome.status, ErrorCode::Cancelled);
	CHECK(!tests::Exists(output.path));
	CHECK(!tests::Exists(UniqueTempPath(output.path, kTestJobId)));
}

TEST(ProgressIsCounted)
{
	std::string input = PathOf("in");
	std::string content = tests::SampleText(500000);
	WriteFile(input, content);
	std::atomic<bool> abort{false};
	Progress progress;
	Job job{CodecFileJob{Compress(ZstdOptions{}), input, Output(PathOf("in.zst"))}, kDefaultLimits};
	CHECK_OK(RunJob(kTestJobId, job, abort, progress).status);
	CHECK(progress.inputSize.load() == static_cast<int64_t>(content.size()));
	CHECK(progress.bytesRead.load() == content.size());
	CHECK(progress.bytesWritten.load() == ReadFile(PathOf("in.zst")).size());
}

TEST(LibraryVersionsAreKnown)
{
	for (Codec codec : kAllCodecs)
		CHECK(CodecLibraryVersion(codec).find(' ') != std::string::npos);
	CHECK(ArchiveLibraryVersion().find("libarchive") == 0);
}
