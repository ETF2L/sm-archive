/**
 * Copyright 2026 ETF2L Contributors, Dave
 *
 * The tests of smarchive.inc, run with smrunner (sm-runner) against the
 * built extension:
 *
 *     smrunner test test_smarchive.smx --extensions <dist>/extensions
 *
 * Every native is called as a plugin calls it: the options and their
 * ranges, every codec and container through files and memory, the rules of
 * the outputs, the checks of extraction on hand-made hostile tars, the
 * limits, the jobs (callbacks, ids, serial lanes, cancellation) and the
 * native errors the include documents. Jobs run on the extension's worker
 * threads and are waited for with Runner.WaitUntil.
 */

// no critic (ProhibitLongFiles)
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smarchive>
#include <smrunner>

/* ------------------------------------------------------------------ helpers */

/* Runs failing, which must stop with a native error containing text. */
void ExpectNativeError(Function failing, const char[] text) {
	Runner.ExpectError(text);
	Call_StartFunction(null, failing);
	Call_Finish();
}

/* What the jobs reported, in the order their callbacks ran. */
int g_calls;
int g_order[128];
ArchiveError g_errors[128];
ArchiveResult g_results[128];
bool g_calledInside;

/* How many of the reported jobs Finish has handed out. */
int g_finished;

/* Set while a job is being queued, to catch a callback inside the call. */
bool g_queueing;

/* Records how a job ended; data is its number in the test. */
void OnDone(ArchiveError error, ArchiveResult result, any data) {
	if (g_queueing) {
		g_calledInside = true;
	}
	Assert.Equal(result.Error, error, "the callback's error is the result's");
	g_order[g_calls] = data;
	g_errors[g_calls] = error;
	g_results[g_calls] = result.Clone();
	g_calls++;
}

bool HasCalledBack(any count) {
	return g_calls >= count;
}

/* Waits until count jobs called back. */
void WaitForJobs(int count) {
	Assert.True(Runner.WaitUntil(HasCalledBack, 30.0, count), "%d job(s) called back, %d did", count, g_calls);
}

/* Waits for the next job in the order they called back, which must end with
 * expected, and returns its result. */
ArchiveResult Finish(ArchiveError expected = ArchiveError_None) {
	int before = g_finished;
	WaitForJobs(before + 1);
	g_finished++;
	ArchiveResult result = g_results[before];
	char message[256];
	result.GetMessage(message, sizeof(message));
	char name[64];
	Compression.ErrorString(g_errors[before], name, sizeof(name));
	char expectedName[64];
	Compression.ErrorString(expected, expectedName, sizeof(expectedName));
	Assert.Equal(g_errors[before], expected, "the job ended with %s, not %s: %s", name, expectedName, message);
	return result;
}

void WriteTextFile(const char[] path, const char[] text) {
	File file = OpenFile(path, "wb");
	Assert.True(file != null, "%s can be written", path);
	file.WriteString(text, false);
	delete file;
}

void WriteBytesFile(const char[] path, const int[] bytes, int length) {
	File file = OpenFile(path, "wb");
	Assert.True(file != null, "%s can be written", path);
	file.Write(bytes, length, 1);
	delete file;
}

/* Reads the whole text file at path. */
void ReadTextFile(const char[] path, char[] buffer, int maxlength) {
	File file = OpenFile(path, "rb");
	Assert.True(file != null, "%s can be read", path);
	int length = file.ReadString(buffer, maxlength, -1);
	delete file;
	buffer[length < 0 ? 0 : length] = '\0';
}

/* Reads up to maxlength bytes of path; returns the number read. */
int ReadBytesFile(const char[] path, int[] bytes, int maxlength) {
	File file = OpenFile(path, "rb");
	Assert.True(file != null, "%s can be read", path);
	int length = file.Read(bytes, maxlength, 1);
	delete file;
	return length;
}

/* Fails unless the directory holds no temp file or staging directory. */
void AssertNoTempLeft(const char[] folder) {
	DirectoryListing listing = OpenDirectory(folder);
	Assert.True(listing != null, "%s exists", folder);
	char name[PLATFORM_MAX_PATH];
	FileType type;
	while (listing.GetNext(name, sizeof(name), type)) {
		if (StrContains(name, ".smarchive.tmp") != -1) {
			delete listing;
			Assert.Fail("%s/%s was left behind", folder, name);
		}
	}
	delete listing;
}

/* The text the codec tests compress: long enough to compress. */
void SampleText(char[] buffer, int maxlength) {
	buffer[0] = '\0';
	for (int i = 0; i < 40; i++) {
		Format(buffer, maxlength, "%sLine %d: The quick brown fox jumps over the lazy dog.\n", buffer, i);
	}
}

/* A codec's options at their defaults. */
CodecOptions NewOptions(CodecFormat codec) {
	switch (codec) {
		case CodecFormat_Gzip: {
			return new GzipOptions();
		}
		case CodecFormat_Bzip2: {
			return new Bzip2Options();
		}
		case CodecFormat_Brotli: {
			return new BrotliOptions();
		}
		case CodecFormat_Xz: {
			return new XzOptions();
		}
		case CodecFormat_Zstd: {
			return new ZstdOptions();
		}
	}
	return new Lz4Options();
}

/* The file name ending of a codec, as Auto tells it. */
void CodecSuffix(CodecFormat codec, char[] buffer, int maxlength) {
	char suffixes[8][8] = {"", "", "gz", "bz2", "br", "xz", "zst", "lz4"};
	strcopy(buffer, maxlength, suffixes[codec]);
}

/* -------------------------------------------------------- the codec options */

public void Test_CodecOptionsStartWithTheToolsDefaults() {
	GzipOptions gzip = new GzipOptions();
	Assert.Equal(gzip.Format, CodecFormat_Gzip);
	Assert.Equal(gzip.Level, 6);
	delete gzip;

	Bzip2Options bzip2 = new Bzip2Options();
	Assert.Equal(bzip2.Format, CodecFormat_Bzip2);
	Assert.Equal(bzip2.Level, 9);
	delete bzip2;

	BrotliOptions brotli = new BrotliOptions();
	Assert.Equal(brotli.Format, CodecFormat_Brotli);
	Assert.Equal(brotli.Quality, 6);
	Assert.Equal(brotli.Window, 0);
	Assert.Equal(brotli.Mode, BrotliContent_Generic);
	delete brotli;

	XzOptions xz = new XzOptions();
	Assert.Equal(xz.Format, CodecFormat_Xz);
	Assert.Equal(xz.Preset, 6);
	Assert.False(xz.Extreme);
	Assert.Equal(xz.Check, XzCheck_Crc64);
	delete xz;

	ZstdOptions zstd = new ZstdOptions();
	Assert.Equal(zstd.Format, CodecFormat_Zstd);
	Assert.Equal(zstd.Level, 3);
	Assert.Equal(zstd.LongWindow, 0);
	Assert.True(zstd.Checksum);
	delete zstd;

	Lz4Options lz4 = new Lz4Options();
	Assert.Equal(lz4.Format, CodecFormat_Lz4);
	Assert.Equal(lz4.Level, 1);
	Assert.Equal(lz4.BlockSize, Lz4Block_4MiB);
	Assert.True(lz4.ContentChecksum);
	delete lz4;
}

