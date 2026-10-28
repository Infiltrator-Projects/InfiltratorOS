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
# Application workers remain only in the signed maintainer-script bundle;
# they are staged under /Temporary/InfiltratorOS during installation.
RUNTIME_BUILD="$WORK/native-runtime"
THEME_DIR="$PKGROOT/usr/share/plymouth/themes/infiltratoros"
FONT_DIR="$PKGROOT/System/Resources/Fonts"
ICON_DIR="$PKGROOT/System/Resources/Icons"
RESOURCE_MANIFEST_DIR="$PKGROOT/System/Resources/Documentation/InfiltratorOS"
ICON_PART_DIR="$ROOT/assets/icons"
ICON_ARCHIVE_SHA256="8d55ccbe5f1eefe3800ab12578a641524742b9482410d95757317434523b342c"
SYSTEM_ROOT="$PKGROOT/System"
ARTWORK="$ROOT/assets/infiltratoros.png"
ARTWORK_SHA256="4820891225675ed9bb0bb1bb82e3680df4bbdccc736ce5d092b80e95e5d05978"
FONT_MANIFEST="$ROOT/assets/fonts/SHA256SUMS"
rm -rf "$WORK"
mkdir -p \
    "$PKGROOT/DEBIAN" \
    "$RUNTIME_BUILD" \
    "$THEME_DIR" \
    "$DIST" \
    "$PKGROOT/etc/profile.d" \
    "$PKGROOT/etc/xdg/autostart" \
    "$PKGROOT/System/Commands/InfiltratorOS" \
    "$FONT_DIR" \
    "$ICON_DIR" \
    "$RESOURCE_MANIFEST_DIR" \
    "$SYSTEM_ROOT/Commands" \
    "$SYSTEM_ROOT/Libraries" \
    "$SYSTEM_ROOT/Devices" \
    "$SYSTEM_ROOT/Filesystems" \
    "$SYSTEM_ROOT/Kernel/Images" \
    "$SYSTEM_ROOT/Kernel/Modules" \
    "$SYSTEM_ROOT/Kernel/Firmware" \
    "$SYSTEM_ROOT/Resources/Calendars" \
    "$SYSTEM_ROOT/Resources/Documentation" \
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
# The source archive exists only in the build workspace, never on an
# installed machine. The Debian payload owns precisely 1,233 flat icons.
archive="$WORK/mercedes-icons-v1.tar.xz"
cat "$ICON_PART_DIR"/mercedes-icons-v1.part?? > "$archive"
printf '%s  %s\n' "$ICON_ARCHIVE_SHA256" "$archive" | sha256sum -c -
test "$(tar -tJf "$archive" | grep -c '^mercedes/')" -eq 1233
mkdir -p "$WORK/icon-source"
tar -xJf "$archive" -C "$WORK/icon-source" --no-same-owner
original_manifest="$WORK/icon-source/SHA256SUMS"
test "$(wc -l < "$original_manifest")" -eq 1233
(cd "$WORK/icon-source/mercedes" && sha256sum -c "$original_manifest") >/dev/null
icon_manifest="$RESOURCE_MANIFEST_DIR/icons.sha256"
: > "$icon_manifest"
icon_count=0
while IFS= read -r entry || [ -n "$entry" ]; do
    digest=${entry%%  *}
    name=${entry#*  }
    case "$name" in
        adapter/default/*|adapter/night/*|connect/default/*|connect/night/*|\
        eco-coach/default/*|eco-coach/night/*|finance/default/*|finance/night/*|\
        logbook/default/*|logbook/night/*|remote-parking/default/*|remote-parking/night/*) ;;
        *) echo "Invalid icon name: $name" >&2; exit 1 ;;
    esac
    case "$name" in *'..'*|*'//'*) echo "Unsafe icon path: $name" >&2; exit 1 ;; esac
    case "$name" in *.png|*.webp) ;; *) echo "Invalid icon format: $name" >&2; exit 1 ;; esac
    flat=$(printf '%s' "$name" | tr / -)
    case "$flat" in *[!a-zA-Z0-9._-]*|'') echo "Unsafe flattened icon: $flat" >&2; exit 1 ;; esac
    test ! -e "$ICON_DIR/$flat"
    install -m 0644 -- "$WORK/icon-source/mercedes/$name" "$ICON_DIR/$flat"
    printf '%s  %s\n' "$digest" "$flat" >> "$icon_manifest"
    icon_count=$((icon_count + 1))
done < "$original_manifest"
test "$icon_count" -eq 1233
(cd "$ICON_DIR" && sha256sum -c "$icon_manifest") >/dev/null
test "$(find "$ICON_DIR" -maxdepth 1 -type f | wc -l)" -eq 1233

sed "s/@VERSION@/$VERSION/g" "$ROOT/debian/control.in" > "$PKGROOT/DEBIAN/control"
install -m 0755 "$ROOT/debian/postinst" "$PKGROOT/DEBIAN/postinst"
install -m 0755 "$ROOT/debian/prerm" "$PKGROOT/DEBIAN/prerm"
install -m 0755 "$ROOT/packaging/ensure-native-fonts.sh" \
    "$PKGROOT/System/Commands/InfiltratorOS/ensure-native-fonts"
install -m 0755 "$ROOT/packaging/ensure-native-icons.sh" \
    "$PKGROOT/System/Commands/InfiltratorOS/ensure-native-icons"
install -m 0644 "$FONT_MANIFEST" "$RESOURCE_MANIFEST_DIR/fonts.sha256"
while IFS= read -r entry; do
    font_file=${entry#*  }
    install -m 0644 "$ROOT/assets/fonts/$font_file" "$FONT_DIR/$font_file"
done < "$FONT_MANIFEST"
(cd "$FONT_DIR" && sha256sum -c "$RESOURCE_MANIFEST_DIR/fonts.sha256") >/dev/null
test "$(find "$FONT_DIR" -maxdepth 1 -type f -name '*.ttf' | wc -l)" -eq 23
install -m 0644 "$ROOT/packaging/infiltrator-native-path.sh" \
    "$PKGROOT/etc/profile.d/90-infiltratoros-native-path.sh"
install -m 0755 "$ROOT/packaging/activate-calendar.py" \
    "$PKGROOT/System/Commands/InfiltratorOS/activate-calendar"
install -m 0644 "$ROOT/packaging/infiltrator-calendar.desktop" \
    "$PKGROOT/etc/xdg/autostart/infiltrator-calendar.desktop"
install -m 0755 "$ROOT/packaging/activate-program-icons.sh" \
    "$PKGROOT/System/Commands/InfiltratorOS/activate-program-icons"
install -m 0644 "$ROOT/packaging/infiltrator-program-icons.desktop" \
    "$PKGROOT/etc/xdg/autostart/infiltrator-program-icons.desktop"

cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/packaging/stage-theme.c" -o "$WORK/stage-theme"
"$WORK/stage-theme" "$ROOT/plymouth/infiltratoros.plymouth" \
    "$ROOT/plymouth/infiltratoros.script" "$ARTWORK" "$THEME_DIR"
cc -std=c11 -O2 -Wall -Wextra -Werror "$ROOT/packaging/select-theme.c" \
    -o "$PKGROOT/System/Commands/InfiltratorOS/select-theme"

for product in system-monitor calculator files defragmenter temporal-resources filesystem-resources; do
    case "$product" in
        system-monitor) profile=INSTALL_SYSTEM_MONITOR ;;
        calculator) profile=INSTALL_CALCULATOR ;;
        files) profile=INSTALL_FILES ;;
        defragmenter) profile=INSTALL_DEFRAGMENTER ;;
        temporal-resources) profile=INSTALL_TEMPORAL_RESOURCES ;;
        filesystem-resources) profile=INSTALL_FILESYSTEM_RESOURCES ;;
    esac
    if [ "$product" = temporal-resources ]; then
        # Embed the exact 54-provider catalogue in this installation-only worker.
        python3 - "$ROOT" "$RUNTIME_BUILD/install-$product" <<'PY'
import json, pathlib, sys
root, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
catalogue = json.loads((root / "packaging/temporal-resources.json").read_text())
assert len(catalogue) == 54
source = (root / "packaging/install-temporal-resources.py").read_text()
marker = "CATALOGUE = []  # @EMBED_TEMPORAL_CATALOGUE@"
assert source.count(marker) == 1
output.write_text(source.replace(marker, "CATALOGUE = " + repr(catalogue)))
output.chmod(0o755)
PY
    elif [ "$product" = filesystem-resources ]; then
        python3 - "$ROOT" "$RUNTIME_BUILD/install-$product" <<'PY'
import json, pathlib, sys
root, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
catalogue = json.loads((root / "packaging/filesystem-resources.json").read_text())
assert len(catalogue) == 3
source = (root / "packaging/install-filesystem-resources.py").read_text()
marker = "CATALOGUE = []  # @EMBED_FILESYSTEM_CATALOGUE@"
assert source.count(marker) == 1
output.write_text(source.replace(marker, "CATALOGUE = " + repr(catalogue)))
output.chmod(0o755)
PY
    else
        cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
            "$ROOT/packaging/install-$product.c" \
            -o "$RUNTIME_BUILD/install-$product"
    fi
    cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic "-D$profile" \
        "$ROOT/packaging/start-native-install.c" "$ROOT/packaging/installer-process.c" \
        -o "$RUNTIME_BUILD/start-$product-install"
    # pkg-config emits compiler arguments, intentionally split by the shell.
    # shellcheck disable=SC2086
    cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic "-D$profile" \
        $(pkg-config --cflags gtk+-3.0) \
        "$ROOT/packaging/native-install-progress.c" "$ROOT/packaging/installer-status.c" \
        -o "$RUNTIME_BUILD/$product-progress" $(pkg-config --libs gtk+-3.0)
done

install -m 0755 "$ROOT/packaging/prepare-native-releases.py" "$RUNTIME_BUILD/prepare-native-releases"
test "$(file -b --mime-type "$THEME_DIR/infiltratoros.png")" = image/png
test ! -e "$PKGROOT/usr/lib/infiltratoros"
test -x "$PKGROOT/System/Commands/InfiltratorOS/activate-program-icons"
grep -Fxq "Exec=/System/Commands/InfiltratorOS/activate-program-icons" \
    "$PKGROOT/etc/xdg/autostart/infiltrator-program-icons.desktop"
test -x "$PKGROOT/System/Commands/InfiltratorOS/activate-calendar"
grep -Fxq 'Exec=/System/Commands/InfiltratorOS/activate-calendar' \
    "$PKGROOT/etc/xdg/autostart/infiltrator-calendar.desktop"
for product in system-monitor calculator files defragmenter temporal-resources filesystem-resources; do
    for tool in "install-$product" "start-$product-install" "$product-progress"; do
        test -x "$RUNTIME_BUILD/$tool"
        test ! -e "$PKGROOT/System/Commands/InfiltratorOS/$tool"
    done
done
# The package contains one maintainer-script-driven installation transaction.
# The eighteen internal workers exist only in this integrity-checked
# embedded bundle, NOT as permanent entries in the installed filesystem.
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 \
    --numeric-owner -czf "$WORK/native-runtime.tar.gz" -C "$RUNTIME_BUILD" .
runtime_digest=$(sha256sum "$WORK/native-runtime.tar.gz" | cut -d' ' -f1)
sed -i "s/@NATIVE_RUNTIME_SHA256@/$runtime_digest/g" "$PKGROOT/DEBIAN/postinst"
cat >> "$PKGROOT/DEBIAN/postinst" <<'RUNTIME_MARKER'
: <<'__INFILTRATOROS_NATIVE_BUNDLE_END__'
__INFILTRATOROS_NATIVE_BUNDLE_BEGIN__
RUNTIME_MARKER
base64 -w 76 "$WORK/native-runtime.tar.gz" >> "$PKGROOT/DEBIAN/postinst"
printf '%s\n' '__INFILTRATOROS_NATIVE_BUNDLE_END__' >> "$PKGROOT/DEBIAN/postinst"
sh -n "$PKGROOT/DEBIAN/postinst"
sh "$PKGROOT/DEBIAN/postinst" --verify-installer-bundle
OUT="$DIST/infiltratoros_${VERSION}_amd64.deb"
dpkg-deb --root-owner-group --build "$PKGROOT" "$OUT"
dpkg-deb --info "$OUT" >/dev/null
dpkg-deb --contents "$OUT" >/dev/null
echo "$OUT"
