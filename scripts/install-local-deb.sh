#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Install a locally built DEB without requiring APT's download sandbox to
# traverse a private home directory. The package remains readable by _apt;
# sandboxing stays enabled and every genuine APT diagnostic is preserved.
set -eu
[ "$#" -eq 1 ] && [ -f "$1" ] || {
    echo 'Usage: install-local-deb.sh PACKAGE.deb' >&2
    exit 2
}
[ "$(dpkg-deb -f "$1" Package)" = infiltratoros ] || {
    echo 'Expected an InfiltratorOS Debian package' >&2
    exit 2
}
stage=$(mktemp -d /tmp/infiltratoros-local.XXXXXXXX)
trap 'rm -rf -- "$stage"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
# The directory is private during the copy. Expose only read/traverse access
# after staging; no other user receives write permissions.
package="$stage/infiltratoros.deb"
install -m 0644 -- "$1" "$package"
chmod 0755 "$stage"
if [ "$(id -u)" -eq 0 ]; then
    apt-get -o DPkg::Lock::Timeout=600 install "$package"
else
    sudo apt-get -o DPkg::Lock::Timeout=600 install "$package"
fi
