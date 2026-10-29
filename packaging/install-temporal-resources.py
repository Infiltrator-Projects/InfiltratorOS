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
import stat
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

STATUS_ROOT = Path("/Temporary/InfiltratorOS")
STATUS_DIR = STATUS_ROOT
STATUS_PATH = None  # Set per provider and per installation run in main().
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


def installed_provenance(entry, root=Path("/"), query=True):
    """Support both the original BUILD-INFO and the two-file provider package.

    The two-file package stores its native-build attestation in dpkg control
    fields rather than writing developer records into the native filesystem.
    """
    docs = root / "System/Resources/Documentation" / entry["repository"]
    legacy = docs / "BUILD-INFO"
    if legacy.is_file():
        return dict(line.split(": ", 1) for line in legacy.read_text().splitlines()
                    if ": " in line)
    guide = docs / (entry["name"] + ".pdf")
    if not guide.is_file() or not guide.read_bytes()[:5] == b"%PDF-":
        raise ValueError("Illustrated calendar PDF is missing or invalid")
    if not query:
        raise ValueError("Two-file provider package provenance needs dpkg metadata")
    package_status = subprocess.check_output(
        ["/usr/bin/dpkg-query", "-s", entry["package"]], text=True)
    fields = dict(line.split(": ", 1) for line in package_status.splitlines()
                  if ": " in line)
    return {
        "Version": fields.get("Version", "").removesuffix("+native1"),
        "Package version": fields.get("Version", ""),
        "Repository": entry["repository"],
        "Profile": fields.get("X-Infiltrator-Native-Profile", ""),
        "LTO": fields.get("X-Infiltrator-LTO", ""),
        "PGO": ("trained two-pass profile" if fields.get("X-Infiltrator-PGO") == "trained-two-pass"
                else fields.get("X-Infiltrator-PGO", "")),
        "Resource": entry["resource"],
        "C flags": fields.get("X-Infiltrator-Cflags", ""),
        "Source revision": fields.get("X-Infiltrator-Source-Revision", ""),
        "SHA256": fields.get("X-Infiltrator-Resource-SHA256", ""),
    }


def verify_installation(entry, version, root=Path("/"), query=True):
    if query:
        identity = subprocess.check_output(["/usr/bin/dpkg-query", "-W", "-f=${Status}\n${Version}\n", entry["package"]], text=True)
        if identity.splitlines() != ["install ok installed", version + "+native1"]:
            raise ValueError("Native Debian package was not installed at the selected version")
    library = root / entry["resource"].lstrip("/")
    info = installed_provenance(entry, root, query)
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


def native_installed(entry, version, release):
    """Accept a cached native provider only after checking its real installed ABI."""
    revision = release.get("target_commitish")
    if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
        print("Native cache unavailable: release commit is not pinned.", flush=True)
        return False
    library = Path(entry["resource"])
    docs = Path("/System/Resources/Documentation") / entry["repository"]
    legacy = docs / "BUILD-INFO"
    guide = docs / (entry["name"] + ".pdf")
    record = legacy if legacy.is_file() else guide
    try:
        for path in (Path("/System"), Path("/System/Resources"), library.parent,
                     docs, library, record):
            metadata = path.lstat()
            if (stat.S_ISLNK(metadata.st_mode) or metadata.st_uid != 0 or
                    metadata.st_mode & 0o022):
                raise ValueError("Installed native resource is not trusted: " + str(path))
        if not stat.S_ISREG(library.stat().st_mode) or not stat.S_ISREG(record.stat().st_mode):
            raise ValueError("Installed native resource or PDF is not a regular file")
        for path in (library, record):
            owner = subprocess.check_output(["/usr/bin/dpkg-query", "-S", str(path)],
                                            text=True)
            if not owner.startswith(entry["package"] + ":"):
                raise ValueError("Installed native file has unexpected package ownership")
        verify_installation(entry, version)
        provenance = installed_provenance(entry)
        if provenance.get("Source revision") != revision:
            raise ValueError("Installed native source differs from published release")
        return True
    except (OSError, ValueError, UnicodeError, subprocess.SubprocessError) as error:
        print("Native cache miss for " + entry["repository"] + ": " + str(error), flush=True)
        return False


