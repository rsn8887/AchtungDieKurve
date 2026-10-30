#!/bin/bash
#---------------------------------------------------------------------------------
# Achtung, die Kurve!  -  builds a macOS .app bundle and a .dmg installer.
#
# Run this on a Mac, in the folder that contains achtung.cpp, icon.jpg and romfs/:
#       ./make_dmg.sh
# Result: Achtung-die-Kurve.dmg  (open it, drag the game onto "Applications")
#
# One-time setup (Homebrew, https://brew.sh):
#       xcode-select --install
#       brew install sdl2 sdl3 sdl2_ttf dylibbundler
#---------------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")"

APP_DIR="Achtung die Kurve.app"
EXE="achtung"
DMG="Achtung-die-Kurve.dmg"
VERSION="1.0.0"
MIN_MACOS="11.0"

for tool in clang++ sdl2-config dylibbundler sips iconutil hdiutil codesign; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "Missing tool: $tool"
        echo "Install the prerequisites:  xcode-select --install ; brew install sdl2 sdl3 sdl2_ttf dylibbundler"
        exit 1
    }
done
for f in achtung.cpp icon.jpg romfs/font.ttf; do
    [ -f "$f" ] || { echo "Missing file: $f (run this script in the game's folder)"; exit 1; }
done
PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"

rm -rf build "$APP_DIR" "$DMG" dmg_stage
mkdir -p build

echo "==> Compiling"
clang++ -std=c++11 -O2 -mmacosx-version-min="$MIN_MACOS" achtung.cpp -o "build/$EXE" \
    $(sdl2-config --cflags) -I"$PREFIX/include" -I"$PREFIX/include/SDL2" \
    $(sdl2-config --libs) -L"$PREFIX/lib" -lSDL2_ttf

echo "==> Creating $APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS" "$APP_DIR/Contents/Resources" "$APP_DIR/Contents/Frameworks"
cp "build/$EXE" "$APP_DIR/Contents/MacOS/$EXE"
cp romfs/font.ttf "$APP_DIR/Contents/Resources/font.ttf"        # found via SDL_GetBasePath()
[ -f romfs/FONT-LICENSE.txt ] && cp romfs/FONT-LICENSE.txt "$APP_DIR/Contents/Resources/"

echo "==> Icon"
ICONSET="build/AppIcon.iconset"
mkdir -p "$ICONSET"
for s in 16 32 64 128 256 512; do
    sips -s format png -z $s $s icon.jpg --out "$ICONSET/tmp_$s.png" >/dev/null
done
cp "$ICONSET/tmp_16.png"  "$ICONSET/icon_16x16.png"
cp "$ICONSET/tmp_32.png"  "$ICONSET/icon_16x16@2x.png"
cp "$ICONSET/tmp_32.png"  "$ICONSET/icon_32x32.png"
cp "$ICONSET/tmp_64.png"  "$ICONSET/icon_32x32@2x.png"
cp "$ICONSET/tmp_128.png" "$ICONSET/icon_128x128.png"
cp "$ICONSET/tmp_256.png" "$ICONSET/icon_128x128@2x.png"
cp "$ICONSET/tmp_256.png" "$ICONSET/icon_256x256.png"
cp "$ICONSET/tmp_512.png" "$ICONSET/icon_256x256@2x.png"
cp "$ICONSET/tmp_512.png" "$ICONSET/icon_512x512.png"
rm "$ICONSET"/tmp_*.png
iconutil -c icns "$ICONSET" -o "$APP_DIR/Contents/Resources/AppIcon.icns"

cat > "$APP_DIR/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>               <string>Achtung, die Kurve!</string>
    <key>CFBundleDisplayName</key>        <string>Achtung, die Kurve!</string>
    <key>CFBundleIdentifier</key>         <string>com.rsn8887.achtung</string>
    <key>CFBundleExecutable</key>         <string>$EXE</string>
    <key>CFBundleIconFile</key>           <string>AppIcon</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>$VERSION</string>
    <key>CFBundleVersion</key>            <string>$VERSION</string>
    <key>LSMinimumSystemVersion</key>     <string>$MIN_MACOS</string>
    <key>LSApplicationCategoryType</key>  <string>public.app-category.games</string>
    <key>NSHighResolutionCapable</key>    <true/>
    <key>NSHumanReadableCopyright</key>   <string>rsn8887</string>
</dict>
</plist>
PLIST

echo "==> Bundling SDL2 / SDL2_ttf and their libraries into the app"
dylibbundler -od -b -x "$APP_DIR/Contents/MacOS/$EXE" \
    -d "$APP_DIR/Contents/Frameworks" -p @executable_path/../Frameworks/ >/dev/null

# Homebrew's "sdl2" is sdl2-compat: a thin layer that loads libSDL3 with dlopen() when the
# game starts. dylibbundler cannot see such libraries, so copy SDL3 in by hand (next to
# libSDL2 in Frameworks, where sdl2-compat looks for it, and next to the executable).
SDL3_LIB=""
for c in "$PREFIX/lib/libSDL3.0.dylib" "$PREFIX/opt/sdl3/lib/libSDL3.0.dylib" /usr/local/lib/libSDL3.0.dylib; do
    [ -f "$c" ] && { SDL3_LIB="$c"; break; }
done
if [ -n "$SDL3_LIB" ]; then
    echo "==> Bundling SDL3 (needed by sdl2-compat): $SDL3_LIB"
    cp -L "$SDL3_LIB" "$APP_DIR/Contents/Frameworks/libSDL3.0.dylib"
    chmod u+w "$APP_DIR/Contents/Frameworks/libSDL3.0.dylib"
    install_name_tool -id @executable_path/../Frameworks/libSDL3.0.dylib "$APP_DIR/Contents/Frameworks/libSDL3.0.dylib"
    dylibbundler -of -b -x "$APP_DIR/Contents/Frameworks/libSDL3.0.dylib" \
        -d "$APP_DIR/Contents/Frameworks" -p @executable_path/../Frameworks/ >/dev/null
    ln -sf libSDL3.0.dylib "$APP_DIR/Contents/Frameworks/libSDL3.dylib"
    cp -L "$APP_DIR/Contents/Frameworks/libSDL3.0.dylib" "$APP_DIR/Contents/MacOS/libSDL3.0.dylib"
elif otool -L "$APP_DIR/Contents/Frameworks"/libSDL2*.dylib | grep -qi sdl3 || \
     strings "$APP_DIR/Contents/Frameworks"/libSDL2*.dylib | grep -q "libSDL3"; then
    echo "ERROR: your SDL2 is sdl2-compat but libSDL3 was not found. Run: brew install sdl3"
    exit 1
fi

echo "==> Signing (ad-hoc; required for Apple Silicon to run it)"
xattr -cr "$APP_DIR"
codesign --force --deep --sign - "$APP_DIR"
codesign --verify --deep --strict "$APP_DIR"

echo "==> Creating $DMG"
mkdir dmg_stage
cp -R "$APP_DIR" dmg_stage/
ln -s /Applications dmg_stage/Applications
hdiutil create -volname "Achtung die Kurve" -srcfolder dmg_stage -ov -format UDZO "$DMG" >/dev/null
rm -rf dmg_stage

echo
echo "Done: $DMG"
echo "Note: the app is not notarized. If you share the .dmg with others, macOS may block it at"
echo "first launch: right-click the app > Open (or run: xattr -dr com.apple.quarantine <app>)."
