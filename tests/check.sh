#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Test native InfiltratorOS code. Do not invoke the retired Python workers or
# compile/test independent provider source trees in this repository.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"
sh -n packaging/ensure-native-icons.sh packaging/ensure-native-fonts.sh debian/postinst
grep -Fq 'fc-cache -f "$FONT_DIR"' packaging/ensure-native-fonts.sh
! grep -Fq 'fc-cache -f "$FONT_DIR" >/dev/null 2>&1 || true' packaging/ensure-native-fonts.sh
grep -Fq 'infiltratr_theme_resolve(' packaging/native-install-progress.c
grep -Fq 'required_fonts_visible("/System/Resources/Fonts")' packaging/native-install-progress.c
grep -Fq 'pango_font_map_load_font(' packaging/native-install-progress.c
grep -Fq 'pkg-config --libs gtk+-3.0 pangoft2 fontconfig' scripts/build-deb.sh
grep -Fq 'infiltratr-common/src/design.c' scripts/build-deb.sh
grep -Fq 'Waiting for the verified final installation result' packaging/native-install-progress.c
grep -Fq 'required(initramfs)' packaging/select-theme.c
grep -Fq '"--kill-after=30s", "86300s"' packaging/start-native-install.c
! grep -Fq '"plymouth-set-default-theme"' packaging/select-theme.c

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
c++ -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic -pthread \
    $(pkg-config --cflags libcurl json-c openssl) \
    tests/native-component-test.cpp -o "$WORK/native-component-test" \
    $(pkg-config --libs libcurl json-c openssl)
"$WORK/native-component-test"

for test in native-release-contract select-theme-command; do
    cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
       "tests/$test-test.c" -o "$WORK/$test-test"
    "$WORK/$test-test"
done
sh tests/native-icons.sh

profile=packaging/installer-profile.h
grep -Fq '#define STATUS_DIR "/Temporary/InfiltratorOS"' "$profile"
grep -Fq '#define LOG_PATH STATUS_DIR "/" INSTALL_SLUG "-install.log"' "$profile"
grep -Fq '"/Temporary/InfiltratorOS/native-worker."' packaging/start-native-install.c

mkdir -p "$WORK/native-checks"
printf '%s\n' 'known-good-native-installer' > "$WORK/native.run"
digest="$(sha256sum "$WORK/native.run" | cut -d' ' -f1)"
printf '%s  %s\n' "$digest" "test-native-installer.run" > "$WORK/native-checks/Calculator.sha256"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    tests/native-download-test.c -o "$WORK/native-download-test"
"$WORK/native-download-test" "$WORK/native-checks" \
    "https://github.com/Infiltrator-Projects/Calculator/releases/download/v1.2.3/test-native-installer.run" \
    "$WORK/native.run"
for profile in INSTALL_TEMPORAL_RESOURCES INSTALL_FILESYSTEM_RESOURCES; do
    cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic "-D$profile" \
        tests/native-provider-slug-test.c packaging/installer-process.c \
        -o "$WORK/native-provider-slug-test"
    "$WORK/native-provider-slug-test"
done
grep -Fq 'fopen(status_file(), "w")' packaging/start-native-install.c
grep -Fq 'provider_slug' packaging/start-native-install.c
grep -Fq 'run_status' packaging/start-native-install.c
grep -Fq 'provider_slug, NULL' packaging/start-native-install.c
grep -Fq 'ui.product_name' packaging/native-install-progress.c
grep -Fq 'status_root="$stage/status"' debian/postinst
cc -std=c11 -O2 -DNDEBUG -Wall -Wextra -Werror -Wpedantic \
    tests/native-progress-count-test.c packaging/native-progress-count.c \
    packaging/installer-status.c -o "$WORK/native-progress-count-test"
"$WORK/native-progress-count-test"
! grep -Fq 'additional_filesystem_jobs' packaging/native-install-progress.c
grep -Fq 'refresh_aggregate' packaging/native-install-progress.c
grep -Fq 'INFILTRATOROS_PROGRESS_MODE' packaging/start-native-install.c
grep -Fq 'INFILTRATOROS_PROGRESS_MODE' packaging/native-orchestrator.cpp
grep -Fq 'INFILTRATOROS_PROGRESS_MODE=aggregate' debian/postinst
grep -Fq 'provision.lock' debian/postinst
grep -Fq 'flock -n 9' debian/postinst
# An unresolved worker must never surrender the run-wide lock at 24 hours.
grep -Fq 'while :; do' debian/postinst
grep -Fq 'Installation exceeded 24 hours; retaining the provisioning lock' debian/postinst
! grep -Fq 'while [ "$elapsed" -lt 86400 ]; do' debian/postinst
! grep -Fq 'publish_summary timeout' debian/postinst
grep -Fq 'else if (strcmp(status.state, "complete") != 0)' packaging/native-progress-count.c
! grep -Fq 'rm -rf -- "$STATUS_ROOT"' debian/postinst
for contract in process status; do
    cc -std=c11 -O2 -DNDEBUG -Wall -Wextra -Werror -Wpedantic \
       "tests/installer-$contract-test.c" "packaging/installer-$contract.c" \
       -o "$WORK/$contract-test"
    "$WORK/$contract-test"
done
sh -n packaging/activate-program-icons.sh
grep -Fq 'OnlyShowIn=X-Cinnamon;' packaging/infiltrator-program-icons.desktop
grep -Fq 'metadata::custom-icon' packaging/activate-program-icons.sh
! grep -q -E '(^|[[:space:],])python3([[:space:],]|$)' debian/control.in
echo 'Native C/C++ installer contracts passed.'
