#!/usr/bin/env python3
"""package_firmware.py -- build the merged flashable image for MoneyLaundring.

Reads the four component binaries produced by `pio run -e esp32dev`
(bootloader.bin, partitions.bin, firmware.bin) plus boot_app0.bin from the
installed framework-arduinoespressif32 package, and merges them into a
single flashable image using the esp32 arduino-esp32 build's exact offsets:

    0x1000   bootloader.bin
    0x8000   partitions.bin
    0xe000   boot_app0.bin
    0x10000  firmware.bin (the application)

These offsets, plus flash_mode=dio, flash_freq=40m, and flash_size=4MB, were
read directly out of PlatformIO's own build scripts for this exact board
(not assumed) -- see reports/firmware-release-report.md for the citations.

ASCII only. Standard library only (subprocess, hashlib, json, os, sys,
pathlib, datetime, platform). Calls the esptool PlatformIO already
installed (tool-esptoolpy) as a subprocess; installs nothing.

Run with the PlatformIO Python interpreter, e.g.:
    C:\\Python315\\python.exe scripts\\package_firmware.py
(PlatformIO's own python; see the README's "Building a flashable image"
section and the release report for why this is not literally
%USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe on this machine --
that path does not exist here, so the script locates the interpreter that
is actually running it and uses that one, which is intentional: it is
always correct to invoke this script with whatever Python PlatformIO
itself resolves to on the operator's machine.)

Refuses to run (non-zero exit, no files written) if:
  - the git working tree is dirty (git status --porcelain is non-empty), or
  - AUTO_RUN_ON_BOOT in include/config.h is not exactly `false`.
"""

import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / ".pio" / "build" / "esp32dev"
DIST_DIR = REPO_ROOT / "dist"
CONFIG_H = REPO_ROOT / "include" / "config.h"

# PlatformIO package directories (resolved relative to the user's home,
# matching how `pio` itself locates them -- PLATFORMIO_CORE_DIR is not
# overridden on this machine, so the default ~/.platformio applies).
PLATFORMIO_HOME = Path(os.path.expanduser("~")) / ".platformio"
FRAMEWORK_DIR = PLATFORMIO_HOME / "packages" / "framework-arduinoespressif32"
ESPTOOL_PACKAGE_DIR = PLATFORMIO_HOME / "packages" / "tool-esptoolpy"
ESPTOOL_SCRIPT = ESPTOOL_PACKAGE_DIR / "esptool.py"

BOOT_APP0_PATH = FRAMEWORK_DIR / "tools" / "partitions" / "boot_app0.bin"

# Exact offsets confirmed from PlatformIO's own build scripts for the
# esp32 Arduino framework target (see module docstring and the release
# report for the file/line citations).
OFFSET_BOOTLOADER = 0x1000
OFFSET_PARTITIONS = 0x8000
OFFSET_BOOT_APP0 = 0xE000
OFFSET_APPLICATION = 0x10000

FLASH_MODE = "dio"
FLASH_FREQ = "40m"
FLASH_SIZE = "4MB"
FLASH_SIZE_BYTES = 4 * 1024 * 1024

CHIP = "esp32"


