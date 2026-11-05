#!/usr/bin/env python3
"""Offline safety tests for full 23-filesystem provisioning readiness."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "filesystem_worker", ROOT / "packaging/install-filesystem-resources.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


def release_for(entry, version="1.0.0"):
    repo = entry["repository"]
    names = [
        f"{repo.lower()}-{version}-native-installer.run",
        f"{entry['package']}_{version}_amd64.deb",
    ]
    base = f"https://github.com/Infiltrator-Projects/{repo}/releases/download/v{version}/"
    return {
        "tag_name": "v" + version, "draft": False, "prerelease": False,
        "target_commitish": "b" * 40,
        "assets": [{"name": name, "browser_download_url": base + name,
                    "digest": "sha256:" + "a" * 64, "size": 100}
                   for name in names],
    }


class AdditionalFilesystemTests(unittest.TestCase):
    def test_one_authoritative_module_per_filesystem(self):
        baseline = worker.catalogue()
        additional = worker.additional_catalogue()
        complete = worker.all_catalogue()
        self.assertEqual((len(baseline), len(additional), len(complete)), (5, 18, 23))
        self.assertEqual(len({item["resource"] for item in complete}), 23)
        self.assertEqual(len({item["package"] for item in complete}), 23)
        self.assertTrue(all(item["abi"].startswith("infiltrator_filesystem_")
                            for item in additional))
        self.assertEqual({item["id"] for item in additional}, {
            "ntfs", "ext2", "ext3", "ext4", "xfs", "btrfs", "ffs", "sfs",
            "pfs3", "hfs", "hfsplus", "minix", "ufs2", "zfs", "apfs", "swap", "sfs2", "ufs1"
        })

    def test_provider_abi_identity_is_component_owned(self):
        # The OS validates the signed catalogue, not an independently
        # compiled module's exported ABI: that qualification belongs to RUN.
        entries = {e["id"]: e for e in worker.additional_catalogue()}
        self.assertEqual(entries["ext2"]["abi"], "infiltrator_filesystem_module_v1")
        for format_id in ("ext3", "ext4"):
            self.assertEqual(entries[format_id]["abi"], "infiltrator_filesystem_ext_v1")

    def test_deb_is_portable_and_run_rebuilds_native_package_contract(self):
        """Admission checks the same two published assets as FAT32/OFS."""
        for entry in worker.additional_catalogue():
            with self.subTest(entry=entry["id"]):
                release = release_for(entry)
                names = {asset["name"] for asset in release["assets"]}
                self.assertEqual(names, {
                    entry["package"] + "_1.0.0_amd64.deb",
                    entry["repository"].lower() + "-1.0.0-native-installer.run",
                })
                self.assertEqual(worker.release_installer(entry, release)[0], "1.0.0")

    def test_all_eighteen_require_real_complete_release_assets(self):
        for entry in worker.additional_catalogue():
            with self.subTest(entry=entry["id"]):
                release = release_for(entry)
                self.assertEqual(worker.release_installer(entry, release)[0], "1.0.0")
                for asset in list(release["assets"]):
                    corrupted = dict(release)
                    corrupted["assets"] = [other for other in release["assets"]
                                           if other is not asset]
                    with self.assertRaises(ValueError):
                        worker.release_installer(entry, corrupted)
                archived = dict(release)
                archived["assets"] = [{"name": "build.so", "size": 100,
                                       "digest": "sha256:" + "a" * 64,
                                       "browser_download_url": "https://github.com/" +
                                           "Infiltrator-Projects/" + entry["repository"]}]
                with self.assertRaises(ValueError):
                    worker.release_installer(entry, archived)

    def test_discovery_records_unpublished_without_faking_installs(self):
        selected = worker.additional_catalogue()[0]
        release = release_for(selected)
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            stage = base / "native-worker.test"
            snapshot = stage / "release-json"
            snapshot.mkdir(parents=True)
            def fake_fetch(request, timeout=None):
                if selected["repository"] in request.full_url:
                    return io.BytesIO(json.dumps(release).encode())
                raise urllib.error.HTTPError(request.full_url, 404, "Not Found", {}, None)
            with patch.object(worker, "STATUS_ROOT", base), \
                 patch.object(worker.urllib.request, "urlopen", side_effect=fake_fetch):
                worker.discover_additional(snapshot)
            ready = (stage / "filesystems-optional.list").read_text().splitlines()
            pending = (stage / "filesystems-pending.list").read_text().splitlines()
            self.assertEqual(ready, [selected["repository"].lower()])
            self.assertEqual(len(pending), 17)
            self.assertEqual(json.loads(
                (snapshot / (selected["repository"] + ".json")).read_text()),
                release)
            self.assertEqual(len(list(snapshot.glob("*.json"))), 1)

    def test_unqualified_release_remains_pending(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            stage = base / "native-worker.test"
            snapshot = stage / "release-json"
            snapshot.mkdir(parents=True)
            def fake_fetch(request, timeout=None):
                return io.BytesIO(json.dumps({"tag_name": "v1.0.0",
                                               "assets": []}).encode())
            with patch.object(worker, "STATUS_ROOT", base), \
                 patch.object(worker.urllib.request, "urlopen", side_effect=fake_fetch):
                worker.discover_additional(snapshot)
            self.assertEqual((stage / "filesystems-optional.list").read_text(), "")
            self.assertEqual(len((stage / "filesystems-pending.list").read_text().splitlines()), 18)
            self.assertFalse(list(snapshot.glob("*.json")))

    def test_release_with_branch_source_remains_pending(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            stage = base / "native-worker.test"
            snapshot = stage / "release-json"
            snapshot.mkdir(parents=True)
            entries = {entry["repository"]: entry for entry in worker.additional_catalogue()}
            def fake_fetch(request, timeout=None):
                repository = request.full_url.split("/")[-3]
                release = dict(release_for(entries[repository]), target_commitish="main")
                return io.BytesIO(json.dumps(release).encode())
            with patch.object(worker, "STATUS_ROOT", base), \
                 patch.object(worker.urllib.request, "urlopen", side_effect=fake_fetch):
                worker.discover_additional(snapshot)
            self.assertEqual((stage / "filesystems-optional.list").read_text(), "")
            pending = (stage / "filesystems-pending.list").read_text().splitlines()
            self.assertEqual(len(pending), 18)
            self.assertTrue(all("no exact source commit identity" in line for line in pending))
            self.assertFalse(list(snapshot.glob("*.json")))


if __name__ == "__main__":
    unittest.main()
