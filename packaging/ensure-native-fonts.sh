#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Establish the canonical Linux Infiltrator font resource cache from the
# repository-owned InfiltratorOS font seed. The native cache is persistent
# machine state and is never removed by an application or InfiltratorOS uninstall.
set -eu

umask 022

BRAND_FILE="mb_corpo_a_cond_regular.ttf"
UI_BOLD_FILE="mb_corpo_s_bold.ttf"
UI_REGULAR_FILE="mb_corpo_s_regular.ttf"
BRAND_SHA256="c8bcd7e1a7d71169b38491d9b7c1ffe7ba7b46e888f0c1219931343a47bc0e05"
UI_BOLD_SHA256="d37ea986e2344d83390f94f170e6272b56efd00bfec808afe8314c4ca45d43b4"
UI_REGULAR_SHA256="94ede6629443c03d4362dcef425fb3ff520be5d654370021a34e81286804465c"

ROOT="${INFILTRATR_NATIVE_ROOT:-}"
case "$ROOT" in
    ""|/*) ;;
    *) echo "INFILTRATR_NATIVE_ROOT must be empty or absolute" >&2; exit 2 ;;
esac
SOURCE_DIR="${INFILTRATR_FONT_SOURCE_DIR:-/usr/lib/infiltratoros/font-seed}"
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

ensure_font "$BRAND_SHA256" "$BRAND_FILE"
ensure_font "$UI_BOLD_SHA256" "$UI_BOLD_FILE"
ensure_font "$UI_REGULAR_SHA256" "$UI_REGULAR_FILE"

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
