#!/usr/bin/env python3
"""Per-user, reversible switch from Cinnamon's stock clock to Infiltrator Calendar.

This runs inside the user's Cinnamon login session, NOT from root's Debian
post-installation script. It leaves Cinnamon packages and all other panel
applets, positions and instance identifiers alone.
"""
import argparse
import ast
import json
import os
from pathlib import Path
import subprocess
import sys

STOCK = "calendar@cinnamon.org"
NATIVE = "calendar@the-infiltratr"
SCHEMA = "org.cinnamon"
KEY = "enabled-applets"
APPLET = Path("/usr/share/cinnamon/applets/calendar@the-infiltratr/metadata.json")


def setting_get():
    output = subprocess.check_output(
        ["gsettings", "get", SCHEMA, KEY], text=True
    ).strip()
    # GSettings uses a GVariant array of strings, normally printed as ['...'].
    # Do not evaluate input as code; fail closed on an unknown serialization.
    entries = ast.literal_eval(output)
    if (not isinstance(entries, list) or
            not all(isinstance(entry, str) for entry in entries)):
        raise ValueError("Unexpected Cinnamon enabled-applets value")
    return output, entries


def setting_set(entries):
    subprocess.run(
        ["gsettings", "set", SCHEMA, KEY, repr(entries)], check=True
    )


def main():
    parser = argparse.ArgumentParser(
        description="Reversibly activate the InfiltratorOS Calendar panel applet"
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--restore", action="store_true",
                        help="Restore the original panel entries from the backup")
    parser.add_argument("--activate", action="store_true",
                        help="Explicitly reactivate Calendar after an earlier restore")
    args = parser.parse_args()

    if os.geteuid() == 0:
        parser.error("Run as the signed-in Cinnamon user, never with sudo")
    state_home = Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state")))
    state_dir = state_home / "infiltratoros"
    backup = state_dir / "calendar-panel.before"
    marker = state_dir / "calendar-panel-migrated"
    previous_raw, current = setting_get()

    if args.restore:
        if not backup.is_file():
            print("No saved calendar panel state exists", file=sys.stderr)
            return 1
        saved = ast.literal_eval(backup.read_text(encoding="utf-8").strip())
        if not isinstance(saved, list) or not all(isinstance(s, str) for s in saved):
            raise ValueError("Invalid saved panel settings")
        setting_set(saved)
        _, observed = setting_get()
        if observed != saved:
            raise RuntimeError("Cinnamon did not accept the restored panel")
        marker.write_text("Original calendar restored by user; leave it alone on future logins\n", encoding="utf-8")
        print("Original Cinnamon panel entries restored; auto-migration disabled")
        return 0

    if marker.is_file() and not args.activate:
        print("Calendar migration already performed; respecting the user's panel changes")
        return 0

    # Verify the actual Calendar applet is installed before ever removing the
    # stock panel entry. The Linux packages themselves remain untouched.
    if not APPLET.is_file() or APPLET.is_symlink():
        print("Infiltrator Calendar applet is not installed; stock calendar retained")
        return 0
    metadata = json.loads(APPLET.read_text(encoding="utf-8"))
    if metadata.get("uuid") != NATIVE:
        raise RuntimeError("Installed Calendar applet identity does not match")

    revised = []
    swapped = 0
    for entry in current:
        segments = entry.split(":")
        if len(segments) >= 4 and segments[3] == STOCK:
            segments[3] = NATIVE
            entry = ":".join(segments)
            swapped += 1
        revised.append(entry)

    if not swapped:
        print("No stock Cinnamon Calendar in this user's panels; leaving layout unchanged")
        return 0
    if any(e.split(":")[3] == NATIVE for e in current if len(e.split(":")) >= 4):
        print("Infiltrator Calendar is already on a panel; leaving layout unchanged")
        return 0
    if args.dry_run:
        print("Would replace", swapped, "stock Calendar instance(s), preserving panel positions")
        return 0

    state_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(state_dir, 0o700)
    if not backup.exists():
        with backup.open("x", encoding="utf-8") as stream:
            os.chmod(backup, 0o600)
            stream.write(previous_raw + "\n")
    try:
        setting_set(revised)
        _, observed = setting_get()
        if observed != revised:
            raise RuntimeError("Cinnamon did not accept the Calendar applet replacement")
    except Exception:
        # GSettings mutations are per-user; restore the exact pre-change state
        # rather than leaving a missing clock on the panel.
        setting_set(current)
        raise
    marker.write_text("calendar@cinnamon.org -> calendar@the-infiltratr\n", encoding="utf-8")
    print("Replaced stock Cinnamon Calendar in", swapped, "panel position(s)")
    print("To undo: /System/Commands/InfiltratorOS/activate-calendar --restore")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print("Calendar panel migration failed:", error, file=sys.stderr)
        sys.exit(1)
