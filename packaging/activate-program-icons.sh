#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# InfiltratorOS: register native /Programs executable icons for this user.
# Nemo uses GVfs metadata for raw executable file icons; Debian package
# postinst runs as root and cannot set the user's icon associations.
# This hook is run only by Cinnamon's session autostart as the actual user.
set -eu
[ "$(id -u)" -ne 0 ] || exit 0
command -v gio >/dev/null 2>&1 || exit 0

register_icon() {
    program=$1
    native=$2
    fallback=$3
    file="/Programs/$program"
    [ -f "$file" ] && [ -x "$file" ] && [ ! -L "$file" ] || return 0

    # Do not overwrite user-selected custom artwork.
    current=$(gio info -a metadata::custom-icon -a metadata::custom-icon-name "$file" 2>/dev/null || :)
    case "$current" in
        *'metadata::custom-icon:'*|*'metadata::custom-icon-name:'*) return 0 ;;
    esac
    if [ -f "$native" ] && [ ! -L "$native" ]; then
        gio set -t string "$file" metadata::custom-icon "file://$native" >/dev/null 2>&1 || :
    else
        gio set -t string "$file" metadata::custom-icon-name "$fallback" >/dev/null 2>&1 || :
    fi
}

register_icon calculator /System/Resources/Icons/calculator.svg calculator
register_icon files /System/Resources/Icons/files.svg system-file-manager
register_icon system-monitor /System/Resources/Icons/system-monitor.png system-monitor
register_icon defragmenter /System/Resources/Icons/defragmenter.png io.github.defragmenter
register_icon defragmenter-test-media /System/Resources/Icons/defragmenter.png io.github.defragmenter