public void Test_CodecOptionsTakeValuesInTheirRange() {
	GzipOptions gzip = new GzipOptions();
	gzip.Level = 9;
	Assert.Equal(gzip.Level, 9);
	delete gzip;

	BrotliOptions brotli = new BrotliOptions();
	brotli.Quality = 0;
	brotli.Window = 24;
	brotli.Mode = BrotliContent_Text;
	Assert.Equal(brotli.Quality, 0);
	Assert.Equal(brotli.Window, 24);
	Assert.Equal(brotli.Mode, BrotliContent_Text);
	delete brotli;

	XzOptions xz = new XzOptions();
	xz.Preset = 0;
	xz.Extreme = true;
	xz.Check = XzCheck_Sha256;
	Assert.Equal(xz.Preset, 0);
	Assert.True(xz.Extreme);
	Assert.Equal(xz.Check, XzCheck_Sha256);
	delete xz;

	ZstdOptions zstd = new ZstdOptions();
	zstd.Level = 22;
	zstd.LongWindow = 27;
	zstd.Checksum = false;
	Assert.Equal(zstd.Level, 22);
	Assert.Equal(zstd.LongWindow, 27);
	Assert.False(zstd.Checksum);
	delete zstd;

	Lz4Options lz4 = new Lz4Options();
	lz4.Level = 12;
	lz4.BlockSize = Lz4Block_64KiB;
	lz4.ContentChecksum = false;
	Assert.Equal(lz4.Level, 12);
	Assert.Equal(lz4.BlockSize, Lz4Block_64KiB);
	Assert.False(lz4.ContentChecksum);
	delete lz4;
}

/* The options the failing setters below change: codec or archive options. */
Handle g_options; // no critic (ProhibitBareHandle)

public void Test_CodecOptionSettersRefuseValuesOutsideTheirRange() {
	g_options = new GzipOptions();
	ExpectNativeError(SetGzipLevel0, "gzip level 0 is outside 1-9");
	Assert.Equal(view_as<GzipOptions>(g_options).Level, 6, "the handle is as it was");
	delete g_options;

	g_options = new Bzip2Options();
	ExpectNativeError(SetBzip2Level10, "bzip2 level 10 is outside 1-9");
	delete g_options;

	g_options = new BrotliOptions();
	ExpectNativeError(SetBrotliQuality12, "brotli quality 12 is outside 0-11");
	ExpectNativeError(SetBrotliWindow9, "brotli window 9 is neither 0 (automatic) nor within 10-24");
	ExpectNativeError(SetBrotliMode3, "invalid brotli mode 3");
	Assert.Equal(view_as<BrotliOptions>(g_options).Window, 0, "the handle is as it was");
	delete g_options;

	g_options = new XzOptions();
	ExpectNativeError(SetXzPreset10, "xz preset 10 is outside 0-9");
	ExpectNativeError(SetXzCheck4, "invalid xz check 4");
	delete g_options;

	g_options = new ZstdOptions();
	ExpectNativeError(SetZstdLevel23, "zstd level 23 is outside 1-22");
	ExpectNativeError(SetZstdLongWindow28, "zstd long window 28 is neither 0 (off) nor within 10-27");
	delete g_options;

	g_options = new Lz4Options();
	ExpectNativeError(SetLz4Level0, "lz4 level 0 is outside 1-12");
	ExpectNativeError(SetLz4BlockSize4, "invalid lz4 block size 4");
	delete g_options;
}

void SetGzipLevel0() {
	view_as<GzipOptions>(g_options).Level = 0;
}

void SetBzip2Level10() {
	view_as<Bzip2Options>(g_options).Level = 10;
}

void SetBrotliQuality12() {
	view_as<BrotliOptions>(g_options).Quality = 12;
}

void SetBrotliWindow9() {
	view_as<BrotliOptions>(g_options).Window = 9;
}

void SetBrotliMode3() {
	view_as<BrotliOptions>(g_options).Mode = view_as<BrotliContent>(3);
}

void SetXzPreset10() {
	view_as<XzOptions>(g_options).Preset = 10;
}

void SetXzCheck4() {
	view_as<XzOptions>(g_options).Check = view_as<XzCheck>(4);
}

void SetZstdLevel23() {
	view_as<ZstdOptions>(g_options).Level = 23;
}

void SetZstdLongWindow28() {
	view_as<ZstdOptions>(g_options).LongWindow = 28;
}

void SetLz4Level0() {
	view_as<Lz4Options>(g_options).Level = 0;
}

void SetLz4BlockSize4() {
	view_as<Lz4Options>(g_options).BlockSize = view_as<Lz4BlockSize>(4);
}

public void Test_OptionsOfAnotherCodecAreRefused() {
	g_options = new ZstdOptions();
	ExpectNativeError(SetGzipLevel0, "is no GzipOptions but zstd options");
	delete g_options;
}

/* ------------------------------------------------------ the archive options */

public void Test_ArchiveWriteOptionsStartWithTheirDefaults() {
	TarOptions tar = new TarOptions();
	Assert.Equal(tar.Format, ArchiveFormat_Tar);
	Assert.Equal(tar.Codec, CodecFormat_None);
	delete tar;

	XzOptions xz = new XzOptions();
	tar = new TarOptions(xz);
	delete xz;
	Assert.Equal(tar.Codec, CodecFormat_Xz, "the codec's settings were copied");
	delete tar;

	ZipOptions zip = new ZipOptions();
	Assert.Equal(zip.Format, ArchiveFormat_Zip);
	Assert.Equal(zip.Method, ZipMethod_Deflate);
	Assert.Equal(zip.Level, 6);
	zip.Method = ZipMethod_Zstd;
	zip.Level = 0;
	Assert.Equal(zip.Method, ZipMethod_Zstd);
	Assert.Equal(zip.Level, 0);
	delete zip;

	SevenZipOptions sevenZip = new SevenZipOptions();
	Assert.Equal(sevenZip.Format, ArchiveFormat_SevenZip);
	Assert.Equal(sevenZip.Method, SevenZipMethod_Lzma2);
	Assert.Equal(sevenZip.Level, 5);
	sevenZip.Method = SevenZipMethod_Ppmd;
	sevenZip.Level = 9;
	Assert.Equal(sevenZip.Method, SevenZipMethod_Ppmd);
	Assert.Equal(sevenZip.Level, 9);
	delete sevenZip;
}

public void Test_ArchiveWriteOptionSettersRefuseValuesOutsideTheirRange() {
	g_options = new ZipOptions();
	ExpectNativeError(SetZipLevel10, "10");
	ExpectNativeError(SetZipMethod6, "6");
	Assert.Equal(view_as<ZipOptions>(g_options).Level, 6, "the handle is as it was");
	delete g_options;

	g_options = new SevenZipOptions();
	ExpectNativeError(SetSevenZipLevel10, "10");
	ExpectNativeError(SetSevenZipMethod6, "6");
	Assert.Equal(view_as<SevenZipOptions>(g_options).Method, SevenZipMethod_Lzma2, "the handle is as it was");
	delete g_options;

	g_options = new TarOptions();
	ExpectNativeError(SetZipLevel10, "is no ZipOptions");
	delete g_options;
}

void SetZipLevel10() {
	view_as<ZipOptions>(g_options).Level = 10;
}

void SetZipMethod6() {
	view_as<ZipOptions>(g_options).Method = view_as<ZipMethod>(6);
}

void SetSevenZipLevel10() {
	view_as<SevenZipOptions>(g_options).Level = 10;
}

void SetSevenZipMethod6() {
	view_as<SevenZipOptions>(g_options).Method = view_as<SevenZipMethod>(6);
}

/* --------------------------------------------------------------- the limits */

public void Test_LimitsStartWithTheServersDefaults() {
	ArchiveLimits limits = new ArchiveLimits();
	Assert.Equal(limits.MaxSize, 1073741824, "1 GiB");
	Assert.Equal(limits.MaxEntries, 10000);
	Assert.Equal(limits.MaxRatio, 1000);
	Assert.Equal(limits.MaxMemory, 67108864, "64 MiB");
	delete limits;
}

