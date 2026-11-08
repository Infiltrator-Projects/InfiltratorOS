#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Small offline safety regressions for the installed-root handoff trial."""
import gzip
import importlib.util
from pathlib import Path
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parent / "native-root-handoff.py"
spec = importlib.util.spec_from_file_location("native_root_handoff", SOURCE)
trial = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trial)


class NativeRootHandoffTests(unittest.TestCase):
    def test_initramfs_requires_real_root_identity_and_switch_root(self):
        script = trial.initramfs_script("0.1.1", "NATIVE_ONLY").replace(
            "__MODULE_LOADS__", "/bin/busybox insmod /modules/infiltratorfs.ko")
        self.assertIn('mount -t infiltratorfs -o rw "$dev" /newroot', script)
        self.assertIn("NATIVE_ONLY", script)
        self.assertIn("exec /bin/busybox switch_root /newroot /sbin/init", script)
        self.assertNotIn("/bin/busybox exec ", script)
        self.assertNotIn("mount --bind /", script)
        self.assertNotIn("pivot_root / /", script)

    def test_native_root_requires_pid1_and_real_native_mount(self):
        script = trial.installed_init_script("0.1.1", "NATIVE_ONLY")
        self.assertIn('[ "$$" = 1 ]', script)
        self.assertIn(" / infiltratorfs ", script)
        self.assertIn("INFS_INSTALLED_ROOT_HANDOFF_PASS", script)
        self.assertIn("INFS_NATIVE_FIRST_STAGE_SERVICE_PASS",
                      trial.HEALTH_SERVICE)
        self.assertIn("installed-root-first-boot", script)
        self.assertIn("INFS_INSTALLED_ROOT_REBOOT_PERSISTENCE_PASS", script)

    def test_bootstrap_initrd_contains_no_installed_pid1(self):
        with tempfile.TemporaryDirectory() as temp:
            busybox = Path(temp) / "busybox"
            busybox.write_bytes(b"ELF TEST DATA")
            archive = Path(temp) / "initrd.img"
            records = []
            def stub_cpio(name, mode, payload=b"", major=0, minor=0):
                records.append(name)
                return name.encode("utf-8") + b"\n"
            trial.make_initramfs(
                stub_cpio, busybox, [("infiltratorfs", b"MODULE")],
                "0.1.1", "NATIVE_ONLY", archive)
            self.assertTrue(gzip.decompress(archive.read_bytes()))
            self.assertIn("init", records)
            self.assertIn("modules/infiltratorfs.ko", records)
            self.assertNotIn("sbin/init", records)
            self.assertNotIn("System/Commands/InfiltratorOS/root-health-check",
                             records)

    def test_guest_shutdown_required_for_both_boots(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "serial.log").write_text(
                "INFS_HANDOFF_INITRAMFS_ENTER\n"
                "INFS_NATIVE_ROOT_MOUNT_PASS\n"
                "INFS_NATIVE_ROOT_PAYLOAD_PASS\n"
                "INFS_INSTALLED_PID1_ROOT_PASS\n"
                "INFS_NATIVE_FIRST_STAGE_SERVICE_PASS\n"
                "INFS_INSTALLED_ROOT_FIRST_WRITE_PASS\n"
                "INFS_INSTALLED_ROOT_HANDOFF_PASS\n"
                "reboot: Power down\n"
            )
            (directory / "OVMF_CODE_4M.fd").write_bytes(b"mock ovmf code")
            (directory / "OVMF_VARS_4M.fd").write_bytes(b"mock ovmf vars")
            # A missing persistent marker must be rejected even if all of the
            # earlier stage markers were present.
            reboot = (directory / "serial-reboot.log")
            reboot.write_text((directory / "serial.log").read_text())
            with self.assertRaises(RuntimeError):
                # Mock only the QEMU call; force log-assertion on copied serial.
                import unittest.mock
                with unittest.mock.patch.object(trial.subprocess, "run"):
                    trial.boot_once(directory, directory, directory / "esp",
                                    directory / "root", 2, 30)


if __name__ == "__main__":
    unittest.main()
