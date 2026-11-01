#!/usr/bin/env bash
# scripts/test-native.sh - run `pio test -e native` reproducibly on Linux/macOS.
#
# If a host g++ is on PATH, this just runs `pio test -e native` directly
# (fastest path, no Docker involved). Otherwise it builds
# docker/Dockerfile.native-test once (tagged moneylaundring-native-test)
# and runs the tests in a throwaway container:
#   - the repo is bind-mounted read/write at /workspace
#   - PLATFORMIO_BUILD_DIR is set to a path INSIDE the container
#     (/tmp/pio-native-build), not under the mounted repo, so the
#     container's build artifacts never touch the host's .pio/build - this
#     is the isolation mechanism Module 3 identified as missing (its ad
#     hoc container shared .pio/build with the host and once corrupted
#     the native build's SCons cache). This was verified working during
#     Module 4 by inspecting the container's /tmp and the host's
#     .pio/build after a run: only /tmp/pio-native-build inside the
#     container was touched.
#   - PLATFORMIO_CORE_DIR (PlatformIO's package/platform cache) is backed
#     by a named Docker volume (moneylaundring-pio-core), so platforms and
#     tools are downloaded once and reused by every later throwaway
#     container, instead of being re-fetched on every run.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if command -v g++ >/dev/null 2>&1; then
    echo "[test-native] g++ found on PATH, running pio test -e native directly."
    exec pio test -e native
fi

echo "[test-native] No g++ on PATH. Using Docker (docker/Dockerfile.native-test)."

if ! command -v docker >/dev/null 2>&1; then
    echo "[test-native] ERROR: neither g++ nor docker found on PATH. Install one." >&2
    exit 1
fi

IMAGE="moneylaundring-native-test:latest"
VOLUME="moneylaundring-pio-core"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "[test-native] Building $IMAGE from docker/Dockerfile.native-test ..."
    docker build -f "$REPO_ROOT/docker/Dockerfile.native-test" -t "$IMAGE" "$REPO_ROOT"
fi

if ! docker volume inspect "$VOLUME" >/dev/null 2>&1; then
    echo "[test-native] Creating named volume $VOLUME for PlatformIO core/packages ..."
    docker volume create "$VOLUME" >/dev/null
fi

echo "[test-native] Running pio test -e native in a throwaway container ..."
# MSYS_NO_PATHCONV=1 stops Git Bash/MSYS on Windows from mangling the
# container-side paths below (e.g. -w /workspace, the volume target
# /root/.platformio) into bogus host paths before docker ever sees them.
MSYS_NO_PATHCONV=1 docker run --rm \
    -v "$VOLUME:/root/.platformio" \
    -v "$REPO_ROOT:/workspace" \
    -w /workspace \
    -e PLATFORMIO_BUILD_DIR=/tmp/pio-native-build \
    "$IMAGE" \
    pio test -e native