public void Test_LimitsTakeAnyValueAndZeroForNone() {
	ArchiveLimits limits = new ArchiveLimits();
	limits.MaxSize = 0;
	limits.MaxEntries = 5;
	limits.MaxRatio = 0;
	limits.MaxMemory = 1024;
	Assert.Equal(limits.MaxSize, 0);
	Assert.Equal(limits.MaxEntries, 5);
	Assert.Equal(limits.MaxRatio, 0);
	Assert.Equal(limits.MaxMemory, 1024);
	delete limits;
}

public void Test_LimitsBeyond2GiBTakeTwoCells() {
	ArchiveLimits limits = new ArchiveLimits();
	int bytes[2] = {0, 2};
	limits.SetMaxSize64(bytes);
	Assert.Equal(limits.MaxSize, 2147483647, "8 GiB saturates in the int getter");
	int back[2];
	limits.GetMaxSize64(back);
	Assert.Equal(back[0], 0);
	Assert.Equal(back[1], 2);

	bytes = {1, 1};
	limits.SetMaxMemory64(bytes);
	limits.GetMaxMemory64(back);
	Assert.Equal(back[0], 1);
	Assert.Equal(back[1], 1);
	delete limits;
}

ArchiveLimits g_limits;

public void Test_LimitsRefuseNegativeValues() {
	g_limits = new ArchiveLimits();
	ExpectNativeError(SetNegativeMaxSize, "A limit cannot be negative (-1); 0 is no limit");
	ExpectNativeError(SetNegativeMaxEntries, "A limit cannot be negative (-2); 0 is no limit");
	ExpectNativeError(SetNegativeMaxSize64, "A limit cannot be negative; 0 is no limit");
	Assert.Equal(g_limits.MaxSize, 1073741824, "the handle is as it was");
	delete g_limits;
}

void SetNegativeMaxSize() {
	g_limits.MaxSize = -1;
}

void SetNegativeMaxEntries() {
	g_limits.MaxEntries = -2;
}

void SetNegativeMaxSize64() {
	int bytes[2] = {0, -1};
	g_limits.SetMaxSize64(bytes);
}

/* ------------------------------------------------- sources and prefixes */

ArchiveSources g_sources;
ArchivePrefixes g_prefixes;

public void Test_SourcesAndPrefixesCountWhatWasAdded() {
	ArchiveSources sources = new ArchiveSources();
	Assert.Equal(sources.Length, 0);
	sources.AddFile("cfg/server.cfg", "server.cfg");
	sources.AddDirectory("cfg", "");
	sources.AddDirectory("cfg", "configs/all");
	Assert.Equal(sources.Length, 3);
	delete sources;

	ArchivePrefixes prefixes = new ArchivePrefixes();
	prefixes.Add("addons");
	prefixes.Add("cfg/sourcemod");
	Assert.Equal(prefixes.Length, 2);
	delete prefixes;
}

public void Test_SourcesAndPrefixesRefuseBadPaths() {
	g_sources = new ArchiveSources();
	ExpectNativeError(AddFileWithoutArchivePath, "A file needs a non-empty archivePath");
	ExpectNativeError(AddFileWithDotDot, "archivePath \"../x\" must be a relative path");
	ExpectNativeError(AddFileOutsideTheGame, "must be a path relative to the game directory");
	ExpectNativeError(AddDirectoryWithEmptyPart, "archivePath \"a//b\" must be a relative path");
	Assert.Equal(g_sources.Length, 0, "nothing was added");
	delete g_sources;

	g_prefixes = new ArchivePrefixes();
	ExpectNativeError(AddEmptyPrefix, "prefix \"\" must be a non-empty relative path");
	ExpectNativeError(AddAbsolutePrefix, "prefix \"/addons\" must be a non-empty relative path");
	Assert.Equal(g_prefixes.Length, 0, "nothing was added");
	delete g_prefixes;
}

void AddFileWithoutArchivePath() {
	g_sources.AddFile("cfg/server.cfg", "");
}

void AddFileWithDotDot() {
	g_sources.AddFile("cfg/server.cfg", "../x");
}

void AddFileOutsideTheGame() {
	g_sources.AddFile("../outside.cfg", "x");
}

void AddDirectoryWithEmptyPart() {
	g_sources.AddDirectory("cfg", "a//b");
}

void AddEmptyPrefix() {
	g_prefixes.Add("");
}

void AddAbsolutePrefix() {
	g_prefixes.Add("/addons");
}

/* ------------------------------------------------------------ codecs in memory */

public void Test_EveryCodecRoundTripsAString() {
	char text[4096];
	SampleText(text, sizeof(text));
	int length = strlen(text);

	for (CodecFormat codec = CodecFormat_Gzip; codec <= CodecFormat_Lz4; codec++) {
		CodecOptions options = NewOptions(codec);
		Compression.CompressString(text, options, OnDone, codec);
		delete options;
		ArchiveResult compressed = Finish();
		Assert.Equal(compressed.Codec, codec);
		Assert.True(compressed.Size > 0 && compressed.Size < length, "codec %d compresses %d bytes to %d", codec,
		            length, compressed.Size);

		int[] bytes = new int[compressed.Size];
		Assert.Equal(compressed.GetBytes(bytes, compressed.Size), compressed.Size);
		Compression.DecompressBytes(bytes, compressed.Size, OnDone, codec, codec);
		ArchiveResult decompressed = Finish();
		Assert.Equal(decompressed.Codec, codec);
		Assert.Equal(decompressed.Size, length);

		char back[4096];
		Assert.Equal(decompressed.GetString(back, sizeof(back)), length);
		Assert.StrEqual(back, text, "codec %d round trips", codec);
		delete compressed;
		delete decompressed;
	}
}

public void Test_AutoTellsEveryCodecInMemoryButBrotli() {
	for (CodecFormat codec = CodecFormat_Gzip; codec <= CodecFormat_Lz4; codec++) {
		CodecOptions options = NewOptions(codec);
		Compression.CompressString("auto", options, OnDone);
		delete options;
		ArchiveResult compressed = Finish();
		int[] bytes = new int[compressed.Size];
		compressed.GetBytes(bytes, compressed.Size);

		Compression.DecompressBytes(bytes, compressed.Size, OnDone);
		ArchiveError expected = codec == CodecFormat_Brotli ? ArchiveError_UnknownFormat : ArchiveError_None;
		ArchiveResult decompressed = Finish(expected);
		if (expected == ArchiveError_None) {
			Assert.Equal(decompressed.Codec, codec, "Auto told codec %d", codec);
		}
		delete compressed;
		delete decompressed;
	}
}

public void Test_GzipWritesTheSameBytesForTheSameInput() {
	GzipOptions gzip = new GzipOptions();
	Compression.CompressString("same", gzip, OnDone);
	Compression.CompressString("same", gzip, OnDone);
	delete gzip;
	ArchiveResult first = Finish();
	ArchiveResult second = Finish();

	int a[64];
	int length = first.GetBytes(a, sizeof(a));
	int b[64];
	Assert.Equal(second.GetBytes(b, sizeof(b)), length);
	for (int i = 0; i < length; i++) {
		Assert.Equal(a[i], b[i], "byte %d", i);
	}
	Assert.Equal(a[0], 0x1F);
	Assert.Equal(a[1], 0x8B);
	Assert.True(a[4] == 0 && a[5] == 0 && a[6] == 0 && a[7] == 0, "the mtime is 0");
	delete first;
	delete second;
}

