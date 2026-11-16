#!/usr/bin/env python3
"""Port/eame: package the linked iOS game as an unsigned .ipa.

Writes Payload/YFM Re-Decomp.app (the executable, Info.plist, the language
packs and license notices) and zips it as an .ipa under tmp/pc-release/ios.
The package holds no disc data, no saves and no code mods, and it is NOT
signed: iOS runs it only after the player's own signing tool has signed it.
The disc image goes in the app's Documents/game folder through the Files
app (UIFileSharingEnabled)."""
import argparse
import hashlib
import json
import plistlib
import re
import shutil
import stat
import subprocess
import zipfile
from pathlib import Path

from build_config import IOS_MINIMUM, ROOT, TARGETS

APP_NAME = "YFM Re-Decomp"
EXECUTABLE = TARGETS["ios"]["executable"]
LANGUAGES = ("de", "en-eu", "es", "fr", "it")


def info_plist(version, build_number, bundle_id):
    landscape = [
        "UIInterfaceOrientationLandscapeLeft",
        "UIInterfaceOrientationLandscapeRight",
    ]
    return dict(
        CFBundleDevelopmentRegion="en",
        CFBundleName=APP_NAME,
        CFBundleDisplayName=APP_NAME,
        CFBundleIdentifier=bundle_id,
        CFBundleExecutable=EXECUTABLE,
        CFBundlePackageType="APPL",
        CFBundleInfoDictionaryVersion="6.0",
        CFBundleShortVersionString=version,
        CFBundleVersion=build_number,
        CFBundleSupportedPlatforms=["iPhoneOS"],
        LSRequiresIPhoneOS=True,
        MinimumOSVersion=IOS_MINIMUM,
        UIDeviceFamily=[1, 2],
        UIRequiredDeviceCapabilities=["arm64"],
        # The game is a 4:3 picture with the touch controls at its sides.
        UISupportedInterfaceOrientations=landscape,
        **{"UISupportedInterfaceOrientations~ipad": landscape},
        UIRequiresFullScreen=True,
        UIStatusBarHidden=True,
        UIViewControllerBasedStatusBarAppearance=False,
        # An empty launch screen makes the app use the whole display.
        UILaunchScreen={},
        # The scene life cycle iOS 27 requires; SDL 3.4's UIKit backend
        # supplies this delegate (SDL_uikitappdelegate.m).
        UIApplicationSceneManifest=dict(
            UIApplicationSupportsMultipleScenes=False,
            UISceneConfigurations=dict(
                UIWindowSceneSessionRoleApplication=[
                    dict(
                        UISceneConfigurationName="SDLSceneConfiguration",
                        UISceneDelegateClassName="SDLUIKitSceneDelegate",
                    )
                ]
            ),
        ),
        # Documents shows in the Files app: the disc image, saves and logs.
        UIFileSharingEnabled=True,
        LSSupportsOpeningDocumentsInPlace=True,
        # A Bluetooth mouse or trackpad on iPad is a pointer, not a touch.
        UIApplicationSupportsIndirectInputEvents=True,
        GCSupportsControllerUserInteraction=True,
        ITSAppUsesNonExemptEncryption=False,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--binary", type=Path, default=ROOT / "tmp/pc/ios" / EXECUTABLE)
    parser.add_argument("--out", type=Path, default=ROOT / "tmp/pc-release/ios")
    parser.add_argument("--version", default="dev-local")
    parser.add_argument("--bundle-id", default="org.porteame.yfm-redecomp")
    options = parser.parse_args()
    binary = options.binary.resolve()
    if not binary.is_file():
        raise SystemExit(f"No iOS executable at {binary}: build it first")
    identity_file = binary.parent / "build-identity.json"
    identity = json.loads(identity_file.read_text()) if identity_file.is_file() else {}
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if identity and identity.get("binary_sha256") != digest:
        raise SystemExit("The executable changed since its build identity was written")

    stage = options.out.resolve() / "stage"
    shutil.rmtree(stage, ignore_errors=True)
    app = stage / "Payload" / f"{APP_NAME}.app"
    app.mkdir(parents=True)
    executable = app / EXECUTABLE
    shutil.copyfile(binary, executable)
    executable.chmod(0o755)
    # No signature at all: a stale or ad hoc one only confuses signing tools.
    subprocess.run(
        ["xcrun", "codesign", "--remove-signature", str(executable)], check=False
    )
    (app / "PkgInfo").write_text("APPL????")
    languages = app / "languages"
    languages.mkdir()
    for name in LANGUAGES:
        shutil.copyfile(ROOT / f"languages/{name}.txt", languages / f"{name}.txt")
    licenses = app / "licenses"
    licenses.mkdir()
    shutil.copyfile(ROOT / "LICENSE", app / "LICENSE.txt")
    for name, source in {
        "FreeType.txt": "freetype/docs/FTL.TXT",
        "SDL3.txt": "sdl-source/LICENSE.txt",
        "libpng.txt": "libpng/LICENSE",
        "zlib.txt": "zlib/LICENSE",
    }.items():
        path = ROOT / "tmp/pc/ios-deps/src" / source
        if not path.is_file():
            raise SystemExit(f"Missing license notice: {path}")
        shutil.copyfile(path, licenses / name)

    release = re.fullmatch(r"v?(\d+\.\d+\.\d+)(?:[-.].*)?", options.version)
    revision = identity.get("revision", "")
    build_number = str(int(revision[:7], 16)) if revision else "1"
    info = info_plist(
        release.group(1) if release else "0.0.0", build_number, options.bundle_id
    )
    (app / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))

    archive = options.out.resolve() / f"yfm-redecomp-{options.version}-ios-arm64-unsigned.ipa"
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path in sorted(stage.rglob("*")):
            name = path.relative_to(stage).as_posix()
            if path.is_dir():
                continue
            entry = zipfile.ZipInfo.from_file(path, name)
            mode = 0o755 if path == executable else 0o644
            entry.external_attr = (stat.S_IFREG | mode) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            bundle.writestr(entry, path.read_bytes())
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    Path(str(archive) + ".sha256").write_text(f"{checksum}  {archive.name}\n")

    # Verify the archive as an installer reads it.
    with zipfile.ZipFile(archive) as bundle:
        names = bundle.namelist()
        prefix = f"Payload/{APP_NAME}.app/"
        if not all(name.startswith(prefix) for name in names):
            raise SystemExit("The .ipa holds files outside Payload/<app>")
        read = plistlib.loads(bundle.read(prefix + "Info.plist"))
        if read["CFBundleExecutable"] != EXECUTABLE or prefix + EXECUTABLE not in names:
            raise SystemExit("The .ipa's Info.plist does not name its executable")
        if hashlib.sha256(bundle.read(prefix + EXECUTABLE)).hexdigest() != hashlib.sha256(
            executable.read_bytes()
        ).hexdigest():
            raise SystemExit("The .ipa's executable differs from the staged one")
        forbidden = [n for n in names if n.lower().endswith((".bin", ".cue", ".sav", ".state"))]
        if forbidden:
            raise SystemExit(f"The .ipa must hold no disc or save data: {forbidden}")
    print(f"{archive} ({archive.stat().st_size} bytes, {len(names)} files)")
    print(f"sha256 {checksum}")
    print(f"bundle id {options.bundle_id}, version {info['CFBundleShortVersionString']} ({build_number}), iOS {IOS_MINIMUM}+, unsigned")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
