#!/bin/sh
set -eu

DESTINATION="${1:?usage: stage-mb-fonts.sh DESTINATION WORK}"
WORK="${2:?usage: stage-mb-fonts.sh DESTINATION WORK}"

SOURCE_COMMIT="aa161e7342112beab8feb7669f072c870f742765"
ARCHIVE_URL="https://raw.githubusercontent.com/Infiltrator-Projects/MBLink/${SOURCE_COMMIT}/assets/fonts/mb-corpo-fonts.tar.xz"
ARCHIVE_SHA256="bdb6063f838a7fab22b4d6b412170640c69511df53aa3dfa9a4ea8431c9d8274"
BRAND_REGULAR_SHA256="c8bcd7e1a7d71169b38491d9b7c1ffe7ba7b46e888f0c1219931343a47bc0e05"
UI_BOLD_SHA256="d37ea986e2344d83390f94f170e6272b56efd00bfec808afe8314c4ca45d43b4"
UI_REGULAR_SHA256="94ede6629443c03d4362dcef425fb3ff520be5d654370021a34e81286804465c"

ARCHIVE="$WORK/mb-corpo-fonts.tar.xz"
EXTRACTED="$WORK/extracted"
rm -rf "$WORK"
mkdir -p "$DESTINATION" "$EXTRACTED"

wget --quiet --https-only --secure-protocol=TLSv1_2 \
    --output-document="$ARCHIVE" "$ARCHIVE_URL"
printf '%s  %s\n' "$ARCHIVE_SHA256" "$ARCHIVE" | sha256sum -c -
tar --no-same-owner -xJf "$ARCHIVE" -C "$EXTRACTED"

stage_font() {
    file="$1"
    expected_sha256="$2"
    source="$EXTRACTED/$file"
    target="$DESTINATION/$file"
    test -f "$source"
    printf '%s  %s\n' "$expected_sha256" "$source" | sha256sum -c -
    install -m 0644 "$source" "$target"
    if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
        touch -d "@${SOURCE_DATE_EPOCH}" "$target"
    fi
}

stage_font mb_corpo_a_cond_regular.ttf "$BRAND_REGULAR_SHA256"
stage_font mb_corpo_s_bold.ttf "$UI_BOLD_SHA256"
stage_font mb_corpo_s_regular.ttf "$UI_REGULAR_SHA256"
