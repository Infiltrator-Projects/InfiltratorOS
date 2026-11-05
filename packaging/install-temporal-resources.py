#!/usr/bin/python3 -u
# SPDX-License-Identifier: GPL-3.0-or-later
"""Installation-only worker for independently released calendars and clocks."""
from __future__ import annotations

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


def install_entry(entry):
    """Verify and invoke the independent calendar or clock RUN installer."""
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
    if native_current(entry, version, release):
        return True
    write_status("running", 10, "Installing " + entry["name"],
                 "Executing its independently published native installer")
    with tempfile.TemporaryDirectory(dir=STATUS_DIR, prefix="temporal-resource-") as temporary:
        installer = Path(temporary) / "provider.run"
        download_installer(url, installer, digest, size)
        required([installer, "--profile", "aggressive", "--system-package-mode"])
    if not native_current(entry, version, release):
        raise RuntimeError("Independent calendar/clock RUN did not install its verified native SO and PDF")
    return False


def main():
    # Each published provider is installed by its own process, status record
    # and graphical progress window. Shared source code is not a combined job.
    entries = catalogue()
    if sys.argv[1:] == ["--list"]:
        # Keep selection in the orchestrator; providers own their installers.
        # Unset means every calendar and clock is selected.
        requested = os.environ.get("INFILTRATOROS_SELECTED_TEMPORAL")
        selected = None if requested is None else {
            value.strip().lower() for value in requested.split(",") if value.strip()
        }
        known = {entry["repository"].lower() for entry in entries}
        if selected is not None and not selected <= known:
            raise ValueError("Unknown selected temporal provider: " + ", ".join(sorted(selected - known)))
        for entry in entries:
            slug = entry["repository"].lower()
            if selected is None or slug in selected:
                print(slug)
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
