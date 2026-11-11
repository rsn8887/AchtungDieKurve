#!/bin/sh
# Runs inside the Docker image (see Dockerfile): builds build-android/AchtungDieKurve-<version>.apk next to the project.
set -e
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"       # the project folder (mounted below /work)
OUT="$(dirname "$PROJECT")/build-android"
mkdir -p "$OUT"
export GRADLE_USER_HOME="$OUT/gradle-home"       # downloaded Gradle plugins, kept between builds

# The APK is signed with this key. It is kept in build-android/ so that new versions install over old ones;
# don't delete it (a different key means uninstalling the old version first).
KEY="$OUT/achtung.keystore"
if [ ! -f "$KEY" ]; then
  keytool -genkeypair -keystore "$KEY" -storepass achtung -keypass achtung -alias achtung \
          -keyalg RSA -keysize 2048 -validity 20000 -dname "CN=rsn8887" > /dev/null
fi

cd "$PROJECT/android"
gradle --no-daemon --console=plain --project-cache-dir "$OUT/.gradle" -Pkeystore="$KEY" assembleRelease

# build-android/AchtungDieKurve-<version>.apk (the version from CMakeLists.txt)
VERSION=$(sed -n 's/^project(AchtungDieKurve VERSION \([0-9.]*\).*/\1/p' "$PROJECT/CMakeLists.txt")
cp "$OUT/app/outputs/apk/release/app-release.apk" "$OUT/AchtungDieKurve-$VERSION.apk"
echo "done: build-android/AchtungDieKurve-$VERSION.apk"