public void Test_BytesRoundTripEveryByteValue() {
	int bytes[256];
	for (int i = 0; i < sizeof(bytes); i++) {
		bytes[i] = i;
	}
	ZstdOptions zstd = new ZstdOptions();
	Compression.CompressBytes(bytes, sizeof(bytes), zstd, OnDone);
	delete zstd;
	ArchiveResult compressed = Finish();

	int[] packed = new int[compressed.Size];
	compressed.GetBytes(packed, compressed.Size);
	Compression.DecompressBytes(packed, compressed.Size, OnDone);
	ArchiveResult result = Finish();
	Assert.Equal(result.Size, 256);

	int back[256];
	Assert.Equal(result.GetBytes(back, sizeof(back)), 256);
	for (int i = 0; i < sizeof(back); i++) {
		Assert.Equal(back[i], i, "byte %d", i);
	}

	int tail[16];
	Assert.Equal(result.GetBytes(tail, sizeof(tail), 250), 6, "from offset 250 six bytes are left");
	Assert.Equal(tail[0], 250);
	Assert.Equal(result.GetBytes(tail, sizeof(tail), 256), 0, "the end is a valid offset");

	char text[8];
	Assert.Equal(result.GetString(text, sizeof(text)), 7, "the bytes that fit before the terminating null");
	Assert.Equal(text[0], 0, "byte 0 ends the string at once for SourcePawn");
	delete compressed;
	delete result;
}

public void Test_Base64RoundTrips() {
	Lz4Options lz4 = new Lz4Options();
	Compression.CompressString("base64 text", lz4, OnDone);
	delete lz4;
	ArchiveResult compressed = Finish();

	int size = compressed.Base64Size;
	Assert.Equal(size, (compressed.Size + 2) / 3 * 4 + 1, "padded base64 and the null");
	char[] base64 = new char[size];
	Assert.Equal(compressed.GetBase64(base64, size), size - 1);

	Compression.DecompressBase64(base64, OnDone, 0, CodecFormat_Lz4);
	ArchiveResult result = Finish();
	char text[32];
	result.GetString(text, sizeof(text));
	Assert.StrEqual(text, "base64 text");
	delete compressed;
	delete result;
}

public void Test_CompressingIntoMemoryIsHeldToMaxMemory() {
	char text[4096];
	SampleText(text, sizeof(text));
	ArchiveLimits limits = new ArchiveLimits();
	limits.MaxMemory = 16;
	GzipOptions gzip = new GzipOptions();
	Compression.CompressString(text, gzip, OnDone, .limits = limits);
	delete gzip;
	delete limits;
	ArchiveResult result = Finish(ArchiveError_LimitMemory);
	Assert.Equal(result.Size, 0, "a failed job reports no bytes");
	delete result;
}

public void Test_DecompressingIntoMemoryIsHeldToMaxMemory() {
	char text[4096];
	SampleText(text, sizeof(text));
	XzOptions xz = new XzOptions();
	Compression.CompressString(text, xz, OnDone);
	delete xz;
	ArchiveResult compressed = Finish();
	int[] bytes = new int[compressed.Size];
	compressed.GetBytes(bytes, compressed.Size);

	ArchiveLimits limits = new ArchiveLimits();
	limits.MaxMemory = 100;
	Compression.DecompressBytes(bytes, compressed.Size, OnDone, .limits = limits);
	delete limits;
	delete Finish(ArchiveError_LimitMemory);
	delete compressed;
}

public void Test_BrokenDataIsCorrupt() {
	int bytes[16] = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	Compression.DecompressBytes(bytes, sizeof(bytes), OnDone, 0, CodecFormat_Gzip);
	delete Finish(ArchiveError_Corrupt);

	Compression.DecompressBytes(bytes, sizeof(bytes), OnDone, 0, CodecFormat_Zstd);
	delete Finish(ArchiveError_Corrupt);
}

public void Test_UnknownDataIsAnUnknownFormat() {
	int bytes[8] = {'h', 'e', 'l', 'l', 'o', '!', '!', '!'};
	Compression.DecompressBytes(bytes, sizeof(bytes), OnDone);
	delete Finish(ArchiveError_UnknownFormat);
}

public void Test_EmptyOrCutOffInputEndsUnexpectedly() {
	int none[1];
	Compression.DecompressBytes(none, 0, OnDone, 0, CodecFormat_Gzip);
	delete Finish(ArchiveError_UnexpectedEOF);

	Bzip2Options bzip2 = new Bzip2Options();
	Compression.CompressString("cut off", bzip2, OnDone);
	delete bzip2;
	ArchiveResult compressed = Finish();
	int[] bytes = new int[compressed.Size];
	compressed.GetBytes(bytes, compressed.Size);
	Compression.DecompressBytes(bytes, compressed.Size - 4, OnDone, 0, CodecFormat_Bzip2);
	delete Finish(ArchiveError_UnexpectedEOF);
	delete compressed;
}

public void Test_JoinedStreamsDecompressWholeAndOtherDataTrails() {
	GzipOptions gzip = new GzipOptions();
	Compression.CompressString("first ", gzip, OnDone, .serial = true);
	Compression.CompressString("second", gzip, OnDone, .serial = true);
	delete gzip;
	ArchiveResult first = Finish();
	ArchiveResult second = Finish();

	int joined[256];
	int length = first.GetBytes(joined, sizeof(joined));
	length += second.GetBytes(joined[length], sizeof(joined) - length);
	Compression.DecompressBytes(joined, length, OnDone);
	ArchiveResult result = Finish();
	char text[32];
	result.GetString(text, sizeof(text));
	Assert.StrEqual(text, "first second", "both gzip members");
	delete result;

	int trailing[256];
	int size = first.GetBytes(trailing, sizeof(trailing));
	trailing[size] = 'x';
	Compression.DecompressBytes(trailing, size + 1, OnDone);
	delete Finish(ArchiveError_TrailingData);
	delete first;
	delete second;
}

/* -------------------------------------------------------------- codecs on files */

public void Test_EveryCodecRoundTripsAFile() {
	char text[4096];
	SampleText(text, sizeof(text));
	WriteTextFile("cfg/sample.txt", text);

	for (CodecFormat codec = CodecFormat_Gzip; codec <= CodecFormat_Lz4; codec++) {
		char suffix[8];
		CodecSuffix(codec, suffix, sizeof(suffix));
		char packed[PLATFORM_MAX_PATH];
		Format(packed, sizeof(packed), "cfg/out/sample.txt.%s", suffix);
		char unpacked[PLATFORM_MAX_PATH];
		Format(unpacked, sizeof(unpacked), "cfg/back/sample-%s.txt", suffix);

		CodecOptions options = NewOptions(codec);
		int job = Compression.CompressFile("cfg/sample.txt", packed, options, OnDone);
		delete options;
		ArchiveResult compressed = Finish();
		Assert.Equal(compressed.Job, job);
		Assert.Equal(compressed.Size, 0, "a file job keeps nothing in memory");
		Assert.True(FileExists(packed), "%s was written", packed);
		Assert.True(FileSize(packed) < strlen(text));

		Compression.DecompressFile(packed, unpacked, OnDone);
		ArchiveResult decompressed = Finish();
		Assert.Equal(decompressed.Codec, codec, "Auto told %s", packed);

		char back[4096];
		ReadTextFile(unpacked, back, sizeof(back));
		Assert.StrEqual(back, text, "%s round trips", packed);
		delete compressed;
		delete decompressed;
	}
	AssertNoTempLeft("cfg/out");
	AssertNoTempLeft("cfg/back");
}

public void Test_MemoryOutputCanGoToAFile() {
	ZstdOptions zstd = new ZstdOptions();
	Compression.CompressString("to a file", zstd, OnDone, 0, "cfg/memory.zst");
	delete zstd;
	delete Finish();

	int bytes[256];
	int length = ReadBytesFile("cfg/memory.zst", bytes, sizeof(bytes));
	Compression.DecompressBytes(bytes, length, OnDone, 0, CodecFormat_Auto, "cfg/memory.txt");
	ArchiveResult result = Finish();
	Assert.Equal(result.Size, 0, "the bytes went to the file");
	delete result;

	char text[32];
	ReadTextFile("cfg/memory.txt", text, sizeof(text));
	Assert.StrEqual(text, "to a file");
}

