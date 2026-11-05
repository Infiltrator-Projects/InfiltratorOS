#!/usr/bin/env python3
"""Selection must create exactly the jobs the OS launches, no phantom jobs."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "native_dispatch", ROOT / "packaging/orchestrate-native-install.py")
dispatch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dispatch)


class DispatcherSelectionTest(unittest.TestCase):
    def exercise(self, choices):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            stage = root / "native-worker.test"
            (stage / "status").mkdir(parents=True)
            launched = []
            paired = []

            def catalogue(stage_arg, worker, filename, expected):
                source = ("temporal-resources.json" if worker == "install-temporal-resources"
                          else "filesystem-resources.json")
                entries = [x["repository"].lower() for x in json.loads(
                    (ROOT / "packaging" / source).read_text())]
                self.assertEqual(len(entries), expected)
                (stage_arg / filename).write_text("".join(x + "\n" for x in entries))
                return entries

            def prepare(args, **kwargs):
                Path(args[1]).mkdir()

            def discover(args, **kwargs):
                self.assertEqual(args[1], "--discover-additional")
                (stage / "filesystems-optional.list").write_text("filesystem-ntfs\n")
                (stage / "filesystems-pending.list").write_text("")
                return SimpleNamespace(returncode=0)

            def launch(stage_arg, name, slug=None, *, pilot_pair=False):
                self.assertEqual(stage_arg, stage)
                launched.append((name, slug))
                if pilot_pair:
                    paired.append((name, slug))

            with (
                patch.object(dispatch, "STATUS_ROOT", root),
                patch.object(dispatch, "catalogue", side_effect=catalogue),
                patch.object(dispatch, "command", side_effect=prepare),
                patch.object(dispatch.subprocess, "run", side_effect=discover),
                patch.object(dispatch, "launch", side_effect=launch),
                patch.object(sys, "argv", ["orchestrate-native-install", str(stage)]),
                patch.dict(os.environ, choices, clear=True),
            ):
                dispatch.main()

            selected = (stage / "selected.list").read_text().splitlines()
            self.assertEqual(len(selected), len(launched) + len(paired))
            if paired:
                self.assertEqual(paired, [("filesystem-resources", "filesystem-fat12")])
                self.assertIn("filesystem-fat16", selected)
            self.assertEqual(len(set(selected)), len(selected))
            return selected, launched

    def test_all_jobs_selected_by_default(self):
        selected, launched = self.exercise({})
        self.assertEqual(len(selected), 4 + 54 + 5 + 1)
        self.assertEqual(selected[:4], list(dispatch.APPLICATIONS))
        self.assertEqual(selected[-1], "filesystem-ntfs")

    def test_partial_install_is_exact(self):
        selected, launched = self.exercise({
            "INFILTRATOROS_SELECTED_APPLICATIONS": "calculator",
            "INFILTRATOROS_SELECTED_TEMPORAL": "clock-binary",
            "INFILTRATOROS_SELECTED_FILESYSTEMS": "ntfs",
        })
        self.assertEqual(selected, ["calculator", "clock-binary", "filesystem-ntfs"])
        self.assertEqual(launched, [
            ("calculator", None), ("temporal-resources", "clock-binary"),
            ("filesystem-resources", "filesystem-ntfs"),
        ])

    def test_only_fat_pair_is_coordinated(self):
        selected, launched = self.exercise({
            "INFILTRATOROS_SELECTED_APPLICATIONS": "",
            "INFILTRATOROS_SELECTED_TEMPORAL": "",
            "INFILTRATOROS_SELECTED_FILESYSTEMS": "fat12,fat16",
        })
        self.assertEqual(selected, ["filesystem-fat12", "filesystem-fat16"])
        self.assertEqual(launched, [("filesystem-resources", "filesystem-fat12")])

    def test_pilot_opt_out_restores_two_serial_jobs(self):
        selected, launched = self.exercise({
            "INFILTRATOROS_SELECTED_APPLICATIONS": "",
            "INFILTRATOROS_SELECTED_TEMPORAL": "",
            "INFILTRATOROS_SELECTED_FILESYSTEMS": "fat12,fat16",
            "INFILTRATOROS_PARALLEL_FAT_PILOT": "0",
        })
        self.assertEqual(selected, ["filesystem-fat12", "filesystem-fat16"])
        self.assertEqual(launched, [
            ("filesystem-resources", "filesystem-fat12"),
            ("filesystem-resources", "filesystem-fat16"),
        ])

    def test_bad_selection_fails_instead_of_silently_skipping(self):
        with self.assertRaisesRegex(ValueError, "unknown components"):
            self.exercise({"INFILTRATOROS_SELECTED_FILESYSTEMS": "not-a-filesystem"})


if __name__ == "__main__":
    unittest.main()
