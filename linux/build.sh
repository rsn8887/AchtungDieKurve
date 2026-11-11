#!/bin/sh
# Runs inside the Docker image (see Dockerfile): builds the game and packs it into
# build-linux-<arch>/AchtungDieKurve-<version>-linux-<arch>.tar.gz next to the project (arch: x86_64 or aarch64).
# SDL2 and SDL2_ttf are dynamic libraries: the target system needs libsdl2-2.0-0 and libsdl2-ttf-2.0-0 (or the
# equivalent packages), and a libstdc++ at least as new as the one in the build image.
set -e
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"       # the project folder (mounted below /work)
ARCH="$(uname -m)"
OUT="$(dirname "$PROJECT")/build-linux-$ARCH"
VERSION=$(sed -n 's/^project(AchtungDieKurve VERSION \([0-9.]*\).*/\1/p' "$PROJECT/CMakeLists.txt")
NAME="AchtungDieKurve-$VERSION-linux-$ARCH"

cmake -S "$PROJECT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release
cmake --build "$OUT" -j "$(nproc)"

rm -rf "$OUT/package"
mkdir -p "$OUT/package/$NAME"
cp "$OUT/achtung" "$OUT/font.ttf" "$OUT/FONT-LICENSE.txt" "$OUT/package/$NAME/"
strip "$OUT/package/$NAME/achtung"
# Optional: SDL2_ttf (not SDL2 itself) comes along, for systems that only have SDL2 installed. ./runme.sh starts the
# game with it (./achtung alone uses the system's copy).
cp -L "/lib/$ARCH-linux-gnu/libSDL2_ttf-2.0.so.0" "$OUT/package/$NAME/"
printf '#!/usr/bin/env bash\nLD_LIBRARY_PATH=./ ./achtung\n' >"$OUT/package/$NAME/runme.sh"   # (sh's echo has no -e)
chmod +x "$OUT/package/$NAME/runme.sh" "$OUT/package/$NAME/achtung"
tar -czf "$OUT/$NAME.tar.gz" -C "$OUT/package" "$NAME"
rm -rf "$OUT/package"
echo "done: build-linux-$ARCH/$NAME.tar.gz"
