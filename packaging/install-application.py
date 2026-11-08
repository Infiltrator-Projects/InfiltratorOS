#!/usr/bin/python3 -u
# SPDX-License-Identifier: GPL-3.0-or-later
"""One native RUN handoff for all four separately published applications."""
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
import urllib.request

ROOT = Path("/Temporary/InfiltratorOS")
# Only installer invocation contracts and expected results. Builds belong to RUN.
PRODUCTS = {
    "system-monitor": ("System-Monitor", "system-monitor", "", "/Programs/system-monitor"),
    "calculator": ("Calculator", "calculator", "", "/Programs/calculator"),
    "files": ("Files", "files", "+native1", "/Programs/files"),
    "defragmenter": ("Defragmenter", "defragmenter", "+native1", "/Programs/defragmenter"),
}
REQUIRED = {
    "system-monitor": ("/usr/share/doc/system-monitor/BUILD-INFO",),
    "calculator": ("/System/Resources/Documentation/Calculator.pdf",
                   "/usr/share/applications/net.ssmith.calculator.desktop",
                   "/usr/share/icons/hicolor/scalable/apps/calculator.svg",
                   "/usr/share/metainfo/calculator.metainfo.xml"),
    "files": ("/usr/share/applications/files.desktop",),
    "defragmenter": ("/Programs/defragmenter-test-media",
                     "/System/Commands/Defragmenter/defragmenter-operation-engine",
                     "/System/Commands/Defragmenter/defragmenter-mapper",
                     "/System/Commands/Defragmenter/ext4/ext-metadata-worker"),
}


def report(path, state, progress, stage, detail):
    clean = lambda s: str(s).replace("\n", " ").replace("\r", " ")
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, prefix="native-status-", delete=False) as file:
        file.write(f"state={clean(state)}\nprogress={progress}\nstage={clean(stage)}\ndetail={clean(detail)}\n")
        tmp = Path(file.name)
    tmp.chmod(0o644)
    tmp.replace(path)


