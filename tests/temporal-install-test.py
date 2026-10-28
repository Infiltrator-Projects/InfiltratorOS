#!/usr/bin/env python3
"""Exercise the actual provisioning selection and installed module contract."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("temporal_install", ROOT / "packaging/install-temporal-resources.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class InstallationContract(unittest.TestCase):
    def setUp(self):
        self.entry = worker.catalogue()[0]
        version = "1.2.3"
        slug = self.entry["repository"].lower()
        names = [f"{slug}-{version}-native-installer.run", f"{slug}-{version}-source.zip", f"{self.entry['package']}_{version}_amd64.deb"]
        prefix = f"https://github.com/Infiltrator-Projects/{self.entry['repository']}/releases/download/v{version}/"
        self.release = {"tag_name": "v" + version, "draft": False, "prerelease": False,
                        "assets": [{"name": name, "browser_download_url": prefix + name,
                                    "digest": "sha256:" + "a" * 64, "size": 123} for name in names]}

    def test_full_catalogue(self):
        entries = worker.catalogue()
        self.assertEqual(len(entries), 54)
        self.assertEqual(sum(x["kind"] == "calendar" for x in entries), 31)
        self.assertEqual(sum(x["kind"] == "clock" for x in entries), 23)
        self.assertEqual({x["id"] for x in entries if x["kind"] == "clock"}, set("babylonian-ancient babylonian-hours binary chinese-ke chinese-time decimal hexadecimal indian-ghati internet italian-hours japanese-temporal japanese-temporal-early julian mean-solar modified-julian nuremberg-hours nuremberg-solar roman-temporal sidereal solar standard-12 standard-24 unix".split()))
        self.assertEqual({x["id"] for x in entries if x["kind"] == "calendar"}, set("armenian-traditional bahai buddhist byzantine chinese coptic dangi egyptian-nabonassar ethiopian ethiopic-amete-alem french-republican gregorian hebrew iso-week indian international-fixed islamic islamic-civil islamic-tbla islamic-umalqura japanese julian mayan minguo persian positivist revised-julian roman swedish-historical thai-buddhist-solar world".split()))

    def test_exact_native_installer(self):
        version, url, digest, size = worker.release_installer(self.entry, self.release)
        self.assertEqual((version, digest, size), ("1.2.3", "a" * 64, 123))
        self.assertTrue(url.endswith("-native-installer.run"))

    def test_rejects_incomplete_or_untrusted_release(self):
        mutations = [lambda x: x.update(draft=True), lambda x: x.update(prerelease=True),
                     lambda x: x.update(tag_name="main"), lambda x: x["assets"].pop(),
                     lambda x: x["assets"][0].update(browser_download_url="https://example.com/provider.run"),
                     lambda x: x["assets"][0].update(digest=None),
                     lambda x: x["assets"][0].update(size=0)]
        for mutate in mutations:
            release = copy.deepcopy(self.release)
            mutate(release)
            with self.subTest(release=release), self.assertRaises((ValueError, TypeError)):
                worker.release_installer(self.entry, release)

    def test_each_provider_and_each_install_run_has_independent_status(self):
        entries = worker.catalogue()
        first, second = entries[:2]
        attempted = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(worker, "STATUS_ROOT", root), patch.object(worker, "STATUS_DIR", root), \
                 patch.object(worker, "STATUS_PATH", None), patch.object(worker, "required"), \
                 patch.object(worker, "install_entry", side_effect=lambda entry: attempted.append(entry["repository"])), \
                 patch.object(worker.os, "geteuid", return_value=0):
                for index, entry in enumerate((first, second)):
                    runtime = root / f"native-worker.run{index}"
                    runtime.mkdir()
                    with patch.dict(worker.os.environ, {"INFILTRATOROS_INSTALL_RUNTIME": str(runtime)}), \
                         patch.object(worker.sys, "argv", ["installer", entry["repository"].lower()]):
                        worker.main()
                    status = runtime / "status" / (entry["repository"].lower() + "-install.status")
                    self.assertIn("state=complete\n", status.read_text())
                    self.assertEqual(len(list((runtime / "status").glob("*-install.status"))), 1)
                self.assertEqual(attempted, [first["repository"], second["repository"]])
                with patch.object(worker.sys, "argv", ["installer", "unknown-provider"]):
                    with self.assertRaisesRegex(ValueError, "signed calendar/clock"):
                        worker.main()
                with patch.object(worker.sys, "argv", ["installer", "--list"]):
                    from contextlib import redirect_stdout
                    import io
                    output = io.StringIO()
                    with redirect_stdout(output):
                        worker.main()
                    self.assertEqual(len(output.getvalue().splitlines()), 54)

    def test_unchanged_native_release_skips_download_and_rebuild(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \\
                 patch.object(worker, "native_installed", return_value=True), \\
                 patch.object(worker, "ensure_dependencies", side_effect=AssertionError("unnecessary apt")), \\
                 patch.object(worker, "download_installer", side_effect=AssertionError("unnecessary download")):
                self.assertTrue(worker.install_entry(self.entry))

    def test_stale_native_release_uses_original_build_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \\
                 patch.object(worker, "STATUS_DIR", root), \\
                 patch.object(worker, "STATUS_PATH", root / "test.status"), \\
                 patch.object(worker, "native_installed", return_value=False), \\
                 patch.object(worker, "ensure_dependencies") as dependencies, \\
                 patch.object(worker, "download_installer") as download, \\
                 patch.object(worker, "required") as compile_install, \\
                 patch.object(worker, "verify_installation") as verify:
                self.assertFalse(worker.install_entry(self.entry))
                dependencies.assert_called_once()
                download.assert_called_once()
                compile_install.assert_called_once()
                verify.assert_called_once()


    def test_failed_provider_does_not_mark_other_provider_complete(self):
        entry = worker.catalogue()[0]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "native-worker.failed"
            runtime.mkdir()
            with patch.object(worker, "STATUS_ROOT", root), patch.object(worker, "STATUS_DIR", root), \
                 patch.object(worker, "STATUS_PATH", None), patch.object(worker, "required"), \
                 patch.object(worker, "install_entry", side_effect=RuntimeError("training failed")), \
                 patch.object(worker.os, "geteuid", return_value=0), \
                 patch.dict(worker.os.environ, {"INFILTRATOROS_INSTALL_RUNTIME": str(runtime)}), \
                 patch.object(worker.sys, "argv", ["installer", entry["repository"].lower()]):
                with self.assertRaisesRegex(RuntimeError, "training failed"):
                    worker.main()
                status = runtime / "status" / (entry["repository"].lower() + "-install.status")
                self.assertIn("state=running\n", status.read_text())
                self.assertFalse(any("state=complete" in x.read_text() for x in (runtime / "status").glob("*-install.status")))


if __name__ == "__main__":
    unittest.main()
