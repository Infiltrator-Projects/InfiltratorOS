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
printf 'Supervisor recovery contracts passed.\n'
