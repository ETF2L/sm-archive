# Formats

## Codecs

| Codec | Options (default) | Library | Told by |
|---|---|---|---|
| gzip | `Level` 1-9 (6) | zlib-ng (zlib-compatible) | `1f 8b` |
| bzip2 | `Level` 1-9 (9) | libbzip2 | `BZh1`-`BZh9` |
| brotli | `Quality` 0-11 (6), `Window` 0 or 10-24 (0), `Mode` (generic) | brotli | the name `.br` |
| xz | `Preset` 0-9 (6), `Extreme` (false), `Check` (CRC64) | liblzma | `fd 37 7a 58 5a 00` |
| zstd | `Level` 1-22 (3), `LongWindow` 0 or 10-27 (0), `Checksum` (true) | zstd | `28 b5 2f fd` |
| lz4 | `Level` 1-12 (1), `BlockSize` (4 MiB), `ContentChecksum` (true) | lz4 (frame format) | `04 22 4d 18` |

The defaults are the command line tools' (brotli's quality is SMbrotli's 6, not the tool's 11).

- gzip writes no file name and an mtime of 0, as `gzip -n`: the same input always gives the same bytes.
- brotli's window 0 is sized from the input as the brotli tool sizes it, so the output is the tool's at the same
  quality. Brotli has no magic: Auto tells it only by the name, and memory input needs `CodecFormat_Brotli`.
- zstd decompresses frames with windows up to 2^27 bytes, as the zstd tool does without `--memory`; a larger window
  fails with `ArchiveError_Unsupported`.
- xz decompresses streams that need up to 128 MiB of memory; every preset fits (-9e needs 65 MiB). A stream that
  needs more fails with `ArchiveError_Memory`.
- lz4 levels 3 and above use lz4's high compression mode.

Decompression takes files of several streams one after the other, where the format defines them: gzip members,
bzip2 streams (pbzip2, joined with `cat`), xz streams with their padding, zstd and lz4 frames. Anything else after
the stream fails with `ArchiveError_TrailingData`; brotli streams cannot be joined, so any byte after one does. An
empty input fails with `ArchiveError_UnexpectedEOF`.

## Containers

| Container | Read | Written | Told by |
|---|---|---|---|
| tar | ustar, pax, GNU and old v7 tars, plain or through any codec | pax (ustar headers, pax ones where an entry needs them), plain or through any codec | `ustar` at offset 257, or the name `.tar`, `.tar.<codec>`, `.tgz`, `.tbz`, `.tbz2`, `.tbr`, `.txz`, `.tzst`, `.tlz4` |
| zip | stored, deflate, bzip2, lzma, xz, zstd entries; zip64 | `Method` store, deflate (default), bzip2, lzma, xz, zstd; `Level` 0-9 (6) | `PK 03 04`, `PK 05 06`, `PK 07 08` |
| 7z | the methods libarchive reads | `Method` copy, deflate, bzip2, lzma1, lzma2 (default), ppmd; `Level` 0-9 (5) | `37 7a bc af 27 1c` |
| RAR | RAR 4 and 5, solid too | never (the format is proprietary) | `Rar! 1a 07 00` and `Rar! 1a 07 01 00` |

zip, 7z and RAR are read from files only, never through a codec, since reading them seeks. libarchive parses and writes
the containers; the extraction itself, its checks and its limits are SMArchive's, so the safety against hostile archives
does not depend on libarchive's flags and is tested in one place. libarchive's 7z writer keeps its compressed data in a
file in the system's temp directory while it writes.

Encrypted entries and archives (zip's ZipCrypto and AES, 7z's and RAR's encryption) are never decrypted; they fail
with `ArchiveError_Encrypted`. zip names without zip's UTF-8 flag are taken as they are: plain ASCII works, CP437
names with other characters fail with `ArchiveError_EntryName`.

## Auto

For decompression, `CodecFormat_Auto` tells the codec by its magic bytes and, for brotli, by the name. Memory input
has no name, so brotli in memory needs `CodecFormat_Brotli`.

For archives, `ArchiveFormat_Auto` tells zip, 7z and RAR by their magic bytes. Anything else is taken as a tar when its
first block is a tar header, when it is a codec stream whose decompressed start is a tar header, or when its name says
tar; otherwise the job fails with `ArchiveError_UnknownFormat`. `ArchiveFormat_Auto` takes only `CodecFormat_Auto`.
With `ArchiveFormat_Tar`, `CodecFormat_Auto` tells the codec by its magic bytes or the name, `CodecFormat_None` reads
a plain tar and a codec reads through that codec.
