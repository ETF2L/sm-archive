#!/bin/sh
# Copyright 2026 ETF2L Contributors, Dave
#
# Builds the libraries, the extension and the shims for x86 and x86_64 in
# Docker (the unit and interop tests run on the way) and writes dist/ and
# dist-shims/ anew, each laid out like addons/sourcemod/ on a game server:
#   rsync -a dist/ <server>:<game folder>/tf/addons/sourcemod/
# dist-shims/ replaces smbz2.ext and smbrotli.ext and needs dist/ installed.
# It also copies the libraries' headers to extension/.libs/include/ for
# clangd.
#
# Needs the shared images of the repository's server/ (tf2-dev/ext-toolchain
# and tf2-dev/server) and docker buildx.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
for image in tf2-dev/ext-toolchain tf2-dev/server; do
	if ! docker image inspect "${image}" >/dev/null 2>&1; then
		echo "the shared image ${image} is missing; build it: docker compose -f server/compose.yaml build (repository root)" >&2
		exit 1
	fi
done
rm -rf "${here}/dist" "${here}/dist-shims" "${here}/extension/.libs"
for target in dist dist-shims; do
	docker buildx build -f "${here}/server/Dockerfile" --target "${target}" \
		--output "type=local,dest=${here}/${target}" "${here}"
done
docker buildx build -f "${here}/server/Dockerfile" --target libraries --load -t tf2-smarchive:libraries "${here}"
container="$(docker create tf2-smarchive:libraries)"
mkdir -p "${here}/extension/.libs"
docker cp "${container}:/build/libs/x86/include" "${here}/extension/.libs/include" >/dev/null
docker rm "${container}" >/dev/null
echo
for target in dist dist-shims; do
	echo "${target}/ holds:"
	(cd "${here}/${target}" && find . -type f | sort | sed 's|^\./|  |')
done
