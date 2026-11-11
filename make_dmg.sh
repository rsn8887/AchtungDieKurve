#!/bin/bash
# Builds Achtung, die Kurve! for macOS and packs it into a .dmg (drag the app to Applications).
#
#   ./make_dmg.sh [build folder]      default: build-macos next to the project folder
#
# Needs Homebrew's sdl2, sdl3, sdl2_ttf and cmake (brew install sdl2 sdl3 sdl2_ttf cmake).
# The SDL libraries (and everything they link to) are copied into the app, so it runs on Macs without Homebrew. The app is signed ad hoc (no developer ID): the first
# time, open it with right-click > Open, or allow it under System Settings > Privacy & Security.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="${1:-$ROOT/../build-macos}"
mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"
VERSION="$(sed -n 's/^project(AchtungDieKurve VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
APP="$BUILD/AchtungDieKurve.app"
DMG="$BUILD/AchtungDieKurve-$VERSION-macOS-$(uname -m).dmg"

echo "== building in $BUILD"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --target achtung -j "$(sysctl -n hw.ncpu)"

echo "== copying libraries into the app"
FW="$APP/Contents/Frameworks"
EXE="$APP/Contents/MacOS/AchtungDieKurve"
rm -rf "$FW"
mkdir -p "$FW"

# library references of a binary that are not part of macOS (Homebrew and other local installs)
deps() {
  otool -L "$1" | tail -n +2 | awk '{print $1}' | grep -v -E '^(/usr/lib/|/System/)' || true
}
# turn a reference (absolute, @rpath, @loader_path) into a real file path; $2 = the binary that refers to it
resolve() {
  local ref="$1" from="$2" dir name
  dir="$(dirname "$from")"
  case "$ref" in
    @loader_path/*|@executable_path/*) echo "$dir/${ref#*/}" ;;
    @rpath/*)
      name="${ref#@rpath/}"
      for d in "$dir" "${HOMEBREW_PREFIX:-/opt/homebrew}/lib" /usr/local/lib; do
        if [ -e "$d/$name" ]; then echo "$d/$name"; return; fi
      done
      echo "$ref" ;;
    *) echo "$ref" ;;
  esac
}

# copy every library the game needs (recursively) and point all references at the copies
queue=("$EXE")
orig=("$EXE")          # where each queued binary came from, to resolve its relative references
i=0
while [ $i -lt ${#queue[@]} ]; do
  bin="${queue[$i]}"; src="${orig[$i]}"
  i=$((i + 1))
  for ref in $(deps "$bin"); do
    case "$ref" in @executable_path/../Frameworks/*) continue ;; esac   # already bundled
    path="$(resolve "$ref" "$src")"
    name="$(basename "$path")"
    if [ ! -e "$FW/$name" ]; then
      if [ ! -e "$path" ]; then echo "error: cannot find $ref (needed by $bin)"; exit 1; fi
      cp -L "$path" "$FW/$name"
      chmod u+w "$FW/$name"
      install_name_tool -id "@executable_path/../Frameworks/$name" "$FW/$name" 2>/dev/null
      queue+=("$FW/$name"); orig+=("$path")
      echo "   $name"
    fi
    install_name_tool -change "$ref" "@executable_path/../Frameworks/$name" "$bin" 2>/dev/null
  done
done

# Homebrew's "sdl2" is sdl2-compat: a thin layer that loads libSDL3 with dlopen() when the game starts.
# The loop above cannot see such libraries, so copy SDL3 in by hand (next to libSDL2 in Frameworks, where
# sdl2-compat looks for it, and next to the executable).
if strings "$FW"/libSDL2*.dylib | grep -q "libSDL3"; then
  SDL3_LIB=""
  for c in "${HOMEBREW_PREFIX:-/opt/homebrew}/lib/libSDL3.0.dylib" "$(brew --prefix sdl3 2>/dev/null)/lib/libSDL3.0.dylib" /usr/local/lib/libSDL3.0.dylib; do
    if [ -f "$c" ]; then SDL3_LIB="$c"; break; fi
  done
  if [ -z "$SDL3_LIB" ]; then echo "error: your SDL2 is sdl2-compat but libSDL3 was not found. Run: brew install sdl3"; exit 1; fi
  echo "== bundling SDL3 (needed by sdl2-compat): $SDL3_LIB"
  cp -L "$SDL3_LIB" "$FW/libSDL3.0.dylib"
  chmod u+w "$FW/libSDL3.0.dylib"
  install_name_tool -id "@executable_path/../Frameworks/libSDL3.0.dylib" "$FW/libSDL3.0.dylib" 2>/dev/null
  # libraries SDL3 itself links to (if any are not part of macOS)
  for ref in $(deps "$FW/libSDL3.0.dylib"); do
    path="$(resolve "$ref" "$SDL3_LIB")"; name="$(basename "$path")"
    [ -e "$FW/$name" ] || { cp -L "$path" "$FW/$name"; chmod u+w "$FW/$name"; install_name_tool -id "@executable_path/../Frameworks/$name" "$FW/$name" 2>/dev/null; }
    install_name_tool -change "$ref" "@executable_path/../Frameworks/$name" "$FW/libSDL3.0.dylib" 2>/dev/null
  done
  ln -sf libSDL3.0.dylib "$FW/libSDL3.dylib"
  cp -L "$FW/libSDL3.0.dylib" "$APP/Contents/MacOS/libSDL3.0.dylib"
fi

echo "== signing"
for lib in "$FW"/* "$APP"/Contents/MacOS/*.dylib; do [ -f "$lib" ] && [ ! -L "$lib" ] && codesign --force --sign - "$lib"; done
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo "== checking that nothing outside the app is referenced"
for bin in "$EXE" "$FW"/*; do
  if deps "$bin" | grep -v '^@executable_path/' | grep -q .; then
    echo "error: $bin still refers to:"; deps "$bin" | grep -v '^@executable_path/'; exit 1
  fi
done

echo "== making $DMG"
STAGE="$BUILD/dmg"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/Achtung die Kurve.app"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "Achtung die Kurve" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"
echo "done: $DMG"
