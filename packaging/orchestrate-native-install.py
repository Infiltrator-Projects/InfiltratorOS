#!/usr/bin/python3 -u
# SPDX-License-Identifier: GPL-3.0-or-later
"""One InfiltratorOS installation dispatcher. Components own their installers.

The dispatcher only enumerates jobs, prepares the published release snapshot,
launches each independent worker, and writes launch-failure statuses.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

APPLICATIONS = ("system-monitor", "calculator", "files", "defragmenter")
STATUS_ROOT = Path("/Temporary/InfiltratorOS")


def command(arguments, *, environment=None, output=None):
    return subprocess.run([str(arg) for arg in arguments], check=True,
                          env=environment, stdout=output)


def catalogue(stage, worker, filename, expected):
    # Selection belongs to this dispatcher, never to component installers.
    env = os.environ.copy()
    env.pop("INFILTRATOROS_SELECTED_FILESYSTEMS", None)
    env.pop("INFILTRATOROS_SELECTED_TEMPORAL", None)
    result = subprocess.run([str(stage / worker), "--list"], check=True,
                            text=True, capture_output=True, env=env)
    entries = result.stdout.splitlines()
    if len(entries) != expected or len(set(entries)) != expected:
        raise ValueError(f"{worker}: expected {expected} distinct providers")
    (stage / filename).write_text("".join(item + "\n" for item in entries))
    return entries


def selection(variable, entries, identifier):
    requested = os.environ.get(variable)
    if requested is None:
        return entries
    names = {item.strip().lower() for item in requested.split(",") if item.strip()}
    unknown = names - {identifier(item) for item in entries}
    if unknown:
        raise ValueError(variable + ": unknown components: " + ", ".join(sorted(unknown)))
    return [item for item in entries if identifier(item) in names]


def launch(stage, launcher, slug=None):
    args = [stage / ("start-" + launcher + "-install")]
    if slug is not None:
        args.append(slug)
    env = os.environ.copy()
    env["INFILTRATOROS_PROGRESS_MODE"] = "none"
    env["INFILTRATOROS_INSTALL_RUNTIME"] = str(stage)
    result = subprocess.run([str(arg) for arg in args], env=env, check=False)
    if result.returncode:
        name = slug or launcher
        print("Failed to launch " + name + "; see " + str(stage / "status"), file=sys.stderr)
        (stage / "status" / (name + "-install.status")).write_text(
            "state=failed\nprogress=100\nstage=Native installer could not start\n"
            "detail=See the run logs.\n")


def main():
    if len(sys.argv) != 2:
        raise ValueError("Usage: orchestrate-native-install STAGE")
    stage = Path(sys.argv[1]).resolve()
    if stage.parent != STATUS_ROOT or not stage.name.startswith("native-worker."):
        raise ValueError("Invalid native installation stage")
    if not (stage / "status").is_dir():
        raise ValueError("Missing installation status directory")

    providers = catalogue(stage, "install-temporal-resources", "providers.list", 54)
    filesystems = catalogue(stage, "install-filesystem-resources", "filesystems.list", 5)

    # Release preparation requires the full canonical catalogue, regardless
    # of what the future component-selection UI chooses to launch.
    log = stage / "native-release-preparation.log"
    with log.open("w") as output:
        try:
            command([stage / "prepare-native-releases", stage / "release-json",
                     stage / "providers.list", stage / "filesystems.list"], output=output)
        except subprocess.CalledProcessError:
            print(log.read_text(), file=sys.stderr)
            raise
    os.environ["INFILTRATOROS_RELEASE_DIR"] = str(stage / "release-json")

    discovery_log = stage / "optional-filesystem-discovery.log"
    with discovery_log.open("w") as output:
        result = subprocess.run(
            [str(stage / "install-filesystem-resources"), "--discover-additional",
             str(stage / "release-json")], stdout=output, stderr=subprocess.STDOUT)
    optional_path = stage / "filesystems-optional.list"
    pending_path = stage / "filesystems-pending.list"
    if result.returncode:
        with discovery_log.open("a") as output:
            output.write("Additional filesystem discovery failed; five established drivers remain enabled.\n")
        optional_path.write_text("")
        all_items = subprocess.run(
            [str(stage / "install-filesystem-resources"), "--list-all"],
            text=True, capture_output=True, check=True).stdout.splitlines()
        pending_path.write_text("".join(item + "\n" for item in all_items
                                        if item not in filesystems))
    if not optional_path.is_file() or not pending_path.is_file():
        raise ValueError("Filesystem discovery did not produce its manifests")
    optional = optional_path.read_text().splitlines()
    if (len(optional) > 18 or len(set(optional)) != len(optional) or
            set(optional) & set(filesystems) or
            any(not item.startswith("filesystem-") for item in optional)) :
        raise ValueError("Invalid optional filesystem list")
    if pending_path.stat().st_size:
        print("Some independent filesystem modules await qualified native releases.")
        (STATUS_ROOT / "filesystems-pending.list").write_text(pending_path.read_text())

    applications = selection("INFILTRATOROS_SELECTED_APPLICATIONS",
                             list(APPLICATIONS), lambda item: item)
    providers = selection("INFILTRATOROS_SELECTED_TEMPORAL",
                          providers, lambda item: item)
    all_filesystems = filesystems + optional
    chosen = selection("INFILTRATOROS_SELECTED_FILESYSTEMS", all_filesystems,
                       lambda item: item.removeprefix("filesystem-"))
    for product in applications:
        launch(stage, product)
    for provider in providers:
        launch(stage, "temporal-resources", provider)
    for filesystem in chosen:
        launch(stage, "filesystem-resources", filesystem)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("InfiltratorOS dispatcher: " + str(error), file=sys.stderr)
        raise SystemExit(1)