def ensure_dependencies(entry):
    ready = STATUS_DIR / "dependencies-ready"
    if ready.exists():
        return
    write_status("running", 5, "Preparing native build dependencies", entry["name"])
    # The Mint apt frontend may still own the dpkg lock; never bypass it.
    required(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "check"])
    required(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "update"])
    required(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "install",
              "-y", "--no-install-recommends", "build-essential", "binutils",
              "pkg-config", "libglib2.0-dev", "libicu-dev", "python3"])
    ready.touch(mode=0o644)


def install_entry(entry):
    release_dir = os.environ.get("INFILTRATOROS_RELEASE_DIR")
    if release_dir:
        data = (Path(release_dir) / (entry["repository"] + ".json")).read_bytes()
    else:
        api = f"https://api.github.com/repos/Infiltrator-Projects/{entry['repository']}/releases/latest"
        with open_url(api) as response:
            data = response.read(4 * 1024 * 1024 + 1)
    if len(data) > 4 * 1024 * 1024:
        raise ValueError("Release metadata is too large")
    release = json.loads(data)
    version, url, digest, size = release_installer(entry, release)
    if native_installed(entry, version, release):
        print("Already installed and verified: " + entry["repository"], flush=True)
        return True
    ensure_dependencies(entry)
    write_status("running", 10, "Optimising " + entry["name"],
                 "Running its independent checksum-verified native installer.")
    with tempfile.TemporaryDirectory(dir=STATUS_DIR, prefix="temporal-resource-") as temporary:
        installer = Path(temporary) / "provider.run"
        download_installer(url, installer, digest, size)
        required([installer, "--profile", "aggressive", "--system-package-mode"])
        verify_installation(entry, version)
    return False


def main():
    # Each published provider is installed by its own process, status record
    # and graphical progress window. Shared source code is not a combined job.
    entries = catalogue()
    if sys.argv[1:] == ["--list"]:
        for entry in entries:
            print(entry["repository"].lower())
        return
    if len(sys.argv) != 2:
        raise ValueError("Usage: install-temporal-resources PROVIDER_SLUG")
    slug = sys.argv[1]
    entry = next((item for item in entries if item["repository"].lower() == slug), None)
    if entry is None:
        raise ValueError("Provider is not in the signed calendar/clock catalogue")
    if os.geteuid() != 0:
        raise RuntimeError("The InfiltratorOS provisioning worker requires root")
    global STATUS_DIR, STATUS_PATH
    runtime = os.environ.get("INFILTRATOROS_INSTALL_RUNTIME")
    if runtime:
        directory = Path(runtime)
        if directory.name.startswith("native-worker.") is False or directory.parent != STATUS_ROOT:
            raise ValueError("Invalid InfiltratorOS installation instance")
        STATUS_DIR = directory / "status"
    else:
        STATUS_DIR = STATUS_ROOT
    STATUS_PATH = STATUS_DIR / (slug + "-install.status")
    STATUS_ROOT.mkdir(mode=0o755, parents=True, exist_ok=True)
    STATUS_DIR.mkdir(mode=0o755, parents=True, exist_ok=True)
    with (STATUS_ROOT / (slug + ".lock")).open("a") as own:
        # A second OS installation must wait for the previous invocation of
        # this provider; it must never silently report the first run's status.
        fcntl.flock(own, fcntl.LOCK_EX)
        with (STATUS_ROOT / "native-install.lock").open("a") as global_lock:
            write_status("queued", 2, "Waiting for another native installation",
                         entry["name"] + " will start when the current native build finishes.")
            fcntl.flock(global_lock, fcntl.LOCK_EX)
            os.environ["DEBIAN_FRONTEND"] = "noninteractive"
            os.environ["TMPDIR"] = str(STATUS_DIR)
            write_status("running", 5, "Checking installed native build", entry["name"])
            reused = install_entry(entry)
            print("Verified native installation: " + entry["repository"], flush=True)
    write_status("complete", 100,
                 entry["name"] + (" already installed" if reused else " installed"),
                 ("Existing current native build verified; no compilation required."
                  if reused else "This provider has been built, installed and verified independently."))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        if STATUS_PATH is not None:
            write_status("failed", 100, "Calendar or clock installation failed", error)
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
