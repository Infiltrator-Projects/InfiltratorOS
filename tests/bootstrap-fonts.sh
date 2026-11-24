#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Exercise the actual preinst font installer and rollback against a private
# fake system root. The host font directory is never modified by this test.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$ROOT/packaging/bootstrap-fonts.sh"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
system="$work/system"
run="$work/run"
fonts="$system/System/Resources/Fonts"
mkdir -p "$system" "$run/bootstrap-fonts"
for font in $bootstrap_font_names; do
    cp "$ROOT/assets/fonts/$font" "$run/bootstrap-fonts/$font"
done
bootstrap_install_fonts "$run" "$system"
test ! -e "$run/bootstrap-fonts"
test "$(find "$fonts" -maxdepth 1 -type f -name '*.ttf' | wc -l)" -eq 5
for font in $bootstrap_font_names; do
    cmp "$ROOT/assets/fonts/$font" "$fonts/$font"
done
bootstrap_rollback_fonts "$run" "$system"
test "$(find "$fonts" -maxdepth 1 -type f | wc -l)" -eq 0

# An upgrade must restore previous bytes but leave unchanged fonts, unrelated
# resources and externally modified files alone.
mkdir "$run/bootstrap-fonts"
for font in $bootstrap_font_names; do
    cp "$ROOT/assets/fonts/$font" "$run/bootstrap-fonts/$font"
done
printf 'old font\n' > "$fonts/mb_corpo_s_text_regular.ttf"
chmod 0600 "$fonts/mb_corpo_s_text_regular.ttf"
cp "$ROOT/assets/fonts/mb_corpo_s_title_bold.ttf" "$fonts/"
printf 'unrelated font\n' > "$fonts/custom.ttf"
bootstrap_install_fonts "$run" "$system"
test ! -e "$run/bootstrap-fonts"
cmp "$ROOT/assets/fonts/mb_corpo_s_text_regular.ttf" "$fonts/mb_corpo_s_text_regular.ttf"
printf 'external edit\n' > "$fonts/mb_corpo_a_title_cond_regular.ttf"
bootstrap_rollback_fonts "$run" "$system"
grep -Fxq 'old font' "$fonts/mb_corpo_s_text_regular.ttf"
test "$(stat -c %a "$fonts/mb_corpo_s_text_regular.ttf")" = 600
cmp "$ROOT/assets/fonts/mb_corpo_s_title_bold.ttf" "$fonts/mb_corpo_s_title_bold.ttf"
grep -Fxq 'external edit' "$fonts/mb_corpo_a_title_cond_regular.ttf"
grep -Fxq 'unrelated font' "$fonts/custom.ttf"
test ! -e "$fonts/mb_corpo_a_screen_2_cond_regular.ttf"

# Refuse unsafe destinations before modifying any installed face.
mkdir "$run/bootstrap-fonts"
for font in $bootstrap_font_names; do
    cp "$ROOT/assets/fonts/$font" "$run/bootstrap-fonts/$font"
done
ln -s "$fonts/custom.ttf" "$fonts/mb_corpo_a_screen_2_cond_regular.ttf"
if bootstrap_install_fonts "$run" "$system"; then
    echo 'Accepted an unsafe font destination' >&2; exit 1
fi
grep -Fxq 'old font' "$fonts/mb_corpo_s_text_regular.ttf"
grep -Fxq 'unrelated font' "$fonts/custom.ttf"
test ! -e "$run/font-rollback"
printf 'Canonical interface font installation and rollback contracts passed.\n'
