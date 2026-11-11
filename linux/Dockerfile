# Build environment for the Linux versions (x86-64 and arm64) of Achtung, die Kurve!: a compiler, CMake, and the
# SDL2 and SDL2_ttf development packages. See README.md (Linux) for how to use it.
# The image is built for whichever platform Docker is asked for (--platform linux/amd64 or linux/arm64).
# Ubuntu 22.04 (glibc 2.35) is old enough for current desktop distributions, Steam Deck and Raspberry Pi OS (Bookworm).
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential cmake pkg-config ca-certificates \
      libsdl2-dev libsdl2-ttf-dev \
 && rm -rf /var/lib/apt/lists/*

# the folder that contains the project folder (AchtungDieKurve/) is mounted at /work; the result lands in /work/build-linux-<arch>
WORKDIR /work
# PROJECT_DIR is the name of the project folder (override it with docker run -e PROJECT_DIR=...)
ENV PROJECT_DIR=AchtungDieKurve
CMD ["sh", "-c", "exec sh /work/$PROJECT_DIR/linux/build.sh"]
