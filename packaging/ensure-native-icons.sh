#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Validate the 1,233 directly packaged icons and safely retire unchanged
# icons from the legacy nested layout. No runtime archive or Seed directory.
set -eu
umask 022

ROOT=${INFILTRATR_NATIVE_ROOT:-}
case "$ROOT" in ''|/*) ;; *) echo 'Native root must be absolute' >&2; exit 2 ;; esac
native_path() { printf '%s%s\n' "$ROOT" "$1"; }

ICON_ROOT=$(native_path /System/Resources/Icons)
MANIFEST=$(native_path /System/Resources/Documentation/InfiltratorOS/icons.sha256)
LEGACY_ROOT="$ICON_ROOT/mercedes"
for path in "$(native_path /System)" "$(native_path /System/Resources)" \
            "$(native_path /System/Resources/Documentation)" \
            "$(native_path /System/Resources/Documentation/InfiltratorOS)" \
            "$ICON_ROOT" "$LEGACY_ROOT"; do
    [ ! -L "$path" ] || { echo "Unsafe icon path: $path" >&2; exit 3; }
done
[ -f "$MANIFEST" ] && [ ! -L "$MANIFEST" ] || {
    echo "Missing packaged icon checksum manifest: $MANIFEST" >&2; exit 4;
}

count=0
while IFS= read -r entry || [ -n "$entry" ]; do
    [ -n "$entry" ] || continue
    digest=${entry%%  *}
    name=${entry#*  }
    case "$digest" in *[!0-9a-f]*|'') echo 'Invalid icon checksum' >&2; exit 4 ;; esac
    [ "${#digest}" -eq 64 ] || exit 4
    case "$name" in
        adapter-default-*|adapter-night-*|connect-default-*|connect-night-*|\
        eco-coach-default-*|eco-coach-night-*|finance-default-*|finance-night-*|\
        logbook-default-*|logbook-night-*|remote-parking-default-*|remote-parking-night-*) ;;
        *) echo "Unexpected icon identity: $name" >&2; exit 4 ;;
    esac
    case "$name" in
        *..*|*/*|*[!a-zA-Z0-9._-]*) echo "Unsafe icon name: $name" >&2; exit 4 ;;
        *.png|*.webp) ;;
        *) echo "Invalid icon format: $name" >&2; exit 4 ;;
    esac
    target="$ICON_ROOT/$name"
    [ -f "$target" ] && [ ! -L "$target" ] || {
        echo "Missing/unsafe packaged icon: $target" >&2; exit 4;
    }
    printf '%s  %s\n' "$digest" "$target" | sha256sum -c - >/dev/null || {
        echo "Invalid packaged icon: $target" >&2; exit 4;
    }
    count=$((count + 1))
done < "$MANIFEST"
[ "$count" -eq 1233 ] || { echo 'Incomplete packaged icon catalogue' >&2; exit 4; }

# Migrate only unchanged old layout entries after every replacement verifies.
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
    for app in adapter connect eco-coach finance logbook remote-parking; do
        case "$flat" in "$app"-*) ;; *) continue ;; esac
        for variant in default night; do
            prefix="$app-$variant-"
            case "$flat" in "$prefix"*) ;; *) continue ;; esac
            filename=${flat#"$prefix"}
            previous="$LEGACY_ROOT/$app/$variant/$filename"
            if [ -f "$previous" ] && [ ! -L "$previous" ] && \
                printf '%s  %s\n' "$digest" "$previous" | sha256sum -c - >/dev/null 2>&1; then
                rm -f -- "$previous"
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
printf 'Verified %s icons in %s\n' "$count" "$ICON_ROOT"
