#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
sh -n "$ROOT/packaging/ensure-native-icons.sh"
sh -n "$ROOT/packaging/ensure-native-fonts.sh"
grep -Fq 'fc-cache -f "$FONT_DIR"' "$ROOT/packaging/ensure-native-fonts.sh"
! grep -Fq 'fc-cache -f "$FONT_DIR" >/dev/null 2>&1 || true' "$ROOT/packaging/ensure-native-fonts.sh"
grep -Fq 'infiltratr_theme_resolve(' "$ROOT/packaging/native-install-progress.c"
grep -Fq 'required_fonts_visible("/System/Resources/Fonts")' "$ROOT/packaging/native-install-progress.c"
grep -Fq 'pango_font_map_load_font(' "$ROOT/packaging/native-install-progress.c"
grep -Fq 'pkg-config --libs gtk+-3.0 pangoft2 fontconfig' "$ROOT/scripts/build-deb.sh"
grep -Fq 'snprintf(native_version, sizeof(native_version), "%s", release_version)' "$ROOT/packaging/install-system-monitor.c"
grep -Fq 'infiltratr-common/src/design.c' "$ROOT/scripts/build-deb.sh"
python3 "$ROOT/tests/calendar-switch-test.py"
python3 "$ROOT/tests/temporal-install-test.py"
python3 "$ROOT/tests/filesystem-install-test.py"
python3 "$ROOT/tests/filesystem-additional-test.py"
python3 "$ROOT/tests/postinst-bootstrap-test.py"
python3 "$ROOT/tests/orchestrate-native-install-test.py"
python3 "$ROOT/tests/native-release-test.py"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/tests/native-release-contract-test.c" -o "$ROOT/.build-native-release-contract-test"
"$ROOT/.build-native-release-contract-test"
rm -f "$ROOT/.build-native-release-contract-test"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/tests/select-theme-command-test.c" -o "$ROOT/.build-select-theme-test"
"$ROOT/.build-select-theme-test"
rm -f "$ROOT/.build-select-theme-test"
# Prevent regression to purging stock monitors before verifying the replacement.
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
monitor = (root / "packaging/install-system-monitor.c").read_text()
assert monitor.index('native_package_version_matches("/var/lib/dpkg/status"') < monitor.index("purge_stock_monitors();")
progress = (root / "packaging/native-install-progress.c").read_text()
assert 'Waiting for the verified final installation result' in progress
assert progress.index('if (outcome != 0)') < progress.index('if (finished == total_jobs)')
theme = (root / "packaging/select-theme.c").read_text()
assert "optional(" not in theme
assert '"plymouth-set-default-theme"' not in theme
assert 'required(add) != 0 || required(select) != 0' in theme
assert "required(initramfs)" in theme
worker = (root / "packaging/start-native-install.c").read_text()
assert '"--kill-after=30s", "86300s"' in worker
PY
sh "$ROOT/tests/native-icons.sh"

# InfiltratorOS-owned install state must live under the native /Temporary.
profile="$ROOT/packaging/installer-profile.h"
grep -Fq '#define STATUS_DIR "/Temporary/InfiltratorOS"' "$profile"
grep -Fq '#define LOG_PATH STATUS_DIR "/" INSTALL_SLUG "-install.log"' "$profile"
grep -Fq '"/Temporary/InfiltratorOS/native-worker."' "$ROOT/packaging/start-native-install.c"
python3 "$ROOT/tests/application-install-test.py"
grep -Fq 'native-verified-' "$ROOT/packaging/install-application.py"
grep -Fq 'DPkg::Lock::Timeout=-1' "$ROOT/packaging/install-application.py"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir -p "$WORK/native-checks"
printf '%s\n' 'known-good-native-installer' > "$WORK/native.run"
digest="$(sha256sum "$WORK/native.run" | cut -d' ' -f1)"
printf '%s  %s\n' "$digest" "test-native-installer.run" > "$WORK/native-checks/Calculator.sha256"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/tests/native-download-test.c" -o "$WORK/native-download-test"
"$WORK/native-download-test" "$WORK/native-checks" \
    "https://github.com/Infiltrator-Projects/Calculator/releases/download/v1.2.3/test-native-installer.run" \
    "$WORK/native.run"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -DINSTALL_TEMPORAL_RESOURCES \
    "$ROOT/packaging/start-native-install.c" "$ROOT/packaging/installer-process.c" \
    -o "$WORK/start-temporal-resources-install"
