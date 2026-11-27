#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
# Report the last named assembly stage on any otherwise silent shell failure.
stage='initialising'
trap 'rc=$?; if [ "$rc" -ne 0 ]; then printf "InfiltratorOS package build failed during %s (exit %s)\\n" "$stage" "$rc" >&2; fi' EXIT

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
# Validate the staging path before deleting any previous build.
printf '%s\n' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'
# Compile the pinned first-party Common implementation, never copy its colours.
test "$(dpkg --print-architecture)" = amd64
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || printf 0)}"
case "$SOURCE_DATE_EPOCH" in ""|*[!0-9]*) echo "Invalid SOURCE_DATE_EPOCH" >&2; exit 1 ;; esac
export SOURCE_DATE_EPOCH

# Cache individual compilation units when ccache is available. Linking and
# package assembly always use the current source and version.
compile_c() {
    if command -v ccache >/dev/null 2>&1; then ccache cc "$@"; else cc "$@"; fi
}
compile_cxx() {
    if command -v ccache >/dev/null 2>&1; then ccache c++ "$@"; else c++ "$@"; fi
}

DIST="$ROOT/dist"
WORK="$ROOT/.build/infiltratoros_${VERSION}"
PKGROOT="$WORK/root"
# Application workers remain only in the signed maintainer-script bundle;
# they are staged under /Temporary/InfiltratorOS during installation.
RUNTIME_BUILD="$WORK/native-runtime"
THEME_DIR="$PKGROOT/usr/share/plymouth/themes/infiltratoros"
FONT_DIR="$PKGROOT/System/Resources/Fonts"
ICON_DIR="$PKGROOT/System/Resources/Icons"
RESOURCE_MANIFEST_DIR="$PKGROOT/System/Resources/Documentation"
ICON_PART_DIR="$ROOT/assets/icons"
ICON_ARCHIVE_SHA256="8d55ccbe5f1eefe3800ab12578a641524742b9482410d95757317434523b342c"
SYSTEM_ROOT="$PKGROOT/System"
ARTWORK="$ROOT/assets/infiltratoros.png"
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
stage='verifying artwork and fonts'
test -f "$FONT_MANIFEST"
(cd "$ROOT/assets/fonts" && sha256sum -c SHA256SUMS)
# Reassemble and verify the source archive before flattening its icons.
stage='assembling icons'
archive="$WORK/mercedes-icons-v1.tar.xz"
cat "$ICON_PART_DIR"/mercedes-icons-v1.part?? > "$archive"
printf '%s  %s\n' "$ICON_ARCHIVE_SHA256" "$archive" | sha256sum -c -
mkdir -p "$WORK/icon-source"
tar -xJf "$archive" -C "$WORK/icon-source" --no-same-owner
original_manifest="$WORK/icon-source/SHA256SUMS"
icon_manifest="$RESOURCE_MANIFEST_DIR/InfiltratorOS-icons.sha256"
: > "$icon_manifest"
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
    if [ -e "$ICON_DIR/$flat" ]; then echo "Duplicate icon destination: $flat" >&2; exit 1; fi
    install -m 0644 -- "$WORK/icon-source/mercedes/$name" "$ICON_DIR/$flat"
    printf '%s  %s\n' "$digest" "$flat" >> "$icon_manifest"
done < "$original_manifest"

stage='staging Debian files and fonts'
sed "s/@VERSION@/$VERSION/g" "$ROOT/debian/control.in" > "$PKGROOT/DEBIAN/control"
install -m 0755 "$ROOT/debian/postinst" "$PKGROOT/DEBIAN/postinst"
install -m 0755 "$ROOT/debian/prerm" "$PKGROOT/DEBIAN/prerm"
install -m 0755 "$ROOT/debian/preinst" "$PKGROOT/DEBIAN/preinst"
install -m 0755 "$ROOT/debian/postrm" "$PKGROOT/DEBIAN/postrm"
for script in preinst postinst postrm; do
    sed -i "s/@VERSION@/$VERSION/g" "$PKGROOT/DEBIAN/$script"
done
# Share the rollback implementation between Debian's two control scripts;
# it is not an installed OS command or a persistent resource.
for script in preinst postrm; do
    sed -i "/^# @BOOTSTRAP_FONT_FUNCTIONS@$/r $ROOT/packaging/bootstrap-fonts.sh" "$PKGROOT/DEBIAN/$script"
done
for script in preinst postinst; do
    sed -i "/^# @SUPERVISOR_RECOVERY_FUNCTIONS@$/r $ROOT/packaging/supervisor-recovery.sh" "$PKGROOT/DEBIAN/$script"
