#!/bin/sh
# Exercise the actual seed and installer across fresh and upgraded resources.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
seed="$WORK/seed"
mkdir -p "$seed" "$WORK/reference"
cat "$ROOT"/assets/icons/mercedes-icons-v1.part?? > "$seed/mercedes-icons-v1.tar.xz"
tar -xJf "$seed/mercedes-icons-v1.tar.xz" -C "$WORK/reference"
awk '{ name=$2; gsub("/", "-", name); print $1 "  " name }' \
    "$WORK/reference/SHA256SUMS" > "$WORK/flat-checksums"
test "$(awk '{ print $2 }' "$WORK/flat-checksums" | sort -u | wc -l)" -eq 1233
helper="$ROOT/packaging/ensure-native-icons.sh"
install_icons() {
    INFILTRATR_NATIVE_ROOT="$1" INFILTRATR_ICON_SOURCE_DIR="$seed" sh "$helper"
}
verify_icons() {
    (cd "$1/System/Resources/Icons" && sha256sum -c "$WORK/flat-checksums") > "$WORK/verified"
}

# Fresh installation has 1,233 verified files directly in Icons, and no folders.
fresh="$WORK/fresh"
install_icons "$fresh"
icons="$fresh/System/Resources/Icons"
test "$(find "$icons" -maxdepth 1 -type f | wc -l)" -eq 1233
test "$(find "$icons" -mindepth 1 -type d | wc -l)" -eq 0
verify_icons "$fresh"
find "$icons" -type f -printf '%f %i %T@\n' | sort > "$WORK/before"
install_icons "$fresh"
find "$icons" -type f -printf '%f %i %T@\n' | sort > "$WORK/after"
cmp "$WORK/before" "$WORK/after"
verify_icons "$fresh"

# An unmodified 1.0.22 tree disappears only after its flat replacements verify.
upgrade="$WORK/upgrade"
icons="$upgrade/System/Resources/Icons"
mkdir -p "$icons"
cp -a "$WORK/reference/mercedes" "$icons/"
printf 'shared custom icon\n' > "$icons/custom.svg"
install_icons "$upgrade"
test ! -e "$icons/mercedes"
test "$(find "$icons" -maxdepth 1 -type f | wc -l)" -eq 1234
grep -qx 'shared custom icon' "$icons/custom.svg"
verify_icons "$upgrade"

# Migration must retain a modified old icon and an unrelated file in that tree.
preserve="$WORK/preserve"
icons="$preserve/System/Resources/Icons"
mkdir -p "$icons"
cp -a "$WORK/reference/mercedes" "$icons/"
changed="$icons/mercedes/adapter/default/arrow_link_icon.png"
# Break the archive hard link before modifying this one resource.
rm "$changed"
printf 'locally modified icon\n' > "$changed"
printf 'local addition\n' > "$icons/mercedes/custom.svg"
install_icons "$preserve"
grep -qx 'locally modified icon' "$changed"
grep -qx 'local addition' "$icons/mercedes/custom.svg"
test "$(find "$icons/mercedes" -type f | wc -l)" -eq 2
verify_icons "$preserve"

# A destination symlink must never redirect a write outside the Icons directory.
unsafe="$WORK/unsafe"
icons="$unsafe/System/Resources/Icons"
mkdir -p "$icons" "$WORK/outside"
printf 'outside sentinel\n' > "$WORK/outside/sentinel"
ln -s "$WORK/outside" "$icons/adapter-default-arrow_link_icon.png"
if install_icons "$unsafe" > "$WORK/unsafe-log" 2>&1; then
    echo 'Icon installation accepted a destination symlink' >&2
    exit 1
fi
grep -q 'Unsafe icon destination:' "$WORK/unsafe-log"
test "$(find "$WORK/outside" -type f | wc -l)" -eq 1
grep -qx 'outside sentinel' "$WORK/outside/sentinel"
printf 'Native icon installation and migration checks passed.\n'
