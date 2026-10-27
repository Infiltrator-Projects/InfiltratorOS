#!/usr/bin/python3 -u
# SPDX-License-Identifier: GPL-3.0-or-later
"""Installation-only worker for independently released calendars and clocks."""
from __future__ import annotations

import ctypes
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

STATUS_DIR = Path("/Temporary/InfiltratorOS")
STATUS_PATH = STATUS_DIR / "temporal-resources-install.status"
CATALOGUE = []  # @EMBED_TEMPORAL_CATALOGUE@


def catalogue():
    entries = CATALOGUE or json.loads((Path(__file__).parent / "temporal-resources.json").read_text())
    if len(entries) != 54 or sum(x["kind"] == "calendar" for x in entries) != 31 or sum(x["kind"] == "clock" for x in entries) != 23:
        raise ValueError("Expected exactly 31 calendars and 23 clocks")
    for field in ("repository", "package", "resource"):
        if len({x[field] for x in entries}) != 54:
            raise ValueError("Duplicate temporal identity: " + field)
    for entry in entries:
        repository, kind, name = entry["repository"], entry["kind"], entry["name"]
        if (not re.fullmatch(r"(?:Calendar|Clock)-[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*", repository) or
                not repository.lower().startswith(kind + "-") or
                entry["package"] != "infiltrator-" + repository.lower() or
                entry["id"] != repository.lower().split("-", 1)[1] or
                not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 .-]+", name) or
                entry["resource"] != "/System/Resources/" + ("Calendars" if kind == "calendar" else "Clocks") + "/" + name + ".so"):
            raise ValueError("Invalid temporal catalogue identity: " + repository)
    return entries


def write_status(state, progress, stage, detail):
    STATUS_DIR.mkdir(mode=0o755, parents=True, exist_ok=True)
    clean = lambda value: str(value).replace("\n", " ").replace("\r", " ")
    with tempfile.NamedTemporaryFile(mode="w", dir=STATUS_DIR, prefix="temporal-status-", delete=False) as file:
        file.write(f"state={clean(state)}\nprogress={int(progress)}\nstage={clean(stage)}\ndetail={clean(detail)}\n")
        temporary = Path(file.name)
    temporary.chmod(0o644)
    temporary.replace(STATUS_PATH)


def required(arguments):
    print("+ " + " ".join(map(str, arguments)), flush=True)
    subprocess.run(list(map(str, arguments)), check=True)


def open_url(url):
    headers = {"User-Agent": "InfiltratorOS-native-temporal-installer", "Accept": "application/vnd.github+json"}
    # CI can authenticate public API reads without adding a runtime dependency
    # on an account. Never forward an API token to release-asset hosts.
    token = os.environ.get("GITHUB_TOKEN")
    if token and urllib.parse.urlsplit(url).netloc == "api.github.com":
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(url, headers=headers)
    for attempt in range(4):
        try:
            return urllib.request.urlopen(request, timeout=120)
        except urllib.error.HTTPError as error:
            if error.code in (403, 429) and error.headers.get("X-RateLimit-Remaining") == "0":
                delay = max(1, min(3600, int(error.headers.get("X-RateLimit-Reset", time.time() + 60)) - int(time.time()) + 2))
                print(f"GitHub release API rate limit; retrying in {delay} seconds.", flush=True)
                # Keep the last per-provider stage visible while the queue waits.
                time.sleep(delay)
            elif error.code >= 500 and attempt < 3:
                time.sleep(2 ** attempt)
            else:
                raise
        except (urllib.error.URLError, TimeoutError):
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)
    raise RuntimeError("Release download retries exhausted")


