#!/usr/bin/env bash
# Build scrcpy-cam on Linux.
#
#   ./build.sh          release build
#   ./build.sh debug    debug build
#
# Requires:
#   sudo apt install build-essential cmake pkg-config \
#                    libavcodec-dev libswscale-dev libavutil-dev libsdl3-dev
#
# At runtime, expose a virtual camera for OBS:
#   sudo modprobe v4l2loopback
set -euo pipefail

cd "$(dirname "$0")"

BUILD_TYPE="${1:-Release}"

if [[ ! -f vendor/scrcpy-server ]]; then
    echo "[!!] vendor/scrcpy-server is missing." >&2
    echo "     It is our modified scrcpy server build; see README.md." >&2
    exit 1
fi

echo "==> configuring (${BUILD_TYPE})"
cmake -S . -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ${CMAKE_ARGS:-}

echo "==> building"
cmake --build build -j "$(nproc 2>/dev/null || echo 4)"
cmake --build build --target vendor_server

echo
echo "Done: build/bin/scrcpy-cam"