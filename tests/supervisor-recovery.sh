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
# A pre-1.0.105 status directory must never block the next upgrade.
parent="$work/migration"
mkdir -m 0755 "$parent"
legacy="$parent/InfiltratorOS"
mkdir -m 0775 "$legacy"
printf 'old diagnostic\n' > "$legacy/last-provision.status"
ln -s "$work/untouched" "$legacy/provision.lock"
printf 'protected\n' > "$work/untouched"
if [ "$(id -u)" = 0 ]; then
    chown 65534:65534 "$legacy"
    chmod 0755 "$legacy"
else
    chmod 0775 "$legacy"
fi
prepare_status_root "$legacy"
test "$(stat -c '%u:%a' "$legacy")" = "$(id -u):755"
test ! -e "$legacy/last-provision.status"
test ! -L "$legacy/provision.lock"
grep -Fxq protected "$work/untouched"
set -- "$parent"/.infiltratoros-legacy.*/status
test "$#" -eq 1
grep -Fxq 'old diagnostic' "$1/last-provision.status"
test -L "$1/provision.lock"
prepare_status_root "$legacy"
printf 'current\n' > "$legacy/last-provision.status"
prepare_status_root "$legacy"
grep -Fxq current "$legacy/last-provision.status"
ln -s "$legacy" "$parent/linked"
if prepare_status_root "$parent/linked"; then
    echo 'Accepted symbolic link as installation directory' >&2; exit 1
fi
printf 'Supervisor recovery and legacy directory migration contracts passed.\n'
