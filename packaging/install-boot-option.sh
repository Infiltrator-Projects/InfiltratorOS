#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Add the independently packaged BOOT loader during InfiltratorOS upgrades.
set -euo pipefail
export LC_ALL=C
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

loader='\EFI\InfiltratorBoot\BOOTX64.EFI'
boot_label='Infiltrator BOOT'

note() { printf 'Infiltrator BOOT: %s\n' "$*"; }
skip() { note "$*"; exit 77; }
firmware() { efibootmgr "$@"; }
deploy() {
    local mode=$1
    shift
    /usr/sbin/infiltrator-boot-deploy "$mode" --confirm "$@"
}

boot_order() {
    local line order= count=0
    while IFS= read -r line; do
        if [[ $line =~ ^BootOrder:[[:space:]]*([[:xdigit:]]{4}(,[[:xdigit:]]{4})*)[[:space:]]*$ ]]; then
            order=${BASH_REMATCH[1]^^}
            count=$((count + 1))
        fi
    done <<< "$1"
    [[ $count == 1 ]] || return 1
    printf '%s\n' "$order"
}

next_boot() {
    local line
    while IFS= read -r line; do
        if [[ $line == BootNext:* ]]; then printf '%s\n' "$line"; fi
    done <<< "$1"
}

# A registered option is meaningful only for the same mounted ESP.
find_boot_option() {
    local snapshot=$1 part=$2 uuid=$3 line result=
    local locator="hd($part,gpt,${uuid,,},"
    local target="file(${loader,,})"
    while IFS= read -r line; do
        [[ $line =~ ^Boot([[:xdigit:]]{4})(\*)?[[:space:]]+ ]] || continue
        if [[ ${line,,} == *"$locator"*"$target"* ]]; then
            [[ -z $result && ${BASH_REMATCH[2]} == '*' ]] || return 1
            result=${BASH_REMATCH[1]^^}
        elif [[ ${line,,} == *"$target"* || ${line,,} == *"${boot_label,,}"* ]]; then
            # An alternate ESP or an older manual entry needs review.
            return 1
        fi
    done <<< "$snapshot"
    printf '%s' "$result"
}

default_is_grub() {
    local snapshot=$1 order=$2 part=$3 uuid=$4 esp=$5 line
    local locator="hd($part,gpt,${uuid,,},"
    local grub_path='file\(\\efi\\([a-z0-9][a-z0-9_-]*)\\(shimx64|grubx64)\.efi\)'
    while IFS= read -r line; do
        if [[ ${line^^} == "BOOT${order%%,*}*"[[:space:]]* && ${line,,} == *"$locator"* &&
              ${line,,} =~ $grub_path ]]; then
            [[ -f "$esp/EFI/${BASH_REMATCH[1]}/${BASH_REMATCH[2]}.efi" ]]
            return
        fi
    done <<< "$snapshot"
    return 1
}