done
install -m 0755 "$ROOT/packaging/ensure-native-fonts.sh" \
    "$PKGROOT/System/Commands/InfiltratorOS/ensure-native-fonts"
install -m 0755 "$ROOT/packaging/ensure-native-icons.sh" \
    "$PKGROOT/System/Commands/InfiltratorOS/ensure-native-icons"
install -m 0644 "$FONT_MANIFEST" "$RESOURCE_MANIFEST_DIR/InfiltratorOS-fonts.sha256"
while IFS= read -r entry; do
    font_file=${entry#*  }
    install -m 0644 "$ROOT/assets/fonts/$font_file" "$FONT_DIR/$font_file"
done < "$FONT_MANIFEST"
install -m 0644 "$ROOT/packaging/infiltrator-native-path.sh" \
    "$PKGROOT/etc/profile.d/90-infiltratoros-native-path.sh"
# Compile the per-user Cinnamon integration with GSettings directly in C.
compile_c -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    $(pkg-config --cflags gio-2.0 json-c) \
    -c "$ROOT/packaging/activate-calendar.c" -o "$WORK/activate-calendar.o"
cc "$WORK/activate-calendar.o" \
    -o "$PKGROOT/System/Commands/InfiltratorOS/activate-calendar" \
    $(pkg-config --libs gio-2.0 json-c)
install -m 0644 "$ROOT/packaging/infiltrator-calendar.desktop" \
    "$PKGROOT/etc/xdg/autostart/infiltrator-calendar.desktop"
install -m 0755 "$ROOT/packaging/activate-program-icons.sh" \
    "$PKGROOT/System/Commands/InfiltratorOS/activate-program-icons"
install -m 0644 "$ROOT/packaging/infiltrator-program-icons.desktop" \
    "$PKGROOT/etc/xdg/autostart/infiltrator-program-icons.desktop"

compile_c -std=c11 -O2 -Wall -Wextra -Werror \
    -c "$ROOT/packaging/stage-theme.c" -o "$WORK/stage-theme.o"
cc "$WORK/stage-theme.o" -o "$WORK/stage-theme"
"$WORK/stage-theme" "$ROOT/plymouth/infiltratoros.plymouth" \
    "$ROOT/plymouth/infiltratoros.script" "$ARTWORK" "$THEME_DIR"
compile_c -std=c11 -O2 -Wall -Wextra -Werror -c "$ROOT/packaging/select-theme.c" \
    -o "$WORK/select-theme.o"
cc "$WORK/select-theme.o" -o "$PKGROOT/System/Commands/InfiltratorOS/select-theme"

# Keep the udev integration in the conversion package. The executable and
# the actual versioned module loader live in the native System hierarchy.
compile_c -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    -c "$ROOT/packaging/load-native-driver.c" -o "$WORK/load-native-driver.o"
cc "$WORK/load-native-driver.o" -o "$PKGROOT/System/Commands/InfiltratorOS/load-native-driver"
install -D -m 0644 "$ROOT/packaging/81-infiltratoros-drivers.rules" \
    "$PKGROOT/etc/udev/rules.d/81-infiltratoros-drivers.rules"

# Store the three static signed provider catalogues in the temporary seal,
# not in the installed OS filesystem. All invocation paths use one C++ worker.
stage='compiling native worker'
compile_cxx -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic -pthread \
    $(pkg-config --cflags libcurl json-c openssl) -I"$ROOT/src/infiltratr-common/include" \
    -c "$ROOT/packaging/native-component-worker.cpp" -o "$WORK/native-component-worker.o"
c++ -pthread "$WORK/native-component-worker.o" -o "$WORK/native-component-worker" \
    $(pkg-config --libs libcurl json-c openssl) -ldl
for inventory in temporal-resources.json filesystem-resources.json filesystem-additional.json; do
    install -m 0644 "$ROOT/packaging/$inventory" "$RUNTIME_BUILD/$inventory"
done

# These shared objects have no product profile or version dependency.
stage='compiling installer UI and shared objects'
for unit in installer-process installer-status native-progress-count; do
    compile_c -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
        -c "$ROOT/packaging/$unit.c" -o "$WORK/$unit.o"
done
compile_c -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    $(pkg-config --cflags gtk+-3.0 pangoft2 fontconfig) \
    -I"$ROOT/src/infiltratr-common/include" \
    -c "$ROOT/src/infiltratr-common/src/design.c" -o "$WORK/design.o"

for product in system-monitor calculator calendar files defragmenter boot temporal-resources filesystem-resources; do
    case "$product" in
        system-monitor) profile=INSTALL_SYSTEM_MONITOR ;;
        calculator) profile=INSTALL_CALCULATOR ;;
        calendar) profile=INSTALL_CALENDAR ;;
        files) profile=INSTALL_FILES ;;
        defragmenter) profile=INSTALL_DEFRAGMENTER ;;
        boot) profile=INSTALL_BOOT ;;
        temporal-resources) profile=INSTALL_TEMPORAL_RESOURCES ;;
        filesystem-resources) profile=INSTALL_FILESYSTEM_RESOURCES ;;
    esac
    # The one compiled native worker handles each signed catalogue identity.
    install -m 0755 "$WORK/native-component-worker" "$RUNTIME_BUILD/install-$product"
    compile_c -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic "-D$profile" \
        -c "$ROOT/packaging/start-native-install.c" -o "$WORK/start-$product.o"
    cc "$WORK/start-$product.o" "$WORK/installer-process.o" \
        -o "$RUNTIME_BUILD/start-$product-install"
    compile_c -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic "-D$profile" \
        "-DINFILTRATOROS_VERSION=\"$VERSION\"" \
        $(pkg-config --cflags gtk+-3.0 pangoft2 fontconfig) \
        -I"$ROOT/src/infiltratr-common/include" \
        -c "$ROOT/packaging/native-install-progress.c" -o "$WORK/progress-$product.o"
    cc "$WORK/progress-$product.o" "$WORK/native-progress-count.o" \
        "$WORK/installer-status.o" "$WORK/design.o" \
        -o "$RUNTIME_BUILD/$product-progress" $(pkg-config --libs gtk+-3.0 pangoft2 fontconfig)