def release(name, slug, directory):
    if not directory:
        raise ValueError("Missing pinned native release snapshot")
    data = (Path(directory) / (name + ".json")).read_bytes()
    if len(data) > 4 * 1024 * 1024:
        raise ValueError("Oversized release metadata")
    meta = json.loads(data)
    if not isinstance(meta, dict) or meta.get("draft") or meta.get("prerelease"):
        raise ValueError("Unpublished component release")
    tag = meta.get("tag_name")
    if not isinstance(tag, str) or not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?", tag):
        raise ValueError("Invalid release version")
    version = tag[1:]
    filename = (f"Defragmenter-{version}-local-folder.run" if slug == "defragmenter"
                else f"{slug}-{version}-native-installer.run")
    assets = meta.get("assets")
    if not isinstance(assets, list):
        raise ValueError("No release assets")
    matches = [asset for asset in assets if isinstance(asset, dict) and asset.get("name") == filename]
    if len(matches) != 1:
        raise ValueError("Missing or duplicate native RUN")
    asset = matches[0]
    url = f"https://github.com/Infiltrator-Projects/{name}/releases/download/{tag}/{filename}"
    digest, length = asset.get("digest"), asset.get("size")
    if (asset.get("browser_download_url") != url or
        not isinstance(digest, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", digest) or
        not isinstance(length, int) or not 0 < length <= 512 * 1024 * 1024):
        raise ValueError("Untrusted native RUN asset")
    return version, url, digest[7:], length


def package_info(package):
    fields = ("$" + "{Status}\t" + "$" + "{Version}\t" + "$" + "{X-Defragmenter-Build}")
    r = subprocess.run(["/usr/bin/dpkg-query", "-W", "-f=" + fields, package],
                       capture_output=True, text=True, check=False)
    if r.returncode:
        return None
    parts = r.stdout.rstrip("\n").split("\t")
    return parts if len(parts) == 3 and parts[0] == "install ok installed" else None


def native_proof(slug):
    if slug == "defragmenter":
        info = package_info("defragmenter")
        return bool(info and info[2] == "native")
    if slug in ("system-monitor", "files"):
        if slug == "system-monitor":
            info = Path("/usr/share/doc/system-monitor/BUILD-INFO")
        else:
            info = Path("/System/Resources/Documentation/Files-BUILD-INFO")
            if not info.is_file():
                info = Path("/System/Resources/Documentation/Files/BUILD-INFO")
        try:
            text = info.read_text()
        except OSError:
            return False
        return all(mark in text for mark in ("Profile: aggressive", "PGO: trained two-pass profile", "-march=native"))
    return True  # Calculator: verified RUN plus exact attestation and Debian files.


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as file:
        while chunk := file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def current(slug, version, digest, record):
    _, package, suffix, program = PRODUCTS[slug]
    info = package_info(package)
    if not info or info[1] != version + suffix:
        return False
    executable = Path(program)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        return False
    if any(not Path(path).is_file() for path in REQUIRED[slug]) or not native_proof(slug):
        return False
    try:
        saved = json.loads(record.read_text())
        if saved != {"version": version, "installer_sha256": digest,
                     "program_sha256": file_hash(executable)}:
            return False
    except (OSError, ValueError):
        return False
    return subprocess.run(["/usr/bin/dpkg", "--verify", package], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, check=False).returncode == 0


# A transient GitHub/CDN gateway failure must not fail all four independent apps.
# Retry only network/server errors; never retry a rejected release or bad hash.
TRANSIENT_HTTP = {408, 429, 500, 502, 503, 504}
DOWNLOAD_ATTEMPTS = 5


def download(url, digest, size, destination):
    request = urllib.request.Request(url, headers={"User-Agent": "InfiltratorOS-native"})
    partial = destination.with_name(destination.name + ".part")
    for attempt in range(DOWNLOAD_ATTEMPTS):
        total, hasher = 0, hashlib.sha256()
        try:
            with urllib.request.urlopen(request, timeout=120) as source, partial.open("wb") as dest:
                while True:
                    data = source.read(1024 * 1024)
                    if not data:
                        break
                    total += len(data)
                    if total > size:
                        raise ValueError("RUN exceeds published size")
                    hasher.update(data)
                    dest.write(data)
            if total != size or hasher.hexdigest() != digest:
                raise ValueError("RUN checksum or length mismatch")
            partial.chmod(0o700)
            partial.replace(destination)
            return
        except (urllib.error.HTTPError, urllib.error.URLError, TimeoutError,
                ConnectionError) as error:
            partial.unlink(missing_ok=True)
            if isinstance(error, urllib.error.HTTPError) and error.code not in TRANSIENT_HTTP:
                raise
            if attempt + 1 == DOWNLOAD_ATTEMPTS:
                raise
            delay = 2 ** (attempt + 1)
            print(f"Temporary native installer download failure ({error}); "
                  f"retry {attempt + 2}/{DOWNLOAD_ATTEMPTS} in {delay}s",
                  flush=True)
            time.sleep(delay)
        except Exception:
            partial.unlink(missing_ok=True)
            raise


def main():
    slug = Path(sys.argv[0]).name.removeprefix("install-")
    if slug not in PRODUCTS or len(sys.argv) != 1:
        raise ValueError("Unknown native application")
    if os.geteuid() != 0:
        raise RuntimeError("Native provisioning requires root")
    runtime = Path(os.environ["INFILTRATOROS_INSTALL_RUNTIME"])
    if runtime.parent != ROOT or not runtime.name.startswith("native-worker."):
        raise ValueError("Invalid native installation stage")
    directory = runtime / "status"
    if not directory.is_dir():
        raise ValueError("Missing stage status directory")
    status = directory / (slug + "-install.status")
    verified = ROOT / ("native-verified-" + slug + ".json")
    with (ROOT / (slug + ".lock")).open("a") as own:
        fcntl.flock(own, fcntl.LOCK_EX)
        with (ROOT / "native-install.lock").open("a") as global_lock:
            report(status, "queued", 2, "Waiting for native installer", slug)
            fcntl.flock(global_lock, fcntl.LOCK_EX)
            os.environ["DEBIAN_FRONTEND"] = "noninteractive"
            os.environ["TMPDIR"] = str(directory)
            name, package, suffix, program = PRODUCTS[slug]
            report(status, "running", 5, "Checking native release", name)
            version, url, digest, size = release(name, slug, os.environ.get("INFILTRATOROS_RELEASE_DIR"))
            if current(slug, version, digest, verified):
                report(status, "complete", 100, "Already installed", name + " is current and verified")
                return
            report(status, "running", 10, "Waiting for package manager", name)
            subprocess.run(["/usr/bin/apt-get", "-o", "DPkg::Lock::Timeout=-1", "check"], check=True)
            with tempfile.TemporaryDirectory(dir=directory, prefix="native-app-") as temporary:
                installer = Path(temporary) / (slug + ".run")
                report(status, "running", 20, "Downloading independent installer", name)
                download(url, digest, size, installer)
                report(status, "running", 35, "Running independent installer", name)
                arguments = [str(installer)] + ([] if slug == "defragmenter" else ["--system-package-mode"])
                subprocess.run(arguments, check=True)
            if (not (info := package_info(package)) or info[1] != version + suffix or
                not Path(program).is_file() or
                any(not Path(path).is_file() for path in REQUIRED[slug]) or not native_proof(slug)):
                raise RuntimeError("Independent RUN failed installed-contract verification")
            evidence = {"version": version, "installer_sha256": digest,
                        "program_sha256": file_hash(program)}
            with tempfile.NamedTemporaryFile(mode="w", dir=ROOT, prefix="verified-native-", delete=False) as file:
                json.dump(evidence, file, sort_keys=True)
                tmp = Path(file.name)
            tmp.chmod(0o644)
            tmp.replace(verified)
            report(status, "complete", 100, "Native application installed", name)


if __name__ == "__main__":
    try:
        main()
    except (OSError, KeyError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        slug = Path(sys.argv[0]).name.removeprefix("install-")
        runtime = os.environ.get("INFILTRATOROS_INSTALL_RUNTIME", "")
        status = Path(runtime) / "status" / (slug + "-install.status")
        if slug in PRODUCTS and status.parent.is_dir():
            report(status, "failed", 100, "Native application installation failed", error)
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
