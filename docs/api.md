# API reference

`scripting/include/smarchive.inc` documents every native; this page describes how they behave together.

## Operations

| Native | Does |
|---|---|
| `Compression.CompressFile(inFile, outFile, options, callback, ...)` | Compresses a file with the codec of `options`. |
| `Compression.DecompressFile(inFile, outFile, callback, data, format, ...)` | Decompresses a file. |
| `Compression.CompressString(text, options, callback, data, outFile, ...)` | Compresses a string into memory or `outFile`. |
| `Compression.CompressBytes(bytes, length, options, callback, ...)` | The same for bytes, one per cell. |
| `Compression.DecompressBytes(bytes, length, callback, data, format, outFile, ...)` | Decompresses bytes. |
| `Compression.DecompressBase64(base64, callback, data, format, outFile, ...)` | Decompresses base64 text. |
| `Archive.List(archive, callback, data, format, codec, ...)` | Lists the entries. |
| `Archive.Extract(archive, targetDir, callback, data, format, codec, prefixes, ...)` | Extracts into a new directory. |
| `Archive.ReadEntry(archive, entryPath, callback, data, format, codec, ...)` | Reads one file into memory. |
| `Archive.Create(outFile, sources, options, callback, ...)` | Creates a tar, zip or 7z. |
| `Compression.Cancel(job)` | Cancels a job of the calling plugin. |
| `Compression.ErrorString(error, buffer, maxlength)` | Names an `ArchiveError`. |

Every operation that queues a job returns the job's id. Optional arguments can be named:
`Archive.Extract(file, dir, OnDone, .prefixes = list, .priority = ArchivePriority_High)`.

