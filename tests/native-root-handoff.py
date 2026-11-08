#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Qualify an installed-root PID 1 handoff using independent native components.

This test consumes an independently compiled Kernel, independent InfiltratorFS
tools/driver, and the separately packaged Boot EFI. It writes only disposable
regular-file disk images and OVMF variable copies. It does NOT certify Setup,
a graphical desktop, or physical machine boot.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys


def run(*args: object) -> None:
    subprocess.run([str(arg) for arg in args], check=True)


def require_file(path: Path) -> Path:
    path = path.resolve(strict=True)
    if not path.is_file() or not path.stat().st_size:
        raise ValueError("Missing nonempty input: " + str(path))
    return path


def kernel_helpers(source: Path):
    helper_file = source / "tests/infiltratorfs-native-boot.py"
    spec = importlib.util.spec_from_file_location("kernel_native_vm_tools", helper_file)
    if spec is None or spec.loader is None:
        raise ValueError("Exact Kernel source missing its three-way boot helpers")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    for member in ("modules_for_vm", "cpio"):
        if not hasattr(module, member):
            raise ValueError("Incompatible Kernel helper: " + member)
    return module


def initramfs_script(release: str, boot_identity: str) -> str:
    # The initramfs contains only the bootstrap /init and matching .ko drivers.
    # The next PID 1 will run from the separate InfiltratorFS volume itself.
    return """#!/bin/busybox sh
export PATH=/bin:/sbin
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev
fail() {
  /bin/busybox echo "INFS_HANDOFF_FAIL:$1" >/dev/console
  /bin/busybox poweroff -f
  /bin/busybox sleep 15
  exit 1
}
/bin/busybox echo INFS_HANDOFF_INITRAMFS_ENTER >/dev/console
[ "$(/bin/busybox uname -r)" = "__RELEASE__" ] || fail KERNEL_ABI
__MODULE_LOADS__
/bin/busybox mkdir -p /newroot
mounted=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  /bin/busybox mdev -s || :
  for sysdev in /sys/class/block/*; do
    [ -e "$sysdev" ] || continue
    name=$(/bin/busybox basename "$sysdev")
    case "$name" in loop*|ram*|sr*) continue ;; esac
    dev="/dev/$name"
    [ -b "$dev" ] || continue
    if /bin/busybox mount -t infiltratorfs -o rw "$dev" /newroot; then
      if [ "$(/bin/busybox cat /newroot/System/Boot/source-identity.txt 2>/dev/null)" = "__IDENTITY__" ]; then
        mounted=1
        break
      fi
      /bin/busybox umount /newroot || fail WRONG_VOLUME
    fi
  done
  [ "$mounted" = 1 ] && break
  /bin/busybox sleep 1
done
[ "$mounted" = 1 ] || fail NATIVE_ROOT_MOUNT
/bin/busybox echo INFS_NATIVE_ROOT_MOUNT_PASS >/dev/console
[ -f /newroot/sbin/init ] || fail MISSING_INSTALLED_INIT
[ -f /newroot/bin/busybox ] || fail MISSING_INSTALLED_BUSYBOX
# The portable writer creates source payloads mode 0644. Confirm the native
# Linux metadata adapter can persist executable modes before switch_root.
/bin/busybox chmod 0755 /newroot/bin/busybox || fail ROOT_EXECUTABLE_MODE
/bin/busybox chmod 0755 /newroot/sbin/init || fail ROOT_INIT_MODE
( cd /newroot && /bin/busybox sha256sum -c System/Boot/root-payloads.sha256 ) >/dev/console 2>&1 || fail BOOT_HASH
/bin/busybox echo INFS_NATIVE_ROOT_PAYLOAD_PASS >/dev/console
# A real switch_root frees the initramfs and replaces PID 1, unlike an initrd
# probe that merely mounts a filesystem and powers off.
/bin/busybox exec /bin/busybox switch_root /newroot /sbin/init
fail SWITCH_ROOT_RETURNED
""".replace("__RELEASE__", release).replace("__IDENTITY__", boot_identity)


