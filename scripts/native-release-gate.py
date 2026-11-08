#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed, read-only preflight for a native InfiltratorOS release candidate.

Published packages, exact-source CI qualifications, and a standalone OS are
different deliverables. This gate evaluates the first two. It never changes
a release, kernel, disk, firmware or repository.
"""
import argparse
import base64
import datetime
import json
import os
import pathlib
import sys
import urllib.error
import urllib.request

OWNER = "Infiltrator-Projects"
WORKFLOWS = {
    "Boot": ("Verify Boot release",),
    "Kernel": (
        "Three-way Boot + InfiltratorFS + Kernel VM qualification",
        "Qualify independent EXT2 against InfiltratorOS Kernel",
    ),
    "InfiltratorFS": ("Publish release",),
    "InfiltratorOS": (
        "Build InfiltratorOS DEB",
        "Qualify independent filesystem installation from OS bundle",
    ),
}
VERSION_FILES = {"Boot", "Kernel", "InfiltratorOS"}


def api(path):
    headers = {"Accept": "application/vnd.github+json",
               "User-Agent": "InfiltratorOS-native-release-readiness",
               "X-GitHub-Api-Version": "2022-11-28"}
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request("https://api.github.com" + path,
                                     headers=headers)
    with urllib.request.urlopen(request, timeout=25) as response:
        return json.load(response)


def repo_api(name, suffix):
    return api("/repos/{}/{}{}".format(OWNER, name, suffix))


def tag_commit(name, tag):
    ref = repo_api(name, "/git/ref/tags/" + tag)["object"]
    if ref["type"] == "tag":
        ref = repo_api(name, "/git/tags/" + ref["sha"])["object"]
    if ref["type"] != "commit":
        raise ValueError("Non-commit tag target: {}/{} {}".format(OWNER, name, tag))
    return ref["sha"]


def collect():
    result = {
        "checked_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "repositories": {},
    }
    for name in WORKFLOWS:
        head = repo_api(name, "/commits/main")["sha"]
        published = repo_api(name, "/releases?per_page=30")
        if name in VERSION_FILES:
            contents = repo_api(name, "/contents/VERSION")
            version = base64.b64decode(contents["content"]).decode("utf-8").strip()
            tag = "v" + version
        else:
            # InfiltratorFS has no root VERSION file; use its published tag.
            tag = next((r["tag_name"] for r in published if not r.get("draft")), "")
            version = tag[1:] if tag.startswith("v") else tag
        release = next((r for r in published if r["tag_name"] == tag
                        and not r.get("draft")), None)
        release_info = {"tag": tag, "sha": None, "assets": [], "url": None}
        if release:
            release_info = {
                "tag": tag,
                "sha": tag_commit(name, tag),
                "assets": [a["name"] for a in release.get("assets", [])],
                "url": release.get("html_url"),
            }
        runs = repo_api(name, "/actions/runs?head_sha={}&per_page=100".format(head))
        latest = {}
        for run in runs.get("workflow_runs", []):
            label = run.get("name")
            if label in WORKFLOWS[name] and run.get("head_sha") == head:
                if label not in latest or run["id"] > latest[label]["id"]:
                    latest[label] = {
                        "id": run["id"], "head_sha": head,
                        "status": run.get("status"),
                        "conclusion": run.get("conclusion"),
                        "url": run.get("html_url"),
                    }
        result["repositories"][name] = {
            "head_sha": head,
            "version": version,
            "release": release_info,
            "workflows": latest,
        }
    # Repository pushes can happen while the other evidence is collected.
    for name in WORKFLOWS:
        refreshed = repo_api(name, "/commits/main")["sha"]
        result["repositories"][name]["head_unchanged"] = (
            refreshed == result["repositories"][name]["head_sha"])
    return result


def expected_assets(name, version):
    if name == "Boot":
        return {"infiltrator-boot_{}_amd64.deb".format(version),
                "SHA256SUMS.txt"}
    if name == "Kernel":
        return {"InfiltratorOS-Kernel-{}-amd64.run".format(version),
                "InfiltratorOS-Kernel-{}-InfiltratorFS-Trial-amd64.run".format(version),
                "SHA256SUMS.txt"}
    if name == "InfiltratorFS":
        return {"infiltratorfs_{}_amd64.deb".format(version),
                "infiltratorfs-{}-linux-native.run".format(version),
                "SHA256SUMS.txt"}
    return {"infiltratoros_{}_amd64.deb".format(version),
            "infiltratoros-{}-source.zip".format(version),
            "SHA256SUMS.txt", "SOURCE_COMMIT"}


def evaluate(snapshot):
    checks = []
    blockers = []
    for name, required in WORKFLOWS.items():
        repo = snapshot.get("repositories", {}).get(name, {})
        version = repo.get("version")
        sha = repo.get("head_sha")
        release = repo.get("release") or {}
        tag = "v" + str(version) if version else None

        def record(condition, detail, evidence=None):
            item = {"repository": name, "check": detail,
                    "result": "passed" if condition else "blocked"}
            if evidence:
                item["evidence"] = evidence
            checks.append(item)
            if not condition:
                blockers.append("{}: {}".format(name, detail))

        record(bool(sha) and repo.get("head_unchanged") is True,
               "Main source revision remained stable during inspection")
        record(bool(version) and release.get("tag") == tag and
               bool(release.get("url")),
               "Current version has a published release", release.get("url"))
        record(bool(sha) and release.get("sha") == sha,
               "Published release tag points to current main commit",
               release.get("url"))
        record(bool(version) and
               expected_assets(name, version).issubset(set(release.get("assets", []))),
               "Published release contains expected installer and checksum assets",
               release.get("url"))
        for workflow in required:
            run = (repo.get("workflows") or {}).get(workflow) or {}
            passed = (run.get("head_sha") == sha and run.get("status") == "completed"
                      and run.get("conclusion") == "success")
            record(bool(sha) and passed,
                   "Exact-source CI passed: " + workflow, run.get("url"))
    return {
        "checked_at_utc": snapshot.get("checked_at_utc"),
        "gate": "CI-candidate" if not blockers else "blocked",
        "ready_for_native_release_candidate": not blockers,
        "standalone_os_qualified": False,
        "standalone_os_limit": (
            "No physical UEFI installation, persistent native root-to-desktop, "
            "crash recovery, Setup installer or hardware qualification is certified "
            "by this gate. Passing only authorises a CI-qualified candidate."
        ),
        "checks": checks,
        "blockers": blockers,
        "repositories": snapshot.get("repositories", {}),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", help="Offline source snapshot for regression tests")
    parser.add_argument("--output", help="Write full JSON evidence to this path")
    args = parser.parse_args(argv)
    try:
        snapshot = json.loads(pathlib.Path(args.fixture).read_text()) if args.fixture else collect()
        result = evaluate(snapshot)
    except (OSError, ValueError, KeyError, TypeError, urllib.error.URLError) as exc:
        result = {
            "gate": "blocked", "ready_for_native_release_candidate": False,
            "standalone_os_qualified": False, "checks": [],
            "blockers": ["Evidence acquisition failed closed: {}".format(exc)],
        }
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        pathlib.Path(args.output).write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0 if result["ready_for_native_release_candidate"] else 1


if __name__ == "__main__":
    sys.exit(main())
