# Development

## Building

`./build.sh` builds everything in Docker and writes `dist/` (SMArchive) and `dist-shims/` (the shims), each laid out
like `addons/sourcemod/`. It needs the repository's shared images `tf2-dev/ext-toolchain` and `tf2-dev/server`
(`docker compose -f server/compose.yaml build` in the repository root) and copies the libraries' headers to
`extension/.libs/include/` for clangd. The stages of `server/Dockerfile`:

1. `libraries`: vcpkg at `VCPKG_COMMIT`, the commit `vcpkg/vcpkg.json` names as its `builtin-baseline`, builds the
   libraries for both architectures into `/build/libs/x86` and `/build/libs/x64`.
2. `extension-build`: AMBuild builds `smarchive.ext`, the shims and the unit tests; the tests run on the fixtures,
   the real tools check our output, and the binaries are checked to export only `GetSMExtAPI` and need at most glibc
   2.35.
3. `plugin-build`: spcomp compiles the companion plugin and the test plugins.
4. `dist`, `dist-shims`: the installed files.
5. The last, unnamed stage: the test server, `tf2-dev/server` with SMArchive, the shims and the test plugins.

`.github/workflows/ci.yml` runs the same steps on an Ubuntu 22.04 runner, with vcpkg's packages cached, and packs the
release archives. It pins the SourceMod commit it builds against (`SOURCEMOD_COMMIT`, the one of the shared toolchain
image when it was set) and the SourceMod build whose spcomp compiles the plugin (`SOURCEMOD_DROP`); move both only on
purpose, like the vcpkg baseline. It takes the vcpkg commit from `vcpkg/vcpkg.json`. A separate job, `lint`, checks
the SourcePawn files in `scripting/` with pawncritic through sourcepawn-tools' SourcePawn Lint action
(`davenonymous/sourcepawn-tools@v0`) at its default severity 3 and fails on any violation; run
`pawncritic scripting` before pushing.

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

The end-to-end test runs on the test server, started from the project root with
`docker compose -f server/compose.yaml up --build tf2` (32-bit) or
`docker compose -f server/compose.yaml --profile x64 up --build tf2-x64` (64-bit; only one of the two runs at a
time), then `python3 server/e2e/e2e.py [--port 27015] [--password tf2dev]`. It writes its files into `server/artest/`
(`tf/artest/` on the server), drives the test plugins over RCON and checks the outputs on the host.

## Layout

| Path | What |
|---|---|
| `extension/src/core/` | The core, without SourceMod: `status`, `control` (abort, progress, limits), `stream` (sources, sinks, the transform adapters), `codec` and `codecs/` (one file per codec), `detect`, `entry` (an entry as a job reports it), `paths`, `base64`, `archive_io` (libarchive), `extract`, `jobs`, `pool`, `jobdesk` (job ids, owners, lanes, cancellation on unload, progress pacing). |
| `extension/src/` | The SourceMod layer: `extension` (the job desk's receivers that call the plugins' callbacks, the shims' interface), `natives_*`. |
| `extension/src/api/smarchive_api.h` | The internal interface of the shims. |
| `extension/shims/` | `shim.*` and one directory per shim. |
| `extension/tests/` | The unit tests and fixtures. |
| `vcpkg/` | The manifest, the overlay port `zlib` (zlib-ng in its zlib-compatible mode) and the triplets. |
| `scripting/` | `smarchive.inc`, the companion plugin, the shims' includes. |
| `server/` | Dockerfile, compose file, test plugins (`test-plugin/`), end-to-end test (`e2e/`) and its files (`artest/`). |

Adding a codec: a value in `Codec`, an options struct in `codec.h` with its validation and names in `codec.cpp`, a file
in `codecs/` with its `Transform`s (declared in `codecs/codecs.h`), its magic and file names in `detect.cpp`, its
`CodecFormat` value, methodmap and natives in `smarchive.inc` and `natives_options.cpp`, and its round trips in the
tests. Files, memory, limits, progress and tar on top come with it.

## Updating the libraries

Move `builtin-baseline` in `vcpkg/vcpkg.json` and `VCPKG_COMMIT` in `server/Dockerfile` to a newer vcpkg commit, copy
that commit's `ports/zlib-ng/portfile.cmake` and version into `vcpkg/ports/zlib/` (keeping `set(ZLIB_COMPAT ON)`),
build and run all tests.

## Releasing

Set `SMARCHIVE_VERSION_STRING` in `extension/src/version.h` and the companion plugin's version, commit, and push a tag
`v<version>` to `etf2l/sm-archive`; the workflow checks the tag against `version.h` and publishes
`sm-archive-v<version>-linux.tar.gz` and `sm-archive-shims-v<version>-linux.tar.gz` (builds without a tag name their
archives after the commit instead).
