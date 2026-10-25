#!/usr/bin/env python3
"""Offline checks for the reversible Cinnamon Calendar panel migration."""
import ast
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("calendar_switch", root / "packaging/activate-calendar.py")
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)

with tempfile.TemporaryDirectory(prefix="calendar-switch-test-") as work:
    sandbox = Path(work)
    applet = sandbox / "calendar@the-infiltratr" / "metadata.json"
    applet.parent.mkdir()
    applet.write_text(json.dumps({"uuid": "calendar@the-infiltratr"}), encoding="utf-8")
    saved = [
        "panel1:left:0:menu@cinnamon.org:0",
        "panel1:right:11:calendar@cinnamon.org:14",
        "panel1:right:12:sound@cinnamon.org:15",
    ]
    state = {"entries": list(saved), "writes": 0}

    def fake_get(args, text=False):
        assert args == ["gsettings", "get", "org.cinnamon", "enabled-applets"]
        assert text
        return repr(state["entries"])

    def fake_run(args, check=False):
        assert args[:4] == ["gsettings", "set", "org.cinnamon", "enabled-applets"]
        assert check
        state["entries"] = ast.literal_eval(args[4])
        state["writes"] += 1
        return subprocess.CompletedProcess(args, 0)

    def invoke(*args):
        with (
            mock.patch.object(sys, "argv", ["activate-calendar", *args]),
            mock.patch.object(migration.os, "geteuid", return_value=1000),
            mock.patch.object(migration, "APPLET", applet),
            mock.patch.dict(os.environ, {"XDG_STATE_HOME": str(sandbox / "state")}),
            mock.patch.object(migration.subprocess, "check_output", side_effect=fake_get),
            mock.patch.object(migration.subprocess, "run", side_effect=fake_run),
        ):
            return migration.main()

    assert invoke("--dry-run") == 0
    assert state["entries"] == saved and state["writes"] == 0
    assert invoke() == 0
    expected = list(saved)
    expected[1] = "panel1:right:11:calendar@the-infiltratr:14"
    assert state["entries"] == expected
    backup = sandbox / "state/infiltratoros/calendar-panel.before"
    assert ast.literal_eval(backup.read_text().strip()) == saved
    assert invoke() == 0
    assert state["writes"] == 1, "idempotent operation must not overwrite user changes"
    assert invoke("--restore") == 0
    assert state["entries"] == saved
    writes = state["writes"]
    assert invoke() == 0, "Auto-start may not undo a deliberate restore"
    assert state["writes"] == writes
    assert invoke("--activate") == 0
    assert state["entries"] == expected
    assert invoke("--restore") == 0
    assert state["entries"] == saved
    # If no stock applet is present, don't insert a new icon or rewrite the panel.
    state["entries"] = ["panel1:left:0:menu@cinnamon.org:0"]
    before = state["writes"]
    assert invoke() == 0
    assert state["writes"] == before
    # A mismatched applet metadata identity must fail closed.
    applet.write_text('{"uuid":"other@provider"}', encoding="utf-8")
    state["entries"] = list(saved)
    try:
        invoke()
    except RuntimeError:
        pass
    else:
        raise AssertionError("Must reject an invalid replacement applet")
    assert state["entries"] == saved
print("Cinnamon Calendar applet migration tests passed")