def installed_init_script(release: str, boot_identity: str) -> str:
    return """#!/bin/busybox sh
# This script is physically stored on the separately formatted InfiltratorFS
# native root. It cannot run from the temporary initramfs.
export PATH=/bin:/sbin
fail() {
  /bin/busybox echo "INFS_NATIVE_PID1_FAIL:$1" >/dev/console
  /bin/busybox poweroff -f
  /bin/busybox sleep 15
  exit 1
}
[ "$$" = 1 ] || fail NOT_PID1
[ "$(/bin/busybox uname -r)" = "__RELEASE__" ] || fail WRONG_KERNEL
[ "$(/bin/busybox cat /System/Boot/source-identity.txt 2>/dev/null)" = "__IDENTITY__" ] || fail NOT_NATIVE_ROOT
/bin/busybox grep -q ' / infiltratorfs ' /proc/mounts || fail ROOT_NOT_INFILTRATORFS
[ -d /Programs ] && [ -d /System/Filesystems ] && [ -d /System/Resources/Fonts ] || fail NATIVE_LAYOUT
/bin/busybox echo INFS_INSTALLED_PID1_ROOT_PASS >/dev/console
/bin/busybox sh /System/Commands/InfiltratorOS/root-health-check || fail FIRST_STAGE_SERVICE
if [ -f /Temporary/InfiltratorOS/first-boot.status ]; then
  [ "$(/bin/busybox cat /Temporary/InfiltratorOS/first-boot.status)" = "installed-root-first-boot" ] || fail PERSISTENCE_CONTENT
  /bin/busybox echo installed-root-rebooted >/Temporary/InfiltratorOS/reboot.status || fail REBOOT_WRITE
  /bin/busybox sync
  /bin/busybox echo INFS_INSTALLED_ROOT_REBOOT_PERSISTENCE_PASS >/dev/console
else
  /bin/busybox echo installed-root-first-boot >/Temporary/InfiltratorOS/first-boot.status || fail FIRST_WRITE
  /bin/busybox sync
  /bin/busybox echo INFS_INSTALLED_ROOT_FIRST_WRITE_PASS >/dev/console
fi
/bin/busybox echo INFS_INSTALLED_ROOT_HANDOFF_PASS >/dev/console
/bin/busybox poweroff -f
/bin/busybox sleep 15
""".replace("__RELEASE__", release).replace("__IDENTITY__", boot_identity)


HEALTH_SERVICE = """#!/bin/busybox sh
# Minimal first-stage native resource/service smoke test, not full service
# management or desktop startup.
[ -d /System/Resources/Icons ] || exit 1
[ -d /System/Kernel/Images ] || exit 1
[ -d /Users ] || exit 1
[ -d /Volumes ] || exit 1
/bin/busybox echo INFS_NATIVE_FIRST_STAGE_SERVICE_PASS >/dev/console
"""


def make_initramfs(cpio, busybox: Path, modules: list[tuple[str, bytes]],
                   release: str, boot_identity: str, output: Path) -> None:
    loads = "\n".join(
        '/bin/busybox insmod /modules/{}.ko || fail MODULE_{}'.format(name, name)
        for name, _ in modules
    )
    script = initramfs_script(release, boot_identity).replace("__MODULE_LOADS__", loads)
    names = ("bin", "dev", "proc", "sys", "modules", "newroot")
    payload = b"".join(cpio(name, stat.S_IFDIR | 0o755) for name in names)
    payload += cpio("dev/console", stat.S_IFCHR | 0o600, major=5, minor=1)
    payload += cpio("bin/busybox", stat.S_IFREG | 0o755, busybox.read_bytes())
    payload += cpio("init", stat.S_IFREG | 0o755, script.encode("utf-8"))
    for name, blob in modules:
        payload += cpio("modules/" + name + ".ko", stat.S_IFREG | 0o644, blob)
    payload += cpio("TRAILER!!!", 0)
    output.write_bytes(gzip.compress(payload, compresslevel=6, mtime=0))


