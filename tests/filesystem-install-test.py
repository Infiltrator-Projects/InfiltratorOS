#!/usr/bin/env python3
"""Contract tests for independent, checksum-verified native RUN delegation."""
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("worker", ROOT / "packaging/install-filesystem-resources.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class NativeRunContract(unittest.TestCase):
    def setUp(self):
        self.entry = worker.catalogue()[0]
        self.slug = self.entry["repository"].lower()
        self.version = "0.1.0"
        prefix = f"https://github.com/Infiltrator-Projects/{self.entry['repository']}/releases/download/v{self.version}/"
        run = f"{self.slug}-{self.version}-native-installer.run"
        deb = f"{self.entry['package']}_{self.version}_amd64.deb"
        names = [run, deb]
        self.release = {"tag_name": "v" + self.version, "draft": False, "prerelease": False,
                        "target_commitish": "a" * 40,
                        "assets": [{"name": name, "browser_download_url": prefix + name,
                                    "digest": "sha256:" + "b" * 64, "size": 123} for name in names]}

    def test_catalogue(self):
        self.assertEqual(len(worker.catalogue()), 5)
        self.assertEqual(len(worker.additional_catalogue()), 18)

    def test_release_contract(self):
        version, url, digest, size = worker.release_installer(self.entry, self.release)
        self.assertEqual(version, self.version)
        self.assertTrue(url.endswith("-native-installer.run"))
        self.assertEqual(digest, "b" * 64)
        self.assertEqual(size, 123)

    def test_reject_unpublished_or_tampered_release(self):
        for key in ("draft", "prerelease"):
            bad = json.loads(json.dumps(self.release))
            bad[key] = True
            with self.assertRaises(ValueError):
                worker.release_installer(self.entry, bad)
        bad = json.loads(json.dumps(self.release))
        bad["assets"][0]["digest"] = "sha256:bad"
        with self.assertRaises(ValueError):
            worker.release_installer(self.entry, bad)

    def test_installer_delegation_uses_published_run(self):
        with tempfile.TemporaryDirectory() as directory:
            snapshot = Path(directory)
            (snapshot / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(snapshot)}), \
                 patch.object(worker, "STATUS_DIR", snapshot), \
                 patch.object(worker, "download") as download, \
                 patch.object(worker, "run") as execute, \
                 patch.object(worker, "status"), \
                 patch.object(worker, "native_current", side_effect=[False, True]):
                result = worker.install(self.entry)
            self.assertFalse(result)
            self.assertEqual(download.call_count, 1)
            args = execute.call_args.args[0]
            self.assertEqual(args[1:], ["--profile", "aggressive", "--system-package-mode"])
            self.assertEqual(args[0].suffix, ".run")

    def test_current_native_installed_skips_download_and_rebuild(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": directory}), \
                 patch.object(worker, "native_current", return_value=True), \
                 patch.object(worker, "download", side_effect=AssertionError("download attempted")):
                self.assertTrue(worker.install(self.entry))

    def test_reject_oversized_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / (self.entry["repository"] + ".json")).write_bytes(b"x" * (4 * 1024 * 1024 + 1))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": directory}):
                with self.assertRaisesRegex(ValueError, "large|Oversized"):
                    worker.install(self.entry)


if __name__ == "__main__":
    unittest.main()
