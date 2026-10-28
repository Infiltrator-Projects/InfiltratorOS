#!/usr/bin/env python3
"""Contract tests for the independently packaged FAT12/FAT16 installers."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("fs_install", ROOT / "packaging/install-filesystem-resources.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.entries = worker.catalogue()
        self.entry = self.entries[0]
        version = "0.1.2"
        repo = self.entry["repository"]
        url = f"https://github.com/Infiltrator-Projects/{repo}/releases/download/v{version}/"
        self.release = {
            "tag_name": "v" + version, "draft": False, "prerelease": False,
            "assets": [
                {"name": f"{repo.lower()}-{version}-native-installer.run",
                 "browser_download_url": url + f"{repo.lower()}-{version}-native-installer.run",
                 "digest": "sha256:" + "a"*64, "size": 100},
                {"name": f"{self.entry['package']}_{version}_amd64.deb",
                 "browser_download_url": url + f"{self.entry['package']}_{version}_amd64.deb",
                 "digest": "sha256:" + "b"*64, "size": 200}
            ]
        }

    def test_signed_distinct_catalogue(self):
        self.assertEqual({item["id"] for item in self.entries}, {"fat12", "fat16"})
        self.assertEqual({item["resource"] for item in self.entries},
                         {"/System/Filesystems/FAT12.so", "/System/Filesystems/FAT16.so"})

    def test_two_assets_and_github_native_zip(self):
        version, url, digest, size = worker.release_installer(self.entry, self.release)
        self.assertEqual(version, "0.1.2")
        self.assertEqual(digest, "a"*64)
        self.assertEqual(size, 100)
        self.assertTrue(url.endswith("-native-installer.run"))

    def test_reject_corrupt_release(self):
        changes = [
            lambda x: x.update(draft=True),
            lambda x: x.update(prerelease=True),
            lambda x: x.update(tag_name="main"),
            lambda x: x["assets"].pop(),
            lambda x: x["assets"][0].update(size=0),
            lambda x: x["assets"][0].update(digest=None),
            lambda x: x["assets"][0].update(browser_download_url="https://example.com/evil.run")
        ]
        for change in changes:
            bad = copy.deepcopy(self.release)
            change(bad)
            with self.subTest(bad=bad), self.assertRaises((ValueError, TypeError)):
                worker.release_installer(self.entry, bad)

    def test_separate_per_run_terminal_records(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            calls = []
            with patch.object(worker, "STATUS_ROOT", root), patch.object(worker, "STATUS_DIR", root), \
                 patch.object(worker, "STATUS_PATH", None), patch.object(worker, "run"), \
                 patch.object(worker, "install", side_effect=lambda e: calls.append(e["id"])), \
                 patch.object(worker.os, "geteuid", return_value=0):
                for index, entry in enumerate(self.entries):
                    stage = root / f"native-worker.run{index}"
                    stage.mkdir()
                    with patch.dict(worker.os.environ, {"INFILTRATOROS_INSTALL_RUNTIME": str(stage)}), \
                         patch.object(worker.sys, "argv", ["installer", entry["repository"].lower()]):
                        worker.main()
                    record = stage / "status" / (entry["repository"].lower() + "-install.status")
                    self.assertIn("state=complete\n", record.read_text())
                self.assertEqual(calls, ["fat12", "fat16"])
                with patch.object(worker.sys, "argv", ["installer", "other"]):
                    with self.assertRaisesRegex(ValueError, "signed filesystem"):
                        worker.main()

if __name__ == "__main__":
    unittest.main()
