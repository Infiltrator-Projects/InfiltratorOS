#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline regressions for the read-only native release gate."""
import importlib.util
import pathlib
import unittest


GATE_PATH = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "native-release-gate.py"
spec = importlib.util.spec_from_file_location("native_release_gate", GATE_PATH)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def valid_snapshot():
    repositories = {}
    for index, (name, workflows) in enumerate(gate.WORKFLOWS.items()):
        version = {"Boot": "0.1.40", "Kernel": "0.1.1",
                   "InfiltratorFS": "0.19.42", "InfiltratorOS": "1.0.66"}[name]
        sha = str(index + 1) * 40
        repositories[name] = {
            "version": version, "head_sha": sha, "head_unchanged": True,
            "release": {
                "tag": "v" + version, "sha": sha,
                "url": "https://github.com/Infiltrator-Projects/" + name +
                       "/releases/tag/v" + version,
                "assets": sorted(gate.expected_assets(name, version)),
            },
            "workflows": {
                workflow: {
                    "head_sha": sha, "status": "completed", "conclusion": "success",
                    "url": "https://github.com/Infiltrator-Projects/" + name +
                           "/actions/runs/123",
                } for workflow in workflows
            },
        }
    return {"checked_at_utc": "2026-11-08T00:00:00+00:00",
            "repositories": repositories}


class NativeReleaseGateTests(unittest.TestCase):
    def test_exact_source_candidate_passes_without_claiming_standalone(self):
        result = gate.evaluate(valid_snapshot())
        self.assertTrue(result["ready_for_native_release_candidate"])
        self.assertFalse(result["standalone_os_qualified"])
        self.assertEqual("CI-candidate", result["gate"])

    def test_running_kernel_boot_is_a_blocker(self):
        snapshot = valid_snapshot()
        run = snapshot["repositories"]["Kernel"]["workflows"][
            "Three-way Boot + InfiltratorFS + Kernel VM qualification"]
        run.update(status="in_progress", conclusion=None)
        result = gate.evaluate(snapshot)
        self.assertFalse(result["ready_for_native_release_candidate"])
        self.assertTrue(any("Three-way Boot" in blocker for blocker in
                            result["blockers"]))

    def test_stale_published_os_tag_is_a_blocker(self):
        snapshot = valid_snapshot()
        snapshot["repositories"]["InfiltratorOS"]["release"]["sha"] = "a" * 40
        result = gate.evaluate(snapshot)
        self.assertTrue(any("InfiltratorOS: Published release tag" in blocker
                            for blocker in result["blockers"]))

    def test_missing_kernel_trial_installer_is_a_blocker(self):
        snapshot = valid_snapshot()
        snapshot["repositories"]["Kernel"]["release"]["assets"] = [
            "InfiltratorOS-Kernel-0.1.1-amd64.run", "SHA256SUMS.txt"]
        self.assertFalse(gate.evaluate(snapshot)["ready_for_native_release_candidate"])

    def test_new_commit_during_evidence_collection_is_a_blocker(self):
        snapshot = valid_snapshot()
        snapshot["repositories"]["Boot"]["head_unchanged"] = False
        self.assertFalse(gate.evaluate(snapshot)["ready_for_native_release_candidate"])

    def test_old_success_does_not_qualify_current_source(self):
        snapshot = valid_snapshot()
        snapshot["repositories"]["Kernel"]["workflows"][
            "Qualify independent EXT2 against InfiltratorOS Kernel"]["head_sha"] = "c" * 40
        self.assertFalse(gate.evaluate(snapshot)["ready_for_native_release_candidate"])


if __name__ == "__main__":
    unittest.main()
