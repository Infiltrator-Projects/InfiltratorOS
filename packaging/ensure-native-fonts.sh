#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Register the directly packaged native fonts with Fontconfig.
# No font seeds, copies, downloads or alternate font installation locations.
set -eu
umask 022

ROOT=${INFILTRATR_NATIVE_ROOT:-}
case "$ROOT" in ''|/*) ;; *) echo 'Native root must be absolute' >&2; exit 2 ;; esac
native_path() { printf '%s%s\n' "$ROOT" "$1"; }

FONT_DIR=$(native_path /System/Resources/Fonts)
FONTCONFIG_DIR=$(native_path /etc/fonts/conf.d)
FONTCONFIG_FILE="$FONTCONFIG_DIR/90-infiltrator-native-fonts.conf"
mkdir -p "$FONTCONFIG_DIR"
conf_tmp="$FONTCONFIG_FILE.new.$$"
trap 'rm -f -- "$conf_tmp"' EXIT HUP INT TERM
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
if [ -z "$ROOT" ]; then
    command -v fc-cache >/dev/null 2>&1 || {
        echo 'Fontconfig cache updater is unavailable' >&2; exit 5;
    }
    fc-cache "$FONT_DIR" >/dev/null 2>&1 || {
        echo 'Verified fonts could not be registered with Fontconfig' >&2; exit 5;
    }
fi