def run_git(args):
    result = subprocess.run(
        ["git"] + args,
        cwd=str(REPO_ROOT),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    return result.returncode, result.stdout.strip(), result.stderr.strip()


def fail(message):
    sys.stderr.write("package_firmware.py: ERROR: " + message + "\n")
    sys.exit(1)


def check_git_clean():
    code, out, err = run_git(["status", "--porcelain"])
    if code != 0:
        fail("git status failed: " + err)
    if out.strip() != "":
        fail(
            "working tree is dirty; refusing to package. "
            "git status --porcelain output:\n" + out
        )


def get_commit_info():
    code, sha, err = run_git(["rev-parse", "HEAD"])
    if code != 0:
        fail("git rev-parse HEAD failed: " + err)
    code, short_sha, err = run_git(["rev-parse", "--short", "HEAD"])
    if code != 0:
        fail("git rev-parse --short HEAD failed: " + err)
    code, branch, err = run_git(["rev-parse", "--abbrev-ref", "HEAD"])
    if code != 0:
        fail("git rev-parse --abbrev-ref HEAD failed: " + err)
    return sha, short_sha, branch


def read_config_bool(name):
    """Read a `constexpr bool NAME = value;` line from include/config.h."""
    if not CONFIG_H.is_file():
        fail("include/config.h not found at " + str(CONFIG_H))
    text = CONFIG_H.read_text(encoding="utf-8")
    pattern = r"constexpr\s+bool\s+" + re.escape(name) + r"\s*=\s*(true|false)\s*;"
    match = re.search(pattern, text)
    if match is None:
        fail("could not find `constexpr bool " + name + " = ...;` in config.h")
    return match.group(1) == "true"


def read_config_uint32(name):
    if not CONFIG_H.is_file():
        fail("include/config.h not found at " + str(CONFIG_H))
    text = CONFIG_H.read_text(encoding="utf-8")
    pattern = (
        r"constexpr\s+(?:uint32_t|unsigned\s+long)\s+"
        + re.escape(name)
        + r"\s*=\s*(\d+)\s*;"
    )
    match = re.search(pattern, text)
    if match is None:
        fail("could not find `constexpr uint32_t|unsigned long " + name + " = ...;` in config.h")
    return int(match.group(1))


def check_auto_run_on_boot_false():
    value = read_config_bool("AUTO_RUN_ON_BOOT")
    if value is not False:
        fail(
            "AUTO_RUN_ON_BOOT is not false in include/config.h; refusing to "
            "package a build that could auto-run on boot."
        )


def resolve_build_toolchain_info():
    """Parse `pio pkg list -e esp32dev` output for the resolved platform,
    framework, and esptool package versions. Does not trust any hardcoded
    version string; reads what is actually installed right now."""
    result = subprocess.run(
        ["pio", "pkg", "list", "-e", "esp32dev"],
        cwd=str(REPO_ROOT),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    output = result.stdout + "\n" + result.stderr
    info = {
        "platform": None,
        "framework": None,
        "esptool": None,
        "toolchain": None,
    }
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("Platform "):
            info["platform"] = line
        elif "framework-arduinoespressif32" in line:
            info["framework"] = line.lstrip("|- \t")
        elif "tool-esptoolpy" in line:
            info["esptool"] = line.lstrip("|- \t")
        elif "toolchain-xtensa-esp32" in line:
            info["toolchain"] = line.lstrip("|- \t")
    return info


def check_inputs_exist():
    required = {
        "bootloader.bin": BUILD_DIR / "bootloader.bin",
        "partitions.bin": BUILD_DIR / "partitions.bin",
        "firmware.bin": BUILD_DIR / "firmware.bin",
    }
    for label, path in required.items():
        if not path.is_file():
            fail(
                label + " not found at " + str(path) + " -- run "
                "`pio run -e esp32dev` first."
            )
    if not BOOT_APP0_PATH.is_file():
        fail("boot_app0.bin not found at " + str(BOOT_APP0_PATH))
    if not ESPTOOL_SCRIPT.is_file():
        fail("esptool.py not found at " + str(ESPTOOL_SCRIPT))
    return required


def sha256_of_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def run_esptool_merge(python_exe, output_path, components):
    """components: list of (offset_int, path) in ascending offset order."""
    cmd = [
        python_exe,
        str(ESPTOOL_SCRIPT),
        "--chip", CHIP,
        "merge_bin",
        "--output", str(output_path),
        "--flash_mode", FLASH_MODE,
        "--flash_freq", FLASH_FREQ,
        "--flash_size", FLASH_SIZE,
    ]
    for offset, path in components:
        cmd.append("0x%X" % offset)
        cmd.append(str(path))

    result = subprocess.run(
        cmd,
        cwd=str(REPO_ROOT),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        fail(
            "esptool merge_bin failed (exit "
            + str(result.returncode)
            + "):\n"
            + result.stdout
        )
    return result.stdout, cmd


def main():
    print("package_firmware.py: checking gates...")
    check_git_clean()
    check_auto_run_on_boot_false()
    print("package_firmware.py: gates passed (clean tree, AUTO_RUN_ON_BOOT=false).")

    sha, short_sha, branch = get_commit_info()
    run_telemetry = read_config_bool("RUN_TELEMETRY")
    auto_run_delay_ms = read_config_uint32("AUTO_RUN_DELAY_MS")
    max_travel_ms = read_config_uint32("MAX_TRAVEL_MS")
    max_clear_ms = read_config_uint32("MAX_CLEAR_MS")

    inputs = check_inputs_exist()
    bootloader_path = inputs["bootloader.bin"]
    partitions_path = inputs["partitions.bin"]
    firmware_path = inputs["firmware.bin"]

    toolchain_info = resolve_build_toolchain_info()

    DIST_DIR.mkdir(parents=True, exist_ok=True)

    # Copy the four component images into dist/ alongside the merged image.
    component_dest = {
        "bootloader.bin": bootloader_path,
        "partitions.bin": partitions_path,
        "boot_app0.bin": BOOT_APP0_PATH,
        "firmware.bin": firmware_path,
    }
    copied_paths = {}
    for name, src in component_dest.items():
        dest = DIST_DIR / name
        dest.write_bytes(src.read_bytes())
        copied_paths[name] = dest

    merged_name = "moneylaundring-%s-merged.bin" % short_sha
    merged_path = DIST_DIR / merged_name

    python_exe = sys.executable

    components = [
        (OFFSET_BOOTLOADER, copied_paths["bootloader.bin"]),
        (OFFSET_PARTITIONS, copied_paths["partitions.bin"]),
        (OFFSET_BOOT_APP0, copied_paths["boot_app0.bin"]),
        (OFFSET_APPLICATION, copied_paths["firmware.bin"]),
    ]

    print("package_firmware.py: merging with esptool merge_bin...")
    esptool_output, merge_cmd = run_esptool_merge(python_exe, merged_path, components)
    print(esptool_output)

    if not merged_path.is_file():
        fail("merge_bin did not produce " + str(merged_path))

    # Partition table size (app0 partition) read from the partitions.csv
    # used by this build, for the "application free space" figure.
    partitions_csv = FRAMEWORK_DIR / "tools" / "partitions" / "default.csv"
    app0_size = None
    if partitions_csv.is_file():
        for line in partitions_csv.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line.startswith("#") or not line:
                continue
            fields = [f.strip() for f in line.split(",")]
            if len(fields) >= 5 and fields[0] == "app0":
                app0_size = int(fields[4], 16)
                break

    firmware_size = firmware_path.stat().st_size
    app0_free = None
    if app0_size is not None:
        app0_free = app0_size - firmware_size

    # Write SHA256SUMS over every file written to dist/ (component images
    # plus merged image), relative paths, sha256sum-compatible format.
    all_files = list(copied_paths.values()) + [merged_path]
    sha_lines = []
    for path in sorted(all_files, key=lambda p: p.name):
        digest = sha256_of_file(path)
        sha_lines.append("%s  %s" % (digest, path.name))
    sha_path = DIST_DIR / "SHA256SUMS"
    sha_path.write_text("\n".join(sha_lines) + "\n", encoding="utf-8")

    build_time = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    flash_cmd_str = (
        "python -m esptool --chip esp32 --port COMx --baud 460800 "
        "write_flash 0x0 " + merged_name
    )

    manifest_lines = []
    manifest_lines.append("# MoneyLaundring firmware image manifest")
    manifest_lines.append("")
    manifest_lines.append(
        "**This build has never run on real hardware.** It has only been "
        "compiled and structurally verified on a development machine; no "
        "physical ESP32 has executed this image."
    )
    manifest_lines.append("")
    manifest_lines.append(
        "**The pin map, sensor polarity, the two FORWARD-direction "
        "mapping constants, and the two safety timeouts (MAX_TRAVEL_MS, "
        "MAX_CLEAR_MS) are all provisional defaults**, not values "
        "confirmed against a real gantry. See docs/hardware-test.md "
        "before ever arming this firmware on real hardware."
    )
    manifest_lines.append("")
    manifest_lines.append(
        "**The board boots disarmed.** No motor can move until the "
        "operator explicitly types `arm`, and (if ever enabled) "
        "AUTO_RUN_ON_BOOT is `false` in this build -- confirmed below."
    )
    manifest_lines.append("")
    manifest_lines.append(
        "**Do not leave this gantry running unattended** at any point "
        "during bring-up or calibration."
    )
    manifest_lines.append("")
    manifest_lines.append("## Source")
    manifest_lines.append("")
    manifest_lines.append("- Commit: `%s`" % sha)
    manifest_lines.append("- Short SHA: `%s`" % short_sha)
    manifest_lines.append("- Branch: `%s`" % branch)
    manifest_lines.append("- Build time (UTC): `%s`" % build_time)
    manifest_lines.append("")
    manifest_lines.append("## Toolchain (resolved, not assumed)")
    manifest_lines.append("")
    manifest_lines.append("```")
    for key in ("platform", "framework", "esptool", "toolchain"):
        manifest_lines.append(str(toolchain_info.get(key)))
    manifest_lines.append("```")
    manifest_lines.append("")
    manifest_lines.append("## Flash layout and parameters")
    manifest_lines.append("")
    manifest_lines.append(
        "Offsets and parameters below were read directly from PlatformIO's "
        "own build scripts for this exact board target "
        "(platformio-build.py in framework-arduinoespressif32, and the "
        "esp32dev board JSON) -- see reports/firmware-release-report.md "
        "for the file/line citations."
    )
    manifest_lines.append("")
    manifest_lines.append("| Component | Offset |")
    manifest_lines.append("|---|---|")
    manifest_lines.append("| bootloader.bin | 0x%04X |" % OFFSET_BOOTLOADER)
    manifest_lines.append("| partitions.bin | 0x%04X |" % OFFSET_PARTITIONS)
    manifest_lines.append("| boot_app0.bin | 0x%04X |" % OFFSET_BOOT_APP0)
    manifest_lines.append("| firmware.bin (application) | 0x%04X |" % OFFSET_APPLICATION)
    manifest_lines.append("")
    manifest_lines.append("- flash_mode: `%s`" % FLASH_MODE)
    manifest_lines.append("- flash_freq: `%s`" % FLASH_FREQ)
    manifest_lines.append("- flash_size: `%s`" % FLASH_SIZE)
    manifest_lines.append("")
    manifest_lines.append("## File sizes")
    manifest_lines.append("")
    manifest_lines.append("| File | Size (bytes) |")
    manifest_lines.append("|---|---|")
    for path in sorted(all_files, key=lambda p: p.name):
        manifest_lines.append("| %s | %d |" % (path.name, path.stat().st_size))
    manifest_lines.append("")
    manifest_lines.append("## Application partition")
    manifest_lines.append("")
    if app0_size is not None:
        manifest_lines.append("- app0 partition size: %d bytes" % app0_size)
        manifest_lines.append("- firmware.bin size: %d bytes" % firmware_size)
        manifest_lines.append("- free space: %d bytes" % app0_free)
    else:
        manifest_lines.append(
            "- app0 partition size could not be read from "
            + str(partitions_csv)
        )
    manifest_lines.append("")
    manifest_lines.append("## Firmware configuration (include/config.h)")
    manifest_lines.append("")
    manifest_lines.append("- AUTO_RUN_ON_BOOT: `false`")
    manifest_lines.append("- RUN_TELEMETRY: `%s`" % ("true" if run_telemetry else "false"))
    manifest_lines.append("- AUTO_RUN_DELAY_MS: %d" % auto_run_delay_ms)
    manifest_lines.append("- MAX_TRAVEL_MS (provisional): %d" % max_travel_ms)
    manifest_lines.append("- MAX_CLEAR_MS (provisional): %d" % max_clear_ms)
    manifest_lines.append("")
    manifest_lines.append("## Flashing")
    manifest_lines.append("")
    manifest_lines.append("Flash the single merged image at offset 0x0:")
    manifest_lines.append("")
    manifest_lines.append("```")
    manifest_lines.append(flash_cmd_str)
    manifest_lines.append("```")
    manifest_lines.append("")
    manifest_lines.append(
        "(Replace COMx with the actual serial port. Baud 460800 matches "
        "this project's `upload_speed` in platformio.ini.)"
    )
    manifest_lines.append("")
    manifest_lines.append("## SHA256SUMS")
    manifest_lines.append("")
    manifest_lines.append("See SHA256SUMS in this same folder.")
    manifest_lines.append("")

    manifest_path = DIST_DIR / "MANIFEST.md"
    manifest_path.write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")

    print("package_firmware.py: wrote:")
    for path in sorted(all_files, key=lambda p: p.name):
        print("  " + str(path))
    print("  " + str(sha_path))
    print("  " + str(manifest_path))
    print("package_firmware.py: done.")


if __name__ == "__main__":
    main()
