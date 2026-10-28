#!/usr/bin/env python3
"""Contract tests for three independently packaged FAT installers."""
import copy
import importlib.util
import json
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
        version = "0.1.0"
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
        self.assertEqual({item["id"] for item in self.entries}, {"fat12", "fat16", "fat32"})
        self.assertEqual({item["resource"] for item in self.entries},
                         {"/System/Filesystems/FAT12.so", "/System/Filesystems/FAT16.so", "/System/Filesystems/FAT32.so"})

    def test_two_assets_and_github_native_zip(self):
        version, url, digest, size = worker.release_installer(self.entry, self.release)
        self.assertEqual(version, "0.1.0")
        self.assertEqual(digest, "a"*64)
        self.assertEqual(size, 100)
        self.assertTrue(url.endswith("-native-installer.run"))

    def test_new_triple_driver_releases_require_efi_and_kernel_assets(self):
        for entry in self.entries:
            version = "0.1.2" if entry["id"] == "fat32" else "0.1.3"
            release = copy.deepcopy(self.release)
            release["tag_name"] = "v" + version
            for asset in release["assets"]:
                asset["name"] = asset["name"].replace("0.1.0", version)
                asset["browser_download_url"] = asset["browser_download_url"].replace("0.1.0", version)
            with self.assertRaisesRegex(ValueError, "missing required native driver"):
                worker.release_installer(entry, release)
            prefix = (f"https://github.com/Infiltrator-Projects/{entry['repository']}"
                      f"/releases/download/v{version}/")
            for name in (f"FAT{entry['id'][3:]}.efi",
                         f"fat{entry['id'][3:]}-linux-6.12.107.ko"):
                release["assets"].append({"name": name, "browser_download_url": prefix + name,
                                          "digest": "sha256:" + "c"*64, "size": 300})
            self.assertEqual(worker.release_installer(entry, release)[0], version)

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

    def test_native_fat_012_requires_real_pgo_build_record(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            module = root / self.entry["resource"].lstrip("/")
            module.parent.mkdir(parents=True)
            module.write_bytes(b"test data")
            info = root / "System/Resources/Documentation" / self.entry["repository"] / "BUILD-INFO"
            info.parent.mkdir(parents=True)
            info.write_text(
                "Version: 0.1.2\n"
                f"Module: {self.entry['resource']}\n"
                f"Package: {self.entry['package']}\n"
                "Profile: aggressive\n"
                "C flags: -O3 -march=native -mtune=native -flto=auto\n"
                "SHA256: invalid\n")
            with self.assertRaisesRegex(ValueError, "requires measured two-pass PGO"):
                worker.verify(self.entry, "0.1.2", root=root, query=False)

    def test_current_native_fat_skips_download_and_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "native_installed", return_value=True), \
                 patch.object(worker, "ensure_dependencies", side_effect=AssertionError("unnecessary apt")), \
                 patch.object(worker, "download", side_effect=AssertionError("unnecessary download")):
                self.assertTrue(worker.install(self.entry))

    def test_stale_native_fat_rebuilds(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "STATUS_DIR", root), \
                 patch.object(worker, "STATUS_PATH", root / "test.status"), \
                 patch.object(worker, "native_installed", return_value=False), \
                 patch.object(worker, "ensure_dependencies") as dependencies, \
                 patch.object(worker, "download") as download, \
                 patch.object(worker, "run") as compile_install, \
                 patch.object(worker, "verify") as verify:
                self.assertFalse(worker.install(self.entry))
                dependencies.assert_called_once()
                download.assert_called_once()
                compile_install.assert_called_once()
                verify.assert_called_once()


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
                self.assertEqual(calls, ["fat12", "fat16", "fat32"])
                with patch.object(worker.sys, "argv", ["installer", "other"]):
                    with self.assertRaisesRegex(ValueError, "signed filesystem"):
                        worker.main()

if __name__ == "__main__":
    unittest.main()
