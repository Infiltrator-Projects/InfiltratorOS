#!/usr/bin/python3 -u
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install and verify independently published FAT12/FAT16/FAT32/OFS native .so packages."""
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
STATUS_PATH = None
CATALOGUE = []  # @EMBED_FILESYSTEM_CATALOGUE@

def catalogue():
    entries = CATALOGUE or json.loads((Path(__file__).parent / "filesystem-resources.json").read_text())
    expected = {"fat12", "fat16", "fat32", "ofs"}
    if len(entries) != 4 or {x["id"] for x in entries} != expected:
        raise ValueError("Exactly FAT12, FAT16, FAT32 and OFS must be specified")
    for item in entries:
        if item["id"] == "ofs":
            if item != {"id": "ofs", "name": "Amiga Original File System",
                        "repository": "Filesystem-OFS", "package": "infiltrator-filesystem-ofs",
                        "resource": "/System/Filesystems/OFS.so"}:
                raise ValueError("Invalid OFS catalogue identity")
            continue
        width = item["id"][3:]
        if item != {"id": "fat" + width, "name": "FAT" + width,
                    "repository": "Filesystem-FAT" + width,
                    "package": "infiltrator-filesystem-fat" + width,
                    "resource": "/System/Filesystems/FAT" + width + ".so"}:
            raise ValueError("Invalid filesystem catalogue identity")
    return entries

def status(state, number, label, message):
    STATUS_DIR.mkdir(mode=0o755, parents=True, exist_ok=True)
    clean = lambda x: str(x).replace("\n", " ").replace("\r", " ")
    with tempfile.NamedTemporaryFile(mode="w", dir=STATUS_DIR, prefix="filesystem-status-", delete=False) as f:
        f.write(f"state={clean(state)}\nprogress={number}\nstage={clean(label)}\ndetail={clean(message)}\n")
        tmp = Path(f.name)
    tmp.chmod(0o644)
    tmp.replace(STATUS_PATH)

def run(args):
    print("+ " + " ".join(map(str, args)), flush=True)
    subprocess.run([str(x) for x in args], check=True)

def open_url(url):
    headers = {"User-Agent": "InfiltratorOS-filesystem-installer",
               "Accept": "application/vnd.github+json"}
    token = os.getenv("GITHUB_TOKEN")
    if token and urllib.parse.urlsplit(url).hostname == "api.github.com":
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(url, headers=headers)
    for attempt in range(4):
        try:
            return urllib.request.urlopen(request, timeout=120)
        except urllib.error.HTTPError as error:
            if error.code in (403, 429) and error.headers.get("X-RateLimit-Remaining") == "0":
                delay = max(1, min(1800, int(error.headers.get("X-RateLimit-Reset", time.time()+60))-int(time.time())+2))
                time.sleep(delay)
            elif error.code >= 500 and attempt < 3:
                time.sleep(2**attempt)
            else:
                raise
        except (urllib.error.URLError, TimeoutError):
            if attempt == 3:
                raise
            time.sleep(2**attempt)
    raise RuntimeError("GitHub release lookup failed")

def release_installer(entry, release):
    if not isinstance(release, dict) or release.get("draft") or release.get("prerelease"):
        raise ValueError("Not a published stable filesystem release")
    tag = release.get("tag_name", "")
    if not isinstance(tag, str) or not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag):
        raise ValueError("Not a canonical version tag")
    version = tag[1:]
    run_name = f"{entry['repository'].lower()}-{version}-native-installer.run"
    deb_name = f"{entry['package']}_{version}_amd64.deb"
    assets = release.get("assets")
    expected = {run_name, deb_name}
    bits = entry["id"][3:]
    milestone = (0, 1, 2) if bits == "32" else (0, 1, 3)
    if entry["id"] == "ofs":
        expected.update({"OFS.so", "SHA256SUMS"})
        if tuple(map(int, version.split("."))) >= (0, 1, 1):
            expected.add("ofs-linux-6.12.107.ko")
    elif tuple(map(int, version.split("."))) >= milestone:
        expected.update({f"FAT{bits}.efi", f"fat{bits}-linux-6.12.107.ko"})
    if (not isinstance(assets, list) or len(assets) != len(expected) or
            {x.get("name") for x in assets if isinstance(x, dict)} != expected):
        raise ValueError("Filesystem release is missing required native driver assets")
    asset = next(x for x in assets if x["name"] == run_name)
    url = f"https://github.com/Infiltrator-Projects/{entry['repository']}/releases/download/{tag}/{run_name}"
    digest = asset.get("digest", "")
    if (asset.get("browser_download_url") != url or not isinstance(digest, str) or
        not re.fullmatch(r"sha256:[0-9a-f]{64}", digest) or
        not isinstance(asset.get("size"), int) or not 0 < asset["size"] < 512*1024*1024):
        raise ValueError("Untrusted or unchecksummed filesystem native installer")
    return version, url, digest[7:], asset["size"]

def download(url, path, digest, length):
    count, sha = 0, hashlib.sha256()
    with open_url(url) as src, path.open("wb") as dst:
        while True:
            data = src.read(1024*1024)
            if not data:
                break
            count += len(data)
            if count > length:
                raise ValueError("Installer grew beyond declared size")
            sha.update(data)
            dst.write(data)
    if count != length or sha.hexdigest() != digest:
        raise ValueError("Installer SHA256 or size mismatch")
    path.chmod(0o700)

def verify(entry, version, root=Path("/"), query=True):
    if query:
        expression = "-f=" + chr(36) + "{Status}\n" + chr(36) + "{Version}\n"
        package = subprocess.check_output(["/usr/bin/dpkg-query", "-W", expression, entry["package"]], text=True)
        if package.splitlines() != ["install ok installed", version + "+native1"]:
            raise ValueError("Expected native FAT package missing or wrong version")
    module = root / entry["resource"].lstrip("/")
    info_path = root / "System/Resources/Documentation" / entry["repository"] / "BUILD-INFO"
    data = dict(line.split(": ", 1) for line in info_path.read_text().splitlines() if ": " in line)
    if any(data.get(k) != v for k, v in {
        "Version": version, "Package": entry["package"],
        "Module": entry["resource"], "Profile": "aggressive"}.items()):
        raise ValueError("Installed native filesystem build record mismatch")
    flags = data.get("C flags", "")
    if "-O3" not in flags or "-march=native" not in flags or "-flto" not in flags:
        raise ValueError("Module not compiled with the advertised native flags")
    if entry["id"] == "ofs":
        if (data.get("LTO") != "enabled" or
                data.get("PGO") != "trained two-pass profile (OFS V1/V2/V3 boundary and failure fixtures)"):
            raise ValueError("OFS requires measured two-pass PGO and LTO")
    elif tuple(map(int, version.split("."))) >= (0, 1, 2):
        if (data.get("LTO") != "enabled" or
                data.get("PGO") != "trained two-pass profile (V1 and V3/V4 fixtures)"):
            raise ValueError("FAT 0.1.2+ requires measured two-pass PGO and LTO")
    if hashlib.sha256(module.read_bytes()).hexdigest() != data.get("SHA256"):
        raise ValueError("Installed module differs from its build record")
    if query:
        for p in (root / "System", root / "System/Filesystems", module):
            if p.is_symlink() or p.stat().st_uid != 0 or p.stat().st_mode & 0o022:
                raise ValueError("Module directory/file ownership is not trusted")
        owner = subprocess.check_output(["/usr/bin/dpkg-query", "-S", entry["resource"]], text=True)
        if not owner.startswith(entry["package"] + ":"):
            raise ValueError("Filesystem module owned by wrong package")
    readelf = subprocess.check_output(["/usr/bin/readelf", "-Ws", str(module)], text=True)
    symbols = ["infiltrator_filesystem_module_v1",
               "infiltrator_filesystem_module_v3",
               "infiltrator_filesystem_module_v4"]
    if entry["id"] in {"fat12", "fat16"}:
        symbols.append("infiltrator_filesystem_defrag_v1")
    if entry["id"] == "ofs":
        symbols = ["infiltrator_filesystem_module_v1", "infiltrator_filesystem_module_v2",
                   "infiltrator_filesystem_module_v3", "infiltrator_filesystem_boot_v1",
                   "infiltrator_filesystem_defrag_v2"]
    for symbol in symbols:
        if symbol not in readelf:
            raise ValueError("Missing filesystem or defragmentation ABI")
    identity = subprocess.check_output(["/usr/bin/readelf", "-p", ".ifsmeta", str(module)], text=True)
    for api in ((1, 2, 3) if entry["id"] == "ofs" else (1, 3, 4)):
        if f"IFS-MODULE:{entry['id']}:{api}" not in identity:
            raise ValueError("Wrong filesystem width or ABI")
    if "(NEEDED)" in subprocess.check_output(["/usr/bin/readelf", "-d", str(module)], text=True):
        raise ValueError("Unexpected ELF runtime dependency")

def native_installed(entry, version):
    """Verify an existing native filesystem package without executing its driver."""
    try:
        verify(entry, version)
        info = Path("/System/Resources/Documentation") / entry["repository"] / "BUILD-INFO"
        for path in (Path("/System/Resources"), info.parent, info):
            metadata = path.lstat()
            if metadata.st_uid != 0 or metadata.st_mode & 0o022 or path.is_symlink():
                raise ValueError("Untrusted filesystem build record: " + str(path))
        if not stat.S_ISREG(info.stat().st_mode):
            raise ValueError("Filesystem build record is not a regular file")
        owner = subprocess.check_output(["/usr/bin/dpkg-query", "-S", str(info)], text=True)
        if not owner.startswith(entry["package"] + ":"):
            raise ValueError("Filesystem build record belongs to another package")
        return True
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("Native cache miss for " + entry["repository"] + ": " + str(error), flush=True)
        return False


def ensure_dependencies(entry):
    ready = STATUS_DIR / "filesystem-dependencies-ready"
    if ready.exists():
        return
    status("running", 5, "Preparing filesystem build prerequisites", entry["name"])
    run(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "check"])
    run(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "update"])
    run(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "install",
         "-y", "--no-install-recommends", "build-essential", "binutils", "python3"])
    ready.touch(mode=0o644)


def install(entry):
    release_dir = os.environ.get("INFILTRATOROS_RELEASE_DIR")
    if release_dir:
        payload = (Path(release_dir) / (entry["repository"] + ".json")).read_bytes()
    else:
        url = f"https://api.github.com/repos/Infiltrator-Projects/{entry['repository']}/releases/latest"
        with open_url(url) as response:
            payload = response.read(4*1024*1024+1)
    if len(payload) > 4*1024*1024:
        raise ValueError("Oversized GitHub release metadata")
    version, location, digest, length = release_installer(entry, json.loads(payload))
    if native_installed(entry, version):
        print("Already installed and verified: " + entry["repository"], flush=True)
        return True
    ensure_dependencies(entry)
    status("running", 12, "Compiling " + entry["name"],
           "Using its independent checksum-verified source installer")
    with tempfile.TemporaryDirectory(dir=STATUS_DIR, prefix="filesystem-native-") as folder:
        installer = Path(folder) / "filesystem.run"
        download(location, installer, digest, length)
        run([installer, "--profile", "aggressive", "--system-package-mode"])
        verify(entry, version)
    return False

def main():
    entries = catalogue()
    if sys.argv[1:] == ["--list"]:
        for entry in entries:
            print(entry["repository"].lower())
        return
    if len(sys.argv) != 2:
        raise ValueError("Usage: install-filesystem-resources filesystem-fat12|filesystem-fat16|filesystem-fat32|filesystem-ofs")
    slug = sys.argv[1]
    entry = next((item for item in entries if item["repository"].lower() == slug), None)
    if entry is None:
        raise ValueError("Not a signed filesystem component")
    if os.geteuid() != 0:
        raise RuntimeError("Filesystem provisioning requires root")
    global STATUS_DIR, STATUS_PATH
    runtime = os.environ.get("INFILTRATOROS_INSTALL_RUNTIME")
    if runtime:
        directory = Path(runtime)
        if not directory.name.startswith("native-worker.") or directory.parent != STATUS_ROOT:
            raise ValueError("Invalid installation runtime")
        STATUS_DIR = directory / "status"
    else:
        STATUS_DIR = STATUS_ROOT
    STATUS_PATH = STATUS_DIR / (slug + "-install.status")
    STATUS_ROOT.mkdir(mode=0o755, parents=True, exist_ok=True)
    STATUS_DIR.mkdir(mode=0o755, parents=True, exist_ok=True)
    with (STATUS_ROOT / (slug + ".lock")).open("a") as own:
        fcntl.flock(own, fcntl.LOCK_EX)
        with (STATUS_ROOT / "native-install.lock").open("a") as global_lock:
            status("queued", 2, "Waiting for another native installation", entry["name"])
            fcntl.flock(global_lock, fcntl.LOCK_EX)
            os.environ["DEBIAN_FRONTEND"] = "noninteractive"
            os.environ["TMPDIR"] = str(STATUS_DIR)
            status("running", 5, "Checking installed native filesystem", entry["name"])
            reused = install(entry)
    status("complete", 100,
           entry["name"] + (" already installed" if reused else " installed"),
           ("Existing current filesystem module and ABI verified; no compilation required"
            if reused else "Independent filesystem module and ABI verified"))

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        if STATUS_PATH is not None:
            status("failed", 100, "Filesystem native installation failed", exc)
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
