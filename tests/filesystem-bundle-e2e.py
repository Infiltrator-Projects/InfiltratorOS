#!/usr/bin/env python3
"""Real FAT12/FAT16/FAT32/OFS/exFAT installation via the sealed OS bundle."""
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile

if os.geteuid() != 0 or os.environ.get("GITHUB_ACTIONS") != "true":
    raise SystemExit("Installer e2e is restricted to disposable GitHub Actions hosts")
package = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parents[1]
entries = json.loads((root / "packaging/filesystem-resources.json").read_text())
assert [x["id"] for x in entries] == ["fat12", "fat16", "fat32", "ofs", "exfat"]
kernel_dir = Path("/System/Kernel/Filesystems")
assert "./System/Kernel/Filesystems/" in subprocess.check_output(["dpkg-deb", "-c", package], text=True)
kernel_dir.mkdir(parents=True, exist_ok=True)
native = Path("/Temporary/InfiltratorOS")
native.mkdir(mode=0o755, parents=True, exist_ok=True)
def installed_manual(entry, legacy_name):
    docs = Path("/System/Resources/Documentation")
    flat = docs / (entry["repository"] + ".pdf")
    return flat if flat.exists() else docs / entry["repository"] / legacy_name

with tempfile.TemporaryDirectory(prefix="native-worker.", dir=native) as temporary:
    stage = Path(temporary)
    subprocess.run(["dpkg-deb", "--control", package, stage / "control"], check=True)
    script = (stage / "control/postinst").read_text()
    # Protect the real Debian post-installation entry point from silently
    # retaining its historical FAT-only limit while the Python workers know four.
    assert '"$stage/orchestrate-native-install" "$stage"' in script
    assert "all launched jobs" in script
    assert "publish_summary complete" in script
    assert "publish_summary failed" in script
    assert "publish_summary timeout" in script
    assert "orchestrate-native-install" in script
    assert "filesystem-fat32" in {x["repository"].lower() for x in entries}
    assert len(json.loads((root / "packaging/filesystem-additional.json").read_text())) == 18
    expected = re.search(r"^BUNDLE_SHA256='([0-9a-f]{64})'$", script, re.MULTILINE).group(1)
    payload = script.split("\n__INFILTRATOROS_NATIVE_BUNDLE_BEGIN__\n", 1)[1].split(
        "\n__INFILTRATOROS_NATIVE_BUNDLE_END__", 1)[0]
    blob = base64.b64decode(payload)
    assert hashlib.sha256(blob).hexdigest() == expected
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as archive:
        names = {i.name.removeprefix("./") for i in archive if i.isfile()}
        assert len(names) == 23
        assert {"install-filesystem-resources", "start-filesystem-resources-install",
                "filesystem-resources-progress", "prepare-native-releases"} <= names
        archive.extractall(stage / "workers", filter="data")
    # Recreate old unowned documentation to test migration, without touching real media.
    for entry in entries:
        folder = Path("/System/Resources/Documentation") / entry["repository"]
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "BUILD-INFO").write_text("old build")
        (folder / "MIGRATION-PROVENANCE.md").write_text("old engineering note")
    env = dict(os.environ, INFILTRATOROS_INSTALL_RUNTIME=str(stage))
    # Four source-build candidates enter a two-worker queue; dpkg remains serial.
    # Subsequent per-provider calls must reuse their independently verified state.
    (stage / "status").mkdir(exist_ok=True)
    (stage / "selected.list").write_text("".join(
        item["repository"].lower() + "\n" for item in entries if item["id"] != "ofs"))
    snapshot = stage / "release-json"
    snapshot.mkdir()
    import urllib.request
    for entry in entries:
        if entry["id"] == "ofs":
            continue
        name = entry["repository"]
        address = f"https://api.github.com/repos/Infiltrator-Projects/{name}/releases/latest"
        headers = {"User-Agent": "InfiltratorOS-parallel-FAT-integration",
                   "Accept": "application/vnd.github+json"}
        if os.environ.get("GITHUB_TOKEN"):
            headers["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
        with urllib.request.urlopen(urllib.request.Request(address, headers=headers),
                                    timeout=120) as source:
            (snapshot / (name + ".json")).write_bytes(source.read())
    queued_env = dict(env, INFILTRATOROS_SMART_BUILD_QUEUE="1",
                      INFILTRATOROS_RELEASE_DIR=str(snapshot))
    subprocess.run([stage / "workers/install-filesystem-resources", "filesystem-fat12"],
                   check=True, env=queued_env)
    for slug in ("filesystem-fat12", "filesystem-fat16",
                 "filesystem-fat32", "filesystem-exfat"):
        assert "state=complete\n" in (
            stage / "status" / (slug + "-install.status")).read_text(), slug
    assert (stage / "status/smart-build-timings.log").is_file()
    for entry in entries:
        slug = entry["repository"].lower()
        subprocess.run([stage / "workers/install-filesystem-resources", slug],
                       check=True, env=env)
        record = stage / "status" / (slug + "-install.status")
        assert "state=complete\n" in record.read_text(), slug
        # --system-package-mode deliberately installs the independently
        # packaged native SO and PDF, without host-specific kernel modules.
        module = Path(entry["resource"])
        assert module.is_file(), module
        owner = subprocess.check_output(["dpkg-query", "-S", entry["resource"]],
                                        text=True).strip()
        assert owner == entry["package"] + ": " + entry["resource"], owner
        version = subprocess.check_output(["dpkg-query", "-W", "-f=${Version}",
                                           entry["package"]], text=True)
        assert version.endswith("+native1"), version
        pdf = installed_manual(entry, module.stem + ".pdf")
        assert not pdf.is_symlink() and pdf.read_bytes().startswith(b"%PDF-"), pdf
        pdf_owner = subprocess.check_output(["dpkg-query", "-S", str(pdf)], text=True).strip()
        assert pdf_owner == entry["package"] + ": " + str(pdf), pdf_owner
        if entry["id"] == "ofs":
            original_pdf = pdf.read_bytes()
            original_so = module.read_bytes()
            pdf.unlink()
            subprocess.run([stage / "workers/install-filesystem-resources", slug], check=True, env=env)
            assert pdf.read_bytes() == original_pdf
            assert module.read_bytes() == original_so
    assert len(list((stage / "status").glob("*-install.status"))) == 5
    # Prove the eighteen conditionally published SO providers through the exact sealed
    # InfiltratorOS worker on disposable GitHub Actions hardware.
    extra = json.loads((root / "packaging/filesystem-additional.json").read_text())
    snapshot = stage / "release-json"
    subprocess.run([stage / "workers/install-filesystem-resources",
                    "--discover-additional", snapshot], check=True, env=env)
    candidates = (stage / "filesystems-optional.list").read_text().splitlines()
    pending = (stage / "filesystems-pending.list").read_text().splitlines()
    assert len(extra) == len(candidates) == 18, (candidates, pending)
    assert not pending, pending
    env["INFILTRATOROS_RELEASE_DIR"] = str(snapshot)
    for entry in extra:
        slug = entry["repository"].lower()
        assert slug in candidates, slug
        subprocess.run([stage / "workers/install-filesystem-resources", slug],
                       check=True, env=env)
        status = (stage / "status" / (slug + "-install.status")).read_text()
        assert "state=complete\n" in status, slug
        module = Path(entry["resource"])
        manual = installed_manual(entry, entry["pdf"])
        assert module.is_file() and not module.is_symlink(), module
        assert manual.is_file() and not manual.is_symlink(), manual
        assert manual.read_bytes().startswith(b"%PDF-"), manual
        assert subprocess.check_output(
            ["dpkg-query", "-S", str(module)], text=True).strip() == (
                entry["package"] + ": " + str(module))
        assert subprocess.check_output(
            ["dpkg-query", "-S", str(manual)], text=True).strip() == (
                entry["package"] + ": " + str(manual))
        release = json.loads((snapshot / (entry["repository"] + ".json")).read_text())
        expected_version = release["tag_name"].removeprefix("v") + "+native1"
        assert subprocess.check_output(
            ["dpkg-query", "-W", "-f=\u0024{Version}", entry["package"]],
            text=True).strip() == expected_version
    assert len(list((stage / "status").glob("*-install.status"))) == 23
    print("All 23 independently owned native filesystem SOs and PDFs installed")

    assert {p.name for p in Path("/System/Filesystems").glob("FAT*.so")} == {"FAT12.so", "FAT16.so", "FAT32.so"}
print("Five filesystems installed and verified by the sealed InfiltratorOS worker")

assert Path("/System/Filesystems/OFS.so").is_file()

assert Path("/System/Filesystems/exFAT.so").is_file()
