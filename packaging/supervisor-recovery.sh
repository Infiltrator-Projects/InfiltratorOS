# SPDX-License-Identifier: GPL-3.0-or-later
# Older installations allowed a user-owned status directory. Upgrade without
# trusting user-controlled contents as root or requiring manual chmod/chown.
# Preserve the old directory as diagnostic data, not as executable state.
prepare_status_root() (
    root=$1
    parent=${root%/*}
    owner=$(id -u)
    if [ -L "$parent" ] || ! mkdir -p -- "$parent" ||
       [ "$(find "$parent" -maxdepth 0 -type d -uid "$owner" ! -perm /022 -print)" != "$parent" ]; then
        echo "InfiltratorOS: unsafe installation parent: $parent" >&2
        return 1
    fi
    if [ -L "$root" ] || { [ -e "$root" ] && [ ! -d "$root" ]; }; then
        echo "InfiltratorOS: invalid installation directory: $root" >&2
        return 1
    fi
    if [ -d "$root" ] &&
       [ "$(find "$root" -maxdepth 0 -type d -uid "$owner" ! -perm /022 -print)" != "$root" ]; then
        # The parent is protected. Rename without opening anything in the
        # legacy tree, including any user-supplied locks or journal paths.
        retained=$(mktemp -d "$parent/.infiltratoros-legacy.XXXXXXXX") || return 1
        if ! mv -T -- "$root" "$retained/status"; then
            rmdir -- "$retained" || :
            echo "InfiltratorOS: unable to migrate legacy status directory: $root" >&2
            return 1
        fi
        echo "InfiltratorOS: retained previous installation diagnostics at $retained/status"
    fi
    if ! mkdir -p -- "$root" || ! chmod 0755 "$root"; then
        echo "InfiltratorOS: unable to create installation directory: $root" >&2
        return 1
    fi
)

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