public void Test_AnExistingOutputIsKeptWithoutOverwrite() {
	WriteTextFile("cfg/in.txt", "new");
	WriteTextFile("cfg/in.txt.gz", "old");
	GzipOptions gzip = new GzipOptions();
	Compression.CompressFile("cfg/in.txt", "cfg/in.txt.gz", gzip, OnDone);
	delete Finish(ArchiveError_OutputExists);

	char text[16];
	ReadTextFile("cfg/in.txt.gz", text, sizeof(text));
	Assert.StrEqual(text, "old");

	Compression.CompressFile("cfg/in.txt", "cfg/in.txt.gz", gzip, OnDone, 0, true);
	delete gzip;
	delete Finish();
	Assert.True(FileSize("cfg/in.txt.gz") > 3, "overwrite replaced it");
	AssertNoTempLeft("cfg");
}

public void Test_MissingDirectoriesAreCreatedUnlessCreatePathIsFalse() {
	WriteTextFile("cfg/in.txt", "text");
	GzipOptions gzip = new GzipOptions();
	Compression.CompressFile("cfg/in.txt", "cfg/no/such/dir/in.txt.gz", gzip, OnDone, 0, false, false);
	delete Finish(ArchiveError_OpenOutput);
	Assert.False(DirExists("cfg/no"));

	Compression.CompressFile("cfg/in.txt", "cfg/new/dir/in.txt.gz", gzip, OnDone);
	delete gzip;
	delete Finish();
	Assert.True(FileExists("cfg/new/dir/in.txt.gz"));
}

public void Test_AMissingInputCannotBeOpened() {
	GzipOptions gzip = new GzipOptions();
	Compression.CompressFile("cfg/missing.txt", "cfg/missing.txt.gz", gzip, OnDone);
	delete gzip;
	delete Finish(ArchiveError_OpenInput);
	Assert.False(FileExists("cfg/missing.txt.gz"));
	AssertNoTempLeft("cfg");
}

public void Test_AFailedJobWritesNoOutput() {
	WriteTextFile("cfg/broken.gz", "not gzip at all");
	Compression.DecompressFile("cfg/broken.gz", "cfg/broken.txt", OnDone, 0, CodecFormat_Gzip);
	delete Finish(ArchiveError_Corrupt);
	Assert.False(FileExists("cfg/broken.txt"));
	AssertNoTempLeft("cfg");
}

public void Test_BrotliFilesAreToldByTheirName() {
	WriteTextFile("cfg/text.txt", "brotli by name");
	BrotliOptions brotli = new BrotliOptions();
	Compression.CompressFile("cfg/text.txt", "cfg/text.txt.br", brotli, OnDone);
	Compression.CompressFile("cfg/text.txt", "cfg/text.bin", brotli, OnDone);
	delete brotli;
	delete Finish();
	delete Finish();

	Compression.DecompressFile("cfg/text.txt.br", "cfg/text-back.txt", OnDone);
	ArchiveResult result = Finish();
	Assert.Equal(result.Codec, CodecFormat_Brotli);
	delete result;

	Compression.DecompressFile("cfg/text.bin", "cfg/text-bin.txt", OnDone);
	delete Finish(ArchiveError_UnknownFormat);
	Compression.DecompressFile("cfg/text.bin", "cfg/text-bin.txt", OnDone, 0, CodecFormat_Brotli);
	delete Finish();
}

public void Test_DecompressionIsHeldToMaxSize() {
	char text[4096];
	SampleText(text, sizeof(text));
	WriteTextFile("cfg/sample.txt", text);
	ZstdOptions zstd = new ZstdOptions();
	Compression.CompressFile("cfg/sample.txt", "cfg/sample.txt.zst", zstd, OnDone);
	delete zstd;
	delete Finish();

	ArchiveLimits limits = new ArchiveLimits();
	limits.MaxSize = 100;
	Compression.DecompressFile("cfg/sample.txt.zst", "cfg/sample-back.txt", OnDone, .limits = limits);
	delete limits;
	delete Finish(ArchiveError_LimitSize);
	Assert.False(FileExists("cfg/sample-back.txt"));
}

/* -------------------------------------------------------------------- the jobs */

public void Test_CallbacksRunOnALaterFrame() {
	GzipOptions gzip = new GzipOptions();
	g_queueing = true;
	int job = Compression.CompressString("later", gzip, OnDone);
	g_queueing = false;
	delete gzip;
	Assert.Equal(g_calls, 0, "no callback ran during the call");
	Assert.True(job > 0, "the job has an id");

	ArchiveResult result = Finish();
	Assert.False(g_calledInside);
	Assert.Equal(result.Job, job);
	delete result;
}

public void Test_EveryJobHasItsOwnId() {
	GzipOptions gzip = new GzipOptions();
	int first = Compression.CompressString("a", gzip, OnDone);
	int second = Compression.CompressString("b", gzip, OnDone);
	delete gzip;
	Assert.NotEqual(first, second);
	WaitForJobs(2);
}

public void Test_SerialJobsRunInTheirOrder() {
	XzOptions xz = new XzOptions();
	xz.Preset = 9;
	char text[4096];
	SampleText(text, sizeof(text));
	for (int i = 0; i < 6; i++) {
		Compression.CompressString(text, xz, OnDone, i, .serial = true);
	}
	delete xz;
	WaitForJobs(6);
	for (int i = 0; i < 6; i++) {
		Assert.Equal(g_order[i], i, "callback %d", i);
	}
}

public void Test_CancelDropsAQueuedJob() {
	char text[4096];
	SampleText(text, sizeof(text));
	XzOptions xz = new XzOptions();
	xz.Preset = 9;
	xz.Extreme = true;
	int jobs[4];
	for (int i = 0; i < sizeof(jobs); i++) {
		jobs[i] = Compression.CompressString(text, xz, OnDone, i, .serial = true);
	}
	delete xz;

	Assert.True(Compression.Cancel(jobs[3]), "the last job is still queued");
	WaitForJobs(4);
	for (int i = 0; i < g_calls; i++) {
		if (g_order[i] == 3) {
			Assert.Equal(g_errors[i], ArchiveError_Cancelled);
		}
	}
	Assert.False(Compression.Cancel(jobs[3]), "the job is gone");
}

public void Test_CancelTellsAnUnknownJob() {
	Assert.False(Compression.Cancel(123456));
}

int g_progressCalls;
any g_progressData;
int g_progressInputSize;

public void Test_ProgressIsReportedWhileAJobRuns() {
	int block[4096];
	for (int i = 0; i < sizeof(block); i++) {
		block[i] = (i * 7919 + i / 13) & 0xFF;
	}
	File file = OpenFile("cfg/big.bin", "wb");
	for (int i = 0; i < 2048; i++) {
		file.Write(block, sizeof(block), 1);
	}
	delete file;

	XzOptions xz = new XzOptions();
	xz.Preset = 9;
	xz.Extreme = true;
	Compression.CompressFile("cfg/big.bin", "cfg/big.bin.xz", xz, OnDone, 42, .progress = OnProgress);
	delete xz;
	delete Finish();
	Assert.True(g_progressCalls > 0, "the progress callback ran");
	Assert.Equal(g_progressData, 42);
	Assert.Equal(g_progressInputSize, 2048 * 4096);
}

void OnProgress(int job, int bytesRead, int bytesWritten, int entries, int inputSize, any data) {
	g_progressCalls++;
	g_progressData = data;
	g_progressInputSize = inputSize;
	Assert.True(bytesRead >= 0 && bytesRead <= inputSize, "%d of %d bytes read", bytesRead, inputSize);
}

