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
            [[ $2 == 0007,0005,0003,0001 ]] || return 1
            printf '%s\n' promote >> "$operations"
            sed -i "s/^BootOrder:.*/BootOrder: $2/" "$firmware_file"
            ;;
        --delete-bootnext)
            printf '%s\n' clear-next >> "$operations"
            sed -i '/^BootNext:/d' "$firmware_file"
            ;;
        --bootnum)
            [[ $2 == 0006 && $3 == --delete-bootnum ]] || return 1
            printf '%s\n' delete >> "$operations"
            sed -i '/^Boot0006\*/d; /^BootOrder:/s/,0006//' "$firmware_file"
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
    shift
    mkdir -p "$directory/filesystems_x64"
    for module; do cp "$module" "$directory/filesystems_x64/"; done
}

printf 'first verified BOOT loader\n' > "$payload"
default_is_grub "$(cat "$firmware_file")" 0005,0003,0001 1 "$uuid" "$esp"
deploy_boot "$payload" "$directory"
register_option /dev/vda 1 "$uuid" "$(cat "$original")" 0005,0003,0001
cmp "$operations" <(printf '%s\n' --install-new create promote)
grep -Fxq 'BootOrder: 0007,0005,0003,0001' "$firmware_file"
grep -Fxq 'BootNext: 0003' "$firmware_file"

# A current upgrade is idempotent. A later package refreshes only the owned
# loader and its assets, and reuses the existing firmware entry.
deploy_boot "$payload" "$directory"
register_option /dev/vda 1 "$uuid" "$(cat "$firmware_file")" 0007,0005,0003,0001
printf 'new verified BOOT loader\n' > "$payload"
deploy_boot "$payload" "$directory"
register_option /dev/vda 1 "$uuid" "$(cat "$firmware_file")" 0007,0005,0003,0001
cmp "$operations" <(printf '%s\n' --install-new create promote --update-existing)
cmp "$payload" "$directory/BOOTX64.EFI"

# A previously installed BOOT loader can predate the upgrader's marker.
# Its active entry on the same ESP authorises updating the existing copy.
printf 'older registered BOOT loader\n' > "$directory/BOOTX64.EFI"
rm "$directory/.infiltratoros-loader.sha256"
sed -i 's/^Boot0007\* Infiltrator BOOT /Boot0007* Infiltrator Boot (Test) /' "$firmware_file"
option=$(find_boot_option "$(cat "$firmware_file")" 1 "$uuid")
[[ $option == 0007 ]]
deploy_boot "$payload" "$directory" "$option"
register_option /dev/vda 1 "$uuid" "$(cat "$firmware_file")" 0007,0005,0003,0001
cmp "$payload" "$directory/BOOTX64.EFI"
cmp "$operations" <(printf '%s\n' --install-new create promote --update-existing --update-existing)

printf 'user changed this loader\n' > "$directory/BOOTX64.EFI"
result=0
(deploy_boot "$payload" "$directory") || result=$?
[[ $result == 77 ]]
grep -Fxq 'user changed this loader' "$directory/BOOTX64.EFI"
if register_option /dev/vda 1 "$uuid" "$(cat "$original")" 0005,0003,0001; then
    echo 'Accepted a stale BootOrder snapshot.' >&2
    exit 1
fi
cmp "$operations" <(printf '%s\n' --install-new create promote --update-existing --update-existing)
grep -Fxq 'GRUB loader' "$esp/EFI/ubuntu/shimx64.efi"

# Provider upgrades must also refresh an ESP copy when the loader itself is current.
printf 'verified loader\n' > "$payload"
deploy_boot "$payload" "$directory" "$option"
module="$work/FAT16.so"
printf 'verified module\n' > "$module"
deploy_boot "$payload" "$directory" "$option" "$module"
cmp "$module" "$directory/filesystems_x64/FAT16.so"
count=$(wc -l < "$operations")
deploy_boot "$payload" "$directory" "$option" "$module"
[[ $(wc -l < "$operations") == "$count" ]]
printf 'upgraded module\n' > "$module"
deploy_boot "$payload" "$directory" "$option" "$module"
cmp "$module" "$directory/filesystems_x64/FAT16.so"
[[ $(wc -l < "$operations") == $((count + 1)) ]]

trial="$esp/EFI/InfiltratorBootTrial"
stage_obsolete() {
    mkdir -p "$trial"
    cat > "$trial/boot.conf" <<'CONF'
scanfor manual
menuentry "InfiltratorOS Kernel 0.1.5" {
    filesystem_module ext4
}
CONF
}
stage_obsolete
printf 'Boot0006* InfiltratorOS 0.1.5 Trial HD(1,GPT,%s,0x800,0x20000)/File(%s)\n' \
    "$uuid" "$obsolete_loader" >> "$firmware_file"
sed -i '/^BootOrder:/s/$/,0006/' "$firmware_file"
cp "$firmware_file" "$work/before-cleanup"
sed -i 's/^BootCurrent:.*/BootCurrent: 0006/' "$firmware_file"
if remove_obsolete_boot "$esp" 1 "$uuid"; then
    echo 'Cleanup accepted the active temporary entry.' >&2; exit 1
fi
[[ -d $trial ]]
grep -q '^Boot0006\*' "$firmware_file"

cp "$work/before-cleanup" "$firmware_file"
remove_obsolete_boot "$esp" 1 "$uuid"
[[ ! -e $trial ]]
grep -Fxq 'BootOrder: 0007,0005,0003,0001' "$firmware_file"
grep -Fxq 'BootNext: 0003' "$firmware_file"
! grep -q '^Boot0006\*' "$firmware_file"
count=$(wc -l < "$operations")
remove_obsolete_boot "$esp" 1 "$uuid"
[[ $(wc -l < "$operations") == "$count" ]]

stage_obsolete
cp "$work/before-cleanup" "$firmware_file"
sed -i 's/^BootNext:.*/BootNext: 0006/' "$firmware_file"
remove_obsolete_boot "$esp" 1 "$uuid"
[[ ! -e $trial ]]
! grep -Eq '^BootNext:|^Boot0006\*' "$firmware_file"
cmp <(tail -2 "$operations") <(printf '%s\n' clear-next delete)

stage_obsolete
cp "$work/before-cleanup" "$firmware_file"
sed -i '/^Boot0006\*/s/HD(1,/HD(2,/' "$firmware_file"
cp "$firmware_file" "$work/other-esp"
remove_obsolete_boot "$esp" 1 "$uuid"
cmp "$firmware_file" "$work/other-esp"
[[ ! -e $trial ]]

stage_obsolete
cp "$work/before-cleanup" "$firmware_file"
sed -i '/^Boot0006\*/s/InfiltratorOS 0.1.5 Trial/Custom Loader/' "$firmware_file"
if remove_obsolete_boot "$esp" 1 "$uuid"; then
    echo 'Removed an unrecognised entry.' >&2; exit 1
fi
[[ -d $trial ]]
echo 'BOOT deployment and firmware order contracts passed.'