The common optional arguments: `data` (passed to the callbacks), `overwrite` (replace an existing `outFile`, default
false), `createPath` (create missing directories, default true), `serial`, `priority`, `limits` (null for the
server's defaults) and `progress` (`INVALID_FUNCTION` for none). Not every operation takes all of them:
`CompressFile` and `Archive.Create` take no `limits`, since limits apply to unpacking (`CompressString` and
`CompressBytes` take them for `MaxMemory` only); `List` and `ReadEntry` write no file and take no `overwrite` or
`createPath`; `Extract` takes no `overwrite`, since `targetDir` must not exist.

## Arguments are checked at the call

A bad argument throws a native error in the calling plugin and queues nothing: a path that breaks the rules below,
`inFile` and `outFile` naming the same file, an invalid or wrongly typed handle, a value outside an enum, a callback
that is no function, base64 that is not valid, an empty `ArchiveSources`, a codec with a container other than tar,
a `CodecFormat` other than `CodecFormat_Auto` with `ArchiveFormat_Auto`. Option setters throw for values outside
their format's range and leave the handle as it was.

## Paths

Paths are relative to the game directory (for Team Fortress 2 `tf/`), UTF-8, with `/` between their parts. A path is
refused when it is empty, starts with `/` or `file://`, contains `\` (which SourceMod turns into `/`), has an empty
part (`a//b`, a trailing `/`) or a part that is `.` or `..`, or does not fit into the server's `PLATFORM_MAX_PATH`
(4096 bytes, not SourcePawn's 256) below the game directory.

Paths inside an archive (`AddFile`'s and `AddDirectory`'s `archivePath`, prefixes, `ReadEntry`'s `entryPath`) follow
the same rules, without the length; `AddDirectory` also takes `""` for the archive's root, `AddFile` does not.

An array and the length a plugin passes with it (`bytes` and `length`, `buffer` and `maxlength`, an `int[2]`) must
lie inside the plugin's memory; a length beyond it is a native error, not a write or read past the array.

## Jobs

The worker threads (`sm_archive_threads`, 1 to 8, default 2) take the queued jobs:

- High priority jobs start before normal ones; a normal job that has waited 30 seconds counts as high.
- Among those, jobs start in the order they were queued, by all plugins together. They end in any order, and
  callbacks run in the order the jobs ended.
- Jobs queued with `serial = true` form the plugin's lane: they run one after the other in their order, whatever their
  priority, so a plugin can chain jobs without nesting callbacks.

Callbacks run on the main thread on a later server frame, never inside the call that queued the job. The progress
callback is called at most four times a second, only while something changed.

`Compression.Cancel(job)` drops a queued job or stops a running one at its next chunk; the callback runs with
`ArchiveError_Cancelled` unless the job finished meanwhile. It returns false for an id that is no job of the calling
plugin. When a plugin unloads, its jobs are cancelled without callbacks. When the extension unloads, every job is
stopped and dropped.

## Outputs

A file is written as `<outFile>.<job>.smarchive.tmp` next to it and renamed onto `outFile` when the job succeeded;
without `overwrite`, an `outFile` that appeared meanwhile is refused (`ArchiveError_OutputExists`) and kept. An
extraction writes into `<targetDir>.<job>.smarchive.tmp/` and renames it onto `targetDir`, which must not exist. On any
failure the temp file or staging directory is deleted, and the output does not exist or is unchanged.

A temp file or staging directory is always created anew and never written through a symlink. A running job locks its
temp file or staging directory (`flock`), and a job first deletes every `<outFile>.<n>.smarchive.tmp` and
`<targetDir>.<n>.smarchive.tmp`, file or directory with everything in it, that no running job holds, of this server
or of another one in the same game directory: what an interrupted job (a crash, a server stopped meanwhile) left
behind is gone with the next job that writes the same output. Another job's sweep may delete a fresh temp file or
staging directory before its job locks it; it is then created again, up to three times in all. When a running job
holds the job's own temp name, the job fails. A shim's job writes SMbz2's and SMbrotli's `<outFile>.tmp` instead: a file or symlink there is deleted
first, a directory there is left alone and fails the job with `ArchiveError_OpenOutput`.

A job checks in this order: an existing output (unless `overwrite`), the output's directory (created if
`createPath`, even when the job fails later), the input.

## Extraction

Entries are read in archive order, and each is checked before it is written; since an extraction writes into its
staging directory, a failing entry leaves nothing behind. `List` checks every entry the same way, `ReadEntry` the
entries up to the one it reads:

| Entry | What happens |
|---|---|
| A name that is not UTF-8 (e.g. an old zip with CP437 names) | The job fails with `ArchiveError_EntryName`. |
| An absolute path or a `..` part | The job fails with `ArchiveError_UnsafeEntry`. |
| The same path twice (after normalizing) | The job fails with `ArchiveError_UnsafeEntry`. |
| An encrypted entry or archive | The job fails with `ArchiveError_Encrypted`. |
| More entries than `MaxEntries` | The job fails with `ArchiveError_LimitEntries`. |
| `\` in a name | Separates parts, as in Windows-made zips: `addons\x.smx` is `addons/x.smx`. |
| `./` prefixes, empty parts, a trailing `/` | Dropped: `./a//b/` is `a/b`; `./` alone is the root. |

Then, while extracting:

| Entry | What happens |
|---|---|
| File | Created 0664, or 0775 when the archive marks it executable, less the umask; the archive's mtime. |
| Directory | Created 0775 less the umask; its mtime is set at the end. |
| Symlink | Created when its target is relative and, resolved from the link's directory as the kernel would, stays inside the target; else skipped (`ArchiveSkip_LinkOutside`). After the last entry every created symlink is resolved again with all links: one that a later link made lead out of the target or loop fails the job with `ArchiveError_UnsafeEntry`. |
| Hardlink | Created to a file this job extracted; else skipped (`ArchiveSkip_LinkTargetMissing`). |
| Device, FIFO, socket | Skipped (`ArchiveSkip_SpecialFile`). |
| A path that leads through a created link onto an existing file or link | The job fails with `ArchiveError_UnsafeEntry`. |

Skipped entries are reported in the result with their reason. `prefixes`, an `ArchivePrefixes` handle, selects the
entries at or below any of its paths (`addons` takes `addons` and `addons/...`, not `addons2`); only those are
written and reported, but every entry is still checked.

Listing writes nothing, so it cannot see link collisions; extraction does. Of the entries' data, listing, reading an
entry and an extraction with `prefixes` still decode what a tar through a codec holds before the entry they need,
and those decoded bytes count against `MaxSize` and `MaxRatio` as well, so a compressed tar's bomb fails them too.
Plain tars and zips skip the data without decoding it; 7z and RAR decode solid blocks inside libarchive, which no
limit sees, but cancellation stops them.

## Limits

| Limit | Default | Applies to |
|---|---|---|
| `MaxSize` | 1 GiB | Bytes written by decompression, extraction and ReadEntry; for a tar through a codec also every byte the codec decodes (the tar stream, skipped entries included). |
| `MaxEntries` | 10000 | Entries of an archive, every kind counted. |
| `MaxRatio` | 1000 | Bytes written per byte read, overall and per entry, judged once more than 16 MiB were written; for a tar through a codec also the bytes decoded per byte read. |
| `MaxMemory` | 64 MiB | Output kept in memory (memory jobs, ReadEntry). |

0 means no limit. The defaults are the `sm_archive_max_*` cvars; `new ArchiveLimits()` starts from them, and a
plugin may set any value. A job takes its limits when it is queued. Deflate cannot pack more than about 1032:1, so a
pure deflate bomb is mostly caught by `MaxSize`; bzip2, xz and zstd bombs pass 1000:1 quickly.

## Results

The callback gets the error and an `ArchiveResult`. The result is closed after the callback; `Clone()` gives a copy
the plugin keeps and deletes.

- `Error`, `LibraryCode` (the failing library's own code, e.g. bzip2's `BZ_DATA_ERROR_MAGIC` -5 or a brotli decoder
  code), `GetMessage` (English, for logs), `Job`, `Codec` and `Format` (what was read or written).
- Entries (`EntryCount`, `GetEntry*`): path, type, size, packed size, mtime, the executable bit, link target, whether
  it was written and why not. The packed size is the archive bytes read while a file was extracted (tar and zip;
  0 for small entries libarchive read ahead), -1 when it is not known: other entry types, `List`, `ReadEntry`,
  `Create`, 7z and RAR.
- Bytes (`Size`, `GetString`, `GetBytes`, `GetBase64`): memory output and ReadEntry.

A failed job reports no entries and no bytes.

Sizes beyond 2 GiB saturate at 2147483647 in the `int` getters; the `...64` variants fill an `int[2]` (low, high).

## Errors

One `ArchiveError` serves every format. The values a codec job can end with: `OpenInput`, `OpenOutput`,
`OutputExists`, `CreatePath`, `Read`, `Write`, `Rename`, `Memory`, `Corrupt` (also data of another format than the
one named), `UnexpectedEOF` (also an empty input), `TrailingData`, `UnknownFormat` (Auto), `Unsupported`, the limits
and `Cancelled`. Archive jobs add `Encrypted`, `EntryName`, `UnsafeEntry`, `LimitEntries` and `EntryNotFound`;
`ReadEntry` ends with `Unsupported` for an entry that is no regular file.
`Archive.Create` fails with `EntryName` when a source directory holds a name that is not UTF-8 or contains `\`,
which would not come back as it went in. `Internal` is a bug; please report it with the message.