def boot_once(output: Path, ovmf: Path, esp: Path, native: Path, phase: int,
              timeout: int) -> None:
    code = ovmf / "OVMF_CODE_4M.fd"
    vars_template = ovmf / "OVMF_VARS_4M.fd"
    if not code.is_file():
        code = ovmf / "OVMF_CODE.fd"
        vars_template = ovmf / "OVMF_VARS.fd"
    code, vars_template = require_file(code), require_file(vars_template)
    vars_copy = output / ("OVMF_VARS-" + str(phase) + ".fd")
    shutil.copyfile(vars_template, vars_copy)
    serial = output / ("serial.log" if phase == 1 else "serial-reboot.log")
    argv = [
        "qemu-system-x86_64", "-machine", "q35,accel=tcg", "-cpu", "max",
        "-m", "2048", "-smp", "2",
        "-drive", "if=pflash,format=raw,readonly=on,file=" + str(code),
        "-drive", "if=pflash,format=raw,file=" + str(vars_copy),
        "-drive", "if=virtio,format=raw,file=" + str(esp),
        "-drive", "if=virtio,format=raw,file=" + str(native),
        "-nographic", "-serial", "file:" + str(serial),
        "-monitor", "none", "-no-reboot", "-net", "none",
    ]
    with (output / ("qemu-phase-" + str(phase) + ".log")).open("wb") as log:
        subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT,
                       timeout=timeout, check=True)
    serial_text = serial.read_text(errors="replace")
    expected = (
        "INFS_HANDOFF_INITRAMFS_ENTER",
        "INFS_NATIVE_ROOT_MOUNT_PASS",
        "INFS_NATIVE_ROOT_PAYLOAD_PASS",
        "INFS_INSTALLED_PID1_ROOT_PASS",
        "INFS_NATIVE_FIRST_STAGE_SERVICE_PASS",
        "INFS_INSTALLED_ROOT_FIRST_WRITE_PASS" if phase == 1 else
        "INFS_INSTALLED_ROOT_REBOOT_PERSISTENCE_PASS",
        "INFS_INSTALLED_ROOT_HANDOFF_PASS",
        "reboot: Power down",
    )
    missing = [marker for marker in expected if marker not in serial_text]
    if missing:
        raise RuntimeError("Native installed-root phase {} missing {}. Serial evidence: {}"
                           .format(phase, missing, serial))
    if "INFS_HANDOFF_FAIL:" in serial_text or "INFS_NATIVE_PID1_FAIL:" in serial_text:
        raise RuntimeError("Guest reported native-root failure; see " + str(serial))


