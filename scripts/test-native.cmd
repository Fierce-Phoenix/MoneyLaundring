@echo off
setlocal enabledelayedexpansion
REM scripts\test-native.cmd - run `pio test -e native` reproducibly on Windows.
REM
REM If a host g++ is on PATH, this just runs `pio test -e native` directly
REM (fastest path, no Docker involved). Otherwise it builds
REM docker\Dockerfile.native-test once (tagged moneylaundring-native-test)
REM and runs the tests in a throwaway container:
REM   - the repo is bind-mounted read/write at /workspace
REM   - PLATFORMIO_BUILD_DIR is set to a path INSIDE the container
REM     (/tmp/pio-native-build), not under the mounted repo, so the
REM     container's build artifacts never touch the host's .pio\build -
REM     this is the isolation mechanism Module 3 identified as missing
REM     (its ad hoc container shared .pio\build with the host and once
REM     corrupted the native build's SCons cache). This was verified
REM     working during Module 4 by inspecting the container's /tmp and the
REM     host's .pio\build after a run: only /tmp/pio-native-build inside
REM     the container was touched.
REM   - PLATFORMIO_CORE_DIR (PlatformIO's package/platform cache) is backed
REM     by a named Docker volume (moneylaundring-pio-core), so platforms
REM     and tools are downloaded once and reused by every later throwaway
REM     container, instead of being re-fetched on every run.

where g++ >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [test-native] g++ found on PATH, running pio test -e native directly.
    pio test -e native
    exit /b %ERRORLEVEL%
)

echo [test-native] No g++ on PATH. Using Docker (docker\Dockerfile.native-test).

where docker >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [test-native] ERROR: neither g++ nor docker found on PATH. Install one.
    exit /b 1
)

set IMAGE=moneylaundring-native-test:latest
set VOLUME=moneylaundring-pio-core

docker image inspect %IMAGE% >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [test-native] Building %IMAGE% from docker\Dockerfile.native-test ...
    docker build -f docker\Dockerfile.native-test -t %IMAGE% . || exit /b 1
)

docker volume inspect %VOLUME% >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [test-native] Creating named volume %VOLUME% for PlatformIO core/packages ...
    docker volume create %VOLUME% >nul || exit /b 1
)

echo [test-native] Running pio test -e native in a throwaway container ...
docker run --rm ^
    -v %VOLUME%:/root/.platformio ^
    -v "%~dp0..:/workspace" ^
    -w /workspace ^
    -e PLATFORMIO_BUILD_DIR=/tmp/pio-native-build ^
    %IMAGE% ^
    pio test -e native

exit /b %ERRORLEVEL%
