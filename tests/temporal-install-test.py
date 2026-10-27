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

    def test_failed_provider_does_not_skip_remaining_modules(self):
        entries = worker.catalogue()
        attempted, statuses = [], []
        def install(entry):
            attempted.append(entry["repository"])
            if len(attempted) == 2:
                raise RuntimeError("training failed")
        with tempfile.TemporaryDirectory() as temporary, patch.object(worker, "STATUS_DIR", Path(temporary)), \
                patch.object(worker, "write_status", side_effect=lambda *x: statuses.append(x)), \
                patch.object(worker, "required"), patch.object(worker, "install_entry", side_effect=install), \
                patch.object(worker.os, "geteuid", return_value=0), patch.dict(worker.os.environ):
            with self.assertRaisesRegex(RuntimeError, "1 resource installations failed"):
                worker.main()
        self.assertEqual(attempted, [x["repository"] for x in entries])
        self.assertFalse(any(x[0] == "complete" for x in statuses))


if __name__ == "__main__":
    unittest.main()
