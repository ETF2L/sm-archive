# Migrating from SMbz2 and SMbrotli

## With the shims: plugins unchanged

`sm-archive-shims-v<version>-linux.tar.gz` holds `smbz2.ext.so` and `smbrotli.ext.so` for 32-bit and 64-bit servers.
Install SMArchive first, then copy the shims over the old files. Plugins compiled against `bzip2.inc` or `brotli.inc`
load unchanged: they depend on these file names, and the shims register the same natives (`BZ2.CompressFile`,
`BZ2.DecompressFile`, `BZ2.ErrorString`, `Brotli.CompressFile`, `Brotli.DecompressFile`, `Brotli.ErrorString`) with the
same arguments, checks, callbacks and error values.

What stays as it was:

- The jobs of one shim run one after the other in the order queued by all plugins together.
- The output is written as `<outFile>.tmp` and renamed onto `outFile` on success.
- bzip2 output is libbzip2 1.0.8's; brotli output is brotli 1.2.1's with the window sized as before.
- Every error of the old enums, brotli's decoder codes included.

What differs:

| | SMbz2 / SMbrotli | Shim |
|---|---|---|
| Plugin unloaded before the callback | The job ran to its end. | The job is cancelled. |
| Unpacking more than SMArchive's limits (`sm_archive_max_*`) | Not limited. | The job ends with `BZ2Error_Write` / `BrotliError_Write`. |
| Version | `1.0.8/1`, `1.2.1/1` | SMArchive's, e.g. `1.0.0`. |

The shims need `smarchive.ext`; SourceMod loads it for them. Unloading SMArchive unloads them.

## To the new API

| Old | New |
|---|---|
| `BZ2.CompressFile(in, out, cb, data, level, overwrite, createPath)` | `Bzip2Options o = new Bzip2Options(); o.Level = level; Compression.CompressFile(in, out, o, cb, data, overwrite, createPath); delete o;` |
| `Brotli.CompressFile(in, out, cb, data, quality, window, mode, ...)` | `BrotliOptions` with `Quality`, `Window`, `Mode` (`BrotliContent_*`), then `Compression.CompressFile` |
| `BZ2.DecompressFile` / `Brotli.DecompressFile` | `Compression.DecompressFile(in, out, cb, data, CodecFormat_Bzip2 or CodecFormat_Brotli, ...)` |
| `BZ2.ErrorString` / `Brotli.ErrorString` | `Compression.ErrorString` |
| Callback `(error, inFile, outFile, data)` | `(ArchiveError error, ArchiveResult result, any data)`; keep the paths in `data` if needed |
| `BZ2Error_Data`, `BZ2Error_DataMagic`, brotli's `BrotliError_Format*` | `ArchiveError_Corrupt` with `result.LibraryCode` (bzip2's -4 / -5, brotli's decoder code) |
| `BZ2Error_DataMagic` for data after the last stream, `BrotliError_TrailingData` | `ArchiveError_TrailingData` |
| `<outFile>.tmp` | `<outFile>.<job>.smarchive.tmp` |

Differences beyond the names: jobs run in parallel unless queued with `serial = true`; a plugin's jobs are
cancelled when it unloads; decompression is limited (`ArchiveLimits`).
