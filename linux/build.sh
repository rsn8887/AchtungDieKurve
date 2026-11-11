#!/bin/sh
# Runs inside the Docker image (see Dockerfile): builds the game and packs it into
# build-linux-<arch>/AchtungDieKurve-<version>-linux-<arch>.tar.gz next to the project (arch: x86_64 or aarch64).
# SDL2, SDL2_ttf and the C++ runtime are linked in statically, so the archive runs without installing anything.
set -e
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"       # the project folder (mounted below /work)
ARCH="$(uname -m)"
OUT="$(dirname "$PROJECT")/build-linux-$ARCH"
VERSION=$(sed -n 's/^project(AchtungDieKurve VERSION \([0-9.]*\).*/\1/p' "$PROJECT/CMakeLists.txt")
NAME="AchtungDieKurve-$VERSION-linux-$ARCH"

cmake -S "$PROJECT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release \
      -DSDL2_SOURCE_DIR="$SDL2_SOURCE_DIR" -DSDL2_TTF_SOURCE_DIR="$SDL2_TTF_SOURCE_DIR"
cmake --build "$OUT" -j "$(nproc)"

rm -rf "$OUT/package"
mkdir -p "$OUT/package/$NAME"
cp "$OUT/achtung" "$OUT/font.ttf" "$OUT/FONT-LICENSE.txt" "$OUT/package/$NAME/"
strip "$OUT/package/$NAME/achtung"
tar -czf "$OUT/$NAME.tar.gz" -C "$OUT/package" "$NAME"
rm -rf "$OUT/package"
echo "done: build-linux-$ARCH/$NAME.tar.gz"
