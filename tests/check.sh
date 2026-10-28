#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
sh -n "$ROOT/packaging/ensure-native-icons.sh"
python3 "$ROOT/tests/calendar-switch-test.py"
python3 "$ROOT/tests/temporal-install-test.py"
sh "$ROOT/tests/native-icons.sh"

# InfiltratorOS-owned install state must live under the native /Temporary.
profile="$ROOT/packaging/installer-profile.h"
grep -Fq '#define STATUS_DIR "/Temporary/InfiltratorOS"' "$profile"
grep -Fq '#define LOG_PATH STATUS_DIR "/" INSTALL_SLUG "-install.log"' "$profile"
grep -Fq '"/Temporary/InfiltratorOS/native-worker."' "$ROOT/packaging/start-native-install.c"
for product in system-monitor calculator files defragmenter; do
    worker="$ROOT/packaging/install-$product.c"
    grep -Fq '#define STATUS_DIR "/Temporary/InfiltratorOS"' "$worker"
    grep -Fq '#define APP_LOCK_PATH STATUS_DIR "/' "$worker"
    grep -Fq '#define GLOBAL_LOCK_PATH STATUS_DIR "/native-install.lock"' "$worker"
    grep -Fq 'static const char *run_status_path(void)' "$worker"
    grep -Fq 'INFILTRATOROS_INSTALL_RUNTIME' "$worker"
    grep -Fq "STATUS_DIR \"/$product-XXXXXX\"" "$worker"
    if grep -Eq '"/(run/infiltratoros|run/lock/infiltratoros|tmp/)' "$worker"; then
        echo "Native $product worker still writes to Linux temporary storage" >&2
        exit 1
    fi
done
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -DINSTALL_TEMPORAL_RESOURCES \
    "$ROOT/packaging/start-native-install.c" "$ROOT/packaging/installer-process.c" \
    -o "$WORK/start-temporal-resources-install"
# Distinct per-provider launchers must not be able to use a fixed shared status.
grep -Fq 'provider_slug' "$ROOT/packaging/start-native-install.c"
grep -Fq 'run_status' "$ROOT/packaging/start-native-install.c"
grep -Fq 'provider_slug, NULL' "$ROOT/packaging/start-native-install.c"
grep -Fq 'ui.product_name' "$ROOT/packaging/native-install-progress.c"
grep -Fq 'STATUS_DIR = directory / "status"' "$ROOT/packaging/install-temporal-resources.py"
grep -Fq 'dependencies-ready' "$ROOT/packaging/install-temporal-resources.py"
grep -Fq 'status_root="$stage/status"' "$ROOT/debian/postinst"
# Every native worker must wait for a live Mint aptk/dpkg frontend rather
# than fail after an arbitrary 300-second timeout.
for worker in "$ROOT"/packaging/install-{system-monitor,calculator,files,defragmenter}.c; do
    grep -Fq 'DPkg::Lock::Timeout=-1' "$worker"
    ! grep -Fq 'DPkg::Lock::Timeout=300' "$worker"
done
grep -Fq 'DPkg::Lock::Timeout=-1' "$ROOT/packaging/install-temporal-resources.py"
! grep -Fq 'DPkg::Lock::Timeout=300' "$ROOT/packaging/install-temporal-resources.py"
# A single GTK window summarizes all statuses, individual providers remain
# independently logged but must not spawn 54 graphical windows.
grep -Fq 'refresh_aggregate' "$ROOT/packaging/native-install-progress.c"
grep -Fq 'INFILTRATOROS_PROGRESS_MODE' "$ROOT/packaging/start-native-install.c"
grep -Fq 'INFILTRATOROS_PROGRESS_MODE=none' "$ROOT/debian/postinst"
grep -Fq 'progress_mode=aggregate' "$ROOT/debian/postinst"
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

calculator_worker="$ROOT/packaging/install-calculator.c"
cc -std=c11 -O2 -DNDEBUG -Wall -Wextra -Werror -Wpedantic \
    "$calculator_worker" -o "$WORK/install-calculator"
grep -Fq '"--system-package-mode"' "$calculator_worker"
grep -Fq '"/Programs/calculator"' "$calculator_worker"
grep -Fq 'net.ssmith.calculator.desktop' "$calculator_worker"
grep -Fq 'usr/share/icons/hicolor/scalable/apps/calculator.svg' "$calculator_worker"
grep -Fq '"/System/Resources/Fonts/mb_corpo_a_title_cond_regular.ttf"' "$calculator_worker"
grep -Fq '"/System/Resources/Fonts/mb_corpo_s_title_bold.ttf"' "$calculator_worker"
grep -Fq '"/System/Resources/Fonts/mb_corpo_s_title_regular.ttf"' "$calculator_worker"
if grep -Fq '"/usr/bin/calculator"' "$calculator_worker"; then
    echo "Calculator worker must not require the obsolete /usr/bin executable." >&2
    exit 1
