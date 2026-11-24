# SMArchive

SMArchive ("SourceMod compression and archives") is a SourceMod extension that compresses and decompresses files and
memory and lists, extracts and creates archives for plugins, in the background, without stalling the game server.
It is the successor of SMbz2 and SMbrotli, and ships shims that run their natives on it.

Typical uses: unpacking a plugin release (`.zip`, `.tar.gz`) into a staging directory and seeing its layout first,
keeping SourceTV demos and logs in less space, and preparing `.bz2` copies for the fast download.

## Features

- Codecs: gzip, bzip2, brotli, xz, zstd and lz4, on files and on strings or byte arrays in memory (with base64).
- Containers: tar (plain or through any codec: `.tar.gz`, `.tar.bz2`, `.tar.br`, `.tar.xz`, `.tar.zst`, `.tar.lz4`),
  zip and 7z are listed, extracted and created; RAR 4 and 5 are listed and extracted.
- Formats are told by their magic bytes (and by the name where a format has none), or named by the plugin.
- Typed options per format: levels, brotli's window and mode, xz's check, zstd's long window, zip's and 7z's methods.
- A pool of worker threads (`sm_archive_threads`, 2 by default); jobs start in the order queued, high priority first,
  and a plugin's serial jobs run one after the other. Progress callbacks and cancellation by job id.
- Outputs appear only on success, through a temp file or a staging directory renamed onto the output; what an
  interrupted job left behind goes with the next job for the same output.
- Safe against hostile archives: absolute paths, `..`, names that are not UTF-8, duplicate paths and encrypted entries
  fail the job; symlinks are created only when they stay inside the target, also in the finished tree, hardlinks
  only to extracted files, and devices, FIFOs and sockets never; limits on the unpacked size, the number of entries,
  the ratio and the memory stop archive bombs.
- One error enum for every format, plus the failing library's own code and a message for logs.
- Linux, 32-bit and 64-bit servers, every game SourceMod supports. libarchive, zlib-ng, bzip2, brotli, liblzma, zstd
  and lz4 are built in; nothing else needs to be installed.

## Requirements

- SourceMod 1.12 or newer, on Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12 and newer distributions).

## Installation

1. Download `sm-archive-v<version>-linux.tar.gz` from the [releases](https://github.com/etf2l/sm-archive/releases).
2. Extract it into the game directory, the directory that contains `addons/` (for Team Fortress 2 that is `tf/`):

   | File | Purpose |
   |---|---|
   | `addons/sourcemod/extensions/smarchive.ext.so` | The extension for 32-bit servers. |
   | `addons/sourcemod/extensions/x64/smarchive.ext.so` | The extension for 64-bit servers. |
   | `addons/sourcemod/plugins/smarchive.smx` | The companion plugin: the cvars and admin commands. |
   | `addons/sourcemod/scripting/include/smarchive.inc` | The include file for compiling plugins. |
   | `addons/sourcemod/scripting/smarchive.sp` | The companion plugin's source. |
   | `addons/sourcemod/licenses/smarchive/` | The license texts of SMArchive and the libraries. |

To check that it runs, enter `sm exts list` in the server console; it lists
`SourceMod compression and archives (1.0.0)`. `sm_archive_versions` names the libraries.

### Replacing SMbz2 and SMbrotli

`sm-archive-shims-v<version>-linux.tar.gz` holds `smbz2.ext.so` and `smbrotli.ext.so` (32-bit and 64-bit) and the old
includes. Installed over the old files, they keep plugins built for SMbz2 or SMbrotli working unchanged, with two
differences: a plugin's jobs are cancelled when it unloads, and SMArchive's limits apply. They need SMArchive
installed. See [migration](docs/migration.md).

## Settings

The companion plugin creates these cvars and writes them to `cfg/sourcemod/smarchive.cfg`. Without the plugin, the
defaults below apply:

| Cvar | Default | Meaning |
|---|---|---|
| `sm_archive_threads` | 2 | Worker threads, 1 to 8; changes apply at once. |
| `sm_archive_max_size` | 1024 | Default limit of the bytes one job unpacks, in MiB (0: none). |
| `sm_archive_max_entries` | 10000 | Default limit of an archive's entries (0: none). |
| `sm_archive_max_ratio` | 1000 | Default limit of bytes unpacked per byte read, once more than 16 MiB were unpacked (0: none). |
| `sm_archive_max_memory` | 64 | Default limit of output kept in memory, in MiB (0: none). |

Admins with the config flag have `sm_archive_status` (the queued and running jobs) and `sm_archive_versions`.

## Quick start

A plugin that unpacks a downloaded release and lists what it wrote:

```sourcepawn
#pragma semicolon 1
#pragma newdecls required

#include <sourcemod>
#include <smarchive>

public void OnPluginStart() {
	RegServerCmd("unpack_release", Command_Unpack);
}

public Action Command_Unpack(int args) {
	Archive.Extract("downloads/release.zip", "downloads/release", OnExtracted);
	return Plugin_Handled;
}

void OnExtracted(ArchiveError error, ArchiveResult result, any data) {
	if (error != ArchiveError_None) {
		char name[48], message[256];
		Compression.ErrorString(error, name, sizeof(name));
		result.GetMessage(message, sizeof(message));
		LogError("Cannot unpack the release: %s (%s)", name, message);
		return;
	}
	char path[PLATFORM_MAX_PATH];
	for (int i = 0; i < result.EntryCount; i++) {
		result.GetEntryPath(i, path, sizeof(path));
		PrintToServer("%s, %d bytes", path, result.GetEntrySize(i));
	}
}
```

`Archive.Extract` returns the job's id at once; the callback runs on a later server frame. Paths are relative to the
game directory, as for SourceMod's own file functions.

## Documentation

- [API reference](docs/api.md): jobs, paths, outputs, extraction rules, limits, results and errors.
- [Formats](docs/formats.md): every codec and container, its options and how it is told.
- [Examples](docs/examples.md): short examples of the common tasks.
- [Migration](docs/migration.md): from SMbz2 and SMbrotli, with the shims or to the new API.
- [Development](docs/development.md): building, the tests, the source layout, updating the libraries, releasing.
  CI also checks the SourcePawn code with [pawncritic](https://github.com/davenonymous/sourcepawn-tools).

## AI disclosure

AI helped build this project. If that bothers you, no hard feelings - this project is just not for you.

## License

SMArchive is licensed under the GNU General Public License version 3, with the same exceptions SourceMod grants; see
[LICENSE.txt](LICENSE.txt). The libraries keep their own licenses, which the release installs into
`addons/sourcemod/licenses/smarchive/`.
