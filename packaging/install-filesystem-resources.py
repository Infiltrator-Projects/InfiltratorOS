#!/usr/bin/python3 -u
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify and invoke independently released native filesystem installers."""
import fcntl
from concurrent.futures import ThreadPoolExecutor, as_completed
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

STATUS_ROOT = Path("/Temporary/InfiltratorOS")
STATUS_DIR = STATUS_ROOT
STATUS_PATH = None
CATALOGUE = []  # @EMBED_FILESYSTEM_CATALOGUE@
ADDITIONAL = []  # @EMBED_FILESYSTEM_ADDITIONAL@
KERNEL_TARGET = "6.12.107"

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
    expected = [["ntfs","Filesystem-NTFS","NTFS","infiltrator-filesystem-ntfs","/System/Filesystems/NTFS.so","infiltrator_filesystem_ntfs_v1","NTFS.pdf"],["ext2","Filesystem-EXT2","EXT2","infiltrator-filesystem-ext2","/System/Filesystems/EXT2.so","infiltrator_filesystem_module_v1","EXT2.pdf"],["ext3","Filesystem-EXT3","EXT3","infiltrator-filesystem-ext3","/System/Filesystems/EXT3.so","infiltrator_filesystem_ext_v1","EXT3.pdf"],["ext4","Filesystem-EXT4","EXT4","infiltrator-filesystem-ext4","/System/Filesystems/EXT4.so","infiltrator_filesystem_ext_v1","EXT4.pdf"],["xfs","Filesystem-XFS","XFS","infiltrator-filesystem-xfs","/System/Filesystems/XFS.so","infiltrator_filesystem_xfs_v1","XFS.pdf"],["btrfs","Filesystem-Btrfs","BTRFS","infiltrator-filesystem-btrfs","/System/Filesystems/BTRFS.so","infiltrator_filesystem_btrfs_v1","Btrfs.pdf"],["ffs","Filesystem-FFS","FFS","infiltrator-filesystem-ffs","/System/Filesystems/FFS.so","infiltrator_filesystem_ffs_defrag_v1","FFS.pdf"],["sfs","Filesystem-SFS","SFS","infiltrator-filesystem-sfs","/System/Filesystems/SFS.so","infiltrator_filesystem_sfs_v1","SFS.pdf"],["pfs3","Filesystem-PFS3","PFS3","infiltrator-filesystem-pfs3","/System/Filesystems/PFS3.so","infiltrator_filesystem_pfs3_v1","PFS3.pdf"],["hfs","Filesystem-HFS","HFS","infiltrator-filesystem-hfs","/System/Filesystems/HFS.so","infiltrator_filesystem_hfs_v1","HFS.pdf"],["hfsplus","Filesystem-HFSPlus","HFSPLUS","infiltrator-filesystem-hfsplus","/System/Filesystems/HFSPLUS.so","infiltrator_filesystem_hfsplus_v1","HFSPlus.pdf"],["minix","Filesystem-Minix","MINIX","infiltrator-filesystem-minix","/System/Filesystems/MINIX.so","infiltrator_filesystem_minix_defrag_v1","Minix.pdf"],["ufs2","Filesystem-UFS2","UFS2","infiltrator-filesystem-ufs2","/System/Filesystems/UFS2.so","infiltrator_filesystem_ufs_v1","UFS.pdf"],["zfs","Filesystem-ZFS","ZFS","infiltrator-filesystem-zfs","/System/Filesystems/ZFS.so","infiltrator_filesystem_zfs_v1","ZFS.pdf"],["apfs","Filesystem-APFS","APFS","infiltrator-filesystem-apfs","/System/Filesystems/APFS.so","infiltrator_filesystem_apfs_v1","APFS.pdf"],["swap","Filesystem-Swap","SWAP","infiltrator-filesystem-swap","/System/Filesystems/SWAP.so","infiltrator_filesystem_swap_v1","Swap.pdf"],["sfs2","Filesystem-SFS2","SFS2","infiltrator-filesystem-sfs2","/System/Filesystems/SFS2.so","infiltrator_filesystem_sfs2_v1","SFS2.pdf"],["ufs1","Filesystem-UFS1","UFS1","infiltrator-filesystem-ufs1","/System/Filesystems/UFS1.so","infiltrator_filesystem_ufs_v1","UFS1.pdf"]]
    actual = [[e.get("id"), e.get("repository"), e.get("name"), e.get("package"),
               e.get("resource"), e.get("abi"), e.get("pdf")] for e in entries]
    if len(actual) != 18 or actual != expected:
        raise ValueError("Incomplete or modified eighteen-provider filesystem catalogue")
    return entries


def all_catalogue():
    entries = catalogue() + additional_catalogue()
    if len(entries) != 23 or len(set(x["resource"] for x in entries)) != 23:
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

def open_url(url, asset=False):
    headers = {"User-Agent": "InfiltratorOS-filesystem-installer",
               "Accept": "application/vnd.github+json"}
    token = os.getenv("GITHUB_TOKEN")
    if token and urllib.parse.urlsplit(url).hostname == "api.github.com":
        headers["Authorization"] = "Bearer " + token
    if asset:
        headers["Accept"] = "application/octet-stream"
        headers.pop("Authorization", None)  # Public asset redirects must not receive credentials.
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

def kernel_asset_name(entry):
    return f"{entry['id']}-linux-{KERNEL_TARGET}.ko"


def release_installer(entry, release):
    if not isinstance(release, dict) or release.get("draft") or release.get("prerelease"):
        raise ValueError("Not a published stable filesystem release")
    revision = release.get("target_commitish")
    if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Filesystem release has no exact source commit identity")
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
    kernel_name = kernel_asset_name(entry)
    if entry["id"] == "exfat":
        expected.update({"exFAT.so", kernel_name, "SHA256SUMS"})
    elif entry["id"] == "ofs":
        expected.update({"OFS.so", "SHA256SUMS"})
        if tuple(map(int, version.split("."))) >= (0, 1, 1):
            expected.add(kernel_name)
    elif entry["id"] in {"fat12", "fat16", "fat32"} and tuple(map(int, version.split("."))) >= milestone:
        expected.update({f"FAT{bits}.efi", kernel_name})
    elif entry.get("abi") and entry["id"] != "swap" and isinstance(assets, list):
        # Additional filesystem providers own their kernel adapters. Their
        # released .ko may target the native InfiltratorOS kernel ABI (0.1.1)
        # rather than this OS worker's older 6.12.107 compatibility target.
        # Admit at most one *versioned* provider-owned adapter as a published
        # asset, without treating it as loaded or compatible with this host.
        adapter_pattern = re.compile(
            r"^" + re.escape(entry["id"]) + r"-linux-[0-9]+\.[0-9]+\.[0-9]+\.ko$")
        adapters = [
            item["name"] for item in assets
            if isinstance(item, dict) and isinstance(item.get("name"), str)
            and adapter_pattern.fullmatch(item["name"])
        ]
        if len(adapters) > 1:
            raise ValueError("Ambiguous native kernel adapter assets")
        expected.update(adapters)
        # Providers may publish their own checksum listing in addition to
        # GitHub's required SHA-256 digest on every immutable release asset.
        for checksum_name in ("SHA256SUMS", "SHA256SUMS.txt"):
            if any(isinstance(item, dict) and item.get("name") == checksum_name
                   for item in assets):
                expected.add(checksum_name)
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


def alternate_asset_url(url, digest, length):
    """Resolve the exact signed release asset via the independent GitHub API route."""
    parsed = urllib.parse.urlsplit(url)
    match = re.fullmatch(
        r"/Infiltrator-Projects/([A-Za-z0-9-]+)/releases/download/"
        r"(v[0-9]+\.[0-9]+\.[0-9]+)/([A-Za-z0-9_.+-]+)", parsed.path)
    if parsed.scheme != "https" or parsed.netloc != "github.com" or not match:
        raise ValueError("Refusing noncanonical alternate filesystem asset lookup")
    repository, tag, name = match.groups()
    api = f"https://api.github.com/repos/Infiltrator-Projects/{repository}/releases/tags/{tag}"
    with open_url(api) as response:
        payload = response.read(4 * 1024 * 1024 + 1)
    if len(payload) > 4 * 1024 * 1024:
        raise ValueError("Oversized alternate filesystem release metadata")
    release = json.loads(payload)
    if release.get("tag_name") != tag or release.get("draft") or release.get("prerelease"):
        raise ValueError("Alternate route returned a different filesystem release")
    matches = [item for item in release.get("assets", [])
               if isinstance(item, dict) and item.get("name") == name
               and item.get("browser_download_url") == url
               and item.get("digest") == "sha256:" + digest
               and item.get("size") == length
               and type(item.get("id")) is int and item["id"] > 0]
    if len(matches) != 1:
        raise ValueError("Alternate filesystem asset metadata does not match the pinned release")
    return f"https://api.github.com/repos/Infiltrator-Projects/{repository}/releases/assets/{matches[0]['id']}"


def download(url, path, digest, length):
    errors = []
    for alternate in (False, True):
        source = alternate_asset_url(url, digest, length) if alternate else url
        try:
            count, sha = 0, hashlib.sha256()
            with open_url(source, asset=alternate) as src, path.open("wb") as dst:
                while True:
                    data = src.read(1024 * 1024)
                    if not data:
                        break
                    count += len(data)
                    if count > length:
                        raise ValueError("Installer exceeds published size")
                    sha.update(data)
                    dst.write(data)
            if count != length or sha.hexdigest() != digest:
                raise ValueError("Installer SHA-256 or size mismatch")
            path.chmod(0o700)
            return
        except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError, OSError) as error:
            path.unlink(missing_ok=True)
            errors.append(f"{'asset API' if alternate else 'release CDN'}: {error}")
            print(f"Transient filesystem download failure; trying alternate route: {error}",
                  flush=True)
    raise RuntimeError("Both SHA-256-guarded filesystem download routes failed: " + "; ".join(errors))


def native_current(entry, version, release):
    """Skip only a complete, correctly owned and verified native Debian build."""
    package = entry["package"]
    fields = ("$" + "{Status}\t" + "$" + "{Version}\t" +
              "$" + "{X-Infiltrator-Native-Profile}\t" +
              "$" + "{X-Infiltrator-PGO}")
    proc = subprocess.run(["/usr/bin/dpkg-query", "-W", "-f=" + fields, package],
                          capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        return False
    values = proc.stdout.strip().split("\t")
    if (len(values) != 4 or values[0] != "install ok installed" or
            values[1] != version + "+native1" or
            values[2] != "aggressive" or values[3] != "trained-two-pass"):
        return False
    module = Path(entry["resource"])
    guide = Path("/System/Resources/Documentation") / (entry["repository"] + ".pdf")
    for path in (module, guide):
        if path.is_symlink() or not path.is_file():
            return False
        owned = subprocess.run(["/usr/bin/dpkg-query", "-S", str(path)],
                               capture_output=True, text=True, check=False)
        if owned.returncode or owned.stdout.strip() != package + ": " + str(path):
            return False
    try:
        with guide.open("rb") as stream:
            if stream.read(5) != b"%PDF-":
                return False
    except OSError:
        return False
    return subprocess.run(["/usr/bin/dpkg", "--verify", package], check=False,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def install(entry):
    """Delegate installation entirely to the component's published native RUN.

    The component owns dependency resolution, native compilation, packaging,
    optional kernel modules and its PDF. The OS worker only verifies the
    published installer and its exit status.
    """
    release_dir = os.environ.get("INFILTRATOROS_RELEASE_DIR")
    if release_dir:
        payload = (Path(release_dir) / (entry["repository"] + ".json")).read_bytes()
    else:
        url = f"https://api.github.com/repos/Infiltrator-Projects/{entry['repository']}/releases/latest"
        with open_url(url) as response:
            payload = response.read(4 * 1024 * 1024 + 1)
    if len(payload) > 4 * 1024 * 1024:
        raise ValueError("Oversized GitHub release metadata")
    release = json.loads(payload)
    version, location, digest, length = release_installer(entry, release)
    if native_current(entry, version, release):
        return True
    status("running", 10, "Installing " + entry["name"],
           "Executing its independently published native installer")
    with tempfile.TemporaryDirectory(dir=STATUS_DIR, prefix="filesystem-native-") as folder:
        installer = Path(folder) / "filesystem.run"
        download(location, installer, digest, length)
        run([installer, "--profile", "aggressive", "--system-package-mode"])
    if not native_current(entry, version, release):
        raise RuntimeError("Independent filesystem RUN did not install its verified native SO and PDF")
    return False



# Four independently owned filesystems may build without installation. Never
# run more than two builds simultaneously; all package installation stays serial.
SMART_BUILD_CANDIDATES = ("filesystem-fat12", "filesystem-fat16",
                          "filesystem-fat32", "filesystem-exfat")
SMART_BUILD_LIMIT = 2


def smart_status(slug, state, progress, label, detail):
    global STATUS_PATH
    previous = STATUS_PATH
    STATUS_PATH = STATUS_DIR / (slug + "-install.status")
    try:
        status(state, progress, label, detail)
    finally:
        STATUS_PATH = previous


def smart_build(job):
    """Execute one verified native RUN in build-only mode, retaining its log."""
    entry, installer, target, version = job
    slug = entry["repository"].lower()
    started = time.monotonic()
    with (STATUS_DIR / (slug + "-build.log")).open("w") as output:
        subprocess.run([str(installer), "--profile", "aggressive",
                        "--system-package-mode", "--build-only", "--output",
                        str(target)], stdout=output, stderr=subprocess.STDOUT, check=True)
    results = list(target.glob("*.deb"))
    if len(results) != 1:
        raise RuntimeError(slug + ": build-only produced no unique DEB")
    package = results[0]
    for field, expected in (("Package", entry["package"]),
                            ("Version", version + "+native1"),
                            ("Architecture", "amd64")):
        actual = subprocess.check_output(
            ["/usr/bin/dpkg-deb", "-f", str(package), field], text=True).strip()
        if actual != expected:
            raise RuntimeError(slug + ": invalid native " + field)
    return package, time.monotonic() - started


def smart_can_build(entry):
    """Avoid a native installer invoking APT from concurrent compilation."""
    packages = ["build-essential", "binutils", "python3", "dpkg-dev"]
    tools = ["cc", "readelf", "dpkg-deb", "dpkg-query"]
    if entry["id"] == "exfat":
        # The exFAT standalone builder has a larger prerequisite inventory.
        packages += ["cmake", "git", "pkg-config", "libssl-dev", "libsqlite3-dev",
                     "exfatprogs", "dosfstools"]
        tools += ["c++", "make", "cmake", "git", "nm", "pkg-config", "ctest"]
    for name in packages:
        result = subprocess.run(
            ["/usr/bin/dpkg-query", "-W", "-f=" + "$" + "{Status}", name],
            capture_output=True, text=True, check=False)
        if result.returncode or result.stdout.strip() != "install ok installed":
            return False
    import shutil
    return all(shutil.which(name) for name in tools)


def smart_build_limit():
    """Conservative pressure gate: smaller hosts compile serially."""
    if (os.cpu_count() or 1) < 4:
        return 1
    try:
        memory = Path("/proc/meminfo").read_text()
        available = next(int(line.split()[1]) for line in memory.splitlines()
                         if line.startswith("MemAvailable:"))
    except (OSError, ValueError, StopIteration):
        return 1
    return SMART_BUILD_LIMIT if available >= 3 * 1024 * 1024 else 1


def install_smart_queue(entries):
    """Schedule up to four builds, then serially install and verify each DEB."""
    global STATUS_PATH
    release_dir = os.environ.get("INFILTRATOROS_RELEASE_DIR")
    if not release_dir:
        raise ValueError("Smart build requires the sealed release snapshot")
    manifest = (STATUS_DIR.parent / "selected.list").read_text().splitlines()
    chosen = [slug for slug in SMART_BUILD_CANDIDATES if slug in manifest]
    if len(chosen) < 2:
        raise ValueError("Smart build requires at least two selected providers")
    selected = [next(x for x in entries if x["repository"].lower() == slug)
                for slug in chosen]
    completed = set()
    try:
        from contextlib import ExitStack
        with ExitStack() as stack:
            for slug in chosen:
                lock = stack.enter_context((STATUS_ROOT / (slug + ".lock")).open("a"))
                fcntl.flock(lock, fcntl.LOCK_EX)
                smart_status(slug, "queued", 2, "Smart build queued",
                             "Waiting for the shared native installation lock")
            global_lock = stack.enter_context(
                (STATUS_ROOT / "native-install.lock").open("a"))
            fcntl.flock(global_lock, fcntl.LOCK_EX)
            os.environ["DEBIAN_FRONTEND"] = "noninteractive"
            os.environ["TMPDIR"] = str(STATUS_DIR)
            with tempfile.TemporaryDirectory(dir=STATUS_DIR, prefix="smart-build-") as tmp:
                root = Path(tmp)
                builds, serial, release_info = [], [], {}
                timings = STATUS_DIR / "smart-build-timings.log"
                timings.write_text("Selected: " + ", ".join(chosen) +
                                   "\nBuild concurrency ceiling: 2\n")
                for entry in selected:
                    slug = entry["repository"].lower()
                    payload = (Path(release_dir) / (entry["repository"] + ".json")).read_bytes()
                    if len(payload) > 4 * 1024 * 1024:
                        raise ValueError(slug + ": oversized release snapshot")
                    release = json.loads(payload)
                    version, url, digest, length = release_installer(entry, release)
                    if native_current(entry, version, release):
                        smart_status(slug, "complete", 100, "Already installed",
                                     "Verified native filesystem; rebuild skipped")
                        with timings.open("a") as output:
                            output.write(slug + " skipped (already verified)\n")
                        completed.add(slug)
                        continue
                    if not smart_can_build(entry):
                        serial.append(entry)
                        smart_status(slug, "queued", 10, "Serial fallback queued",
                                     "Native prerequisites missing; avoiding concurrent APT")
                        continue
                    smart_status(slug, "running", 15, "Downloading native RUN",
                                 "Verifying independent release SHA-256")
                    installer = root / (slug + ".run")
                    download(url, installer, digest, length)
                    target = root / slug
                    target.mkdir()
                    builds.append((entry, installer, target, version))
                    release_info[slug] = release
                packages = {}
                if builds:
                    workers = min(smart_build_limit(), len(builds))
                    with timings.open("a") as output:
                        output.write(f"Concurrent builds used: {workers}\n")
                    for entry, _, _, _ in builds:
                        smart_status(entry["repository"].lower(), "running", 35,
                                     "Queued for native compilation",
                                     "Bounded queue: maximum two independent builds")
                    with ThreadPoolExecutor(max_workers=workers) as pool:
                        futures = {pool.submit(smart_build, job): job[0]
                                   for job in builds}
                        for task in as_completed(futures):
                            entry = futures[task]
                            slug = entry["repository"].lower()
                            package, seconds = task.result()
                            packages[slug] = package
                            with timings.open("a") as output:
                                output.write(f"{slug} build {seconds:.2f} seconds\n")
                            smart_status(slug, "running", 75, "Native build verified",
                                         "Build completed; awaiting serial dpkg")
                for entry, _, _, version in builds:
                    slug = entry["repository"].lower()
                    smart_status(slug, "running", 85, "Installing native package",
                                 "Single serial Debian package transaction")
                    run(["/usr/bin/dpkg", "--install", packages[slug]])
                    if not native_current(entry, version, release_info[slug]):
                        raise RuntimeError(slug + ": installed SO or PDF verification failed")
                    smart_status(slug, "complete", 100, "Native installation complete",
                                 "Independently built and verified native package")
                    completed.add(slug)
                for entry in serial:
                    slug = entry["repository"].lower()
                    STATUS_PATH = STATUS_DIR / (slug + "-install.status")
                    reused = install(entry)
                    smart_status(slug, "complete", 100, "Serial fallback complete",
                                 "Existing native package verified" if reused else
                                 "Component-owned installer completed and verified")
                    with timings.open("a") as output:
                        output.write(slug + " serial fallback\n")
                    completed.add(slug)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        for slug in chosen:
            if slug not in completed:
                smart_status(slug, "failed", 100, "Smart build failed", error)
        STATUS_PATH = None
        raise
    finally:
        if STATUS_PATH is not None:
            STATUS_PATH = STATUS_DIR / (chosen[0] + "-install.status")

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
    if sys.argv[1:] in (["--list"], ["--list-all"]):
        # A single catalogue drives today's install-all mode and future
        # checkbox selections. Individual installers own their build details.
        requested = os.environ.get("INFILTRATOROS_SELECTED_FILESYSTEMS")
        selected = None if requested is None else {
            value.strip().lower() for value in requested.split(",") if value.strip()
        }
        known = {entry["id"] for entry in entries}
        if selected is not None and not selected <= known:
            raise ValueError("Unknown selected filesystem: " + ", ".join(sorted(selected - known)))
        visible = entries if sys.argv[1] == "--list-all" else catalogue()
        for entry in visible:
            if selected is None or entry["id"] in selected:
                print(entry["repository"].lower())
        return
    if len(sys.argv) == 3 and sys.argv[1] == "--discover-additional":
        if os.geteuid() != 0:
            raise RuntimeError("Native release discovery requires root")
        discover_additional(Path(sys.argv[2]))
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
    if slug in SMART_BUILD_CANDIDATES and os.environ.get("INFILTRATOROS_SMART_BUILD_QUEUE") == "1":
        install_smart_queue(entries)
        return
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