fi
if grep -Fq '/usr/share/fonts/truetype/calculator/' "$calculator_worker"; then
    echo "Calculator worker must validate the native shared font cache, not retired app-owned font paths." >&2
    exit 1
fi
if grep -Fq 'dpkg-buildpackage' "$calculator_worker" || \
   grep -Fq '_source.tar.gz' "$calculator_worker"; then
    echo "Calculator worker must delegate compilation and packaging to Calculator's native installer." >&2
    exit 1
fi

files_worker="$ROOT/packaging/install-files.c"
cc -std=c11 -O2 -DNDEBUG -Wall -Wextra -Werror -Wpedantic \
    "$files_worker" -o "$WORK/install-files"
grep -Fq 'repos/Infiltrator-Projects/Files/releases/latest' "$files_worker"
grep -Fq '"--system-package-mode"' "$files_worker"
grep -Fq '"/Programs/files"' "$files_worker"
grep -Fq '"/usr/share/applications/files.desktop"' "$files_worker"
grep -Fq '"Exec=/Programs/files"' "$files_worker"
grep -Fq '"CXX flags: -O3"' "$files_worker"
grep -Fq '"PGO: trained two-pass profile"' "$files_worker"
if grep -Eq 'xdg-mime|mimeapps\.list|gio[[:space:]]+mime' "$files_worker"; then
    echo "Files provisioning must not claim the host default file-manager association." >&2
    exit 1
fi
if grep -Fq '"purge"' "$files_worker"; then
    echo "Files provisioning must not purge the existing distribution file manager." >&2
    exit 1
fi

defragmenter_worker="$ROOT/packaging/install-defragmenter.c"
cc -std=c11 -O2 -DNDEBUG -Wall -Wextra -Werror -Wpedantic \
    "$defragmenter_worker" -o "$WORK/install-defragmenter"
grep -Fq 'repos/Infiltrator-Projects/Defragmenter/releases/latest' "$defragmenter_worker"
grep -Fq 'Defragmenter-' "$defragmenter_worker"
grep -Fq 'RELEASE_SHA256SUMS.txt' "$defragmenter_worker"
grep -Fq '"/usr/bin/sha256sum"' "$defragmenter_worker"
grep -Fq '"-O3 -march=native -mtune=native -flto"' "$defragmenter_worker"
grep -Fq '"/Programs/defragmenter"' "$defragmenter_worker"
grep -Fq '"/Programs/defragmenter-desktop", F_OK' "$defragmenter_worker"
grep -Fq '"/Programs/defragmenter-test-media"' "$defragmenter_worker"
grep -Fq '"Exec=/Programs/defragmenter"' "$defragmenter_worker"
grep -Fq '"Exec=/Programs/defragmenter-test-media"' "$defragmenter_worker"
if grep -Fq '"/usr/bin/defragmenter' "$defragmenter_worker"; then
    echo "Defragmenter worker must not require retired /usr/bin application paths." >&2
    exit 1
fi
grep -Fq '"/System/Commands/Defragmenter/defragmenter-operation-engine"' "$defragmenter_worker"
grep -Fq '"/System/Commands/Defragmenter/defragmenter-mapper"' "$defragmenter_worker"
grep -Fq '"/System/Commands/Defragmenter/ext4/ext-metadata-worker"' "$defragmenter_worker"
grep -Fq '"/System/Resources/Documentation/Defragmenter/README.md"' "$defragmenter_worker"
if grep -Fq '"/usr/lib/defragmenter/' "$defragmenter_worker"; then
    echo "Defragmenter may not require obsolete private /usr/lib paths." >&2
    exit 1
fi
grep -Fq '"/usr/share/applications/io.github.defragmenter.desktop"' "$defragmenter_worker"
grep -Fq '+native1' "$defragmenter_worker"
if grep -Fq '"purge"' "$defragmenter_worker"; then
    echo "Defragmenter provisioning must not remove other filesystem utilities." >&2
    exit 1
fi

# Native per-user icon integration: never run root-set GVfs metadata.
sh -n "$ROOT/packaging/activate-program-icons.sh"
grep -Fq 'OnlyShowIn=X-Cinnamon;' "$ROOT/packaging/infiltrator-program-icons.desktop"
grep -Fq 'metadata::custom-icon' "$ROOT/packaging/activate-program-icons.sh"
