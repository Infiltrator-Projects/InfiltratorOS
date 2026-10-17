#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
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
grep -Fq '"/System/Resources/Fonts/mb_corpo_a_cond_regular.ttf"' "$calculator_worker"
grep -Fq '"/System/Resources/Fonts/mb_corpo_s_bold.ttf"' "$calculator_worker"
grep -Fq '"/System/Resources/Fonts/mb_corpo_s_regular.ttf"' "$calculator_worker"
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
