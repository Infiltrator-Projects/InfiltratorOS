#!/usr/bin/env python3
"""Reject every unexpected file/symlink in an InfiltratorOS Debian payload.

The four application binaries belong to their independent packages. This OS
package owns OS helpers, direct native resources and only the host
integration files needed by Debian and Mint. The application installer workers
are embedded in the dpkg maintainer script and staged temporarily only under the native /Temporary/InfiltratorOS.
"""
from __future__ import annotations

import io
import pathlib
import subprocess
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRODUCTS = ("system-monitor", "calculator", "files", "defragmenter")
COMMAND_ROOT = "System/Commands/InfiltratorOS/"
FONT_ROOT = "System/Resources/Fonts/"
ICON_ROOT = "System/Resources/Icons/"
MANIFEST_ROOT = "System/Resources/Documentation/InfiltratorOS/"


def expected_paths(source_root: pathlib.Path) -> set[str]:
    entries = {
        COMMAND_ROOT + command for command in (
            "activate-calendar", "activate-program-icons", "ensure-native-fonts", "ensure-native-icons",
            "select-theme",
        )
    }
    entries.update({
        "etc/profile.d/90-infiltratoros-native-path.sh",
        "etc/xdg/autostart/infiltrator-calendar.desktop",
        "etc/xdg/autostart/infiltrator-program-icons.desktop",
        MANIFEST_ROOT + "fonts.sha256",
        MANIFEST_ROOT + "icons.sha256",
    })
    entries.update({
        "usr/share/plymouth/themes/infiltratoros/" + name
        for name in ("infiltratoros.plymouth", "infiltratoros.script", "infiltratoros.png")
    })
    fonts = []
    for line in (source_root / "assets/fonts/SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ", 1)
        if len(digest) != 64 or not all(c in "0123456789abcdef" for c in digest):
            raise ValueError("Invalid font SHA-256 manifest")
        if not name.endswith(".ttf") or "/" in name or name.startswith("."):
            raise ValueError("Invalid font manifest pathname: " + name)
        fonts.append(name)
    if len(fonts) != 23 or len(set(fonts)) != 23:
        raise ValueError("Expected precisely 23 unique packaged font faces")
    entries.update(FONT_ROOT + name for name in fonts)
    archive = b"".join((source_root / "assets/icons" /
                        f"mercedes-icons-v1.part{part:02d}").read_bytes()
                       for part in range(10))
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:xz") as icons_archive:
        icon_manifest_file = icons_archive.extractfile("SHA256SUMS")
        if icon_manifest_file is None:
            raise ValueError("Missing canonical icon inventory")
        icon_manifest = icon_manifest_file.read().decode("utf-8").splitlines()
    if len(icon_manifest) != 1233:
        raise ValueError("Expected 1,233 named icon identities")
    icon_names = []
    for line in icon_manifest:
        digest, relative = line.split("  ", 1)
        if len(digest) != 64 or not all(c in "0123456789abcdef" for c in digest):
            raise ValueError("Invalid icon SHA-256")
        parts = pathlib.PurePosixPath(relative).parts
        if (len(parts) != 3 or parts[1] not in ("default", "night") or
                parts[0] not in ("adapter", "connect", "eco-coach", "finance",
                                 "logbook", "remote-parking") or
                not relative.endswith((".png", ".webp"))):
            raise ValueError("Invalid icon identity: " + relative)
        flat = relative.replace("/", "-")
        if flat.startswith(".") or ".." in flat:
            raise ValueError("Unsafe flattened icon name")
        icon_names.append(flat)
    if len(set(icon_names)) != 1233:
        raise ValueError("Flattened icon identities collide")
    entries.update(ICON_ROOT + name for name in icon_names)
    return entries


def payload_paths(package: pathlib.Path) -> set[str]:
    installed: set[str] = set()
    proc = subprocess.Popen(
        ["dpkg-deb", "--fsys-tarfile", str(package)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    try:
        assert proc.stdout is not None
        with tarfile.open(fileobj=proc.stdout, mode="r|") as archive:
            for item in archive:
                path = item.name.removeprefix("./").rstrip("/")
                if not path or item.isdir():
                    continue
                if path.startswith("/") or ".." in pathlib.PurePosixPath(path).parts:
                    raise ValueError("Unsafe installed path: " + item.name)
                if not item.isfile():
                    raise ValueError("Unapproved symlink or special file: " + path)
                if path in installed:
                    raise ValueError("Duplicated installed path: " + path)
                installed.add(path)
        proc.stdout.close()
        if proc.wait() != 0:
            raise RuntimeError("dpkg-deb archive extraction failed")
        return installed
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
        if proc.stdout:
            proc.stdout.close()
        if proc.stderr:
            proc.stderr.close()


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: package-layout-test.py PACKAGE.deb", file=sys.stderr)
        return 2
    package = pathlib.Path(sys.argv[1]).resolve()
    expected = expected_paths(ROOT)
    actual = payload_paths(package)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing or unexpected:
        for name in missing:
            print("MISSING:", name, file=sys.stderr)
        for name in unexpected:
            print("UNAPPROVED LOCATION:", name, file=sys.stderr)
        return 1
    print(
        f"InfiltratorOS layout: {len(actual)} persistent files at approved locations; "
        "zero installed application-worker executables"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
