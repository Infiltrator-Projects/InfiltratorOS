#!/bin/sh
# Exercise package-owned direct icons: clean install, repeat verification,
# legacy migration, local modifications and symlink defence. No Seed directory.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir -p "$WORK/reference" "$WORK/flat"
cat "$ROOT"/assets/icons/mercedes-icons-v1.part?? > "$WORK/icons.tar.xz"
printf '%s  %s\n' \
  '8d55ccbe5f1eefe3800ab12578a641524742b9482410d95757317434523b342c' \
  "$WORK/icons.tar.xz" | sha256sum -c - >/dev/null
tar -xJf "$WORK/icons.tar.xz" -C "$WORK/reference"
(cd "$WORK/reference/mercedes" && sha256sum -c "$WORK/reference/SHA256SUMS") >/dev/null
manifest="$WORK/icons.sha256"
: > "$manifest"
while IFS= read -r entry || [ -n "$entry" ]; do
    digest=${entry%%  *}
    name=${entry#*  }
    flat=$(printf '%s' "$name" | tr / -)
    install -m 0644 "$WORK/reference/mercedes/$name" "$WORK/flat/$flat"
    printf '%s  %s\n' "$digest" "$flat" >> "$manifest"
done < "$WORK/reference/SHA256SUMS"
test "$(wc -l < "$manifest")" -eq 1233
(cd "$WORK/flat" && sha256sum -c "$manifest") >/dev/null

helper="$ROOT/packaging/ensure-native-icons.sh"
stage_icons() {
    root=$1
    mkdir -p "$root/System/Resources/Icons" \
             "$root/System/Resources/Documentation/InfiltratorOS"
    cp -a "$WORK/flat/." "$root/System/Resources/Icons/"
    cp "$manifest" "$root/System/Resources/Documentation/InfiltratorOS/icons.sha256"
}
verify_icons() {
    INFILTRATR_NATIVE_ROOT="$1" sh "$helper"
}

fresh="$WORK/fresh"
stage_icons "$fresh"
verify_icons "$fresh"
icons="$fresh/System/Resources/Icons"
test ! -e "$icons/Seed"
test "$(find "$icons" -maxdepth 1 -type f | wc -l)" -eq 1233
test "$(find "$icons" -mindepth 1 -type d | wc -l)" -eq 0
find "$icons" -type f -printf '%f %i %T@\n' | sort > "$WORK/before"
verify_icons "$fresh"
find "$icons" -type f -printf '%f %i %T@\n' | sort > "$WORK/after"
cmp "$WORK/before" "$WORK/after"

upgrade="$WORK/upgrade"
stage_icons "$upgrade"
icons="$upgrade/System/Resources/Icons"
cp -a "$WORK/reference/mercedes" "$icons/"
printf 'shared custom icon\n' > "$icons/custom.svg"
verify_icons "$upgrade"
test ! -e "$icons/mercedes"
test "$(find "$icons" -maxdepth 1 -type f | wc -l)" -eq 1234
grep -qx 'shared custom icon' "$icons/custom.svg"

preserve="$WORK/preserve"
stage_icons "$preserve"
icons="$preserve/System/Resources/Icons"
cp -a "$WORK/reference/mercedes" "$icons/"
changed="$icons/mercedes/adapter/default/arrow_link_icon.png"
rm "$changed"
printf 'locally modified icon\n' > "$changed"
printf 'local addition\n' > "$icons/mercedes/custom.svg"
verify_icons "$preserve"
grep -qx 'locally modified icon' "$changed"
grep -qx 'local addition' "$icons/mercedes/custom.svg"
test "$(find "$icons/mercedes" -type f | wc -l)" -eq 2

unsafe="$WORK/unsafe"
stage_icons "$unsafe"
icons="$unsafe/System/Resources/Icons"
mkdir -p "$WORK/outside"
printf 'outside sentinel\n' > "$WORK/outside/sentinel"
rm "$icons/adapter-default-arrow_link_icon.png"
ln -s "$WORK/outside/sentinel" "$icons/adapter-default-arrow_link_icon.png"
if verify_icons "$unsafe" > "$WORK/unsafe-log" 2>&1; then
    echo 'Accepted an unsafe icon symlink' >&2; exit 1
fi
grep -q 'Missing/unsafe packaged icon:' "$WORK/unsafe-log"
grep -qx 'outside sentinel' "$WORK/outside/sentinel"

missing="$WORK/missing"
stage_icons "$missing"
rm "$missing/System/Resources/Icons/adapter-default-arrow_link_icon.png"
if verify_icons "$missing" > "$WORK/missing-log" 2>&1; then
    echo 'Accepted a missing directly packaged icon' >&2; exit 1
fi

printf 'Direct native icon package and legacy migration checks passed.\n'
