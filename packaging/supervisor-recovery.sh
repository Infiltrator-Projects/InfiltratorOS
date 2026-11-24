# SPDX-License-Identifier: GPL-3.0-or-later
# Called with the provision lock held. Preserve the stage and its logs, never
# remove a possibly orphaned worker's source or package files.
recover_abandoned_supervisor() (
    root=$1
    summary="$root/last-provision.status"
    [ -f "$summary" ] && [ ! -L "$summary" ] || return 0
    grep -Fxq 'state=running' "$summary" || return 0
    previous=$(sed -n 's/^stage=//p' "$summary")
    case "$previous" in "$root"/native-worker.*) ;; *) return 1 ;; esac
    [ "${previous%/*}" = "$root" ] && [ -d "$previous" ] && [ ! -L "$previous" ] &&
        [ "$(stat -c '%u:%a' "$previous")" = "$(id -u):755" ] || return 1
    [ -f "$previous/supervisor-started" ] || return 0
    printf '%s\n' 'Previous provisioning supervisor terminated; retained for diagnosis.' > "$previous/FAILED" || return 1
    temporary=$(mktemp "$root/.recovered-summary.XXXXXXXX") || return 1
    printf 'state=failed\nstage=%s\n' "$previous" > "$temporary" || return 1
    chmod 0644 "$temporary" && mv -fT -- "$temporary" "$summary"
)