public void Test_ErrorStringNamesEveryError() {
	char names[][] = {
		"ArchiveError_None", "ArchiveError_OpenInput", "ArchiveError_OpenOutput", "ArchiveError_OutputExists",
		"ArchiveError_CreatePath", "ArchiveError_Read", "ArchiveError_Write", "ArchiveError_Rename",
		"ArchiveError_Memory", "ArchiveError_Corrupt", "ArchiveError_UnexpectedEOF", "ArchiveError_TrailingData",
		"ArchiveError_UnknownFormat", "ArchiveError_Unsupported", "ArchiveError_Encrypted", "ArchiveError_EntryName",
		"ArchiveError_UnsafeEntry", "ArchiveError_LimitSize", "ArchiveError_LimitEntries", "ArchiveError_LimitRatio",
		"ArchiveError_LimitMemory", "ArchiveError_EntryNotFound", "ArchiveError_Cancelled", "ArchiveError_Internal",
	};
	for (int i = 0; i < sizeof(names); i++) {
		char name[64];
		Assert.Equal(Compression.ErrorString(view_as<ArchiveError>(i), name, sizeof(name)), strlen(names[i]));
		Assert.StrEqual(name, names[i]);
	}
	ExpectNativeError(ErrorStringOfAnInvalidError, "Invalid ArchiveError 24");
}

void ErrorStringOfAnInvalidError() {
	char name[64];
	Compression.ErrorString(view_as<ArchiveError>(24), name, sizeof(name));
}

/* ---------------------------------------------------------------- archives */

/* Writes the source tree the archive tests pack. */
void WriteSourceTree() {
	CreateDirectory("cfg/tree", 511);
	CreateDirectory("cfg/tree/addons", 511);
	CreateDirectory("cfg/tree/addons/sub", 511);
	CreateDirectory("cfg/tree/addons2", 511);
	WriteTextFile("cfg/tree/readme.txt", "read me");
	WriteTextFile("cfg/tree/addons/a.txt", "file a");
	WriteTextFile("cfg/tree/addons/sub/b.txt", "file b in sub");
	WriteTextFile("cfg/tree/addons2/c.txt", "file c");
}

/* Creates outFile from the source tree with options; returns the result. */
ArchiveResult CreateFromTree(const char[] outFile, ArchiveWriteOptions options) {
	WriteSourceTree();
	ArchiveSources sources = new ArchiveSources();
	sources.AddDirectory("cfg/tree");
	Archive.Create(outFile, sources, options, OnDone);
	delete sources;
	return Finish();
}

/* Fails unless the result lists path as an entry of type; returns its index. */
int FindEntry(ArchiveResult result, const char[] path) {
	for (int i = 0; i < result.EntryCount; i++) {
		char entry[PLATFORM_MAX_PATH];
		result.GetEntryPath(i, entry, sizeof(entry));
		if (StrEqual(entry, path)) {
			return i;
		}
	}
	return -1;
}

void AssertExtracted(const char[] dir) {
	char path[PLATFORM_MAX_PATH];
	Format(path, sizeof(path), "%s/readme.txt", dir);
	char text[64];
	ReadTextFile(path, text, sizeof(text));
	Assert.StrEqual(text, "read me", "%s", path);
	Format(path, sizeof(path), "%s/addons/sub/b.txt", dir);
	ReadTextFile(path, text, sizeof(text));
	Assert.StrEqual(text, "file b in sub", "%s", path);
}

/* Creates, lists, extracts and reads an entry of an archive. */
void RoundTripArchive(const char[] outFile, ArchiveWriteOptions options, ArchiveFormat format) {
	ArchiveResult created = CreateFromTree(outFile, options);
	Assert.Equal(created.Format, format, "%s", outFile);
	Assert.True(FindEntry(created, "addons/sub/b.txt") != -1, "%s holds addons/sub/b.txt", outFile);
	delete created;

	Archive.List(outFile, OnDone);
	ArchiveResult listed = Finish();
	Assert.Equal(listed.Format, format, "Auto told %s", outFile);
	int file = FindEntry(listed, "addons/sub/b.txt");
	Assert.True(file != -1, "%s lists addons/sub/b.txt", outFile);
	Assert.Equal(listed.GetEntryType(file), ArchiveEntry_File);
	Assert.Equal(listed.GetEntrySize(file), 13);
	Assert.True(listed.GetEntryTime(file) > 0, "it has a modification time");
	int dir = FindEntry(listed, "addons/sub");
	Assert.True(dir != -1, "%s lists the directory addons/sub", outFile);
	Assert.Equal(listed.GetEntryType(dir), ArchiveEntry_Directory);
	delete listed;

	char target[PLATFORM_MAX_PATH];
	Format(target, sizeof(target), "%s.dir", outFile);
	Archive.Extract(outFile, target, OnDone);
	ArchiveResult extracted = Finish();
	file = FindEntry(extracted, "addons/sub/b.txt");
	Assert.True(extracted.IsEntryWritten(file));
	Assert.Equal(extracted.GetEntrySkip(file), ArchiveSkip_None);
	delete extracted;
	AssertExtracted(target);

	Archive.ReadEntry(outFile, "addons/a.txt", OnDone);
	ArchiveResult entry = Finish();
	char text[32];
	entry.GetString(text, sizeof(text));
	Assert.StrEqual(text, "file a", "%s's addons/a.txt", outFile);
	Assert.Equal(entry.EntryCount, 1);
	delete entry;
}

public void Test_TarRoundTrips() {
	TarOptions tar = new TarOptions();
	RoundTripArchive("cfg/tree.tar", tar, ArchiveFormat_Tar);
	delete tar;
}

public void Test_TarThroughEveryCodecRoundTrips() {
	char suffixes[6][8] = {"tgz", "tbz2", "tbr", "txz", "tzst", "tlz4"};
	for (CodecFormat codec = CodecFormat_Gzip; codec <= CodecFormat_Lz4; codec++) {
		CodecOptions options = NewOptions(codec);
		TarOptions tar = new TarOptions(options);
		delete options;
		char outFile[PLATFORM_MAX_PATH];
		Format(outFile, sizeof(outFile), "cfg/tree.%s", suffixes[view_as<int>(codec) - 2]);
		RoundTripArchive(outFile, tar, ArchiveFormat_Tar);
		delete tar;

		Archive.List(outFile, OnDone);
		ArchiveResult listed = Finish();
		Assert.Equal(listed.Codec, codec, "%s is read through codec %d", outFile, codec);
		delete listed;
	}
}

public void Test_ZipWithEveryMethodRoundTrips() {
	for (ZipMethod method = ZipMethod_Store; method <= ZipMethod_Zstd; method++) {
		ZipOptions zip = new ZipOptions();
		zip.Method = method;
		char outFile[PLATFORM_MAX_PATH];
		Format(outFile, sizeof(outFile), "cfg/tree-%d.zip", method);
		RoundTripArchive(outFile, zip, ArchiveFormat_Zip);
		delete zip;
	}
}

public void Test_SevenZipWithEveryMethodRoundTrips() {
	for (SevenZipMethod method = SevenZipMethod_Copy; method <= SevenZipMethod_Ppmd; method++) {
		SevenZipOptions sevenZip = new SevenZipOptions();
		sevenZip.Method = method;
		char outFile[PLATFORM_MAX_PATH];
		Format(outFile, sizeof(outFile), "cfg/tree-%d.7z", method);
		RoundTripArchive(outFile, sevenZip, ArchiveFormat_SevenZip);
		delete sevenZip;
	}
}

