# Build environment for the portable Linux versions (x86-64 and arm64) of Achtung, die Kurve!: a compiler, CMake,
# the headers SDL2 needs, and the SDL2 / SDL2_ttf sources. See README.md (Linux) for how to use it.
# The image is built for whichever platform Docker is asked for (--platform linux/amd64 or linux/arm64).
# Ubuntu 22.04 (glibc 2.35) is old enough for current desktop distributions, Steam Deck and Raspberry Pi OS (Bookworm).
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential cmake git ca-certificates pkg-config \
      libasound2-dev libpulse-dev libudev-dev libdbus-1-dev \
      libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev \
      libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
      libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
 && rm -rf /var/lib/apt/lists/*

# SDL2 and SDL2_ttf (with its bundled FreeType) are compiled from source and linked statically into the game. SDL still
# loads X11, Wayland, ALSA, PulseAudio, ... when the game starts, so it uses whatever the target system has.
RUN git clone -q --depth 1 -b release-2.32.10 https://github.com/libsdl-org/SDL.git /opt/SDL2 \
 && git clone -q --depth 1 --recurse-submodules --shallow-submodules -b release-2.24.0 https://github.com/libsdl-org/SDL_ttf.git /opt/SDL2_ttf
ENV SDL2_SOURCE_DIR=/opt/SDL2
ENV SDL2_TTF_SOURCE_DIR=/opt/SDL2_ttf

# the folder that contains the project folder (AchtungDieKurve/) is mounted at /work; the result lands in /work/build-linux-<arch>
WORKDIR /work
# PROJECT_DIR is the name of the project folder (override it with docker run -e PROJECT_DIR=...)
ENV PROJECT_DIR=AchtungDieKurve
CMD ["sh", "-c", "exec sh /work/$PROJECT_DIR/linux/build.sh"]
