# Post-build: produce the release images for this env in $BUILD_DIR/release/
#   tasky-<env>-merged.bin   bootloader+partitions+boot_app0+app at 0x0 (esptool, full install)
#   tasky-<env>-app.bin      app only (OTA upload from the Wi-Fi portal)
#   tasky-<env>-<part>.bin   individual parts + tasky-<env>-parts.json with offsets,
#                            used by the web flasher so a reinstall keeps NVS (settings)
#                            unless the user chooses to erase.
Import("env")  # noqa: F821 (provided by PlatformIO/SCons)
import json
import os
import shutil
import subprocess


def build_release(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    pioenv = env["PIOENV"]
    out_dir = os.path.join(build_dir, "release")
    os.makedirs(out_dir, exist_ok=True)
    board = env.BoardConfig()

    app = os.path.join(build_dir, "firmware.bin")
    images = [(env.subst(off), env.subst(path)) for off, path in env.get("FLASH_EXTRA_IMAGES", [])]
    images.append((env.subst("$ESP32_APP_OFFSET") or "0x10000", app))

    parts = []
    for offset, path in images:
        name = os.path.splitext(os.path.basename(path))[0]
        name = "app" if path == app else name
        dest = f"tasky-{pioenv}-{name}.bin"
        shutil.copyfile(path, os.path.join(out_dir, dest))
        parts.append({"path": dest, "offset": int(offset, 16)})
    with open(os.path.join(out_dir, f"tasky-{pioenv}-parts.json"), "w") as f:
        json.dump(parts, f, indent=2)

    merged = os.path.join(out_dir, f"tasky-{pioenv}-merged.bin")
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    cmd = [env.subst("$PYTHONEXE"), esptool, "--chip", board.get("build.mcu"), "merge-bin", "-o", merged,
           "--flash-mode", board.get("build.flash_mode", "dio"),
           "--flash-size", board.get("upload.flash_size", "4MB")]
    for offset, path in images:
        cmd += [offset, path]
    subprocess.check_call(cmd)
    print(f"Release images in {out_dir}: " + ", ".join(sorted(os.listdir(out_dir))))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", build_release)  # noqa: F821