# Test real production slug validation for every filesystem, including OFS.
for profile in INSTALL_TEMPORAL_RESOURCES INSTALL_FILESYSTEM_RESOURCES; do
    cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic "-D$profile" \
        "$ROOT/tests/native-provider-slug-test.c" "$ROOT/packaging/installer-process.c" \
        -o "$WORK/native-provider-slug-test"
    "$WORK/native-provider-slug-test"
done
# Startup errors must write to the current run, not the old shared status.
grep -Fq 'fopen(status_file(), "w")' "$ROOT/packaging/start-native-install.c"
# Distinct per-provider launchers must not be able to use a fixed shared status.
grep -Fq 'provider_slug' "$ROOT/packaging/start-native-install.c"
grep -Fq 'run_status' "$ROOT/packaging/start-native-install.c"
grep -Fq 'provider_slug, NULL' "$ROOT/packaging/start-native-install.c"
grep -Fq 'ui.product_name' "$ROOT/packaging/native-install-progress.c"
grep -Fq 'STATUS_DIR = directory / "status"' "$ROOT/packaging/install-temporal-resources.py"
grep -Fq 'status_root="$stage/status"' "$ROOT/debian/postinst"
! grep -Fq 'DPkg::Lock::Timeout=300' "$ROOT/packaging/install-temporal-resources.py"
! grep -Fq 'DPkg::Lock::Timeout=300' "$ROOT/packaging/install-filesystem-resources.py"
# Count real per-run manifests, including the 18 additional filesystem
# installers in v1.0.51 (81 jobs total), without a fixed 16-job ceiling.
cc -std=c11 -O2 -DNDEBUG -Wall -Wextra -Werror -Wpedantic \
    "$ROOT/tests/native-progress-count-test.c" \
    "$ROOT/packaging/native-progress-count.c" "$ROOT/packaging/installer-status.c" \
    -o "$WORK/native-progress-count-test"
"$WORK/native-progress-count-test"
! grep -Fq 'additional_filesystem_jobs' "$ROOT/packaging/native-install-progress.c"

# A single GTK window summarizes all statuses, individual providers remain
# independently logged but must not spawn 54 graphical windows.
grep -Fq 'refresh_aggregate' "$ROOT/packaging/native-install-progress.c"
grep -Fq 'INFILTRATOROS_PROGRESS_MODE' "$ROOT/packaging/start-native-install.c"
grep -Fq 'env["INFILTRATOROS_PROGRESS_MODE"] = "none"' "$ROOT/packaging/orchestrate-native-install.py"
grep -Fq 'INFILTRATOROS_PROGRESS_MODE=aggregate' "$ROOT/debian/postinst"
grep -Fq 'provision.lock' "$ROOT/debian/postinst"
grep -Fq 'flock -n 9' "$ROOT/debian/postinst"
sh -n "$ROOT/debian/postinst"
! grep -Fq 'rm -rf -- "$STATUS_ROOT"' "$ROOT/debian/postinst"
for contract in process status; do
    cc -std=c11 -O2 -DNDEBUG -Wall -Wextra -Werror -Wpedantic \
        "$ROOT/tests/installer-$contract-test.c" "$ROOT/packaging/installer-$contract.c" \
        -o "$WORK/$contract-test"
    "$WORK/$contract-test"
done

# Native per-user icon integration: never run root-set GVfs metadata.
sh -n "$ROOT/packaging/activate-program-icons.sh"
grep -Fq 'OnlyShowIn=X-Cinnamon;' "$ROOT/packaging/infiltrator-program-icons.desktop"
grep -Fq 'metadata::custom-icon' "$ROOT/packaging/activate-program-icons.sh"
