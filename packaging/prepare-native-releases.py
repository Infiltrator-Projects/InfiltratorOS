#!/usr/bin/python3 -u
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch and verify one signed release snapshot before starting native workers."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

URL = "https://infiltrator-projects.github.io/Infiltrator-Repository/catalogue/native-installers.json"
KEY = Path("/System/Resources/Documentation/InfiltratorOS/repository-key.gpg")
MAX_BYTES = 3 * 1024 * 1024
DIGEST = re.compile(r"sha256:[0-9a-f]{64}$")
VERSION = re.compile(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")


def download(url: str) -> bytes:
    for attempt in range(4):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "InfiltratorOS-native-installer",
                                                       "Cache-Control": "no-cache"})
            with urllib.request.urlopen(req, timeout=30) as response:
                payload = response.read(MAX_BYTES + 1)
            if len(payload) > MAX_BYTES:
                raise ValueError("Release snapshot exceeds the size limit")
            return payload
        except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError) as exc:
            if isinstance(exc, urllib.error.HTTPError) and exc.code not in (408, 429, 500, 502, 503, 504):
                raise
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)
    raise RuntimeError("Release snapshot could not be downloaded")


def expected_repositories(providers: Path, filesystems: Path) -> set[str]:
    temporal = providers.read_text().splitlines()
    fs = filesystems.read_text().splitlines()
    if len(temporal) != 54 or len(fs) != 2:
        raise ValueError("Native resource catalogue is incomplete")
    slugs = temporal + fs
    if len(set(slugs)) != 56 or any(not re.fullmatch(r"(?:calendar|clock|filesystem)-[a-z0-9-]+", item) for item in slugs):
        raise ValueError("Duplicate or unsafe native resource identifier")
    names = {"System-Monitor", "Calculator", "Files", "Defragmenter"}
    names.update("".join(part.capitalize() for part in slug.split("-")[:1]) + "-" + "-".join(slug.split("-")[1:]) for slug in slugs)
    # Canonical names are case-insensitive only for lookup. Display-case in
    # release URLs comes from signed metadata and must still be validated.
    return {x.lower() for x in names}


def verify_snapshot(data: bytes, expected: set[str]) -> dict[str, dict]:
    snapshot = json.loads(data)
    if not isinstance(snapshot, dict) or snapshot.get("schema") != 1:
        raise ValueError("Unsupported native release snapshot")
    releases = snapshot.get("releases")
    if not isinstance(releases, dict) or len(releases) != 60:
        raise ValueError("Native release snapshot must contain exactly 60 entries")
    if {name.lower() for name in releases} != expected:
        raise ValueError("Snapshot does not match the embedded native provider list")
    for repository, release in releases.items():
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9-]+", repository):
            raise ValueError("Unsafe native repository identifier")
        if not isinstance(release, dict) or release.get("draft") or release.get("prerelease"):
            raise ValueError(repository + ": unpublished release")
        tag = release.get("tag_name")
        if not isinstance(tag, str) or not VERSION.fullmatch(tag):
            raise ValueError(repository + ": invalid version tag")
        assets = release.get("assets")
        if not isinstance(assets, list) or not assets:
            raise ValueError(repository + ": missing assets")
        for asset in assets:
            if not isinstance(asset, dict):
                raise ValueError(repository + ": malformed asset")
            name = asset.get("name")
            expected_url = f"https://github.com/Infiltrator-Projects/{repository}/releases/download/{tag}/{name}"
            if (not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.+-]+", name)
                    or asset.get("browser_download_url") != expected_url
                    or not isinstance(asset.get("digest"), str)
                    or not DIGEST.fullmatch(asset["digest"])
                    or not isinstance(asset.get("size"), int)
                    or not 0 < asset["size"] <= 512 * 1024 * 1024):
                raise ValueError(repository + ": invalid release asset")
    return releases


def prepare(directory: Path, providers: Path, filesystems: Path, key: Path = KEY) -> int:
    if os.geteuid() != 0:
        raise RuntimeError("Native snapshot verification requires root")
    if not key.is_file() or key.is_symlink():
        raise RuntimeError("InfiltratorOS trusted repository signing key is missing")
    expected = expected_repositories(providers, filesystems)
    if directory.exists():
        raise ValueError("Refusing to overwrite an existing release snapshot")
    directory.mkdir(mode=0o755)
    with tempfile.TemporaryDirectory(prefix="signed-native-", dir=directory.parent) as work:
        stage = Path(work)
        data = download(URL)
        signature = download(URL + ".asc")
        manifest_path = stage / "native-installers.json"
        signature_path = stage / "native-installers.json.asc"
        manifest_path.write_bytes(data)
        signature_path.write_bytes(signature)
        subprocess.run(["/usr/bin/gpgv", "--keyring", str(key), str(signature_path),
                        str(manifest_path)], check=True)
        releases = verify_snapshot(data, expected)
        for repository, release in releases.items():
            path = directory / (repository + ".json")
            path.write_text(json.dumps(release, separators=(",", ":")) + "\n")
            path.chmod(0o644)
    directory.chmod(0o755)
    return len(releases)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("providers", type=Path)
    parser.add_argument("filesystems", type=Path)
    args = parser.parse_args()
    print(f"Verifying one signed release snapshot for {prepare(args.output, args.providers, args.filesystems)} independent installations", flush=True)


if __name__ == "__main__":
    main()
