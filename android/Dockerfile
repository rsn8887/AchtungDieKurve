# Build environment for the Android version of Achtung, die Kurve!: JDK 17, the Android SDK and NDK, Gradle,
# and the SDL2 / SDL2_ttf sources. See README.md (Android) for how to use it.
# The Android build tools only exist for x86-64 Linux: on Apple Silicon, Docker runs this image emulated.
FROM eclipse-temurin:17-jdk-jammy

RUN apt-get update \
 && apt-get install -y --no-install-recommends unzip git ca-certificates curl \
 && rm -rf /var/lib/apt/lists/*

ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=/opt/android-sdk
RUN mkdir -p $ANDROID_HOME/cmdline-tools \
 && curl -fsSL https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip -o /tmp/tools.zip \
 && unzip -q /tmp/tools.zip -d $ANDROID_HOME/cmdline-tools \
 && mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest \
 && rm /tmp/tools.zip
ENV PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools
RUN yes | sdkmanager --licenses > /dev/null \
 && sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;27.2.12479018" "cmake;3.22.1" > /dev/null

ARG GRADLE_VERSION=8.7
RUN curl -fsSL https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip -o /tmp/gradle.zip \
 && unzip -q /tmp/gradle.zip -d /opt \
 && ln -s /opt/gradle-${GRADLE_VERSION}/bin/gradle /usr/local/bin/gradle \
 && rm /tmp/gradle.zip

# SDL2 and SDL2_ttf (with its bundled FreeType) are compiled from source together with the game
RUN git clone -q --depth 1 -b release-2.32.10 https://github.com/libsdl-org/SDL.git /opt/SDL2 \
 && git clone -q --depth 1 --recurse-submodules --shallow-submodules -b release-2.24.0 https://github.com/libsdl-org/SDL_ttf.git /opt/SDL2_ttf
ENV SDL2_SOURCE_DIR=/opt/SDL2
ENV SDL2_TTF_SOURCE_DIR=/opt/SDL2_ttf

# the folder that contains the project folder (AchtungDieKurve/) is mounted at /work; the APK lands in /work/build-android
WORKDIR /work
# PROJECT_DIR is the name of the project folder (override it with docker run -e PROJECT_DIR=...)
ENV PROJECT_DIR=AchtungDieKurve
CMD ["sh", "-c", "exec sh /work/$PROJECT_DIR/android/build.sh"]