done

stage='compiling dispatcher and validating staged files'
# The embedded OS dispatcher and worker implementations are native C/C++.
compile_cxx -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic \
    -c "$ROOT/packaging/native-orchestrator.cpp" -o "$WORK/native-orchestrator.o"
c++ "$WORK/native-orchestrator.o" "$WORK/installer-status.o" \
    -o "$RUNTIME_BUILD/orchestrate-native-install"
# The early window needs only these five actual font roles, not the complete
# resource collection. Embed a separately sealed bootstrap in preinst so dpkg
# can display the real UI before unpacking its package-owned resource files.
stage='assembling and verifying bootstrap bundle'
BOOTSTRAP="$WORK/bootstrap"
mkdir -p "$BOOTSTRAP/bootstrap-fonts"
for tool in start-system-monitor-install system-monitor-progress; do
    install -m 0755 "$RUNTIME_BUILD/$tool" "$BOOTSTRAP/$tool"
done
for font in mb_corpo_a_title_cond_regular.ttf mb_corpo_s_title_bold.ttf \
            mb_corpo_s_text_regular.ttf mb_corpo_s_text_cond_regular.ttf \
            mb_corpo_a_screen_2_cond_regular.ttf; do
    install -m 0644 "$FONT_DIR/$font" "$BOOTSTRAP/bootstrap-fonts/$font"
done
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 \
    --numeric-owner -czf "$WORK/bootstrap.tar.gz" -C "$BOOTSTRAP" .
bootstrap_digest=$(sha256sum "$WORK/bootstrap.tar.gz" | cut -d' ' -f1)
sed -i "s/@BOOTSTRAP_SHA256@/$bootstrap_digest/g" "$PKGROOT/DEBIAN/preinst"
cat >> "$PKGROOT/DEBIAN/preinst" <<'BOOTSTRAP_MARKER'
: <<'__INFILTRATOROS_BOOTSTRAP_END__'
__INFILTRATOROS_BOOTSTRAP_BEGIN__
BOOTSTRAP_MARKER
base64 -w 76 "$WORK/bootstrap.tar.gz" >> "$PKGROOT/DEBIAN/preinst"
printf '%s\n' '__INFILTRATOROS_BOOTSTRAP_END__' >> "$PKGROOT/DEBIAN/preinst"
sh -n "$PKGROOT/DEBIAN/preinst" "$PKGROOT/DEBIAN/postrm"
sh "$PKGROOT/DEBIAN/preinst" --verify-bootstrap-bundle

# The package contains one maintainer-script-driven installation transaction.
# The internal workers exist only in this integrity-checked
# embedded bundle, NOT as permanent entries in the installed filesystem.
stage='assembling and verifying native installer bundle'
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
stage='building Debian package'
OUT="$DIST/infiltratoros_${VERSION}_amd64.deb"
dpkg-deb --root-owner-group --build "$PKGROOT" "$OUT"
echo "$OUT"
