#!/usr/bin/env python3
"""Reject every unexpected file/symlink in an InfiltratorOS Debian payload.

The four application binaries belong to their independent packages. This OS
package owns OS configuration helpers, verified resource seeds and only the host
integration files needed by Debian and Mint. The application installer workers
are embedded in the dpkg maintainer script and staged temporarily only under the native /Temporary/InfiltratorOS.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PRODUCTS = ("system-monitor", "calculator", "files", "defragmenter")
COMMAND_ROOT = "System/Commands/InfiltratorOS/"
FONT_ROOT = "System/Resources/Fonts/Seed/"


def expected_paths(source_root: pathlib.Path) -> set[str]:
    entries = {
        COMMAND_ROOT + command for command in (
            "activate-calendar", "ensure-native-fonts", "ensure-native-icons",
            "select-theme",
        )
    }
    entries.update({
        "etc/profile.d/90-infiltratoros-native-path.sh",
        "etc/xdg/autostart/infiltrator-calendar.desktop",
        "System/Resources/Icons/Seed/mercedes-icons-v1.tar.xz",
        FONT_ROOT + "SHA256SUMS",
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
