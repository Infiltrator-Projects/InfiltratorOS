#!/usr/bin/env python3
"""Contract tests for four independently packaged filesystem installers."""
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
        self.assertEqual({item["id"] for item in self.entries}, {"fat12", "fat16", "fat32", "ofs"})
        self.assertEqual({item["resource"] for item in self.entries},
                         {"/System/Filesystems/FAT12.so", "/System/Filesystems/FAT16.so", "/System/Filesystems/FAT32.so", "/System/Filesystems/OFS.so"})

    def test_two_assets_and_github_native_zip(self):
        version, url, digest, size = worker.release_installer(self.entry, self.release)
        self.assertEqual(version, "0.1.0")
        self.assertEqual(digest, "a"*64)
        self.assertEqual(size, 100)
        self.assertTrue(url.endswith("-native-installer.run"))

    def test_new_triple_driver_releases_require_efi_and_kernel_assets(self):
        for entry in self.entries[:3]:
            version = "0.1.2" if entry["id"] == "fat32" else "0.1.3"
            release = copy.deepcopy(self.release)
            release["tag_name"] = "v" + version
            prefix = (f"https://github.com/Infiltrator-Projects/{entry['repository']}"
                      f"/releases/download/v{version}/")
            for asset in release["assets"]:
                name = asset["name"].replace(self.entry["repository"].lower(),
                                             entry["repository"].lower())
                name = name.replace(self.entry["package"], entry["package"])
                name = name.replace("0.1.0", version)
                asset["name"] = name
                asset["browser_download_url"] = prefix + name
            with self.assertRaisesRegex(ValueError, "missing required native driver"):
                worker.release_installer(entry, release)
            prefix = (f"https://github.com/Infiltrator-Projects/{entry['repository']}"
                      f"/releases/download/v{version}/")
            for name in (f"FAT{entry['id'][3:]}.efi",
                         f"fat{entry['id'][3:]}-linux-6.12.107.ko"):
                release["assets"].append({"name": name, "browser_download_url": prefix + name,
                                          "digest": "sha256:" + "c"*64, "size": 300})
            self.assertEqual(worker.release_installer(entry, release)[0], version)

    def test_ofs_release_requires_same_so_and_checksums(self):
        entry = self.entries[3]
        version = "0.1.1"
        prefix = (f"https://github.com/Infiltrator-Projects/{entry['repository']}"
                  f"/releases/download/v{version}/")
        names = [f"filesystem-ofs-{version}-native-installer.run",
                 f"infiltrator-filesystem-ofs_{version}_amd64.deb", "OFS.so", "SHA256SUMS",
                 "ofs-linux-6.12.107.ko"]
        release = {"tag_name": "v" + version, "assets": [
            {"name": name, "browser_download_url": prefix + name,
             "digest": "sha256:" + "c" * 64, "size": 100} for name in names]}
        self.assertEqual(worker.release_installer(entry, release)[0], version)
        for missing in names:
            bad = copy.deepcopy(release)
            bad["assets"] = [x for x in bad["assets"] if x["name"] != missing]
            with self.assertRaises(ValueError):
                worker.release_installer(entry, bad)

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

    def test_native_fat_requires_pdf_and_pgo_in_debian_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            module = root / self.entry["resource"].lstrip("/")
            module.parent.mkdir(parents=True)
            module.write_bytes(b"test data")
            guide = root / "System/Resources/Documentation" / self.entry["repository"] / "FAT12.pdf"
            guide.parent.mkdir(parents=True)
            info = ("Version: 0.1.2+native1\n"
                    "X-Infiltrator-Native-Profile: aggressive\n"
                    "X-Infiltrator-LTO: enabled\n"
                    "X-Infiltrator-PGO: disabled\n")
            with patch.object(worker.subprocess, "check_output", return_value=info):
                with self.assertRaisesRegex(ValueError, "PDF"):
                    worker.verify(self.entry, "0.1.2", root=root, query=False)
                guide.write_bytes(b"%PDF-1.4\n")
                with self.assertRaisesRegex(ValueError, "X-Infiltrator-PGO"):
                    worker.verify(self.entry, "0.1.2", root=root, query=False)

    def test_safe_cleanup_all_four_filesystems(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for entry in self.entries:
                docs = root / "System/Resources/Documentation" / entry["repository"]
                docs.mkdir(parents=True)
                name = entry["resource"].rsplit("/", 1)[1].removesuffix(".so") + ".pdf"
                (docs / name).write_bytes(b"%PDF-1.4\n")
                for legacy in ("BUILD-INFO", "README.md", "MIGRATION-PROVENANCE.md", "LICENSE"):
                    (docs / legacy).write_text("obsolete")
                (docs / "personal.txt").write_text("keep")
                self.assertEqual(worker.cleanup_legacy_docs(
                    entry, root=root, owner_lookup=lambda path: path.name == "LICENSE"), 3)
                self.assertEqual({p.name for p in docs.iterdir()},
                                 {name, "LICENSE", "personal.txt"})

    def test_current_native_fat_skips_download_and_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "native_installed", return_value=True), \
                 patch.object(worker, "ensure_dependencies", side_effect=AssertionError("unnecessary apt")), \
                 patch.object(worker, "download", side_effect=AssertionError("unnecessary download")), \
                 patch.object(worker, "cleanup_legacy_docs") as cleanup:
                self.assertTrue(worker.install(self.entry))
                cleanup.assert_called_once_with(self.entry)

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
                 patch.object(worker, "verify") as verify, \
                 patch.object(worker, "cleanup_legacy_docs") as cleanup:
                self.assertFalse(worker.install(self.entry))
                cleanup.assert_called_once_with(self.entry)
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
                self.assertEqual(calls, ["fat12", "fat16", "fat32", "ofs"])
                with patch.object(worker.sys, "argv", ["installer", "other"]):
                    with self.assertRaisesRegex(ValueError, "signed filesystem"):
                        worker.main()

if __name__ == "__main__":
    unittest.main()
