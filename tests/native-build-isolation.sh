#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Disposable hosted VM test. The mount namespace must leave host paths intact.
set -eu
worker=$1
base=$2
root=$(mktemp -d "$base/native-isolation.XXXXXXXX")
trap 'rm -rf -- "$root"' EXIT HUP INT TERM
mkdir "$root/build"
printf '%s\n' original > "$root/outside"
token="infiltrator-isolation-$(basename "$root")"
"$worker" --isolated-build "$root/build" /bin/sh -eu -c '
    printf "%s\n" private > "$1/inside"
    printf "%s\n" scratch > "/tmp/$3"
    if (printf "%s\n" bad > "$2") 2>/dev/null; then
        echo "Host write was not blocked" >&2
        exit 1
    fi
    test "$(cat "$2")" = original
' isolation "$root/build" "$root/outside" "$token"
test "$(cat "$root/build/inside")" = private
test "$(cat "$root/build/tmp/$token")" = scratch
test "$(cat "$root/outside")" = original
test ! -e "/tmp/$token"
printf '%s\n' 'Native build read-only mount isolation passed'
