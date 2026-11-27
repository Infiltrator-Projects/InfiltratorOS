#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Safely retire unchanged
# icons from the legacy nested layout. No runtime archive or Seed directory.
set -eu
umask 022

ROOT=${INFILTRATR_NATIVE_ROOT:-}
case "$ROOT" in ''|/*) ;; *) echo 'Native root must be absolute' >&2; exit 2 ;; esac
native_path() { printf '%s%s\n' "$ROOT" "$1"; }

ICON_ROOT=$(native_path /System/Resources/Icons)
MANIFEST=$(native_path /System/Resources/Documentation/InfiltratorOS-icons.sha256)
LEGACY_ROOT="$ICON_ROOT/mercedes"
for path in "$(native_path /System)" "$(native_path /System/Resources)" \
            "$(native_path /System/Resources/Documentation)" \
            "$ICON_ROOT" "$LEGACY_ROOT"; do
    [ ! -L "$path" ] || { echo "Unsafe icon path: $path" >&2; exit 3; }
done
[ -f "$MANIFEST" ] && [ ! -L "$MANIFEST" ] || {
    echo "Missing packaged icon checksum manifest: $MANIFEST" >&2; exit 4;
}

# Migrate only unchanged old layout entries when a legacy directory exists.
[ -d "$LEGACY_ROOT" ] || exit 0
# Locally modified and custom legacy icons are preserved.
for app in adapter connect eco-coach finance logbook remote-parking; do
    for variant in default night; do
        dir="$LEGACY_ROOT/$app/$variant"
        [ ! -L "$LEGACY_ROOT/$app" ] && [ ! -L "$dir" ] || {
            echo "Unsafe legacy icon directory: $dir" >&2; exit 3;
        }
    done
done
while IFS= read -r entry || [ -n "$entry" ]; do
    [ -n "$entry" ] || continue
    digest=${entry%%  *}
    flat=${entry#*  }
    case "$flat" in *..*|*/*|*[!a-zA-Z0-9._-]*|'') continue ;; esac
    case "$digest" in *[!0-9a-f]*|'') continue ;; esac
    [ "${#digest}" -eq 64 ] || continue
    [ -f "$ICON_ROOT/$flat" ] && [ ! -L "$ICON_ROOT/$flat" ] || continue
    for app in adapter connect eco-coach finance logbook remote-parking; do
        case "$flat" in "$app"-*) ;; *) continue ;; esac
        for variant in default night; do
            prefix="$app-$variant-"
            case "$flat" in "$prefix"*) ;; *) continue ;; esac
            filename=${flat#"$prefix"}
            previous="$LEGACY_ROOT/$app/$variant/$filename"
            if [ -f "$previous" ] && [ ! -L "$previous" ] && \
                printf '%s  %s\n' "$digest" "$previous" | sha256sum -c - >/dev/null 2>&1; then
                if cmp -s -- "$previous" "$ICON_ROOT/$flat"; then rm -f -- "$previous"; fi
            fi
            break
        done
        break
    done
done < "$MANIFEST"
for app in adapter connect eco-coach finance logbook remote-parking; do
    for variant in default night; do
        rmdir -- "$LEGACY_ROOT/$app/$variant" 2>/dev/null || :
    done
    rmdir -- "$LEGACY_ROOT/$app" 2>/dev/null || :
done
rmdir -- "$LEGACY_ROOT" 2>/dev/null || :
