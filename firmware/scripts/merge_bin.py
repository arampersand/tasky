# Post-build: merge bootloader + partitions + boot_app0 + app into one image at
# offset 0x0, so ESP Web Tools / esptool can flash it as a single part.
# Output: .pio/build/<env>/tasky-<env>-merged.bin
Import("env")  # noqa: F821 (provided by PlatformIO/SCons)
import os
import subprocess


def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    out = os.path.join(build_dir, f"tasky-{env['PIOENV']}-merged.bin")
    board = env.BoardConfig()
    parts = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):  # bootloader, partitions, boot_app0
        parts += [env.subst(offset), env.subst(path)]
    parts += [env.subst("$ESP32_APP_OFFSET") or "0x10000", os.path.join(build_dir, "firmware.bin")]
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    cmd = [env.subst("$PYTHONEXE"), esptool, "--chip", board.get("build.mcu"), "merge-bin", "-o", out,
           "--flash-mode", board.get("build.flash_mode", "dio"),
           "--flash-size", board.get("upload.flash_size", "4MB")] + parts
    print("merge-bin:", " ".join(cmd))
    subprocess.check_call(cmd)
    print(f"Merged image: {out} ({os.path.getsize(out)} bytes)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)  # noqa: F821
