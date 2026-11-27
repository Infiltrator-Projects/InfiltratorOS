#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/packaging/supervisor-recovery.sh"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
stage=$(mktemp -d "$work/native-worker.XXXXXXXX")
chmod 0755 "$stage"
printf 'state=running\nstage=%s\n' "$stage" > "$work/last-provision.status"
recover_abandoned_supervisor "$work"
grep -Fxq 'state=running' "$work/last-provision.status"
touch "$stage/supervisor-started"
printf 'preserve logs\n' > "$stage/dispatcher.log"
recover_abandoned_supervisor "$work"
grep -Fxq 'state=failed' "$work/last-provision.status"
test -f "$stage/FAILED"
grep -Fxq 'preserve logs' "$stage/dispatcher.log"
# Existing user-owned directories must be used in place.
parent="$work/existing"
mkdir -m 0755 "$parent"
legacy="$parent/InfiltratorOS"
mkdir -m 0775 "$legacy"
printf 'old diagnostic\n' > "$legacy/last-provision.status"
if [ "$(id -u)" = 0 ]; then
    chown 65534:65534 "$legacy"
    chmod 0755 "$legacy"
fi
original=$(stat -c '%u:%g:%a:%i' "$legacy")
prepare_status_root "$legacy"
test "$(stat -c '%u:%g:%a:%i' "$legacy")" = "$original"
grep -Fxq 'old diagnostic' "$legacy/last-provision.status"
test "$(find "$parent" -mindepth 1 -maxdepth 1 | wc -l)" -eq 1
prepare_status_root "$legacy"
test "$(stat -c '%u:%g:%a:%i' "$legacy")" = "$original"
prepare_status_root "$parent/new"
test -d "$parent/new"
printf 'Supervisor recovery and existing directory contracts passed.\n'