deploy_boot() {
    local payload=$1 directory=$2 registered_option=${3:-}
    if (( $# >= 3 )); then shift 3; else shift 2; fi
    local executable="$directory/BOOTX64.EFI" marker="$directory/.infiltratoros-loader.sha256"
    local installed packaged staged_marker module refresh_modules=0
    packaged=$(sha256sum < "$payload") || return 1
    packaged=${packaged%% *}

    if [[ ! -e $directory && ! -L $directory ]]; then
        deploy --install-new "$@" || return 1
    else
        if [[ -L $directory || ! -f $executable || -L $executable || -L $marker ]]; then
            skip 'An existing BOOT deployment needs review.'
        fi
        installed=$(sha256sum < "$executable") || return 1
        installed=${installed%% *}
        for module; do
            if ! cmp -s -- "$module" "$directory/filesystems_x64/${module##*/}"; then
                refresh_modules=1
            fi
        done
        if [[ $installed != "$packaged" || $refresh_modules == 1 ]]; then
            # Existing BOOT installations may predate the upgrader's marker.
            # An active firmware entry for this exact loader on this ESP is
            # also evidence that this is the BOOT installation to upgrade.
            if [[ ( ! -f $marker || $(< "$marker") != "$installed" ) &&
                  ! $registered_option =~ ^[[:xdigit:]]{4}$ ]]; then
                skip 'The existing BOOT installation has no matching firmware entry.'
            fi
            deploy --update-existing "$@" || return 1
        elif [[ -f $marker && $(< "$marker") == "$packaged" ]]; then
            return 0
        fi
    fi

    cmp -s -- "$payload" "$executable" || return 1
    for module; do
        cmp -s -- "$module" "$directory/filesystems_x64/${module##*/}" || return 1
    done
    staged_marker=$(mktemp "$directory/.infiltratoros-loader.XXXXXXXX") || return 1
    if ! printf '%s\n' "$packaged" > "$staged_marker" || ! mv -fT -- "$staged_marker" "$marker"; then
        rm -f -- "$staged_marker"
        return 1
    fi
    sync -f "$executable"
}

register_option() {
    local disk=$1 part=$2 uuid=$3 baseline=$4 order=$5
    local snapshot option bootnext after next_order
    snapshot=$(firmware -v) || return 1
    [[ $(boot_order "$snapshot") == "$order" && $(next_boot "$snapshot") == "$(next_boot "$baseline")" ]] ||
        { note 'Firmware settings changed while preparing the option.' >&2; return 1; }
    option=$(find_boot_option "$snapshot" "$part" "$uuid") ||
        { note 'An existing BOOT entry needs review.' >&2; return 1; }
    bootnext=$(next_boot "$snapshot")
    if [[ -n $option && ,$order, == *",$option,"* ]]; then
        note "Already available as Boot$option."
        return 0
    fi
    if [[ -z $option ]]; then
        # Unlike --create, --create-only leaves the firmware order unchanged.
        firmware --create-only --disk "$disk" --part "$part" --label "$boot_label" --loader "$loader" ||
            return 1
        snapshot=$(firmware -v) || return 1
        option=$(find_boot_option "$snapshot" "$part" "$uuid") || return 1
        [[ -n $option && $(boot_order "$snapshot") == "$order" && $(next_boot "$snapshot") == "$bootnext" ]] ||
            { note 'Firmware did not confirm the new option with the original boot order.' >&2; return 1; }
    fi
    # Keep the original default and the full relative order of every entry.
    firmware --bootorder "$order,$option" || return 1
    after=$(firmware -v) || return 1
    next_order=$(boot_order "$after") || return 1
    [[ $next_order == "$order,$option" && $(next_boot "$after") == "$bootnext" &&
       $(find_boot_option "$after" "$part" "$uuid") == "$option" ]] ||
        { note 'Could not verify the firmware order after registering BOOT.' >&2; return 1; }
    note "Added Boot$option at the end of the firmware list."
}

main() {
    local module
    for module; do
        [[ $module =~ ^/System/Filesystems/[A-Za-z0-9_-]+\.so$ ]] || {
            note "Invalid filesystem module: $module" >&2; return 2;
        }
    done
    [[ $(id -u) == 0 ]] || { note 'Run as root.' >&2; return 1; }
    local esp=/boot/efi
    local vars=/sys/firmware/efi/efivars
    local payload=/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI
    local secure source info parent partition part type= uuid= extra snapshot order option key value
    local var_options esp_options
    var_options=$(findmnt -rnM "$vars" -t efivarfs -o OPTIONS) || var_options=
    if [[ ,$var_options, != *,rw,* || $(cat /sys/firmware/efi/fw_platform_size 2>/dev/null || true) != 64 ]]; then
        skip 'Writable 64-bit UEFI variables are unavailable.'
    fi
    secure=$(od -An -tu1 -j4 -N1 "$vars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c" 2>/dev/null) || secure=
    if [[ ! $secure =~ ^[[:space:]]*0[[:space:]]*$ ]]; then
        skip 'Secure Boot is enabled or could not be checked.'
    fi
    if [[ ! -f $payload || ! -x /usr/sbin/infiltrator-boot-deploy ]]; then
        note 'The independent BOOT package is unavailable.' >&2
        return 1
    fi
    esp_options=$(findmnt -rnM "$esp" -t vfat -o OPTIONS) || esp_options=
    if [[ -L $esp || -L $esp/EFI || ,$esp_options, != *,rw,* ]]; then
        skip 'No writable FAT EFI System Partition mounted at /boot/efi.'
    fi
    source=$(findmnt -rnM "$esp" -o SOURCE) || return 1
    [[ -b $source ]] || skip 'The ESP is not a direct block partition.'
    info=$(lsblk -dnro PKNAME,KNAME -- "$source") || return 1
    read -r parent partition extra <<< "$info"
    [[ $partition =~ ^[a-zA-Z0-9_.-]+$ ]] || return 1
    part=$(cat "/sys/class/block/$partition/partition" 2>/dev/null) ||
        skip 'The ESP is not a direct block partition.'
    # Probe the GPT entry directly, including before udev has cached metadata.
    info=$(blkid -p -s PART_ENTRY_TYPE -s PART_ENTRY_UUID -o export -- "$source") ||
        skip 'The ESP partition metadata could not be read.'
    while IFS='=' read -r key value; do
        case "$key" in
            PART_ENTRY_TYPE) type=$value ;;
            PART_ENTRY_UUID) uuid=$value ;;
        esac
    done <<< "$info"
    if [[ ! $parent =~ ^[a-zA-Z0-9_.-]+$ || ! $part =~ ^[1-9][0-9]*$ ||
          ${type,,} != c12a7328-f81f-11d2-ba4b-00a0c93ec93b ||
          ! $uuid =~ ^[[:xdigit:]]{8}(-[[:xdigit:]]{4}){3}-[[:xdigit:]]{12}$ || -n $extra ||
          ! -b /dev/$parent ]]; then
        skip 'The ESP did not match the supported GPT layout.'
    fi
    snapshot=$(firmware -v) || return 1
    order=$(boot_order "$snapshot") || skip 'A usable firmware boot order was unavailable.'
    option=$(find_boot_option "$snapshot" "$part" "$uuid") ||
        skip 'An existing BOOT firmware entry needs review.'
    # Existing registered BOOT entries can be upgraded in place regardless of
    # their position in the boot order. New entries retain the GRUB precheck.
    if [[ -z $option ]] && ! default_is_grub "$snapshot" "$order" "$part" "$uuid" "$esp"; then
        skip 'GRUB is not the default entry on this ESP.'
    fi
    deploy_boot "$payload" "$esp/EFI/InfiltratorBoot" "$option" "$@" || return 1
    [[ $(findmnt -rnM "$esp" -o SOURCE) == "$source" ]] || return 1
    register_option "/dev/$parent" "$part" "$uuid" "$snapshot" "$order"
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then main "$@"; fi
