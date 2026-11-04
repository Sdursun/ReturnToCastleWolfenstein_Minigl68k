#!/bin/sh
# Builds the AmigaOS 3.x single player executable in the sacredbanana Docker
# image (bebbo's m68k-amigaos-gcc). Run from anywhere; extra arguments go
# to make, e.g. "./build.sh clean" or "./build.sh -j8".
#
# MSYS_NO_PATHCONV stops Git Bash on Windows from rewriting the paths.

IMAGE=${IMAGE:-sacredbanana/amiga-compiler:m68k-amigaos}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

MSYS_NO_PATHCONV=1 exec docker run --rm -v "$ROOT:/src" -w /src/sp/amigaos "$IMAGE" make "$@"
