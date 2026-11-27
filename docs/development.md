# Development

## Building

`.github/workflows/ci.yml` builds SMArchive on an Ubuntu 22.04 runner, and its `build` job is the reference for a
local build:

1. vcpkg at the commit `vcpkg/vcpkg.json` names as its `builtin-baseline` builds the libraries for both architectures
   with the triplets `x86-linux-smarchive` and `x64-linux-smarchive` into `libs/x86` and `libs/x64`, with vcpkg's
   packages cached.
2. AMBuild builds `smarchive.ext`, the shims and the unit tests with clang
   (`extension/configure.py --targets=x86,x86_64 --sm-path=<sourcemod> --libs-path=<libs>`); the tests run on the
   fixtures, the real tools check our output, and the binaries are checked to export only `GetSMExtAPI` and need at
   most glibc 2.35.
3. spcomp compiles the companion plugin.
4. It packs two archives laid out like a game folder, SMArchive and the shims.
5. sm-runner's setup and test actions (`davenonymous/sm-runner/setup@v0` and `test@v0`) run the SourcePawn tests
   against the packed extension and companion plugin, with both `smrunner` and `smrunner64`. A failing test fails the
   job, so no release is published.

It pins the SourceMod commit it builds against (`SOURCEMOD_COMMIT`, the one of the shared toolchain
image when it was set) and the SourceMod build whose spcomp compiles the plugin (`SOURCEMOD_DROP`); move both only on
purpose, like the vcpkg baseline. It takes the vcpkg commit from `vcpkg/vcpkg.json`. A separate job, `lint`, checks
the SourcePawn files in `scripting/` and `tests/` with pawncritic and `pawntidy --check` through sourcepawn-tools'
SourcePawn Lint action (`davenonymous/sourcepawn-tools@v0`), pawncritic at its default severity 3, and fails on any
violation or file pawntidy would change; run `pawncritic scripting tests` and `pawntidy --check scripting tests`
before pushing.

## Tests

The unit tests (`extension/tests/`) run the core without SourceMod: paths, detection, base64, every codec and option,
joined streams, broken and trailing data, the output rules, limits, cancellation and progress, every container created,
listed and extracted, hostile tars written with libarchive, the temp names taken from the running job's id, the pool's
scheduling and the job desk (ids, owners and their lanes, cancellation, unloading, progress pacing, the status lines)
with a fake `Pool::Runner`. Given a fixtures directory (`smarchive-tests-<arch> <dir>`) they also run on:

- `extension/tests/fixtures/hostile.py`: hostile zips and tars, each named after the error extracting (and, where it
  differs, listing) must end with;
- `extension/tests/fixtures/rar/`: RAR 4 and 5 files from libarchive's test suite;
- `extension/tests/fixtures/interop.py prepare`: what gzip, bzip2, xz, zstd, lz4, brotli, tar, zip and 7z wrote, and
  encrypted zips and 7z; the tests unpack them and pack the same into `ours/<bits>/`, which `interop.py verify`
  unpacks with the tools.

The SourcePawn tests (`tests/`) run under [sm-runner](https://github.com/davenonymous/sm-runner)'s smrunner, a
simulated server, against the built extension:

- `tests/test_smarchive.sp`: every native as a plugin calls it: the options and their ranges, every codec and
  container through files and memory, the output rules (no output and no temp file after a failure, `overwrite`,
  `createPath`), the checks of extraction on hand-made hostile tars, the limits, the jobs (callbacks on a later frame,
  ids, serial lanes, cancellation, progress) and the documented native errors.
- `tests/test_companion.sp`: the companion plugin's cvars, their config file, how their values reach the extension,
  and `sm_archive_status` and `sm_archive_versions` with their admin flag.

Compile them with `scripting/include` and smrunner's `share/include` on the include path, then run them against an
extension folder laid out like `addons/sourcemod/extensions` (the packed archive's):

```
smrunner test test_smarchive.smx --extensions <dist>/extensions
smrunner test test_companion.smx --extensions <dist>/extensions --plugin smarchive.smx
```

`smrunner` loads the x86 extension, `smrunner64` the x64 one.

## Layout

| Path | What |
|---|---|
| `extension/src/core/` | The core, without SourceMod: `status`, `control` (abort, progress, limits), `stream` (sources, sinks, the transform adapters), `codec` and `codecs/` (one file per codec), `detect`, `entry` (an entry as a job reports it), `paths`, `base64`, `archive_io` (libarchive), `extract`, `jobs`, `pool`, `jobdesk` (job ids, owners, lanes, cancellation on unload, progress pacing). |
| `extension/src/` | The SourceMod layer: `extension` (the job desk's receivers that call the plugins' callbacks, the shims' interface), `natives_*`. |
| `extension/src/api/smarchive_api.h` | The internal interface of the shims. |
| `extension/shims/` | `shim.*` and one directory per shim. |
| `extension/tests/` | The unit tests and fixtures. |
| `tests/` | The SourcePawn tests for smrunner. |
| `vcpkg/` | The manifest, the overlay port `zlib` (zlib-ng in its zlib-compatible mode) and the triplets. |
| `scripting/` | `smarchive.inc`, the companion plugin, the shims' includes. |

Adding a codec: a value in `Codec`, an options struct in `codec.h` with its validation and names in `codec.cpp`, a file
in `codecs/` with its `Transform`s (declared in `codecs/codecs.h`), its magic and file names in `detect.cpp`, its
`CodecFormat` value, methodmap and natives in `smarchive.inc` and `natives_options.cpp`, and its round trips in the
tests. Files, memory, limits, progress and tar on top come with it.

## Updating the libraries

Move `builtin-baseline` in `vcpkg/vcpkg.json` (CI reads it from there) to a newer vcpkg commit, copy
that commit's `ports/zlib-ng/portfile.cmake` and version into `vcpkg/ports/zlib/` (keeping `set(ZLIB_COMPAT ON)`),
build and run all tests.

## Releasing

Set `SMARCHIVE_VERSION_STRING` in `extension/src/version.h` and the companion plugin's version, commit, and push a tag
`v<version>` to `etf2l/sm-archive`; the workflow checks the tag against `version.h` and publishes
`sm-archive-v<version>-linux.tar.gz` and `sm-archive-shims-v<version>-linux.tar.gz` (builds without a tag name their
archives after the commit instead).
