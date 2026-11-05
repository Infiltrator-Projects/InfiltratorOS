#!/usr/bin/env python3
"""Safety tests for a common application RUN launcher and native attestation."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("application", ROOT / "packaging/install-application.py")
app = importlib.util.module_from_spec(spec)
spec.loader.exec_module(app)


class ApplicationTests(unittest.TestCase):
    def test_four_independent_applications(self):
        self.assertEqual(set(app.PRODUCTS), {"system-monitor", "calculator", "files", "defragmenter"})

    def test_release_contract_and_corruption(self):
        for slug, item in app.PRODUCTS.items():
            repo = item[0]
            version = "1.8.0-285" if slug == "defragmenter" else "1.2.3"
            name = (f"Defragmenter-{version}-local-folder.run" if slug == "defragmenter"
                    else f"{slug}-{version}-native-installer.run")
            url = f"https://github.com/Infiltrator-Projects/{repo}/releases/download/v{version}/{name}"
            payload = {"tag_name": "v" + version, "draft": False, "prerelease": False,
                       "assets": [{"name": name, "browser_download_url": url,
                                   "digest": "sha256:" + "a" * 64, "size": 123}]}
            with self.subTest(slug=slug), tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / (repo + ".json")
                path.write_text(json.dumps(payload))
                self.assertEqual(app.release(repo, slug, tmp), (version, url, "a" * 64, 123))
                payload["assets"][0]["digest"] = "sha256:no"
                path.write_text(json.dumps(payload))
                with self.assertRaises(ValueError):
                    app.release(repo, slug, tmp)

    def test_missing_optional_dpkg_field_does_not_hide_installed_package(self):
        for metadata in ("", "native"):
            with self.subTest(metadata=metadata):
                result = subprocess.CompletedProcess(
                    [], 0, "install ok installed\\t1.2.3\\t" + metadata, "")
                with patch.object(app.subprocess, "run", return_value=result):
                    self.assertEqual(
                        app.package_info("calculator"),
                        ["install ok installed", "1.2.3", metadata])

    def test_verified_download_fails_on_corruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "native.run"
            with patch.object(app.urllib.request, "urlopen", return_value=io.BytesIO(b"abc")):
                app.download("https://github.com/test", hashlib.sha256(b"abc").hexdigest(), 3, path)
            self.assertEqual(path.read_bytes(), b"abc")
            with patch.object(app.urllib.request, "urlopen", return_value=io.BytesIO(b"abcd")):
                with self.assertRaises(ValueError):
                    app.download("https://github.com/test", hashlib.sha256(b"abc").hexdigest(), 3, path)

    def test_only_matching_healthy_attestation_skips_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "calculator"
            binary.write_bytes(b"native-binary")
            binary.chmod(0o755)
            doc = Path(tmp) / "manual.pdf"
            doc.write_bytes(b"%PDF-1.4")
            evidence = Path(tmp) / "attestation"
            evidence.write_text(json.dumps({"version": "1.2.3", "installer_sha256": "a" * 64,
                                            "program_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}))
            with patch.dict(app.PRODUCTS, {"calculator": ("Calculator", "calculator", "", str(binary))}), \
                 patch.dict(app.REQUIRED, {"calculator": (str(doc),)}), \
                 patch.object(app, "package_info", return_value=["install ok installed", "1.2.3", ""]), \
                 patch.object(app.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
                self.assertTrue(app.current("calculator", "1.2.3", "a" * 64, evidence))
                self.assertFalse(app.current("calculator", "1.2.4", "a" * 64, evidence))
                binary.write_bytes(b"corrupt")
                self.assertFalse(app.current("calculator", "1.2.3", "a" * 64, evidence))
                self.assertEqual(run.call_count, 1)


if __name__ == "__main__":
    unittest.main()
