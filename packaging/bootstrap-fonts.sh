# SPDX-License-Identifier: GPL-3.0-or-later
# This code is embedded in preinst and postrm at package build time. The
# verified bootstrap archive supplies source bytes, never a second font
# search directory. Install only the five UI roles into the canonical
# resource directory; dpkg subsequently owns the complete resource payload.
bootstrap_font_names='mb_corpo_a_title_cond_regular.ttf mb_corpo_s_title_bold.ttf mb_corpo_s_text_regular.ttf mb_corpo_s_text_cond_regular.ttf mb_corpo_a_screen_2_cond_regular.ttf'

bootstrap_font_directory() (
    root=$1
    case "$root" in ''|/*) ;; *) return 1 ;; esac
    # Never follow a symlink or write through an untrusted directory.
    for directory in "$root/System" "$root/System/Resources" "$root/System/Resources/Fonts"; do
        [ ! -L "$directory" ] || return 1
        if [ ! -d "$directory" ]; then mkdir -m 0755 -- "$directory" || return 1; fi
        [ "$(find "$directory" -maxdepth 0 -type d -uid "$(id -u)" ! -perm /022 -print)" = "$directory" ] || return 1
    done
)

bootstrap_install_fonts() (
    run=$1
    root=$2
    destination="$root/System/Resources/Fonts"
    journal="$run/font-rollback"
    source="$run/bootstrap-fonts"
    bootstrap_font_directory "$root" || return 1
    # Validate the complete five-face source and all destinations before
    # modifying the first installed font.
    for font in $bootstrap_font_names; do
        [ -s "$source/$font" ] && [ ! -L "$source/$font" ] || return 1
        [ ! -L "$destination/$font" ] || return 1
        if [ -e "$destination/$font" ]; then
            [ -f "$destination/$font" ] || return 1
        fi
    done
    mkdir -m 0700 -- "$journal" || return 1
    for font in $bootstrap_font_names; do
        target="$destination/$font"
        if [ -f "$target" ] && cmp -s -- "$source/$font" "$target"; then
            continue
        fi
        if [ -f "$target" ]; then
            cp -a -- "$target" "$journal/$font.original" || return 1
        else
            touch "$journal/$font.absent" || return 1
        fi
        # The journal is published before replacement. On abort only bytes
        # installed by this bootstrap may be removed or replaced.
        digest=$(sha256sum -- "$source/$font") || return 1
        printf '%s\n' "${digest%% *}" > "$journal/$font.sha256" || return 1
        temporary=$(mktemp "$destination/.infiltrator-font.XXXXXXXX") || return 1
        if ! install -m 0644 -- "$source/$font" "$temporary" ||
           ! mv -fT -- "$temporary" "$target"; then
            rm -f -- "$temporary"
            return 1
        fi
    done
    # Ensure the actual canonical files match the sealed sources before GTK
    # starts. Remove the extracted source copies, not the installed faces.
    for font in $bootstrap_font_names; do
        cmp -s -- "$source/$font" "$destination/$font" || return 1
    done
    rm -rf -- "$source"
)

bootstrap_rollback_fonts() (
    run=$1
    root=$2
    journal="$run/font-rollback"
    destination="$root/System/Resources/Fonts"
    [ -d "$journal" ] && [ ! -L "$journal" ] || return 0
    bootstrap_font_directory "$root" || return 1
    for font in $bootstrap_font_names; do
        [ -f "$journal/$font.sha256" ] || continue
        target="$destination/$font"
        [ ! -L "$target" ] || continue
        if [ -e "$target" ]; then
            [ -f "$target" ] || continue
            digest=$(cat "$journal/$font.sha256") || return 1
            # An external edit or dpkg's restoration of different bytes is
            # not ours to overwrite.
            printf '%s  %s\n' "$digest" "$target" | sha256sum -c - >/dev/null 2>&1 || continue
        fi
        if [ -f "$journal/$font.original" ]; then
            temporary=$(mktemp "$destination/.infiltrator-font.XXXXXXXX") || return 1
            if ! cp -a -- "$journal/$font.original" "$temporary" ||
               ! mv -fT -- "$temporary" "$target"; then
                rm -f -- "$temporary"
                return 1
            fi
        elif [ -f "$journal/$font.absent" ]; then
            rm -f -- "$target" || return 1
        fi
    done
    rm -rf -- "$journal"
)
