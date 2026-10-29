#!/usr/bin/env python3
"""Run the exact package worker separately for every provider on a CI host."""
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
    raise SystemExit("This integration test installs fixtures and providers on an ephemeral GitHub Actions host only")
package = Path(sys.argv[1]).resolve()
ROOT = Path(__file__).resolve().parents[1]
entries = json.loads((ROOT / "packaging/temporal-resources.json").read_text())
products = ("system-monitor", "calculator", "files", "defragmenter", "temporal-resources", "filesystem-resources")
expected = {name for product in products for name in ("install-" + product, "start-" + product + "-install", product + "-progress")}
expected.add("prepare-native-releases")
native = Path("/Temporary/InfiltratorOS")
native.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="native-worker.", dir=native) as temporary:
    work = Path(temporary)
    subprocess.run(["dpkg-deb", "--control", package, work / "control"], check=True)
    postinst = (work / "control/postinst").read_text()
    expected_digest = re.search(r"^BUNDLE_SHA256='([0-9a-f]{64})'$", postinst, re.MULTILINE).group(1)
    payload = postinst.split("\n__INFILTRATOROS_NATIVE_BUNDLE_BEGIN__\n", 1)[1].split("\n__INFILTRATOROS_NATIVE_BUNDLE_END__", 1)[0]
    bundle = base64.b64decode(payload)
    assert hashlib.sha256(bundle).hexdigest() == expected_digest
    with tarfile.open(fileobj=io.BytesIO(bundle), mode="r:gz") as archive:
        files = {item.name.removeprefix("./") for item in archive if item.isfile()}
        assert files == expected
        archive.extractall(work / "workers", filter="data")
    # Start with the installed monolithic package's exact module paths. This
    # tests the real worker's migration without fetching or editing Common.
    legacy = work / "legacy"
    (legacy / "DEBIAN").mkdir(parents=True)
    (legacy / "DEBIAN/control").write_text("Package: infiltrator-calendar-resources\nVersion: 1.19.78\nArchitecture: amd64\nMaintainer: Infiltrator Projects <noreply@infiltrator-projects.invalid>\nDescription: CI legacy temporal ownership fixture\n")
    for entry in entries:
        module = legacy / entry["resource"].lstrip("/")
        module.parent.mkdir(parents=True, exist_ok=True)
        module.write_text("legacy ownership fixture\n")
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", legacy, work / "legacy.deb"], check=True)
    subprocess.run(["dpkg", "--install", work / "legacy.deb"], check=True)
    # The exact bundled Python worker is invoked independently for all 54
    # providers, with one status file per provider in this CI installation.
    env = dict(os.environ, INFILTRATOROS_INSTALL_RUNTIME=str(work))
    for entry in entries:
        slug = entry["repository"].lower()
        subprocess.run([work / "workers/install-temporal-resources", slug], check=True, env=env)
        status = (work / "status" / (slug + "-install.status")).read_text()
        assert "state=complete\n" in status
    assert len(list((work / "status").glob("*-install.status"))) == 54
    for entry in entries:
        installed = subprocess.check_output(["dpkg-query", "-W", "-f=${Status}", entry["package"]], text=True)
        assert installed == "install ok installed", entry["package"]
        ownership = subprocess.check_output(["dpkg-query", "--search", entry["resource"]], text=True).strip()
        assert ownership == entry["package"] + ": " + entry["resource"], ownership
    assert len(list(Path("/System/Resources/Calendars").glob("*.so"))) == 31
    assert len(list(Path("/System/Resources/Clocks").glob("*.so"))) == 23

    # Qualify the real three-format worker from the exact sealed maintainer bundle.
    filesystem_entries = json.loads((ROOT / "packaging/filesystem-resources.json").read_text())
    assert [e["id"] for e in filesystem_entries] == ["fat12", "fat16", "fat32"]
    for entry in filesystem_entries:
        slug = entry["repository"].lower()
        subprocess.run([work / "workers/install-filesystem-resources", slug], check=True, env=env)
        record = work / "status" / (slug + "-install.status")
        assert "state=complete\n" in record.read_text()
        module = Path(entry["resource"])
        assert module.is_file(), module
        owner = subprocess.check_output(["dpkg-query", "-S", entry["resource"]], text=True).strip()
        assert owner == entry["package"] + ": " + entry["resource"], owner
        version = subprocess.check_output(["dpkg-query", "-W", "-f=${Version}", entry["package"]], text=True)
        assert version.endswith("+native1")
        assert module.stat().st_size > 10000
    assert len(list((work / "status").glob("*-install.status"))) == 57
    assert {p.name for p in Path("/System/Filesystems").glob("FAT*.so")} == {"FAT12.so", "FAT16.so", "FAT32.so"}
assert Path("/System/Filesystems/OFS.so").is_file()
print("Exact InfiltratorOS bundle: 54 temporal and 4 filesystem published native RUN installers independently built, installed, verified and owned")

