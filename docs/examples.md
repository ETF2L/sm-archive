# Examples

Every example needs `#include <smarchive>`. The examples are fragments of a plugin: `json` in "Text in memory, as
base64" is the plugin's own variable.

## Compress a demo with zstd

```sourcepawn
void ArchiveDemo(const char[] demo) {
	char packed[PLATFORM_MAX_PATH];
	FormatEx(packed, sizeof(packed), "%s.zst", demo);
	ZstdOptions options = new ZstdOptions();
	options.Level = 19;
	options.LongWindow = 27;
	// The job knows nothing of the input in its callback; the name travels in a DataPack.
	DataPack pack = new DataPack();
	pack.WriteString(demo);
	Compression.CompressFile(demo, packed, options, OnDemoPacked, pack);
	delete options;
}

/* data is any; turn it back into the DataPack. */
void OnDemoPacked(ArchiveError error, ArchiveResult result, any data) {
	DataPack pack = view_as<DataPack>(data);
	pack.Reset();
	char demo[PLATFORM_MAX_PATH];
	pack.ReadString(demo, sizeof(demo));
	delete pack;
	if (error != ArchiveError_None) {
		char message[256];
		result.GetMessage(message, sizeof(message));
		LogError("Cannot pack %s: %s", demo, message);
		return;
	}
	DeleteFile(demo);
}
```

The options are copied when the job is queued, so they can be deleted at once. The original demo is deleted only once
its packed copy is complete: the output appears only on success.

## Decompress whatever it is

```sourcepawn
Compression.DecompressFile("downloads/map.bsp.bz2", "maps/map.bsp", OnUnpacked);

void OnUnpacked(ArchiveError error, ArchiveResult result, any data) {
	// ... on ArchiveError_None the map is complete in maps/; otherwise log result.GetMessage ...
}
```

`CodecFormat_Auto` tells gzip, bzip2, xz, zstd and lz4 by their magic bytes and brotli by `.br`.

## Look at a release, then extract only addons/

```sourcepawn
void Inspect(const char[] release) {
	DataPack pack = new DataPack();
	pack.WriteString(release);
	Archive.List(release, OnListed, pack);
}

/* data is any; turn it back into the DataPack. */
void OnListed(ArchiveError error, ArchiveResult result, any data) {
	DataPack pack = view_as<DataPack>(data);
	pack.Reset();
	char release[PLATFORM_MAX_PATH];
	pack.ReadString(release, sizeof(release));
	delete pack;
	if (error != ArchiveError_None)
		return;

	char path[PLATFORM_MAX_PATH];
	bool hasAddons = false;
	for (int i = 0; i < result.EntryCount; i++) {
		result.GetEntryPath(i, path, sizeof(path));
		hasAddons = hasAddons || StrEqual(path, "addons") || StrContains(path, "addons/") == 0;
	}
	if (!hasAddons)
		return;

	ArchivePrefixes prefixes = new ArchivePrefixes();
	prefixes.Add("addons");
	Archive.Extract(release, "staging/release", OnExtracted, .prefixes = prefixes);
	delete prefixes;
}

void OnExtracted(ArchiveError error, ArchiveResult result, any data) {
	// ... on ArchiveError_None, result.EntryCount entries are in staging/release/ ...
}
```

## Read one file of an archive

```sourcepawn
Archive.ReadEntry("downloads/release.tar.gz", "manifest.txt", OnManifest);

void OnManifest(ArchiveError error, ArchiveResult result, any data) {
	char text[4096];
	if (error == ArchiveError_None)
		result.GetString(text, sizeof(text));
}
```

## Pack logs into a .tar.xz

```sourcepawn
XzOptions xz = new XzOptions();
TarOptions tar = new TarOptions(xz);
delete xz;
ArchiveSources sources = new ArchiveSources();
sources.AddDirectory("addons/sourcemod/logs", "logs");
sources.AddFile("cfg/server.cfg", "server.cfg");
Archive.Create("archives/logs.tar.xz", sources, tar, OnPacked);
delete sources;
delete tar;

void OnPacked(ArchiveError error, ArchiveResult result, any data) {
	// ... on an error, log result.GetMessage; the output does not exist then ...
}
```

## A zip for players

```sourcepawn
ZipOptions zip = new ZipOptions();
ArchiveSources sources = new ArchiveSources();
sources.AddDirectory("custom/myhud", "myhud");
Archive.Create("download/myhud.zip", sources, zip, OnPacked, .overwrite = true);   // OnPacked as above
delete sources;
delete zip;
```

## Text in memory, as base64

```sourcepawn
BrotliOptions options = new BrotliOptions();
options.Mode = BrotliContent_Text;
Compression.CompressString(json, options, OnPackedText);
delete options;

void OnPackedText(ArchiveError error, ArchiveResult result, any data) {
	if (error != ArchiveError_None)
		return;
	char[] base64 = new char[result.Base64Size];
	result.GetBase64(base64, result.Base64Size);
	// ... and later: Compression.DecompressBase64(base64, OnText, .format = CodecFormat_Brotli);
}

void OnText(ArchiveError error, ArchiveResult result, any data) {
	// ... on ArchiveError_None, result.GetString gives the JSON text back ...
}
```

## Larger limits for one job, progress and cancellation

```sourcepawn
int g_job;

void UnpackMap(const char[] archive) {
	ArchiveLimits limits = new ArchiveLimits();
	int maxSize[2] = {0, 1};  // 4 GiB: low 0, high 1
	limits.SetMaxSize64(maxSize);
	g_job = Archive.Extract(archive, "staging/map", OnMapExtracted, .limits = limits, .progress = OnMapProgress,
		.priority = ArchivePriority_High);
	delete limits;
}

void OnMapExtracted(ArchiveError error, ArchiveResult result, any data) {
	// ... ArchiveError_Cancelled when Abort ran first ...
}

void OnMapProgress(int job, int bytesRead, int bytesWritten, int entries, int inputSize, any data) {
	PrintToServer("%d of %d bytes read", bytesRead, inputSize);
}

void Abort() {
	Compression.Cancel(g_job);
}
```

## Jobs one after the other

```sourcepawn
// The second job starts only after the first ended, whatever it ended with.
Bzip2Options bz2 = new Bzip2Options();
Compression.CompressFile("demos/a.dem", "demos/a.dem.bz2", bz2, OnPacked, .serial = true);
Compression.CompressFile("demos/b.dem", "demos/b.dem.bz2", bz2, OnPacked, .serial = true);
delete bz2;
```
