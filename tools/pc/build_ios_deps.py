#!/usr/bin/env python3
"""Port/eame: build the pinned static libraries the iOS ARM64 game links.

Cross-compiles zlib, libpng, FreeType and SDL3 for the iphoneos SDK into
tmp/pc/ios-deps (macos_deps.py, platform "ios"), then checks that every
object in every archive was built for iOS at the declared baseline, so a
macOS object can never reach the iOS link unnoticed. Needs an ARM64 Mac with
Xcode's iOS SDK; no disc, no signing identity."""
import argparse
import re
import subprocess
import sys

from build_config import IOS_MINIMUM
from macos_deps import IOS_NAMES, ensure

LIBRARIES = ("libz.a", "libpng16.a", "libfreetype.a", "libSDL3.a")
# otool prints the platform of LC_BUILD_VERSION as a number or as its name.
IOS_PLATFORM = {"2", "IOS"}


def build_versions(archive):
    """(platform, minos) of every LC_BUILD_VERSION in the archive's objects."""
    text = subprocess.check_output(["xcrun", "otool", "-l", str(archive)], text=True)
    found = []
    for block in text.split("LC_BUILD_VERSION")[1:]:
        platform = re.search(r"^\s*platform\s+(\S+)", block, re.MULTILINE)
        minimum = re.search(r"^\s*minos\s+(\S+)", block, re.MULTILINE)
        if not platform or not minimum:
            raise SystemExit(f"{archive}: unreadable LC_BUILD_VERSION")
        found.append((platform.group(1), minimum.group(1)))
    return found


def check(install):
    baseline = tuple(map(int, IOS_MINIMUM.split(".")))
    failed = False
    for name in LIBRARIES:
        archive = install / "lib" / name
        if not archive.is_file():
            print(f"MISSING {archive}")
            failed = True
            continue
        versions = build_versions(archive)
        wrong = sorted({p for p, _ in versions if p not in IOS_PLATFORM})
        newer = sorted(
            {m for _, m in versions if tuple(map(int, m.split(".")))[:2] > baseline[:2]}
        )
        state = "ok"
        if not versions:
            state = "FAILED: no LC_BUILD_VERSION in any object"
        elif wrong:
            state = f"FAILED: objects for platform {', '.join(wrong)}"
        elif newer:
            state = f"FAILED: objects need iOS {', '.join(newer)}, baseline {IOS_MINIMUM}"
        failed |= state != "ok"
        print(
            f"{name}: {archive.stat().st_size} bytes, {len(versions)} objects, "
            f"minos {sorted({m for _, m in versions})}: {state}"
        )
    return failed


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--jobs", type=int, default=8)
    options = parser.parse_args()
    install = ensure(IOS_NAMES, jobs=options.jobs, platform_name="ios")
    print(f"iOS static dependencies: {install}")
    if check(install):
        sys.exit("iOS dependency check failed")
    print("iOS dependency check passed")
