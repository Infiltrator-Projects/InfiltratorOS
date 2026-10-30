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
native = Path("/Temporary/InfiltratorOS")
native.mkdir(mode=0o755, parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="native-worker.", dir=native) as temporary:
    stage = Path(temporary)
    subprocess.run(["dpkg-deb", "--control", package, stage / "control"], check=True)
    script = (stage / "control/postinst").read_text()
    # Protect the real Debian post-installation entry point from silently
    # retaining its historical FAT-only limit while the Python workers know four.
    assert '$(wc -l < "$stage/filesystems.list")" -ne 5' in script
    assert "all 63 jobs" in script
    assert "filesystem-fat32" in (root / "packaging/install-filesystem-resources.py").read_text()
    expected = re.search(r"^BUNDLE_SHA256='([0-9a-f]{64})'$", script, re.MULTILINE).group(1)
    payload = script.split("\n__INFILTRATOROS_NATIVE_BUNDLE_BEGIN__\n", 1)[1].split(
        "\n__INFILTRATOROS_NATIVE_BUNDLE_END__", 1)[0]
    blob = base64.b64decode(payload)
    assert hashlib.sha256(blob).hexdigest() == expected
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as archive:
        names = {i.name.removeprefix("./") for i in archive if i.isfile()}
        assert len(names) == 19
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
    for entry in entries:
        slug = entry["repository"].lower()
        subprocess.run([stage / "workers/install-filesystem-resources", slug],
                       check=True, env=env)
        record = stage / "status" / (slug + "-install.status")
        assert "state=complete\n" in record.read_text(), slug
        module = Path(entry["resource"])
        assert module.is_file(), module
        owner = subprocess.check_output(["dpkg-query", "-S", entry["resource"]],
                                        text=True).strip()
        assert owner == entry["package"] + ": " + entry["resource"], owner
        version = subprocess.check_output(["dpkg-query", "-W", "-f=${Version}",
                                           entry["package"]], text=True)
        assert version.endswith("+native1"), version
        folder = Path("/System/Resources/Documentation") / entry["repository"]
        pdf = folder / (module.stem + ".pdf")
        assert {x.name for x in folder.iterdir()} == {pdf.name}, entry["repository"]
        assert pdf.read_bytes().startswith(b"%PDF-"), pdf
        pdf_owner = subprocess.check_output(["dpkg-query", "-S", str(pdf)], text=True).strip()
        assert pdf_owner == entry["package"] + ": " + str(pdf), pdf_owner
    assert len(list((stage / "status").glob("*-install.status"))) == 5
    assert {p.name for p in Path("/System/Filesystems").glob("FAT*.so")} == {"FAT12.so", "FAT16.so", "FAT32.so"}
print("Five filesystems installed and verified by the sealed InfiltratorOS worker")

assert Path("/System/Filesystems/OFS.so").is_file()

assert Path("/System/Filesystems/exFAT.so").is_file()
