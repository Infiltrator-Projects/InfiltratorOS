#!/usr/bin/python3 -u
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install and verify independently published FAT12/16/32, OFS and exFAT native SO packages."""
import fcntl
from concurrent.futures import ThreadPoolExecutor, as_completed
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
ADDITIONAL = []  # @EMBED_FILESYSTEM_ADDITIONAL@

def catalogue():
    entries = CATALOGUE or json.loads((Path(__file__).parent / "filesystem-resources.json").read_text())
    expected = {"fat12", "fat16", "fat32", "ofs", "exfat"}
    if len(entries) != 5 or {x["id"] for x in entries} != expected:
        raise ValueError("Exactly FAT12, FAT16, FAT32, OFS and exFAT must be specified")
    for item in entries:
        if item["id"] == "exfat":
            if item != {"id": "exfat", "name": "exFAT",
                        "repository": "Filesystem-exFAT", "package": "infiltrator-filesystem-exfat",
                        "resource": "/System/Filesystems/exFAT.so"}:
                raise ValueError("Invalid exFAT catalogue identity")
            continue
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

def additional_catalogue():
    entries = ADDITIONAL or json.loads((Path(__file__).parent / "filesystem-additional.json").read_text())
    expected = [["ntfs","Filesystem-NTFS","NTFS","infiltrator-filesystem-ntfs","/System/Filesystems/NTFS.so","infiltrator_filesystem_ntfs_v1"],["ext2","Filesystem-EXT2","EXT2","infiltrator-filesystem-ext2","/System/Filesystems/EXT2.so","infiltrator_filesystem_ext_v1"],["ext3","Filesystem-EXT3","EXT3","infiltrator-filesystem-ext3","/System/Filesystems/EXT3.so","infiltrator_filesystem_ext_v1"],["ext4","Filesystem-EXT4","EXT4","infiltrator-filesystem-ext4","/System/Filesystems/EXT4.so","infiltrator_filesystem_ext_v1"],["xfs","Filesystem-XFS","XFS","infiltrator-filesystem-xfs","/System/Filesystems/XFS.so","infiltrator_filesystem_xfs_v1"],["btrfs","Filesystem-Btrfs","BTRFS","infiltrator-filesystem-btrfs","/System/Filesystems/BTRFS.so","infiltrator_filesystem_btrfs_v1"],["ffs","Filesystem-FFS","FFS","infiltrator-filesystem-ffs","/System/Filesystems/FFS.so","infiltrator_filesystem_ffs_defrag_v1"],["sfs","Filesystem-SFS","SFS","infiltrator-filesystem-sfs","/System/Filesystems/SFS.so","infiltrator_filesystem_sfs_v1"],["pfs3","Filesystem-PFS3","PFS3","infiltrator-filesystem-pfs3","/System/Filesystems/PFS3.so","infiltrator_filesystem_pfs3_v1"],["hfs","Filesystem-HFS","HFS","infiltrator-filesystem-hfs","/System/Filesystems/HFS.so","infiltrator_filesystem_hfs_v1"],["hfsplus","Filesystem-HFSPlus","HFSPLUS","infiltrator-filesystem-hfsplus","/System/Filesystems/HFSPLUS.so","infiltrator_filesystem_hfsplus_v1"],["minix","Filesystem-Minix","MINIX","infiltrator-filesystem-minix","/System/Filesystems/MINIX.so","infiltrator_filesystem_minix_defrag_v1"],["ufs2","Filesystem-UFS2","UFS2","infiltrator-filesystem-ufs2","/System/Filesystems/UFS2.so","infiltrator_filesystem_ufs_v1"],["zfs","Filesystem-ZFS","ZFS","infiltrator-filesystem-zfs","/System/Filesystems/ZFS.so","infiltrator_filesystem_zfs_v1"],["apfs","Filesystem-APFS","APFS","infiltrator-filesystem-apfs","/System/Filesystems/APFS.so","infiltrator_filesystem_apfs_v1"],["swap","Filesystem-Swap","SWAP","infiltrator-filesystem-swap","/System/Filesystems/SWAP.so","infiltrator_filesystem_swap_v1"]]
    actual = [[e.get("id"), e.get("repository"), e.get("name"), e.get("package"),
               e.get("resource"), e.get("abi")] for e in entries]
    if len(actual) != 16 or actual != expected:
        raise ValueError("Incomplete or modified sixteen-provider filesystem catalogue")
    return entries

def all_catalogue():
    entries = catalogue() + additional_catalogue()
    if len(entries) != 21 or len(set(x["resource"] for x in entries)) != 21:
        raise ValueError("Incomplete or overlapping filesystem catalogue")
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
    if entry["id"] == "exfat" and tuple(map(int, version.split("."))) < (0, 1, 3):
        raise ValueError("exFAT native RUN requires 0.1.3 or newer; 0.1.2 fails SIGPIPE")
    run_name = f"{entry['repository'].lower()}-{version}-native-installer.run"
    deb_name = f"{entry['package']}_{version}_amd64.deb"
    assets = release.get("assets")
    expected = {run_name, deb_name}
    # Standard release contract: portable DEB embeds SO+PDF; RUN embeds
    # source and generates/installs a CPU-native DEB embedding native SO+PDF.
    # Neither distribution path requires a separately published bare SO.
    bits = entry["id"][3:]
    milestone = (0, 1, 2) if bits == "32" else (0, 1, 3)
    if entry["id"] == "exfat":
        expected.update({"exFAT.so", "exfat-linux-6.12.107.ko", "SHA256SUMS"})
    elif entry["id"] == "ofs":
        expected.update({"OFS.so", "SHA256SUMS"})
        if tuple(map(int, version.split("."))) >= (0, 1, 1):
            expected.add("ofs-linux-6.12.107.ko")
    elif entry["id"] in {"fat12", "fat16", "fat32"} and tuple(map(int, version.split("."))) >= milestone:
        expected.update({f"FAT{bits}.efi", f"fat{bits}-linux-6.12.107.ko"})
    if (not isinstance(assets, list) or len(assets) != len(expected) or
            {x.get("name") for x in assets if isinstance(x, dict)} != expected):
        raise ValueError("Filesystem release is missing required native driver assets")
    # A release is a complete provenance set: reject malformed metadata for
    # *every* declared asset, not just the executable native installer.
    for item in assets:
        name = item["name"]
        asset_url = (f"https://github.com/Infiltrator-Projects/"
                     f"{entry['repository']}/releases/download/{tag}/{name}")
        if (item.get("browser_download_url") != asset_url or
                not re.fullmatch(r"sha256:[0-9a-f]{64}", item.get("digest", "") or "") or
                not isinstance(item.get("size"), int) or
                not 0 < item["size"] < 512 * 1024 * 1024):
            raise ValueError("Invalid filesystem release asset metadata: " + name)
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
    """Verify an installed SO + PDF against Debian-native source/build data."""
    if query:
        expression = "-f=" + chr(36) + "{Status}\n" + chr(36) + "{Version}\n"
        identity = subprocess.check_output(
            ["/usr/bin/dpkg-query", "-W", expression, entry["package"]], text=True)
        if identity.splitlines() != ["install ok installed", version + "+native1"]:
            raise ValueError("Expected native filesystem package missing or wrong version")
    module = root / entry["resource"].lstrip("/")
    guide = root / "System/Resources/Documentation" / entry["repository"] / (
        entry["resource"].rsplit("/", 1)[1].removesuffix(".so") + ".pdf")
    if guide.is_symlink() or not guide.is_file():
        raise ValueError("Required filesystem illustrated PDF is missing")
    with guide.open("rb") as file:
        if file.read(5) != b"%PDF-":
            raise ValueError("Invalid filesystem illustrated PDF")
    report = subprocess.check_output(
        ["/usr/bin/dpkg-query", "-s", entry["package"]], text=True)
    data = dict(line.split(": ", 1) for line in report.splitlines() if ": " in line)
    expected = {
        "Version": version + "+native1",
        "X-Infiltrator-Native-Profile": "aggressive",
        "X-Infiltrator-LTO": "enabled",
        "X-Infiltrator-PGO": "trained-two-pass",
    }
    for key, expected_value in expected.items():
        if data.get(key) != expected_value:
            raise ValueError("Native filesystem build metadata mismatch: " + key)
    flags = data.get("X-Infiltrator-Cflags", "").split()
    optimisations = [x for x in flags if re.fullmatch(r"-O(?:[0-9]+|s|g|fast)", x)]
    if not optimisations or optimisations[-1] != "-O3" or "-march=native" not in flags or not any(
            item == "-flto" or item.startswith("-flto=") for item in flags):
        raise ValueError("Module not compiled with the advertised native flags")
    if not re.fullmatch(r"[0-9a-f]{40}", data.get("X-Infiltrator-Source-Revision", "")):
        raise ValueError("Missing filesystem source revision in Debian control")
    if hashlib.sha256(module.read_bytes()).hexdigest() != data.get("X-Infiltrator-Resource-SHA256"):
        raise ValueError("Installed filesystem SO differs from Debian package hash")
    if hashlib.sha256(guide.read_bytes()).hexdigest() != data.get("X-Infiltrator-PDF-SHA256"):
        raise ValueError("Installed filesystem PDF differs from Debian package hash")
    if query:
        for p in (root / "System", root / "System/Filesystems", module,
                  root / "System/Resources", guide.parent, guide):
            metadata = p.lstat()
            if metadata.st_uid != 0 or metadata.st_mode & 0o022 or p.is_symlink():
                raise ValueError("Untrusted native filesystem path: " + str(p))
        for path in (module, guide):
            owner = subprocess.check_output(["/usr/bin/dpkg-query", "-S", str(path)], text=True)
            if not owner.startswith(entry["package"] + ":"):
                raise ValueError("Filesystem payload owned by wrong package")
    readelf = subprocess.check_output(["/usr/bin/readelf", "-Ws", str(module)], text=True)
    symbols = ([entry["abi"]] if entry.get("abi") else
               ["infiltrator_filesystem_module_v1",
                "infiltrator_filesystem_module_v3",
                "infiltrator_filesystem_module_v4"])
    if entry["id"] in {"fat12", "fat16"}:
        symbols.append("infiltrator_filesystem_defrag_v1")
    if entry["id"] == "exfat":
        symbols = ["infiltrator_filesystem_module_v3",
                   "infiltrator_filesystem_module_v4",
                   "infiltrator_filesystem_module_v5"]
    if entry["id"] == "ofs":
        symbols = ["infiltrator_filesystem_module_v1", "infiltrator_filesystem_module_v2",
                   "infiltrator_filesystem_module_v3", "infiltrator_filesystem_boot_v1",
                   "infiltrator_filesystem_defrag_v2"]
    for symbol in symbols:
        if symbol not in readelf:
            raise ValueError("Missing filesystem or defragmentation ABI")
    if not entry.get("abi"):
        identity = subprocess.check_output(["/usr/bin/readelf", "-p", ".ifsmeta", str(module)], text=True)
        for api in ((1, 2, 3) if entry["id"] == "ofs" else
                    (3, 4, 5) if entry["id"] == "exfat" else (1, 3, 4)):
            if f"IFS-MODULE:{entry['id']}:{api}" not in identity:
                raise ValueError("Wrong filesystem width or ABI")
    validate_runtime_dependencies(entry, subprocess.check_output(
        ["/usr/bin/readelf", "-d", str(module)], text=True))


def validate_runtime_dependencies(entry, dynamic_section):
    """Allow only exFAT's declared Linux runtime dependencies.

    FAT and OFS remain independently freestanding. Never use ldd or execute
    downloaded module code as part of verification.
    """
    needed = []
    for line in dynamic_section.splitlines():
        if "(NEEDED)" not in line:
            continue
        marker = "Shared library: ["
        if marker not in line or "]" not in line.split(marker, 1)[1]:
            raise ValueError("Malformed ELF runtime dependency record")
        needed.append(line.split(marker, 1)[1].split("]", 1)[0])
    if entry.get("abi"):
        allowed = {"libc.so.6", "libcrypto.so.3", "libsqlite3.so.0",
                   "libstdc++.so.6", "libgcc_s.so.1", "libm.so.6",
                   "libpthread.so.0", "ld-linux-x86-64.so.2"}
        unknown = set(needed) - allowed
        if unknown:
            raise ValueError("Unexpected filesystem runtime dependency: " +
                             ", ".join(sorted(unknown)))
    elif entry["id"] != "exfat" and needed:
        raise ValueError("Unexpected ELF runtime dependency")
    if entry["id"] == "exfat":
        # glibc may record its own architecture-specific dynamic loader as
        # DT_NEEDED when the native LTO/PGO build links against libc.
        # This is a system runtime component, not a third-party filesystem
        # implementation. Retain the strict allowlist for all other SONAMEs.
        allowed = {"libc.so.6", "libcrypto.so.3", "libsqlite3.so.0",
                   "libm.so.6", "libpthread.so.0", "ld-linux-x86-64.so.2"}
        unknown = set(needed) - allowed
        if unknown:
            raise ValueError("Unexpected exFAT runtime dependency: " + ", ".join(sorted(unknown)))


def native_installed(entry, version):
    """Verify the actual two-file package, without executing the driver."""
    try:
        verify(entry, version)
        return True
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("Native cache miss for " + entry["repository"] + ": " + str(error), flush=True)
        return False


def cleanup_legacy_docs(entry, root=Path("/"), owner_lookup=None):
    """Remove known old unowned records; leave all foreign/package-owned files."""
    folder = root / "System/Resources/Documentation" / entry["repository"]
    manual = folder / (entry["resource"].rsplit("/", 1)[1].removesuffix(".so") + ".pdf")
    if not manual.is_file() or manual.is_symlink():
        raise ValueError("Filesystem PDF must exist before any legacy cleanup")
    with manual.open("rb") as file:
        if file.read(5) != b"%PDF-":
            raise ValueError("Invalid filesystem PDF before cleanup")
    if root == Path("/"):
        for d in (root / "System", root / "System/Resources",
                  root / "System/Resources/Documentation", folder):
            meta = d.lstat()
            if not stat.S_ISDIR(meta.st_mode) or meta.st_uid != 0 or meta.st_mode & 0o022:
                raise ValueError("Unsafe filesystem documentation directory")
    names = {"BUILD-INFO", "README.md", "MIGRATION-PROVENANCE.md", "LICENSE"}
    removed = 0
    for item in folder.iterdir():
        if item.name not in names or item.is_symlink() or not item.is_file():
            continue
        if root == Path("/") and item.stat().st_uid != 0:
            continue
        if owner_lookup is None:
            owned = subprocess.run(["/usr/bin/dpkg-query", "-S", str(item)],
                                   capture_output=True, text=True, check=False)
            if owned.returncode not in (0, 1) or owned.returncode == 0:
                print("Preserving package-owned filesystem documentation: " + str(item), flush=True)
                continue
        elif owner_lookup(item):
            continue
        item.unlink()
        removed += 1
    if removed:
        print("Removed " + str(removed) + " unowned obsolete document(s) for " +
              entry["repository"], flush=True)
    return removed


def ensure_dependencies(entry):
    ready_name = ("filesystem-extended-dependencies-ready" if entry.get("abi") else
                  "filesystem-exfat-dependencies-ready" if entry["id"] == "exfat" else
                  "filesystem-dependencies-ready")
    ready = STATUS_DIR / ready_name
    if ready.exists():
        return
    status("running", 5, "Preparing filesystem build prerequisites", entry["name"])
    run(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "check"])
    run(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "update"])
    packages = ["build-essential", "binutils", "python3"]
    if entry["id"] == "exfat" or entry.get("abi"):
        # Additional independent SO providers also use pinned CMake toolchains.
        # The unified source compiles its own V3/V4/V5/defrag SO using CMake,
        # pinned InfiltratrCommon source, OpenSSL and SQLite development headers.
        packages.extend(("cmake", "git", "pkg-config", "libssl-dev", "libsqlite3-dev"))
        if entry["id"] == "exfat":
            packages.extend(("exfatprogs", "dosfstools"))
    run(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "install",
         "-y", "--no-install-recommends", *packages])
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
        cleanup_legacy_docs(entry)
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
        cleanup_legacy_docs(entry)
    return False


def discover_additional(release_dir):
    """Admit extra drivers only with real, complete SHA-256-bound releases.

    GitHub Actions artifacts are intentionally NOT accepted for installation.
    Missing releases remain explicitly pending, never reported as installed.
    """
    if (not release_dir.is_dir() or
            not release_dir.parent.name.startswith("native-worker.") or
            release_dir.parent.parent != STATUS_ROOT):
        raise ValueError("Invalid native release snapshot directory")
    ready = {}
    pending = {}

    def inspect(entry):
        repository = entry["repository"]
        url = ("https://api.github.com/repos/Infiltrator-Projects/" +
               repository + "/releases/latest")
        headers = {"Accept": "application/vnd.github+json",
                   "User-Agent": "InfiltratorOS-native-filesystem-provisioner"}
        token = os.environ.get("GITHUB_TOKEN")
        if token:
            headers["Authorization"] = "Bearer " + token
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=headers),
                                        timeout=12) as response:
                payload = response.read(4 * 1024 * 1024 + 1)
            if len(payload) > 4 * 1024 * 1024:
                raise ValueError("Oversized GitHub release metadata")
            release = json.loads(payload)
            release_installer(entry, release)
            return (repository, release, None)
        except (OSError, ValueError, urllib.error.URLError) as exc:
            return (repository, None, str(exc))

    with ThreadPoolExecutor(max_workers=8) as pool:
        tasks = [pool.submit(inspect, entry) for entry in additional_catalogue()]
        for task in as_completed(tasks):
            repository, release, error = task.result()
            if error is None:
                ready[repository] = release
            else:
                pending[repository] = error

    for repository in sorted(ready):
        path = release_dir / (repository + ".json")
        if path.exists():
            raise ValueError("Additional filesystem release collides with snapshot")
        path.write_text(json.dumps(ready[repository], separators=(",", ":")) + "\n")
        path.chmod(0o644)
    (release_dir.parent / "filesystems-optional.list").write_text(
        "".join(key.lower() + "\n" for key in sorted(ready)))
    (release_dir.parent / "filesystems-pending.list").write_text(
        "".join(key + ": " + pending[key].replace("\n", " ") + "\n"
                for key in sorted(pending)))
    print(f"Filesystem publication: {len(ready)} newly installable, "
          f"{len(pending)} not yet published or not qualified", flush=True)

def main():
    entries = all_catalogue()
    if sys.argv[1:] == ["--list-all"]:
        for entry in entries:
            print(entry["repository"].lower())
        return
    if len(sys.argv) == 3 and sys.argv[1] == "--discover-additional":
        if os.geteuid() != 0:
            raise RuntimeError("Native release discovery requires root")
        discover_additional(Path(sys.argv[2]))
        return
    if sys.argv[1:] == ["--list"]:
        for entry in catalogue():
            print(entry["repository"].lower())
        return
    if len(sys.argv) != 2:
        raise ValueError("Usage: --list | --list-all | --discover-additional RELEASE_DIR | filesystem-provider")
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
