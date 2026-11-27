#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs real provider source installers on an explicitly disposable GitHub
# hosted machine. Never executes BOOT or the full OS integration package.
set -euo pipefail
[[ $# = 3 && $1 = --disposable-ci && $(id -u) = 0 ]] || {
    echo 'Usage: native-live-integration.sh --disposable-ci TOOLS LOG-DIRECTORY' >&2
    exit 2
}
tools=$(realpath "$2")
logs=$(realpath "$3")
mkdir -p "$logs"
for name in infiltrator-filesystem-ofs infiltrator-clock-binary; do
    if dpkg-query -W -f='${Status}' "$name" 2>/dev/null | grep -q '^install ok installed$'; then
        echo "Unexpected pre-existing package: $name" >&2
        exit 1
    fi
done
install -d -m0755 /Temporary/InfiltratorOS
stage=$(mktemp -d /Temporary/InfiltratorOS/native-worker.live.XXXXXXXX)
cleanup() {
    rc=$?
    trap - EXIT
    if [[ -d "$stage/status" ]]; then
        cp -a "$stage/status/." "$logs/" || true
    fi
    for file in "$stage/selected.list" "$stage/filesystems-all.list"; do
        [[ ! -f "$file" ]] || cp "$file" "$logs/"
    done
    chmod -R a+rX "$logs"
    rm -rf -- "$stage"
    exit "$rc"
}
trap cleanup EXIT
mkdir -p "$stage/status"
install -m0755 "$tools/native-component-worker" "$stage/native-component-worker"
install -m0755 "$tools/native-snapshot" "$stage/prepare-native-releases"
for file in filesystem-resources.json filesystem-additional.json temporal-resources.json; do
    install -m0644 "$tools/$file" "$stage/$file"
done
for name in install-filesystem-resources install-temporal-resources; do
    ln -s native-component-worker "$stage/$name"
done
"$stage/install-temporal-resources" --catalogue > "$stage/providers.list"
"$stage/install-filesystem-resources" --catalogue > "$stage/filesystems.list"
"$stage/install-filesystem-resources" --catalogue-all > "$stage/filesystems-all.list"
# BOOT must be in the selected snapshot but is intentionally never launched.
printf '%s\n' filesystem-ofs clock-binary boot > "$stage/selected.list"
"$stage/prepare-native-releases" "$stage/release-json" "$stage/providers.list" \
    "$stage/filesystems.list" "$stage/filesystems-all.list" "$stage/selected.list"
env INFILTRATOROS_INSTALL_RUNTIME="$stage" \
    INFILTRATOROS_RELEASE_DIR="$stage/release-json" \
    INFILTRATOROS_SMART_BUILD_QUEUE=1 \
    "$stage/install-filesystem-resources" filesystem-ofs
for slug in filesystem-ofs clock-binary; do
    grep -qx 'state=complete' "$stage/status/$slug-install.status"
    grep -qx 'progress=100' "$stage/status/$slug-install.status"
    test "$(dpkg-query -W -f='${Status}' "infiltrator-$slug")" = 'install ok installed'
done
test -s /System/Filesystems/OFS.so
"$stage/native-component-worker" --validate-common-v2 /System/Filesystems/OFS.so ofs
test ! -e "$stage/status/boot-install.status"
# A second invocation must verify the already installed native packages
# instead of performing another build or an unnecessary Debian transaction.
cp "$stage/status/smart-build-timings.log" "$stage/status/first-build-timings.log"
env INFILTRATOROS_INSTALL_RUNTIME="$stage" \
    INFILTRATOROS_RELEASE_DIR="$stage/release-json" \
    INFILTRATOROS_SMART_BUILD_QUEUE=1 \
    "$stage/install-filesystem-resources" filesystem-ofs
for slug in filesystem-ofs clock-binary; do
    grep -qx 'stage=Already installed' "$stage/status/$slug-install.status"
done
grep -qx 'Build admission uses available CPUs and RAM. Selected builds: 0' \
    "$stage/status/smart-build-timings.log"
echo "Real provider build, installation, descriptor and idempotence contracts passed"