public void Test_ExtractWritesOnlyTheEntriesBelowThePrefixes() {
	TarOptions tar = new TarOptions();
	delete CreateFromTree("cfg/tree.tar", tar);
	delete tar;

	ArchivePrefixes prefixes = new ArchivePrefixes();
	prefixes.Add("addons");
	Archive.Extract("cfg/tree.tar", "cfg/only-addons", OnDone, .prefixes = prefixes);
	delete prefixes;
	ArchiveResult result = Finish();
	Assert.True(FindEntry(result, "addons/a.txt") != -1);
	Assert.Equal(FindEntry(result, "addons2/c.txt"), -1, "addons2 is not below addons");
	Assert.Equal(FindEntry(result, "readme.txt"), -1);
	delete result;

	Assert.True(FileExists("cfg/only-addons/addons/sub/b.txt"));
	Assert.False(FileExists("cfg/only-addons/addons2/c.txt"));
	Assert.False(FileExists("cfg/only-addons/readme.txt"));
}

public void Test_ExtractNeedsANewTargetDirectory() {
	TarOptions tar = new TarOptions();
	delete CreateFromTree("cfg/tree.tar", tar);
	delete tar;
	CreateDirectory("cfg/exists", 511);
	Archive.Extract("cfg/tree.tar", "cfg/exists", OnDone);
	delete Finish(ArchiveError_OutputExists);
	Assert.False(FileExists("cfg/exists/readme.txt"));

	Archive.Extract("cfg/tree.tar", "cfg/no/parent", OnDone, .createPath = false);
	delete Finish(ArchiveError_CreatePath);
	Assert.False(DirExists("cfg/no"));
}

public void Test_ReadEntryTellsAMissingEntryAndADirectory() {
	ZipOptions zip = new ZipOptions();
	delete CreateFromTree("cfg/tree.zip", zip);
	delete zip;
	Archive.ReadEntry("cfg/tree.zip", "missing.txt", OnDone);
	delete Finish(ArchiveError_EntryNotFound);
	Archive.ReadEntry("cfg/tree.zip", "addons/sub", OnDone);
	delete Finish(ArchiveError_Unsupported);
}

public void Test_CreateRefusesTwoSourcesForOnePath() {
	WriteTextFile("cfg/one.txt", "1");
	WriteTextFile("cfg/two.txt", "2");
	ArchiveSources sources = new ArchiveSources();
	sources.AddFile("cfg/one.txt", "same.txt");
	sources.AddFile("cfg/two.txt", "same.txt");
	TarOptions tar = new TarOptions();
	Archive.Create("cfg/same.tar", sources, tar, OnDone);
	delete tar;
	delete sources;
	delete Finish(ArchiveError_UnsafeEntry);
	Assert.False(FileExists("cfg/same.tar"));
}

public void Test_AnArchiveOfOtherDataIsAnUnknownFormat() {
	WriteTextFile("cfg/not-an-archive.bin", "just some text, no archive");
	Archive.List("cfg/not-an-archive.bin", OnDone);
	delete Finish(ArchiveError_UnknownFormat);
}

public void Test_ListIsHeldToMaxEntries() {
	TarOptions tar = new TarOptions();
	delete CreateFromTree("cfg/tree.tar", tar);
	delete tar;
	ArchiveLimits limits = new ArchiveLimits();
	limits.MaxEntries = 2;
	Archive.List("cfg/tree.tar", OnDone, .limits = limits);
	delete limits;
	ArchiveResult result = Finish(ArchiveError_LimitEntries);
	Assert.Equal(result.EntryCount, 0, "a failed job reports no entries");
	delete result;
}

/* ----------------------------------------------------- hostile hand-made tars */

/* A tar being built: bytes, one per cell. */
int g_tar[16384];
int g_tarLength;

void TarStart() {
	g_tarLength = 0;
}

/* Appends one entry: type '0' file, '2' symlink, '5' directory. */
void TarAdd(const char[] name, int type, const char[] content = "", const char[] link = "") {
	int header = g_tarLength;
	for (int i = 0; i < 512; i++) {
		g_tar[header + i] = 0;
	}
	PutString(header, name);
	PutString(header + 100, "0000644");
	PutString(header + 108, "0000000");
	PutString(header + 116, "0000000");
	int size = type == '0' ? strlen(content) : 0;
	char octal[16];
	FormatOctal(size, 11, octal, sizeof(octal));
	PutString(header + 124, octal);
	PutString(header + 136, "14000000000");
	g_tar[header + 156] = type;
	PutString(header + 157, link);
	PutString(header + 257, "ustar");
	g_tar[header + 263] = '0';
	g_tar[header + 264] = '0';

	int sum = 8 * ' ';
	for (int i = 0; i < 512; i++) {
		sum += (i >= 148 && i < 156) ? 0 : g_tar[header + i];
	}
	FormatOctal(sum, 6, octal, sizeof(octal));
	PutString(header + 148, octal);
	g_tar[header + 155] = ' ';
	g_tarLength += 512;

	for (int i = 0; i < size; i++) {
		g_tar[g_tarLength + i] = content[i];
	}
	int padded = (size + 511) / 512 * 512;
	for (int i = size; i < padded; i++) {
		g_tar[g_tarLength + i] = 0;
	}
	g_tarLength += padded;
}

void TarFinish(const char[] path) {
	for (int i = 0; i < 1024; i++) {
		g_tar[g_tarLength + i] = 0;
	}
	g_tarLength += 1024;
	WriteBytesFile(path, g_tar, g_tarLength);
}

void PutString(int offset, const char[] text) {
	for (int i = 0; text[i] != '\0'; i++) {
		g_tar[offset + i] = text[i];
	}
}

void FormatOctal(int value, int digits, char[] buffer, int maxlength) {
	Assert.True(digits < maxlength);
	for (int i = digits - 1; i >= 0; i--) {
		buffer[i] = '0' + (value & 7);
		value >>= 3;
	}
	buffer[digits] = '\0';
}

public void Test_AHandMadeTarIsRead() {
	TarStart();
	TarAdd("dir", '5');
	TarAdd("dir/file.txt", '0', "hand made");
	TarFinish("cfg/hand.tar");

	Archive.Extract("cfg/hand.tar", "cfg/hand", OnDone);
	ArchiveResult result = Finish();
	Assert.Equal(result.EntryCount, 2);
	delete result;
	char text[32];
	ReadTextFile("cfg/hand/dir/file.txt", text, sizeof(text));
	Assert.StrEqual(text, "hand made");
}

/* Extracts a tar with one hostile entry, which must fail with expected. */
void ExtractFails(const char[] path, ArchiveError expected) {
	Archive.Extract(path, "cfg/hostile", OnDone);
	delete Finish(expected);
	Assert.False(DirExists("cfg/hostile"), "nothing was extracted from %s", path);
	AssertNoTempLeft("cfg");
}

public void Test_ExtractRefusesEntriesThatLeaveTheTarget() {
	TarStart();
	TarAdd("good.txt", '0', "good");
	TarAdd("../evil.txt", '0', "evil");
	TarFinish("cfg/dotdot.tar");
	ExtractFails("cfg/dotdot.tar", ArchiveError_UnsafeEntry);
	Assert.False(FileExists("evil.txt"));

	TarStart();
	TarAdd("/tmp/smarchive-absolute.txt", '0', "evil");
	TarFinish("cfg/absolute.tar");
	ExtractFails("cfg/absolute.tar", ArchiveError_UnsafeEntry);

	Archive.List("cfg/dotdot.tar", OnDone);
	delete Finish(ArchiveError_UnsafeEntry);
}

public void Test_ExtractRefusesThePathTwice() {
	TarStart();
	TarAdd("twice.txt", '0', "first");
	TarAdd("./twice.txt", '0', "second");
	TarFinish("cfg/twice.tar");
	ExtractFails("cfg/twice.tar", ArchiveError_UnsafeEntry);
}

