#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Install the verified icon catalogue directly in the shared Icons directory.
# The source archive is package-owned; individual applications do not own the
# installed icons. A package removal must never delete shared icon resources.
set -eu
umask 022

ARCHIVE_SHA256='8d55ccbe5f1eefe3800ab12578a641524742b9482410d95757317434523b342c'
ROOT=${INFILTRATR_NATIVE_ROOT:-}
SOURCE_DIR=${INFILTRATR_ICON_SOURCE_DIR:-/usr/lib/infiltratoros/icon-seed}
case "$ROOT" in ''|/*) ;; *) echo 'Native root must be absolute' >&2; exit 2 ;; esac
case "$SOURCE_DIR" in /*) ;; *) echo 'Icon source directory must be absolute' >&2; exit 2 ;; esac
source="$SOURCE_DIR/mercedes-icons-v1.tar.xz"
case "$source" in *'/../'*|*'/./'*) echo 'Unsafe source path' >&2; exit 2 ;; esac
[ -f "$source" ] && [ ! -L "$source" ] || { echo 'Missing/unsafe icon archive' >&2; exit 4; }
printf '%s  %s\n' "$ARCHIVE_SHA256" "$source" | sha256sum -c - >/dev/null || {
    echo 'Mercedes icon archive SHA-256 mismatch' >&2; exit 4;
}

native_path() { printf '%s%s\n' "$ROOT" "$1"; }
for path in "$(native_path /System)" "$(native_path /System/Resources)" \
            "$(native_path /System/Resources/Icons)" \
            "$(native_path /System/Resources/Icons/mercedes)"; do
    [ ! -L "$path" ] || { echo "Unsafe icon resource symlink: $path" >&2; exit 3; }
done
ICON_ROOT=$(native_path /System/Resources/Icons)
LEGACY_ROOT="$ICON_ROOT/mercedes"
# Paths are fixed, rather than derived from untrusted archive entries.
for app in adapter connect eco-coach finance logbook remote-parking; do
    for variant in default night; do
        dir="$LEGACY_ROOT/$app/$variant"
        [ ! -L "$LEGACY_ROOT/$app" ] && [ ! -L "$dir" ] || {
            echo "Unsafe icon directory: $dir" >&2; exit 3;
        }
    done
done

work=$(mktemp -d "${TMPDIR:-/tmp}/infiltrator-icons.XXXXXXXX")
staged=''
cleanup() {
    if [ -n "$staged" ]; then rm -f -- "$staged"; fi
    rm -rf -- "$work"
}
trap cleanup EXIT HUP INT TERM

# Exact package SHA checked before extraction. Reject path escapes, symlinks,
# and unapproved archive entries before extraction even for a pinned payload.
tar -tJf "$source" > "$work/names"
while IFS= read -r name; do
    case "$name" in
        SHA256SUMS|SOURCES.csv|mercedes/*/default/*|mercedes/*/night/*) ;;
        *) echo "Unsafe member in icon archive: $name" >&2; exit 4 ;;
    esac
    case "$name" in *'..'*|/*) echo "Unsafe archive member: $name" >&2; exit 4 ;; esac
done < "$work/names"
mkdir -p "$work/extracted"
tar -xJf "$source" -C "$work/extracted"
manifest="$work/extracted/SHA256SUMS"
[ -f "$manifest" ] && [ ! -L "$manifest" ] || { echo 'Missing icon manifest' >&2; exit 4; }
[ "$(wc -l < "$manifest")" -eq 1233 ] || { echo 'Incomplete icon manifest' >&2; exit 4; }
(cd "$work/extracted/mercedes" && sha256sum -c "$manifest") > "$work/checksum-log" || {
    echo 'Invalid extracted Mercedes icon checksum' >&2; exit 4;
}

count=0
while IFS= read -r entry || [ -n "$entry" ]; do
    [ -n "$entry" ] || continue
    digest=${entry%%  *}
    name=${entry#*  }
    case "$digest" in ''|*[!0-9a-f]*) echo 'Invalid icon checksum' >&2; exit 4 ;; esac
    [ "${#digest}" -eq 64 ] || { echo 'Bad icon digest length' >&2; exit 4; }
    case "$name" in
        adapter/default/*|adapter/night/*|connect/default/*|connect/night/*|\
        eco-coach/default/*|eco-coach/night/*|finance/default/*|finance/night/*|\
        logbook/default/*|logbook/night/*|remote-parking/default/*|remote-parking/night/*) ;;
        *) echo "Unexpected icon identity: $name" >&2; exit 4 ;;
    esac
    case "$name" in
        *'..'*|*'//'*) echo 'Unsafe icon path' >&2; exit 4 ;;
        *.png|*.webp) ;;
        *) echo "Invalid icon format: $name" >&2; exit 4 ;;
    esac
    app=${name%%/*}
    relative=${name#*/}
    variant=${relative%%/*}
    filename=${relative#*/}
    case "$filename" in
        ''|*[!a-zA-Z0-9._-]*) echo "Invalid icon filename: $filename" >&2; exit 4 ;;
    esac
    from="$work/extracted/mercedes/$name"
    to="$ICON_ROOT/$app-$variant-$filename"
    [ ! -L "$to" ] || { echo "Unsafe icon destination: $to" >&2; exit 3; }
    if [ -f "$to" ] && [ ! -L "$to" ] && \
        printf '%s  %s\n' "$digest" "$to" | sha256sum -c - >/dev/null 2>&1; then
        count=$((count + 1))
        continue
    fi
    directory=${to%/*}
    [ ! -L "$directory" ] || { echo "Unsafe destination: $directory" >&2; exit 3; }
    mkdir -p "$directory"
    staged="$directory/.${name##*/}.new.$$"
    rm -f -- "$staged"
    ln -- "$from" "$staged" 2>/dev/null || install -m 0644 -- "$from" "$staged"
    mv -fT -- "$staged" "$to"
    staged=''
    printf '%s  %s\n' "$digest" "$to" | sha256sum -c - >/dev/null
    count=$((count + 1))
done < "$manifest"
[ "$count" -eq 1233 ] || { echo 'Incorrect icon count' >&2; exit 4; }

# Upgrade the 1.0.22 layout only after every replacement is verified. Remove
# only unchanged seed files; custom files and locally changed icons survive.
while IFS= read -r entry || [ -n "$entry" ]; do
    [ -n "$entry" ] || continue
    digest=${entry%%  *}
    name=${entry#*  }
    previous="$LEGACY_ROOT/$name"
    if [ -f "$previous" ] && [ ! -L "$previous" ] && \
        printf '%s  %s\n' "$digest" "$previous" | sha256sum -c - >/dev/null 2>&1; then
        rm -f -- "$previous"
    fi
done < "$manifest"
for app in adapter connect eco-coach finance logbook remote-parking; do
    for variant in default night; do
        rmdir -- "$LEGACY_ROOT/$app/$variant" 2>/dev/null || :
    done
    rmdir -- "$LEGACY_ROOT/$app" 2>/dev/null || :
done
rmdir -- "$LEGACY_ROOT" 2>/dev/null || :
# No icon cache rebuild: these are app-selectable resources, not desktop
# theme replacements. Existing GTK/desktop icon themes remain untouched.
printf 'Verified %s icons in %s\n' "$count" "$ICON_ROOT"
