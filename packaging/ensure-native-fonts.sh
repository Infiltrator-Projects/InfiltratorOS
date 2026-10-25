#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify the 23 directly packaged native fonts and register Fontconfig.
# No font seeds, copies, downloads or alternate font installation locations.
set -eu
umask 022

ROOT=${INFILTRATR_NATIVE_ROOT:-}
case "$ROOT" in ''|/*) ;; *) echo 'Native root must be absolute' >&2; exit 2 ;; esac
native_path() { printf '%s%s\n' "$ROOT" "$1"; }

FONT_DIR=$(native_path /System/Resources/Fonts)
MANIFEST=$(native_path /System/Resources/Documentation/InfiltratorOS/fonts.sha256)
FONTCONFIG_DIR=$(native_path /etc/fonts/conf.d)
FONTCONFIG_FILE="$FONTCONFIG_DIR/90-infiltrator-native-fonts.conf"
for path in "$(native_path /System)" "$(native_path /System/Resources)" \
            "$(native_path /System/Resources/Documentation)" \
            "$(native_path /System/Resources/Documentation/InfiltratorOS)" \
            "$FONT_DIR"; do
    [ ! -L "$path" ] || { echo "Unsafe native font path: $path" >&2; exit 3; }
done
[ -f "$MANIFEST" ] && [ ! -L "$MANIFEST" ] || {
    echo "Missing packaged font checksum manifest: $MANIFEST" >&2; exit 4;
}

count=0
while IFS= read -r entry || [ -n "$entry" ]; do
    [ -n "$entry" ] || continue
    digest=${entry%%  *}
    name=${entry#*  }
    case "$digest" in *[!0-9a-f]*|'') echo 'Invalid font checksum' >&2; exit 4 ;; esac
    [ "${#digest}" -eq 64 ] || exit 4
    case "$name" in mb_corpo_*.ttf|daimler_*.ttf) ;; *) echo "Unsafe font name: $name" >&2; exit 4 ;; esac
    case "$name" in */*|*..*) echo "Unsafe font path: $name" >&2; exit 4 ;; esac
    target="$FONT_DIR/$name"
    [ -f "$target" ] && [ ! -L "$target" ] || {
        echo "Missing packaged native font: $target" >&2; exit 4;
    }
    printf '%s  %s\n' "$digest" "$target" | sha256sum -c - >/dev/null || {
        echo "Invalid packaged native font: $target" >&2; exit 4;
    }
    count=$((count + 1))
done < "$MANIFEST"
[ "$count" -eq 23 ] || { echo 'Incomplete native font inventory' >&2; exit 4; }

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
if [ -z "$ROOT" ] && command -v fc-cache >/dev/null 2>&1; then
    fc-cache -f "$FONT_DIR" >/dev/null 2>&1 || true
fi
