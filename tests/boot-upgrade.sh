#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Exercise the packaged helper without accessing the host's firmware or ESP.
set -euo pipefail
source "${1:-packaging/install-boot-option.sh}"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
uuid=976a7c0d-09f7-4ecc-93d0-07f324be0cf0
original="$work/original"
firmware_file="$work/firmware_file"
operations="$work/operations"
esp="$work/esp"
directory="$esp/EFI/InfiltratorBoot"
payload="$work/BOOTX64.EFI"
mkdir -p "$esp/EFI/ubuntu"
printf 'GRUB loader\n' > "$esp/EFI/ubuntu/shimx64.efi"
cat > "$original" <<EOF
BootCurrent: 0005
BootNext: 0003
BootOrder: 0005,0003,0001
Boot0001* UEFI shell BBS(0x5,0x0,0x0)
Boot0003* UEFI network BBS(0x6,0x0,0x0)
Boot0005* ubuntu HD(1,GPT,$uuid,0x800,0x20000)/File(\EFI\ubuntu\shimx64.efi)
EOF
cp "$original" "$firmware_file"
touch "$operations"

firmware() {
    case "$1" in
        -v) cat "$firmware_file" ;;
        --create-only)
            [[ $# == 9 && $2 == --disk && $3 == /dev/vda && $4 == --part && $5 == 1 &&
                $6 == --label && $7 == "$boot_label" && $8 == --loader && $9 == "$loader" ]] || return 1
            printf '%s\n' create >> "$operations"
            printf 'Boot0007* %s HD(1,GPT,%s,0x800,0x20000)/File(%s)\n' \
                "$boot_label" "$uuid" "$loader" >> "$firmware_file"
            ;;
        --bootorder)
            [[ $2 == 0005,0003,0001,0007 ]] || return 1
            printf '%s\n' append >> "$operations"
            sed -i "s/^BootOrder:.*/BootOrder: $2/" "$firmware_file"
            ;;
        *) return 1 ;;
    esac
}

deploy() {
    case "$1" in
        --install-new) mkdir "$directory" ;;
        --update-existing) test -d "$directory" ;;
        *) return 1 ;;
    esac
    printf '%s\n' "$1" >> "$operations"
    cp "$payload" "$directory/BOOTX64.EFI"
}

printf 'first verified BOOT loader\n' > "$payload"
default_is_grub "$(cat "$firmware_file")" 0005,0003,0001 1 "$uuid" "$esp"
deploy_boot "$payload" "$directory"
register_option /dev/vda 1 "$uuid" "$(cat "$original")" 0005,0003,0001
cmp "$operations" <(printf '%s\n' --install-new create append)
grep -Fxq 'BootOrder: 0005,0003,0001,0007' "$firmware_file"
grep -Fxq 'BootNext: 0003' "$firmware_file"

# A current upgrade is idempotent. A later package refreshes only the owned
# loader and its assets, and reuses the existing firmware entry.
deploy_boot "$payload" "$directory"
register_option /dev/vda 1 "$uuid" "$(cat "$firmware_file")" 0005,0003,0001,0007
printf 'new verified BOOT loader\n' > "$payload"
deploy_boot "$payload" "$directory"
register_option /dev/vda 1 "$uuid" "$(cat "$firmware_file")" 0005,0003,0001,0007
cmp "$operations" <(printf '%s\n' --install-new create append --update-existing)
cmp "$payload" "$directory/BOOTX64.EFI"

printf 'user changed this loader\n' > "$directory/BOOTX64.EFI"
result=0
(deploy_boot "$payload" "$directory") || result=$?
[[ $result == 77 ]]
grep -Fxq 'user changed this loader' "$directory/BOOTX64.EFI"
if register_option /dev/vda 1 "$uuid" "$(cat "$original")" 0005,0003,0001; then
    echo 'Accepted a stale BootOrder snapshot.' >&2
    exit 1
fi
cmp "$operations" <(printf '%s\n' --install-new create append --update-existing)
grep -Fxq 'GRUB loader' "$esp/EFI/ubuntu/shimx64.efi"
echo 'BOOT deployment and firmware order contracts passed.'
