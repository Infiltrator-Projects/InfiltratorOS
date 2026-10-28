#!/usr/bin/env python3
"""Offline contract for the single native release snapshot and checksum export."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_releases", ROOT / "packaging/prepare-native-releases.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


def sample():
    names = ["System-Monitor", "Calculator", "Files", "Defragmenter"]
    for source in ("temporal-resources.json", "filesystem-resources.json"):
        names.extend(x["repository"] for x in json.loads((ROOT / "packaging" / source).read_text()))
    assert len(names) == len(set(names)) == 61
    releases = {}
    for repo in names:
        version = "1.2.3"
        name = ("Defragmenter-1.2.3-local-folder.run" if repo == "Defragmenter"
                else repo.lower() + "-1.2.3-native-installer.run")
        url = f"https://github.com/Infiltrator-Projects/{repo}/releases/download/v{version}/{name}"
        releases[repo] = {
            "tag_name": "v" + version, "draft": False, "prerelease": False,
            "assets": [{"name": name, "browser_download_url": url,
                        "digest": "sha256:" + "a" * 64, "size": 200}]
        }
    return json.dumps({"schema": 1, "releases": releases}).encode()


class NativeSnapshotTest(unittest.TestCase):
    def test_one_download_writes_sixty_one_checksums_and_local_releases(self):
        data = sample()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            temporal = root / "temporal.list"
            filesystem = root / "filesystem.list"
            for path, source in ((temporal, "temporal-resources.json"),
                                 (filesystem, "filesystem-resources.json")):
                entries = json.loads((ROOT / "packaging" / source).read_text())
                path.write_text("\n".join(x["repository"].lower() for x in entries) + "\n")
            with patch.object(worker, "download", return_value=data) as download, \
                 patch.object(worker.os, "geteuid", return_value=0):
                count = worker.prepare(root / "release-json", temporal, filesystem)
            self.assertEqual(count, 61)
            self.assertEqual(download.call_count, 1)
            self.assertEqual(len(list((root / "release-json").glob("*.json"))), 61)
            self.assertEqual(len(list((root / "release-json").glob("*.sha256"))), 61)
            self.assertEqual((root / "release-json" / "Calculator.sha256").read_text().split()[0], "a" * 64)

    def test_snapshot_rejects_tampering_and_missing_providers(self):
        payload = json.loads(sample())
        expected = {name.lower() for name in payload["releases"]}
        self.assertEqual(len(worker.verify_snapshot(sample(), expected)), 61)
        broken = copy.deepcopy(payload)
        broken["releases"]["Calculator"]["assets"][0]["digest"] = None
        with self.assertRaises(ValueError):
            worker.verify_snapshot(json.dumps(broken).encode(), expected)
        broken = copy.deepcopy(payload)
        broken["releases"].pop("Calculator")
        with self.assertRaises(ValueError):
            worker.verify_snapshot(json.dumps(broken).encode(), expected)
        broken = copy.deepcopy(payload)
        broken["releases"]["Files"]["assets"][0]["browser_download_url"] = "https://example.com/evil.run"
        with self.assertRaises(ValueError):
            worker.verify_snapshot(json.dumps(broken).encode(), expected)


if __name__ == "__main__":
    unittest.main()
