#!/bin/sh
# Exercise a real dpkg upgrade of the old OS-owned documentation directory.
set -eu
[ "$(id -u)" -eq 0 ] || { echo 'Run in an isolated root or as root' >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
for version in 1 2; do
    image="$work/v$version"
    docs="$image/System/Resources/Documentation"
    mkdir -p "$image/DEBIAN" "$docs"
    printf 'Package: infiltratoros-layout-fixture\nVersion: %s.0\nArchitecture: all\nMaintainer: Test <test@example.invalid>\nDescription: isolated package-layout upgrade\n' "$version" > "$image/DEBIAN/control"
    if [ "$version" -eq 1 ]; then
        mkdir "$docs/InfiltratorOS"
        printf 'old fonts' > "$docs/InfiltratorOS/fonts.sha256"
        printf 'old icons' > "$docs/InfiltratorOS/icons.sha256"
    else
        printf 'new fonts' > "$docs/InfiltratorOS-fonts.sha256"
        printf 'new icons' > "$docs/InfiltratorOS-icons.sha256"
    fi
    dpkg-deb --root-owner-group --build "$image" "$work/v$version.deb" >/dev/null
done
system="$work/system"
dpkg --root "$system" --unpack "$work/v1.deb" >/dev/null
printf 'personal manual' > "$system/System/Resources/Documentation/Personal.pdf"
dpkg --root "$system" --unpack "$work/v2.deb" >/dev/null
docs="$system/System/Resources/Documentation"
test ! -e "$docs/InfiltratorOS"
test "$(cat "$docs/Personal.pdf")" = 'personal manual'
for name in InfiltratorOS-fonts.sha256 InfiltratorOS-icons.sha256; do
    test -f "$docs/$name"
    owner=$(dpkg-query --admindir "$system/var/lib/dpkg" --search "/System/Resources/Documentation/$name")
    test "$owner" = "infiltratoros-layout-fixture: /System/Resources/Documentation/$name"
done
echo 'OS manifest upgrade removes its old empty folder and retains independent documents'
