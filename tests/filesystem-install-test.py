#!/usr/bin/env python3
"""Contract tests for five independently packaged filesystem installers."""
import copy
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
            "target_commitish": "b" * 40,
            "target_commitish": "1"*40,
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
        self.assertEqual({item["id"] for item in self.entries}, {"fat12", "fat16", "fat32", "ofs", "exfat"})
        self.assertEqual({item["resource"] for item in self.entries},
                         {"/System/Filesystems/FAT12.so", "/System/Filesystems/FAT16.so", "/System/Filesystems/FAT32.so", "/System/Filesystems/OFS.so", "/System/Filesystems/exFAT.so"})

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
        release = {"tag_name": "v" + version, "target_commitish": "b" * 40, "assets": [
            {"name": name, "browser_download_url": prefix + name,
             "digest": "sha256:" + "c" * 64, "size": 100} for name in names]}
        self.assertEqual(worker.release_installer(entry, release)[0], version)
        for missing in names:
            bad = copy.deepcopy(release)
            bad["assets"] = [x for x in bad["assets"] if x["name"] != missing]
            with self.assertRaises(ValueError):
                worker.release_installer(entry, bad)

    def test_exfat_requires_complete_checksum_bound_release(self):
        entry = self.entries[4]
        version = "0.1.3"
        names = [f"filesystem-exfat-{version}-native-installer.run",
                 f"infiltrator-filesystem-exfat_{version}_amd64.deb",
                 "exFAT.so", "exfat-linux-6.12.107.ko", "SHA256SUMS"]
        prefix = f"https://github.com/Infiltrator-Projects/Filesystem-exFAT/releases/download/v{version}/"
        release = {"tag_name": "v" + version, "target_commitish": "b" * 40, "assets": [
            {"name": name, "browser_download_url": prefix + name,
             "digest": "sha256:" + "a" * 64, "size": 123} for name in names]}
        self.assertEqual(worker.release_installer(entry, release)[0], version)
        old = copy.deepcopy(release)
        old["tag_name"] = "v0.1.2"
        with self.assertRaisesRegex(ValueError, "SIGPIPE"):
            worker.release_installer(entry, old)
        for item in names:
            bad = copy.deepcopy(release)
            bad["assets"] = [x for x in bad["assets"] if x["name"] != item]
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

    def test_exfat_unified_runtime_allowlist_preserves_freestanding_other_filesystems(self):
        exfat = self.entries[4]
        fat = self.entries[0]
        allowed = (" 0x0000000000000001 (NEEDED) Shared library: [libc.so.6]\n"
                   " 0x0000000000000001 (NEEDED) Shared library: [libcrypto.so.3]\n"
                   " 0x0000000000000001 (NEEDED) Shared library: [libsqlite3.so.0]\n")
        # The actual exFAT v0.1.3 aggressive build includes glibc's
        # x86-64 dynamic loader as a DT_NEEDED entry.
        with_loader = allowed + (
            " 0x0000000000000001 (NEEDED) Shared library: [ld-linux-x86-64.so.2]\n")
        worker.validate_runtime_dependencies(exfat, with_loader)
        with self.assertRaisesRegex(ValueError, "ELF runtime"):
            worker.validate_runtime_dependencies(fat, with_loader)
        with self.assertRaisesRegex(ValueError, "exFAT runtime"):
            worker.validate_runtime_dependencies(exfat, allowed +
                " 0x0000000000000001 (NEEDED) Shared library: [libbad.so.1]\n")

    def test_established_consumer_abis_are_all_required(self):
        formats = {entry["id"]: set(worker.required_interface_symbols(entry))
                   for entry in self.entries}
        for format_id in ("fat12", "fat16", "fat32"):
            self.assertIn("infiltrator_filesystem_boot_v1", formats[format_id])
            self.assertIn("infiltrator_filesystem_defrag_v1", formats[format_id])
        self.assertIn("infiltrator_filesystem_module_v5", formats["fat16"])
        self.assertIn("infiltrator_filesystem_exfat_defrag_v1", formats["exfat"])
        self.assertIn("infiltrator_filesystem_defrag_v2", formats["ofs"])

    def test_abi_matching_rejects_substrings_and_undefined_exports(self):
        row = lambda number, binding, visibility, section, name: (
            f"    {number}: 0000000000000000    32 FUNC    "
            f"{binding} {visibility} {section} {name}\n")
        forged = (
            row(1, "GLOBAL", "DEFAULT", "UND", "infiltrator_filesystem_module_v1") +
            row(2, "GLOBAL", "DEFAULT", "12", "infiltrator_filesystem_module_v1_suffix") +
            row(3, "LOCAL", "DEFAULT", "12", "infiltrator_filesystem_module_v3") +
            row(4, "GLOBAL", "HIDDEN", "12", "infiltrator_filesystem_module_v4"))
        self.assertEqual(worker.exported_interface_symbols(forged),
                         {"infiltrator_filesystem_module_v1_suffix"})
        published = forged + row(5, "GLOBAL", "DEFAULT", "12",
                                 "infiltrator_filesystem_module_v1@@IFS_1")
        self.assertIn("infiltrator_filesystem_module_v1",
                      worker.exported_interface_symbols(published))
        spoofed_object = "  6: 0000000000000000 8 OBJECT GLOBAL DEFAULT 12 infiltrator_filesystem_module_v2\n"
        self.assertNotIn("infiltrator_filesystem_module_v2",
                         worker.exported_interface_symbols(spoofed_object))

    def test_so_verification_checks_pgo_independently_of_the_pdf(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            module = root / self.entry["resource"].lstrip("/")
            module.parent.mkdir(parents=True)
            module.write_bytes(b"test data")
            info = ("Version: 0.1.2+native1\n"
                    "X-Infiltrator-Native-Profile: aggressive\n"
                    "X-Infiltrator-LTO: enabled\n"
                    "X-Infiltrator-PGO: disabled\n")
            self.assertFalse(worker.manual_path(self.entry, root).exists())
            with patch.object(worker.subprocess, "check_output", return_value=info):
                with self.assertRaisesRegex(ValueError, "X-Infiltrator-PGO"):
                    worker.verify(self.entry, "0.1.2", root=root, query=False)

    def test_missing_or_damaged_manual_repairs_only_the_pdf(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            installed = root / "installed"
            module = installed / self.entry["resource"].lstrip("/")
            module.parent.mkdir(parents=True)
            module.write_bytes(b"existing SO fixture")
            original = module.stat()
            release = copy.deepcopy(self.release)
            revision = release["target_commitish"]
            version = release["tag_name"][1:]
            image = root / "package"
            pdf = b"%PDF-1.4\nmanual\n"
            digest = hashlib.sha256(pdf).hexdigest()
            manual = worker.manual_path(self.entry, image)
            manual.parent.mkdir(parents=True)
            manual.write_bytes(pdf)
            control = image / "DEBIAN"
            control.mkdir()
            (control / "control").write_text(
                f"Package: {self.entry['package']}\nVersion: {version}\nArchitecture: amd64\n"
                "Maintainer: Fixture <test@example.invalid>\nDescription: Manual repair fixture\n"
                f"X-Infiltrator-Source-Revision: {revision}\nX-Infiltrator-PDF-SHA256: {digest}\n")
            package = root / "manual.deb"
            subprocess.run(["dpkg-deb", "-b", image, package], check=True, stdout=subprocess.DEVNULL)
            payload = package.read_bytes()
            asset = next(a for a in release["assets"] if a["name"].endswith(".deb"))
            asset.update(size=len(payload), digest="sha256:" + hashlib.sha256(payload).hexdigest())
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(release))
            metadata = {"Version": version + "+native1", "X-Infiltrator-Source-Revision": revision,
                        "X-Infiltrator-PDF-SHA256": digest}
            original_repair = worker.repair_manual
            target = worker.manual_path(self.entry, installed)
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "STATUS_DIR", root), \
                 patch.object(worker, "STATUS_PATH", root / "test.status"), \
                 patch.object(worker, "verified_current_version", return_value=(version, metadata)), \
                 patch.object(worker, "repair_manual", side_effect=lambda *args: original_repair(*args, root=installed)), \
                 patch.object(worker, "verify_owned_path"), \
                 patch.object(worker, "ensure_dependencies", side_effect=AssertionError("unnecessary build")), \
                 patch.object(worker, "open_url", side_effect=lambda url: io.BytesIO(payload)) as fetch, \
                 patch.object(worker, "run", wraps=worker.run) as command:
                self.assertFalse(target.exists())
                self.assertFalse(worker.install(self.entry))
                self.assertEqual(target.read_bytes(), pdf)
                target.write_bytes(b"%PDF-1.4\ndamaged\n")
                self.assertFalse(worker.install(self.entry))
                self.assertEqual(target.read_bytes(), pdf)
                self.assertTrue(worker.install(self.entry))
                self.assertEqual([call.args[0] for call in fetch.call_args_list], [asset["browser_download_url"]] * 2)
                self.assertEqual([call.args[0][:2] for call in command.call_args_list],
                                 [["/usr/bin/dpkg-deb", "-x"]] * 2)
                self.assertEqual(module.stat().st_ino, original.st_ino)
                self.assertEqual(module.read_bytes(), b"existing SO fixture")

    def test_only_a_missing_or_older_so_requires_a_build(self):
        revision = "a" * 40
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            module = root / self.entry["resource"].lstrip("/")
            self.assertIsNone(worker.verified_current_version(self.entry, "1.0.2", revision, root))
            module.parent.mkdir(parents=True)
            module.write_bytes(b"installed SO fixture")
            with patch.object(worker, "package_version", return_value="1.0.1+native1"), \
                 patch.object(worker, "verify") as verify:
                self.assertIsNone(worker.verified_current_version(self.entry, "1.0.2", revision, root))
                verify.assert_not_called()
            for version, profile, expected_revision in (("1.0.2", "portable", revision),
                                                        ("1.0.2+native1", "aggressive", revision),
                                                        ("1.0.10", "portable", None)):
                with patch.object(worker, "package_version", return_value=version), \
                     patch.object(worker, "verify") as verify:
                    provider = version.removesuffix("+native1")
                    self.assertEqual(worker.verified_current_version(self.entry, "1.0.2", revision, root),
                                     (provider, verify.return_value))
                    verify.assert_called_once_with(self.entry, provider, root=root,
                                                   revision=expected_revision, profile=profile)

    def test_current_native_fat_skips_download_and_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "verified_current_version", return_value=("0.1.0", {})), \
                 patch.object(worker, "ensure_dependencies", side_effect=AssertionError("unnecessary apt")), \
                 patch.object(worker, "download", side_effect=AssertionError("unnecessary download")), \
                 patch.object(worker, "repair_manual", return_value=False) as manual:
                self.assertTrue(worker.install(self.entry))
                manual.assert_called_once_with(self.entry, self.release, "0.1.0", {})

    def test_newer_so_skips_build_and_older_kernel_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "verified_current_version", return_value=("0.1.1", {})), \
                 patch.object(worker, "stored_kernel_matches", side_effect=AssertionError("older KO check")), \
                 patch.object(worker, "ensure_dependencies", side_effect=AssertionError("unnecessary apt")), \
                 patch.object(worker, "download", side_effect=AssertionError("unnecessary download")), \
                 patch.object(worker, "repair_manual", return_value=False) as manual:
                self.assertTrue(worker.install(self.entry))
                manual.assert_called_once_with(self.entry, self.release, "0.1.0", {})

    def test_store_one_current_kernel_module_without_rebuilding_current_so(self):
        entry = next(e for e in worker.additional_catalogue() if e["id"] == "ffs")
        version = "1.0.2"
        repo = entry["repository"]
        names = [f"{repo.lower()}-{version}-native-installer.run",
                 f"{entry['package']}_{version}_amd64.deb", worker.kernel_asset_name(entry)]
        base = f"https://github.com/Infiltrator-Projects/{repo}/releases/download/v{version}/"
        payload = b"verified kernel fixture"
        assets = [{"name": name, "browser_download_url": base + name,
                   "size": len(payload), "digest": "sha256:" + hashlib.sha256(payload).hexdigest()}
                  for name in names]
        release = {"tag_name": "v" + version, "target_commitish": "a" * 40, "assets": assets}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            directory = root / "System/Kernel/Filesystems"
            directory.mkdir(parents=True)
            (root / (repo + ".json")).write_text(json.dumps(release))
            target = directory / "ffs.ko"
            target.write_bytes(b"previous stored build")
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "KERNEL_DIR", directory), \
                 patch.object(worker, "STATUS_DIR", root), \
                 patch.object(worker, "STATUS_PATH", root / "test.status"), \
                 patch.object(worker, "verified_current_version", return_value=(version, {})), \
                 patch.object(worker, "ensure_dependencies", side_effect=AssertionError("unnecessary apt")), \
                 patch.object(worker, "run", side_effect=AssertionError("unnecessary build or load")), \
                 patch.object(worker, "repair_manual", return_value=False), \
                 patch.object(worker, "validate_kernel_module"), \
                 patch.object(worker, "open_url") as fetch:
                fetch.return_value = io.BytesIO(b"bad bytes")
                with self.assertRaisesRegex(ValueError, "SHA256 or size mismatch"):
                    worker.install(entry)
                self.assertEqual(target.read_bytes(), b"previous stored build")
                fetch.reset_mock()
                fetch.return_value = io.BytesIO(payload)
                self.assertFalse(worker.install(entry))
                self.assertEqual(target.read_bytes(), payload)
                self.assertEqual({path.name for path in directory.iterdir()}, {"ffs.ko"})
                self.assertTrue(worker.install(entry))
                fetch.assert_called_once_with(base + names[-1])
                release["assets"].pop()
                (root / (repo + ".json")).write_text(json.dumps(release))
                with self.assertRaisesRegex(ValueError, "previously staged kernel module"):
                    worker.install(entry)
                self.assertEqual(target.read_bytes(), payload)

    def test_swap_cannot_supply_a_kernel_module_asset(self):
        entry = next(e for e in worker.additional_catalogue() if e["id"] == "swap")
        release = copy.deepcopy(self.release)
        names = [f"{entry['repository'].lower()}-0.1.0-native-installer.run",
                 f"{entry['package']}_0.1.0_amd64.deb", worker.kernel_asset_name(entry)]
        base = f"https://github.com/Infiltrator-Projects/{entry['repository']}/releases/download/v0.1.0/"
        release["assets"] = [{"name": name, "browser_download_url": base + name,
                              "digest": "sha256:" + "a" * 64, "size": 100} for name in names]
        with self.assertRaisesRegex(ValueError, "missing required native driver"):
            worker.release_installer(entry, release)

    def test_source_commit_required_before_reusing_or_installing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            release = dict(self.release)
            release.pop("target_commitish")
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "ensure_dependencies",
                              side_effect=AssertionError("no build on invalid release")):
                with self.assertRaisesRegex(ValueError, "exact source commit"):
                    worker.install(self.entry)

    def test_stale_native_fat_rebuilds(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / (self.entry["repository"] + ".json")).write_text(json.dumps(self.release))
            with patch.dict(worker.os.environ, {"INFILTRATOROS_RELEASE_DIR": str(root)}), \
                 patch.object(worker, "STATUS_DIR", root), \
                 patch.object(worker, "STATUS_PATH", root / "test.status"), \
                 patch.object(worker, "verified_current_version", return_value=None), \
                 patch.object(worker, "ensure_dependencies") as dependencies, \
                 patch.object(worker, "download") as download, \
                 patch.object(worker, "run") as compile_install, \
                 patch.object(worker, "verify") as verify, \
                 patch.object(worker, "repair_manual", return_value=False) as manual:
                self.assertFalse(worker.install(self.entry))
                dependencies.assert_called_once()
                download.assert_called_once()
                self.assertEqual(compile_install.call_count, 2)
                self.assertEqual(compile_install.call_args_list[1].args[0][:3],
                                 ["/usr/bin/dpkg", "--refuse-downgrade", "-i"])
                verify.assert_called_once()
                manual.assert_called_once()


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
                self.assertEqual(calls, ["fat12", "fat16", "fat32", "ofs", "exfat"])
                with patch.object(worker.sys, "argv", ["installer", "other"]):
                    with self.assertRaisesRegex(ValueError, "signed filesystem"):
                        worker.main()

if __name__ == "__main__":
    unittest.main()
