#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
# Validate the staging path before deleting any previous build.
printf '%s\n' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'
test -f "$ROOT/debian/control.in"
test -x "$ROOT/packaging/ensure-native-fonts.sh"
test "$(dpkg --print-architecture)" = amd64
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || printf 0)}"
case "$SOURCE_DATE_EPOCH" in ""|*[!0-9]*) echo "Invalid SOURCE_DATE_EPOCH" >&2; exit 1 ;; esac
export SOURCE_DATE_EPOCH

DIST="$ROOT/dist"
WORK="$ROOT/.build/infiltratoros_${VERSION}"
PKGROOT="$WORK/root"
THEME_DIR="$PKGROOT/usr/share/plymouth/themes/infiltratoros"
TEMPORAL_ROOT="$PKGROOT/usr/lib/infiltrator/temporal"
FONT_SEED_DIR="$PKGROOT/usr/lib/infiltratoros/font-seed"
ICON_SEED_DIR="$PKGROOT/usr/lib/infiltratoros/icon-seed"
ICON_PART_DIR="$ROOT/assets/icons"
ICON_ARCHIVE_SHA256="8d55ccbe5f1eefe3800ab12578a641524742b9482410d95757317434523b342c"
SYSTEM_ROOT="$PKGROOT/System"
ARTWORK="$ROOT/assets/infiltratoros.png"
ARTWORK_SHA256="4820891225675ed9bb0bb1bb82e3680df4bbdccc736ce5d092b80e95e5d05978"
FONT_MANIFEST="$ROOT/assets/fonts/SHA256SUMS"
rm -rf "$WORK"
mkdir -p \
    "$PKGROOT/DEBIAN" \
    "$THEME_DIR" \
    "$DIST" \
    "$PKGROOT/etc/profile.d" \
    "$PKGROOT/usr/lib/infiltratoros" \
    "$FONT_SEED_DIR" \
    "$ICON_SEED_DIR" \
    "$TEMPORAL_ROOT/calendars" \
    "$TEMPORAL_ROOT/clocks" \
    "$SYSTEM_ROOT/Commands" \
    "$SYSTEM_ROOT/Libraries" \
    "$SYSTEM_ROOT/Devices" \
    "$SYSTEM_ROOT/Filesystems" \
    "$SYSTEM_ROOT/Kernel/Images" \
    "$SYSTEM_ROOT/Kernel/Modules" \
    "$SYSTEM_ROOT/Kernel/Firmware" \
    "$SYSTEM_ROOT/Resources/Calendars" \
    "$SYSTEM_ROOT/Resources/Clocks" \
    "$SYSTEM_ROOT/Resources/Fonts" \
    "$SYSTEM_ROOT/Resources/Locales" \
    "$SYSTEM_ROOT/Resources/Icons" \
    "$SYSTEM_ROOT/Resources/Themes" \
    "$SYSTEM_ROOT/Preferences" \
    "$PKGROOT/Programs" \
    "$PKGROOT/Users" \
    "$PKGROOT/Volumes" \
    "$PKGROOT/Temporary"
printf '%s  %s\n' "$ARTWORK_SHA256" "$ARTWORK" | sha256sum -c -
test -f "$FONT_MANIFEST"
test "$(wc -l < "$FONT_MANIFEST")" -eq 23
test "$(find "$ROOT/assets/fonts" -maxdepth 1 -type f -name '*.ttf' | wc -l)" -eq 23
test "$(awk '{print $1}' "$FONT_MANIFEST" | sort -u | wc -l)" -eq 23
(cd "$ROOT/assets/fonts" && sha256sum -c SHA256SUMS)
test "$(find "$ICON_PART_DIR" -maxdepth 1 -type f -name 'mercedes-icons-v1.part??' | wc -l)" -eq 10
test "$(wc -l < "$ICON_PART_DIR/PART_SHA256SUMS")" -eq 10
(cd "$ICON_PART_DIR" && sha256sum -c PART_SHA256SUMS)
cat "$ICON_PART_DIR"/mercedes-icons-v1.part?? > "$ICON_SEED_DIR/mercedes-icons-v1.tar.xz"
printf '%s  %s\n' "$ICON_ARCHIVE_SHA256" "$ICON_SEED_DIR/mercedes-icons-v1.tar.xz" | sha256sum -c -
test "$(tar -tJf "$ICON_SEED_DIR/mercedes-icons-v1.tar.xz" | grep -c '^mercedes/')" -eq 1233

sed "s/@VERSION@/$VERSION/g" "$ROOT/debian/control.in" > "$PKGROOT/DEBIAN/control"
install -m 0755 "$ROOT/debian/postinst" "$PKGROOT/DEBIAN/postinst"
install -m 0755 "$ROOT/debian/prerm" "$PKGROOT/DEBIAN/prerm"
install -m 0755 "$ROOT/packaging/ensure-native-fonts.sh" \
    "$PKGROOT/usr/lib/infiltratoros/ensure-native-fonts"
install -m 0755 "$ROOT/packaging/ensure-native-icons.sh" \
    "$PKGROOT/usr/lib/infiltratoros/ensure-native-icons"
install -m 0644 "$FONT_MANIFEST" "$FONT_SEED_DIR/SHA256SUMS"
while IFS= read -r entry; do
    font_file=${entry#*  }
    install -m 0644 "$ROOT/assets/fonts/$font_file" "$FONT_SEED_DIR/$font_file"
done < "$FONT_MANIFEST"
install -m 0644 "$ROOT/packaging/infiltrator-native-path.sh" \
    "$PKGROOT/etc/profile.d/90-infiltratoros-native-path.sh"

cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/packaging/stage-theme.c" -o "$WORK/stage-theme"
"$WORK/stage-theme" "$ROOT/plymouth/infiltratoros.plymouth" \
    "$ROOT/plymouth/infiltratoros.script" "$ARTWORK" "$THEME_DIR"
cc -std=c11 -O2 -Wall -Wextra -Werror "$ROOT/packaging/select-theme.c" \
    -o "$PKGROOT/usr/lib/infiltratoros/select-theme"

for product in system-monitor calculator files; do
    case "$product" in
        system-monitor) profile=INSTALL_SYSTEM_MONITOR ;;
        calculator) profile=INSTALL_CALCULATOR ;;
        files) profile=INSTALL_FILES ;;
    esac
    cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
        "$ROOT/packaging/install-$product.c" \
        -o "$PKGROOT/usr/lib/infiltratoros/install-$product"
    cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic "-D$profile" \
        "$ROOT/packaging/start-native-install.c" "$ROOT/packaging/installer-process.c" \
        -o "$PKGROOT/usr/lib/infiltratoros/start-$product-install"
    # pkg-config emits compiler arguments, intentionally split by the shell.
    # shellcheck disable=SC2086
    cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic "-D$profile" \
        $(pkg-config --cflags gtk+-3.0) \
        "$ROOT/packaging/native-install-progress.c" "$ROOT/packaging/installer-status.c" \
        -o "$PKGROOT/usr/lib/infiltratoros/$product-progress" $(pkg-config --libs gtk+-3.0)
done

test "$(file -b --mime-type "$THEME_DIR/infiltratoros.png")" = image/png
OUT="$DIST/infiltratoros_${VERSION}_amd64.deb"
dpkg-deb --root-owner-group --build "$PKGROOT" "$OUT"
dpkg-deb --info "$OUT" >/dev/null
dpkg-deb --contents "$OUT" >/dev/null
echo "$OUT"
