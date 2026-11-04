#!/usr/bin/env python3
"""Isolated failure injection for the real OS package post-install script."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "debian/postinst").read_text()
assert "trap 'bootstrap_on_exit $?' 0" in SOURCE
assert SOURCE.index("publish_summary running") < SOURCE.index("ensure-native-fonts")
assert SOURCE.index("publish_summary running") < SOURCE.index("prepare-native-releases")

for case in ("early-prerequisite", "worker-unpack"):
    with tempfile.TemporaryDirectory(prefix="infiltrator-bootstrap-test-") as temp:
        root = Path(temp) / "state"
        root.mkdir()
        previous = root / "last-provision.status"
        previous.write_text("state=complete\nstage=/previous/success\n")
        shell = SOURCE.replace("STATUS_ROOT=/Temporary/InfiltratorOS", "STATUS_ROOT=" + str(root), 1)
        shell, count = re.subn(r"unpack_native_workers\(\) \{.*?\n\}",
                                "unpack_native_workers() { return 1; }", shell,
                                count=1, flags=re.DOTALL)
        assert count == 1
        for helper in ("ensure-native-fonts", "ensure-native-icons", "select-theme configure"):
            command = "/System/Commands/InfiltratorOS/" + helper
            assert command in shell
            inject = "false" if case == "early-prerequisite" and helper == "ensure-native-fonts" else ":"
            shell = shell.replace(command, inject)
        result = subprocess.run(["sh", "-c", shell, "postinst", "configure"],
                                capture_output=True, text=True, check=False, timeout=20)
        assert result.returncode != 0, (case, result.stdout, result.stderr)
        state = dict(line.split("=", 1) for line in previous.read_text().splitlines())
        assert state["state"] == "failed", (case, state)
        stage = Path(state["stage"])
        assert stage.parent == root and stage.name.startswith("native-worker."), state
        assert (stage / "FAILED").is_file(), case
print("Native bootstrap failure overwrites stale success and preserves diagnostics")