def release_installer(entry, release):
    if not isinstance(release, dict):
        raise ValueError("Invalid release metadata")
    if release.get("draft") or release.get("prerelease"):
        raise ValueError("Only published stable releases may be installed")
    tag = release.get("tag_name", "")
    if not isinstance(tag, str) or not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag):
        raise ValueError("Release has no canonical version tag")
    version, slug = tag[1:], entry["repository"].lower()
    name = f"{slug}-{version}-native-installer.run"
    expected = {name, f"{slug}-{version}-source.zip", f"{entry['package']}_{version}_amd64.deb"}
    assets = release.get("assets", [])
    if not isinstance(assets, list) or len(assets) != 3 or not all(isinstance(x, dict) for x in assets) or {asset.get("name") for asset in assets} != expected:
        raise ValueError("Release must contain exactly its native RUN, generic DEB and source ZIP")
    asset = next(asset for asset in assets if asset["name"] == name)
    expected_url = f"https://github.com/Infiltrator-Projects/{entry['repository']}/releases/download/{tag}/{name}"
    if asset.get("browser_download_url") != expected_url:
        raise ValueError("Native installer URL is outside the expected repository release")
    digest = asset.get("digest", "")
    if not isinstance(digest, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        raise ValueError("GitHub did not provide the native installer's SHA-256 digest")
    if not isinstance(asset.get("size"), int) or not 0 < asset["size"] <= 512 * 1024 * 1024:
        raise ValueError("Native installer size is invalid")
    return version, asset["browser_download_url"], digest[7:], asset["size"]


def download_installer(url, destination, digest, size):
    hasher, received = hashlib.sha256(), 0
    with open_url(url) as response, destination.open("wb") as output:
        while chunk := response.read(1024 * 1024):
            received += len(chunk)
            if received > size:
                raise ValueError("Native installer exceeded its published size")
            hasher.update(chunk)
            output.write(chunk)
    if received != size or hasher.hexdigest() != digest:
        raise ValueError("Native installer does not match the published SHA-256 and size")
    destination.chmod(0o700)


class Metadata(ctypes.Structure):
    _fields_ = [("struct_size", ctypes.c_size_t), ("abi_version", ctypes.c_uint32),
                ("kind", ctypes.c_int), ("id", ctypes.c_char_p), ("name", ctypes.c_char_p),
                ("supports_seconds", ctypes.c_bool), ("requires_latitude", ctypes.c_bool),
                ("requires_longitude", ctypes.c_bool)]


def verify_installation(entry, version, root=Path("/"), query=True):
    if query:
        identity = subprocess.check_output(["/usr/bin/dpkg-query", "-W", "-f=${Status}\n${Version}\n", entry["package"]], text=True)
        if identity.splitlines() != ["install ok installed", version + "+native1"]:
            raise ValueError("Native Debian package was not installed at the selected version")
    library = root / entry["resource"].lstrip("/")
    info_file = root / "System/Resources/Documentation" / entry["repository"] / "BUILD-INFO"
    info = dict(line.split(": ", 1) for line in info_file.read_text().splitlines() if ": " in line)
    expected = {"Version": version, "Package version": version + "+native1", "Repository": entry["repository"],
                "Profile": "aggressive", "LTO": "enabled", "PGO": "trained two-pass profile", "Resource": entry["resource"]}
    if any(info.get(key) != value for key, value in expected.items()):
        raise ValueError("Installed native build record does not match this provider and release")
    flags = info.get("C flags", "")
    if not flags.startswith("-O3 ") or "-march=native" not in flags or "-flto" not in flags:
        raise ValueError("Installed module was not compiled aggressively for this CPU")
    if not re.fullmatch(r"[0-9a-f]{40}", info.get("Source revision", "")):
        raise ValueError("Native build record has no source commit")
    if hashlib.sha256(library.read_bytes()).hexdigest() != info.get("SHA256"):
        raise ValueError("Installed module differs from its qualified native build")
    module = ctypes.CDLL(str(library))
    getter = module.infiltratr_temporal_resource_metadata_v1
    getter.restype = ctypes.POINTER(Metadata)
    pointer = getter()
    if not pointer:
        raise ValueError("Installed module returned no metadata")
    metadata = pointer.contents
    if (metadata.struct_size != ctypes.sizeof(Metadata) or metadata.abi_version != 1 or
            metadata.kind != (1 if entry["kind"] == "calendar" else 2) or
            metadata.id.decode() != entry["id"] or metadata.name.decode() != entry["name"]):
        raise ValueError("Installed module's ABI or provider identity is incorrect")
    getattr(module, "infiltratr_temporal_" + entry["kind"] + "_provider_v1")


def install_entry(entry):
    api = f"https://api.github.com/repos/Infiltrator-Projects/{entry['repository']}/releases/latest"
    with open_url(api) as response:
        data = response.read(4 * 1024 * 1024 + 1)
    if len(data) > 4 * 1024 * 1024:
        raise ValueError("Release metadata is too large")
    version, url, digest, size = release_installer(entry, json.loads(data))
    with tempfile.TemporaryDirectory(dir=STATUS_DIR, prefix="temporal-resource-") as temporary:
        installer = Path(temporary) / "provider.run"
        download_installer(url, installer, digest, size)
        required([installer, "--profile", "aggressive", "--system-package-mode"])
        verify_installation(entry, version)


def main():
    if os.geteuid() != 0:
        raise RuntimeError("The InfiltratorOS provisioning worker requires root")
    entries = catalogue()
    STATUS_DIR.mkdir(mode=0o755, parents=True, exist_ok=True)
    with (STATUS_DIR / "temporal-resources.lock").open("a") as own:
        try:
            fcntl.flock(own, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("Calendar and clock installation is already running.")
            return
        with (STATUS_DIR / "native-install.lock").open("a") as global_lock:
            write_status("queued", 2, "Waiting for another native installation", "Calendars and clocks will start when the current native build finishes.")
            fcntl.flock(global_lock, fcntl.LOCK_EX)
            os.environ["DEBIAN_FRONTEND"] = "noninteractive"
            os.environ["TMPDIR"] = str(STATUS_DIR)
            write_status("running", 5, "Preparing calendar and clock installation", "Waiting for the package transaction to finish.")
            required(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300", "check"])
            required(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300", "update"])
            required(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=300", "install", "-y", "--no-install-recommends",
                      "build-essential", "binutils", "pkg-config", "libglib2.0-dev", "libicu-dev", "python3"])
            failures = []
            for index, entry in enumerate(entries):
                write_status("running", 8 + index * 90 // len(entries), f"Optimising {entry['name']}", f"Resource {index + 1} of {len(entries)} is being built for this computer.")
                try:
                    install_entry(entry)
                    print("Verified native installation: " + entry["repository"], flush=True)
                except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
                    failures.append(entry["repository"])
                    print(entry["repository"] + " FAILED: " + str(error), file=sys.stderr, flush=True)
            if failures:
                raise RuntimeError(f"{len(failures)} resource installations failed: " + ", ".join(failures))
    write_status("complete", 100, "Calendars and clocks installed", "All 31 calendars and 23 clocks are installed and optimised for this computer.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        write_status("failed", 100, "Calendar and clock installation failed", error)
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