def build(args: argparse.Namespace) -> dict:
    args.kernel_source = args.kernel_source.resolve(strict=True)
    args.os_source = args.os_source.resolve(strict=True)
    args.module_root = args.module_root.resolve(strict=True)
    for name in ("kernel", "kernel_config", "module", "boot", "driver",
                 "busybox", "mkfs", "fs_tool"):
        setattr(args, name, require_file(getattr(args, name)))
    release = (args.kernel_source / "VERSION").read_text().strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", release):
        raise ValueError("Invalid native Kernel release")
    helper = kernel_helpers(args.kernel_source)
    modules = helper.modules_for_vm(args.module_root, args.module, release,
                                    args.kernel_config)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    native, esp = output / "root.raw", output / "esp.raw"
    native.write_bytes(b"")
    esp.write_bytes(b"")
    with native.open("r+b") as f:
        f.truncate(768 * 1024 * 1024)
    with esp.open("r+b") as f:
        f.truncate(96 * 1024 * 1024)
    boot_identity = "INFS_INSTALLED_OS_ROOT_EXCLUSIVE"
    initrd = output / "initrd.img"
    make_initramfs(helper.cpio, args.busybox, modules, release, boot_identity,
                   initrd)
    run(args.mkfs, "--force", "-L", "INFOSROOT", native)
    run(sys.executable,
        args.kernel_source / "tests/infiltratoros-native-payload.py",
        "--image", native, "--fs-tool", args.fs_tool,
        "--os-source", args.os_source, "--work", output)
    for directory in ("/bin", "/sbin", "/dev", "/proc", "/sys", "/etc",
                      "/System/Commands/InfiltratorOS"):
        run(args.fs_tool, native, "mkdir", directory)
    def put(destination: str, data: bytes) -> Path:
        local = output / ("stage-" + destination.lstrip("/").replace("/", "-"))
        local.write_bytes(data)
        run(args.fs_tool, native, "put", local, destination)
        return local

    installed_init = put("/sbin/init",
                         installed_init_script(release, boot_identity).encode())
    health = put("/System/Commands/InfiltratorOS/root-health-check",
                 HEALTH_SERVICE.encode())
    identity = put("/System/Boot/source-identity.txt",
                   (boot_identity + "\n").encode())
    run(args.fs_tool, native, "put", args.busybox, "/bin/busybox")
    run(args.fs_tool, native, "put", args.kernel,
        "/System/Kernel/Images/InfiltratorOS-" + release + ".img")
    run(args.fs_tool, native, "put", initrd, "/boot/initrd.img")
    manifest = output / "root-payloads.sha256"
    payload = [
        ("System/Kernel/Images/InfiltratorOS-" + release + ".img", args.kernel),
        ("boot/initrd.img", initrd),
        ("bin/busybox", args.busybox),
        ("sbin/init", installed_init),
        ("System/Commands/InfiltratorOS/root-health-check", health),
        ("System/Boot/source-identity.txt", identity),
    ]
    manifest.write_text("".join(
        hashlib.sha256(source.read_bytes()).hexdigest() + "  " + target + "\n"
        for target, source in payload
    ))
    run(args.fs_tool, native, "put", manifest,
        "/System/Boot/root-payloads.sha256")
    run("mkfs.fat", "-F", "32", "-I", "-n", "BOOTESP", esp)
    run("mmd", "-i", esp, "::/EFI", "::/EFI/BOOT",
        "::/EFI/BOOT/drivers_x64")
    run("mcopy", "-i", esp, args.boot, "::/EFI/BOOT/BOOTX64.EFI")
    run("mcopy", "-i", esp, args.driver,
        "::/EFI/BOOT/drivers_x64/InfiltratorFS_x64.efi")
    config = output / "boot.conf"
    config.write_text("""timeout 1
textonly true
use_nvram false
driver_diagnostics true
scanfor manual
showtools shutdown
menuentry "InfiltratorOS installed-root handoff trial" {
    volume INFOSROOT
    loader \\System\\Kernel\\Images\\InfiltratorOS-__RELEASE__.img
    initrd \\boot\\initrd.img
    linux_load memory
    ostype Linux
    graphics off
    options "console=ttyS0,115200 rdinit=/init panic=-1"
}
""".replace("__RELEASE__", release))
    run("mcopy", "-i", esp, config, "::/EFI/BOOT/boot.conf")
    hashes = {
        "Boot_efi_sha256": hashlib.sha256(args.boot.read_bytes()).hexdigest(),
        "InfiltratorFS_driver_sha256": hashlib.sha256(args.driver.read_bytes()).hexdigest(),
        "InfiltratorFS_module_sha256": hashlib.sha256(args.module.read_bytes()).hexdigest(),
        "Kernel_sha256": hashlib.sha256(args.kernel.read_bytes()).hexdigest(),
        "Kernel_release": release,
        "OS_source_sha": subprocess.check_output(
            ["git", "-C", str(args.os_source), "rev-parse", "HEAD"],
            text=True).strip(),
        "Kernel_source_sha": subprocess.check_output(
            ["git", "-C", str(args.kernel_source), "rev-parse", "HEAD"],
            text=True).strip(),
    }
    (output / "source-identities.json").write_text(json.dumps(hashes, indent=2) + "\n")
    boot_once(output, args.ovmf, esp, native, 1, args.timeout)
    boot_once(output, args.ovmf, esp, native, 2, args.timeout)
    return hashes


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("kernel-source", "kernel", "kernel-config", "module-root",
                 "module", "boot", "driver", "busybox", "mkfs", "fs-tool",
                 "os-source", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--ovmf", type=Path, default=Path("/usr/share/OVMF"))
    p.add_argument("--timeout", type=int, default=600)
    args = p.parse_args()
    if not 30 <= args.timeout <= 1800:
        p.error("QEMU timeout must be 30 to 1800 seconds")
    try:
        hashes = build(args)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print("INFS_NATIVE_INSTALLED_ROOT_FAIL: " + str(exc), file=sys.stderr)
        return 1
    print("INFS_NATIVE_INSTALLED_ROOT_TWO_BOOT_PID1_PASS")
    print(json.dumps(hashes, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
