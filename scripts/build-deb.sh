#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="${VERSION:-$(tr -d '[:space:]' < "$ROOT/VERSION")}"
DIST="$ROOT/dist"
WORK="$ROOT/.build/infiltratoros_${VERSION}"
PKGROOT="$WORK/root"
THEME_DIR="$PKGROOT/usr/share/plymouth/themes/infiltratoros"
ARTWORK="$ROOT/assets/infiltratoros.png"
ARTWORK_SHA256="4820891225675ed9bb0bb1bb82e3680df4bbdccc736ce5d092b80e95e5d05978"

rm -rf "$WORK"
mkdir -p "$PKGROOT/DEBIAN" "$THEME_DIR" "$DIST"
mkdir -p "$PKGROOT/usr/lib/infiltratoros"

printf '%s  %s\n' "$ARTWORK_SHA256" "$ARTWORK" | sha256sum -c -

sed "s/@VERSION@/$VERSION/g" "$ROOT/debian/control.in" > "$PKGROOT/DEBIAN/control"
install -m 0755 "$ROOT/debian/postinst" "$PKGROOT/DEBIAN/postinst"
install -m 0755 "$ROOT/debian/prerm" "$PKGROOT/DEBIAN/prerm"

cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/packaging/stage-theme.c" -o "$WORK/stage-theme"
"$WORK/stage-theme" \
    "$ROOT/plymouth/infiltratoros.plymouth" \
    "$ROOT/plymouth/infiltratoros.script" \
    "$ARTWORK" "$THEME_DIR"

cc -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/packaging/select-theme.c" \
    -o "$PKGROOT/usr/lib/infiltratoros/select-theme"

cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/packaging/install-system-monitor.c" \
    -o "$PKGROOT/usr/lib/infiltratoros/install-system-monitor"

cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/packaging/start-system-monitor-install.c" \
    -o "$PKGROOT/usr/lib/infiltratoros/start-system-monitor-install"

cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/packaging/install-calculator.c" \
    -o "$PKGROOT/usr/lib/infiltratoros/install-calculator"

cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/packaging/start-calculator-install.c" \
    -o "$PKGROOT/usr/lib/infiltratoros/start-calculator-install"

GTK_CFLAGS="$(pkg-config --cflags gtk+-3.0)"
GTK_LIBS="$(pkg-config --libs gtk+-3.0)"
# shellcheck disable=SC2086
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic $GTK_CFLAGS \
    "$ROOT/packaging/system-monitor-progress.c" \
    -o "$PKGROOT/usr/lib/infiltratoros/system-monitor-progress" $GTK_LIBS

# shellcheck disable=SC2086
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic $GTK_CFLAGS \
    "$ROOT/packaging/calculator-progress.c" \
    -o "$PKGROOT/usr/lib/infiltratoros/calculator-progress" $GTK_LIBS

PNG_TYPE="$(file -b --mime-type "$THEME_DIR/infiltratoros.png")"
if [ "$PNG_TYPE" != "image/png" ]; then
    echo "Artwork is not a PNG: $PNG_TYPE" >&2
    exit 1
fi

OUT="$DIST/infiltratoros_${VERSION}_amd64.deb"
rm -f "$OUT"
dpkg-deb --root-owner-group --build "$PKGROOT" "$OUT"

dpkg-deb --info "$OUT" >/dev/null
dpkg-deb --contents "$OUT" >/dev/null

echo "$OUT"
