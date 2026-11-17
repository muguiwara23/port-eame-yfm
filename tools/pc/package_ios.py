#!/usr/bin/env python3
"""Port/eame: package the linked iOS game as an unsigned .ipa.

Writes Payload/YFM Re-Decomp.app (the executable, Info.plist, the language
packs and license notices) and zips it as an .ipa under tmp/pc-release/ios.
The repository's own mods are staged in the app's mods folder, their code
built as iOS ARM64 dylibs for this exact game build (build_mod_arm64.py).
The package holds no disc data and no saves, and it is NOT signed: iOS runs
it, and loads those dylibs, only after the player's own signing tool has
signed the whole bundle.
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


def shipped_mod_files():
    """Only repository-owned resources, never locally installed mods or objects."""
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "mods"], cwd=ROOT, text=True
    ).split("\0")
    return [
        Path(name)
        for name in tracked
        if name and Path(name).suffix not in (".c", ".h", ".o", ".dylib")
    ]


def stage_mods(app, game):
    """The mods beside the program, where the game looks for its own
    (Paths_Program "mods"); code mods as dylibs iOS loads from the bundle."""
    from build_mod_arm64 import build

    files = shipped_mod_files()
    libraries = []
    for relative in files:
        destination = app / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / relative, destination)
    for relative in files:
        if relative.name != "mod.json":
            continue
        manifest = json.loads((ROOT / relative).read_text(encoding="utf-8-sig"))
        if manifest.get("library"):
            library = build(
                ROOT / relative.parent, app / relative.parent, game=game, platform_name="ios"
            )
            subprocess.run(
                ["xcrun", "codesign", "--remove-signature", str(library)], check=False
            )
            libraries.append(library)
    return files, libraries


def stage_icon(app, icon, info):
    """The app's icon from one square picture: actool makes the asset
    catalog and the sizes iPhone and iPad want (Info.plist's CFBundleIcons);
    without actool, sips makes the sizes and the plist names them."""
    work = app.parent.parent / "icon-work"
    shutil.rmtree(work, ignore_errors=True)
    catalog = work / "Assets.xcassets"
    appicon = catalog / "AppIcon.appiconset"
    appicon.mkdir(parents=True)
    # A 1024 x 1024 PNG (the private icon is one already, RGB with no
    # transparency); sips only resizes or converts what is not.
    picture = appicon / "icon.png"
    shutil.copyfile(icon, picture)
    if icon.suffix.lower() != ".png" or icon.read_bytes()[16:24] != (1024).to_bytes(4, "big") * 2:
        subprocess.run(
            ["sips", "-s", "format", "png", "-z", "1024", "1024", str(icon), "--out", str(picture)],
            check=True, stdout=subprocess.DEVNULL,
        )
    (catalog / "Contents.json").write_text('{"info":{"author":"xcode","version":1}}')
    (appicon / "Contents.json").write_text(json.dumps({
        "images": [{"filename": "icon.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}],
        "info": {"author": "xcode", "version": 1},
    }))
    partial = work / "partial.plist"
    made = subprocess.run(
        ["xcrun", "actool", str(catalog), "--compile", str(app), "--platform", "iphoneos",
         "--minimum-deployment-target", IOS_MINIMUM, "--app-icon", "AppIcon",
         "--target-device", "iphone", "--target-device", "ipad",
         "--output-partial-info-plist", str(partial), "--output-format", "human-readable-text"],
        check=False,
    )
    if made.returncode == 0 and partial.is_file():
        info.update(plistlib.loads(partial.read_bytes()))
        print("icon: asset catalog by actool:", sorted(p.name for p in app.glob("AppIcon*")) + (["Assets.car"] if (app / "Assets.car").is_file() else []))
        return
    print("icon: actool failed; plain PNG icons")
    sizes = {"AppIcon60x60@2x.png": 120, "AppIcon60x60@3x.png": 180,
             "AppIcon76x76@2x~ipad.png": 152, "AppIcon83.5x83.5@2x~ipad.png": 167}
    for name, size in sizes.items():
        subprocess.run(["sips", "-z", str(size), str(size), str(appicon / "icon.png"), "--out", str(app / name)],
                       check=True, stdout=subprocess.DEVNULL)
    info["CFBundleIcons"] = {"CFBundlePrimaryIcon": {"CFBundleIconFiles": ["AppIcon60x60"]}}
    info["CFBundleIcons~ipad"] = {"CFBundlePrimaryIcon": {"CFBundleIconFiles": ["AppIcon60x60", "AppIcon76x76", "AppIcon83.5x83.5"]}}


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--binary", type=Path, default=ROOT / "tmp/pc/ios" / EXECUTABLE)
    parser.add_argument("--out", type=Path, default=ROOT / "tmp/pc-release/ios")
    parser.add_argument("--version", default="dev-local")
    parser.add_argument("--bundle-id", default="org.porteame.yfm-redecomp")
    parser.add_argument(
        "--mods",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Stage the repository's mods and build their iOS dylibs (default)",
    )
    parser.add_argument("--icon", type=Path, help="A square picture for the app's icon (kept out of the repository)")
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
    mod_libraries = []
    if options.mods:
        mod_files, mod_libraries = stage_mods(app, binary.parent)
        print(f"mods: {len(mod_files)} files, {len(mod_libraries)} iOS dylibs")
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
    if options.icon:
        stage_icon(app, options.icon.resolve(), info)
    (app / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))

    archive = options.out.resolve() / f"yfm-redecomp-{options.version}-ios-arm64-unsigned.ipa"
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path in sorted(stage.rglob("*")):
            name = path.relative_to(stage).as_posix()
            if path.is_dir():
                continue
            entry = zipfile.ZipInfo.from_file(path, name)
            mode = 0o755 if path == executable or path in mod_libraries else 0o644
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