public void Test_ExtractSkipsSymlinksThatLeadOutside() {
	TarStart();
	TarAdd("inside.txt", '0', "inside");
	TarAdd("good-link", '2', "", "inside.txt");
	TarAdd("bad-link", '2', "", "../../server.cfg");
	TarFinish("cfg/links.tar");

	Archive.Extract("cfg/links.tar", "cfg/links", OnDone);
	ArchiveResult result = Finish();
	int good = FindEntry(result, "good-link");
	int bad = FindEntry(result, "bad-link");
	Assert.Equal(result.GetEntryType(good), ArchiveEntry_Symlink);
	Assert.True(result.IsEntryWritten(good));
	char target[32];
	result.GetEntryLinkTarget(bad, target, sizeof(target));
	Assert.StrEqual(target, "../../server.cfg");
	Assert.False(result.IsEntryWritten(bad));
	Assert.Equal(result.GetEntrySkip(bad), ArchiveSkip_LinkOutside);
	delete result;

	char text[16];
	ReadTextFile("cfg/links/good-link", text, sizeof(text));
	Assert.StrEqual(text, "inside", "the link inside works");
	Assert.False(FileExists("cfg/links/bad-link"));
}

public void Test_ExtractIsHeldToMaxSize() {
	char text[4096];
	SampleText(text, sizeof(text));
	TarStart();
	TarAdd("big.txt", '0', text);
	TarFinish("cfg/big.tar");

	ArchiveLimits limits = new ArchiveLimits();
	limits.MaxSize = 1000;
	Archive.Extract("cfg/big.tar", "cfg/big", OnDone, .limits = limits);
	delete limits;
	delete Finish(ArchiveError_LimitSize);
	Assert.False(DirExists("cfg/big"));
}

/* -------------------------------------------------------------- native errors */

/* Calls an operation with bad arguments; each must be a native error. */
public void Test_OperationsRefuseBadPaths() {
	char failing[][] = {
		"Call_EmptyPath", "Call_AbsolutePath", "Call_FileUrlPath", "Call_BackslashPath", "Call_EmptyPartPath",
		"Call_TrailingSlashPath", "Call_DotPath", "Call_DotDotPath",
	};
	for (int i = 0; i < sizeof(failing); i++) {
		ExpectNativeError(GetFunctionByName(null, failing[i]), "must be a path relative to the game directory");
	}
	Assert.Equal(g_calls, 0, "nothing was queued");
	Runner.Frames(5);
	Assert.Equal(g_calls, 0, "nothing was queued");
}

void DecompressFileTo(const char[] outFile) {
	Compression.DecompressFile("cfg/in.gz", outFile, OnDone);
}

public void Call_EmptyPath() {
	DecompressFileTo("");
}

public void Call_AbsolutePath() {
	DecompressFileTo("/tmp/out.txt");
}

public void Call_FileUrlPath() {
	DecompressFileTo("file:///tmp/out.txt");
}

public void Call_BackslashPath() {
	DecompressFileTo("cfg\\out.txt");
}

public void Call_EmptyPartPath() {
	DecompressFileTo("cfg//out.txt");
}

public void Call_TrailingSlashPath() {
	DecompressFileTo("cfg/out/");
}

public void Call_DotPath() {
	DecompressFileTo("cfg/./out.txt");
}

public void Call_DotDotPath() {
	DecompressFileTo("cfg/../out.txt");
}

public void Test_OperationsRefuseOtherBadArguments() {
	ExpectNativeError(SameInputAndOutput, "are the same file");
	ExpectNativeError(InvalidCallback, "Invalid callback function");
	ExpectNativeError(InvalidPriority, "Invalid ArchivePriority 2");
	ExpectNativeError(InvalidDecompressionFormat, "Invalid CodecFormat 1 for decompression");
	ExpectNativeError(InvalidLength, "Invalid length -1");
	ExpectNativeError(InvalidBase64, "base64 is no valid base64");
	ExpectNativeError(InvalidOptionsHandle, "Invalid CodecOptions handle");
	ExpectNativeError(InvalidLimitsHandle, "Invalid ArchiveLimits handle");
	ExpectNativeError(CodecWithZip, "Only ArchiveFormat_Tar is read through a codec");
	ExpectNativeError(CodecWithAuto, "A CodecFormat other than Auto needs ArchiveFormat_Tar");
	ExpectNativeError(InvalidArchiveFormat, "Invalid ArchiveFormat 5");
	ExpectNativeError(EmptySources, "sources is empty");
	ExpectNativeError(InvalidEntryPath, "entryPath \"../x\"");
	Runner.Frames(5);
	Assert.Equal(g_calls, 0, "nothing was queued");
}

void SameInputAndOutput() {
	GzipOptions gzip = new GzipOptions();
	Compression.CompressFile("cfg/x.txt", "cfg/x.txt", gzip, OnDone);
	delete gzip;
}

void InvalidCallback() {
	Compression.DecompressFile("cfg/in.gz", "cfg/out.txt", view_as<ArchiveCallback>(INVALID_FUNCTION));
}

void InvalidPriority() {
	Compression.DecompressFile("cfg/in.gz", "cfg/out.txt", OnDone, .priority = view_as<ArchivePriority>(2));
}

void InvalidDecompressionFormat() {
	Compression.DecompressFile("cfg/in.gz", "cfg/out.txt", OnDone, 0, CodecFormat_None);
}

void InvalidLength() {
	int bytes[4];
	Compression.DecompressBytes(bytes, -1, OnDone);
}

void InvalidBase64() {
	Compression.DecompressBase64("not base64!", OnDone);
}

void InvalidOptionsHandle() {
	Compression.CompressString("x", null, OnDone);
}

void InvalidLimitsHandle() {
	GzipOptions gzip = new GzipOptions();
	Compression.DecompressFile("cfg/in.gz", "cfg/out.txt", OnDone, .limits = view_as<ArchiveLimits>(gzip));
	delete gzip;
}

void CodecWithZip() {
	Archive.List("cfg/x.zip", OnDone, 0, ArchiveFormat_Zip, CodecFormat_Gzip);
}

void CodecWithAuto() {
	Archive.List("cfg/x.tar.gz", OnDone, 0, ArchiveFormat_Auto, CodecFormat_Gzip);
}

void InvalidArchiveFormat() {
	Archive.List("cfg/x.tar", OnDone, 0, view_as<ArchiveFormat>(5));
}

void EmptySources() {
	ArchiveSources sources = new ArchiveSources();
	TarOptions tar = new TarOptions();
	Archive.Create("cfg/x.tar", sources, tar, OnDone);
	delete tar;
	delete sources;
}

void InvalidEntryPath() {
	Archive.ReadEntry("cfg/x.tar", "../x", OnDone);
}

ArchiveResult g_result;

public void Test_ResultAccessorsRefuseBadArguments() {
	GzipOptions gzip = new GzipOptions();
	Compression.CompressString("result", gzip, OnDone);
	delete gzip;
	g_result = Finish();

	ExpectNativeError(EntryOutsideTheResult, "Entry index 0 is outside 0--1");
	ExpectNativeError(BytesFromBeyondTheEnd, "Offset 1000 is outside 0-");
	ExpectNativeError(Base64IntoATooSmallBuffer, "the base64 needs");
	ExpectNativeError(BytesIntoANegativeBuffer, "Invalid maxlength -1");
	delete g_result;
}

void EntryOutsideTheResult() {
	g_result.GetEntryType(0);
}

void BytesFromBeyondTheEnd() {
	int bytes[4];
	g_result.GetBytes(bytes, sizeof(bytes), 1000);
}

void Base64IntoATooSmallBuffer() {
	char base64[4];
	g_result.GetBase64(base64, sizeof(base64));
}

void BytesIntoANegativeBuffer() {
	int bytes[4];
	g_result.GetBytes(bytes, -1);
}
