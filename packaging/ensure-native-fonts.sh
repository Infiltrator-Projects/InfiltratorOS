#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Establish the canonical Linux Infiltrator font resource cache from the
# repository-owned InfiltratorOS font seed. The native cache is persistent
# machine state and is never removed by an application or InfiltratorOS uninstall.
set -eu

umask 022

ROOT="${INFILTRATR_NATIVE_ROOT:-}"
case "$ROOT" in
    ""|/*) ;;
    *) echo "INFILTRATR_NATIVE_ROOT must be empty or absolute" >&2; exit 2 ;;
esac
SOURCE_DIR="${INFILTRATR_FONT_SOURCE_DIR:-/System/Resources/Fonts/Seed}"
case "$SOURCE_DIR" in
    /*) ;;
    *) echo "INFILTRATR_FONT_SOURCE_DIR must be absolute" >&2; exit 2 ;;
esac

native_path() {
    printf '%s%s\n' "$ROOT" "$1"
}

SYSTEM_DIR="$(native_path /System)"
RESOURCE_DIR="$(native_path /System/Resources)"
FONT_DIR="$(native_path /System/Resources/Fonts)"
FONTCONFIG_DIR="$(native_path /etc/fonts/conf.d)"
FONTCONFIG_FILE="$FONTCONFIG_DIR/90-infiltrator-native-fonts.conf"

for path in "$SYSTEM_DIR" "$RESOURCE_DIR" "$FONT_DIR"; do
    if [ -L "$path" ]; then
        echo "Refusing native font resource path through symlink: $path" >&2
        exit 3
    fi
done
mkdir -p "$FONT_DIR" "$FONTCONFIG_DIR"

font_matches() {
    expected=$1
    path=$2
    [ -f "$path" ] && [ ! -L "$path" ] &&
        printf '%s  %s\n' "$expected" "$path" | sha256sum -c - >/dev/null 2>&1
}

ensure_font() {
    expected=$1
    file=$2
    target="$FONT_DIR/$file"
    source="$SOURCE_DIR/$file"

    if font_matches "$expected" "$target"; then
        return 0
    fi
    if ! font_matches "$expected" "$source"; then
        echo "Canonical InfiltratorOS font seed is missing or invalid: $source" >&2
        exit 4
    fi

    staged="$FONT_DIR/.${file}.new.$$"
    rm -f -- "$staged"
    # On the normal single-filesystem layout this creates another name for the
    # same inode, so the package seed and persistent native resource consume no
    # duplicate font data. A copy is the safe fallback for split filesystems.
    if ln -- "$source" "$staged" 2>/dev/null; then
        :
    else
        install -m 0644 "$source" "$staged"
    fi
    mv -f -- "$staged" "$target"
    font_matches "$expected" "$target"
}

MANIFEST="$SOURCE_DIR/SHA256SUMS"
if [ ! -f "$MANIFEST" ] || [ -L "$MANIFEST" ]; then
    echo "Missing or unsafe InfiltratorOS font manifest: $MANIFEST" >&2
    exit 4
fi

font_count=0
while IFS= read -r entry || [ -n "$entry" ]; do
    [ -n "$entry" ] || continue
    checksum=${entry%%  *}
    font_file=${entry#*  }
    case "$checksum" in
        ""|*[!0-9a-f]*) echo "Invalid font hash in manifest" >&2; exit 4 ;;
    esac
    [ "${#checksum}" -eq 64 ] || { echo "Invalid font SHA-256 length" >&2; exit 4; }
    case "$font_file" in
        mb_corpo_*.ttf|daimler_*.ttf) ;;
        *) echo "Unexpected font filename in manifest: $font_file" >&2; exit 4 ;;
    esac
    case "$font_file" in
        */*|*..*) echo "Unsafe font filename in manifest: $font_file" >&2; exit 4 ;;
    esac
    ensure_font "$checksum" "$font_file"
    font_count=$((font_count + 1))
done < "$MANIFEST"
[ "$font_count" -eq 23 ] || { echo "Incomplete InfiltratorOS font manifest" >&2; exit 4; }

conf_tmp="$FONTCONFIG_FILE.new.$$"
cat > "$conf_tmp" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>/System/Resources/Fonts</dir>
</fontconfig>
EOF
chmod 0644 "$conf_tmp"
if [ -f "$FONTCONFIG_FILE" ] && cmp -s "$conf_tmp" "$FONTCONFIG_FILE"; then
    rm -f "$conf_tmp"
else
    mv -f -- "$conf_tmp" "$FONTCONFIG_FILE"
fi

if [ -z "$ROOT" ] && command -v fc-cache >/dev/null 2>&1; then
    fc-cache -f "$FONT_DIR" >/dev/null 2>&1 || true
fi
