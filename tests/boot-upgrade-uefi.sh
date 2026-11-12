#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the shipped helper with actual efibootmgr and efivarfs in a disposable VM.
set -euo pipefail
helper=$(realpath "$1")
work=$(realpath -m "$2")
guest="$work/guest"
mkdir -p "$guest" "$work/kernel"

# Qualify against a real independent BOOT package and a matching Ubuntu kernel.
boot_deb="$work/infiltrator-boot_0.1.42_amd64.deb"
curl -fsSL --retry 2 \
    https://github.com/Infiltrator-Projects/Boot/releases/download/v0.1.42/infiltrator-boot_0.1.42_amd64.deb \
    -o "$boot_deb"
printf '%s  %s\n' e28a3d2699a5f2f4328a86d8bc18bf20b671fd06ddf9b05583d9dd621184885e "$boot_deb" | sha256sum -c -
dpkg-deb -x "$boot_deb" "$guest"
install -m 0755 "$helper" "$guest/install-boot-option"

image=$(apt-cache depends linux-image-generic | awk '/Depends: linux-image-[0-9]/ { image=$2 } END { print image }')
release=${image#linux-image-}
test -n "$release"
(cd "$work/kernel" && apt-get download "linux-image-unsigned-$release" "linux-modules-$release")
for deb in "$work/kernel"/*.deb; do dpkg-deb -x "$deb" "$work/kernel"; done
kernel="$work/kernel/boot/vmlinuz-$release"
test -s "$kernel"
module_root=$(find "$work/kernel" -type d -path "*/lib/modules/$release" -print -quit)
test -n "$module_root"
mkdir -p "$guest/lib/modules" "$guest/proc" "$guest/sys" "$guest/dev" "$guest/boot/efi" "$guest/bin"
cp -a "$module_root" "$guest/lib/modules/"
depmod -b "$guest" "$release"

for program in bash efibootmgr findmnt lsblk sha256sum cmp od id mktemp mv rm sync cat tail \
               dirname find cp mkdir grep rmdir sed modprobe mount env; do
    path=$(command -v "$program")
    cp -L --parents "$path" "$guest"
    while IFS= read -r library; do
        cp -L --parents "$library" "$guest"
    done < <(ldd "$path" | awk '$2 == "=>" && $3 ~ /^\// {print $3} $1 ~ /^\// {print $1}')
done
cp "$(command -v busybox)" "$guest/bin/busybox"
ln -s busybox "$guest/bin/sh"

cat > "$guest/init" <<'GUEST'
#!/bin/sh
exec /usr/bin/bash /qualify
GUEST
cat > "$guest/qualify" <<'GUEST'
#!/usr/bin/bash
set -euo pipefail
export PATH=/usr/sbin:/usr/bin:/sbin:/bin LC_ALL=C
trap 'result=$?; if (( result != 0 )); then echo "BOOT UEFI qualification failed ($result)"; fi; /bin/busybox poweroff -f' EXIT
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
modprobe efivarfs
mount -t efivarfs efivarfs /sys/firmware/efi/efivars
modprobe virtio_pci
modprobe virtio_blk
modprobe vfat
modprobe nls_cp437
modprobe nls_iso8859-1
for _ in {1..50}; do
    [[ -b /dev/vda1 ]] && break
    /bin/busybox sleep 0.1
done
esp=/boot/efi
mount -t vfat /dev/vda1 "$esp"
mkdir -p "$esp/EFI/ubuntu" "$esp/EFI/Other"
printf 'GRUB unchanged\n' > "$esp/EFI/ubuntu/shimx64.efi"
printf 'Other loader unchanged\n' > "$esp/EFI/Other/BOOTX64.EFI"
efibootmgr --create-only --disk /dev/vda --part 1 --label 'Fixture GRUB' --loader '\EFI\ubuntu\shimx64.efi'
efibootmgr --create-only --disk /dev/vda --part 1 --label 'Fixture Other' --loader '\EFI\Other\BOOTX64.EFI'
before=$(efibootmgr -v)
grub=$(sed -nE 's/^Boot([0-9A-F]{4})\*[[:space:]]+Fixture GRUB.*/\1/p' <<< "$before")
other=$(sed -nE 's/^Boot([0-9A-F]{4})\*[[:space:]]+Fixture Other.*/\1/p' <<< "$before")
[[ $grub =~ ^[0-9A-F]{4}$ && $other =~ ^[0-9A-F]{4}$ ]]
efibootmgr --bootorder "$grub,$other"
efibootmgr --bootnext "$other"

/install-boot-option
registered=$(efibootmgr -v)
printf '%s\n' "$registered"
option=$(sed -nE 's/^Boot([0-9A-F]{4})\*[[:space:]]+Infiltrator BOOT.*/\1/p' <<< "$registered")
[[ $option =~ ^[0-9A-F]{4}$ ]]
grep -Fxq "BootOrder: $grub,$other,$option" <<< "$registered"
grep -Fxq "BootNext: $other" <<< "$registered"
installed="$esp/EFI/InfiltratorBoot"
payload=/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI
cmp "$payload" "$installed/BOOTX64.EFI"
/install-boot-option
[[ $(efibootmgr -v) == "$registered" ]]

# Exercise a later upgrade with a previous managed loader and user resources.
printf 'old BOOT loader\n' > "$installed/BOOTX64.EFI"
old=$(sha256sum < "$installed/BOOTX64.EFI")
printf '%s\n' "${old%% *}" > "$installed/.infiltratoros-loader.sha256"
printf 'user boot configuration\n' > "$installed/boot.conf"
printf 'independent driver\n' > "$installed/drivers_x64/user.efi"
/install-boot-option
cmp "$payload" "$installed/BOOTX64.EFI"
grep -Fxq 'user boot configuration' "$installed/boot.conf"
grep -Fxq 'independent driver' "$installed/drivers_x64/user.efi"
grep -Fxq 'GRUB unchanged' "$esp/EFI/ubuntu/shimx64.efi"
grep -Fxq 'Other loader unchanged' "$esp/EFI/Other/BOOTX64.EFI"
[[ $(efibootmgr -v) == "$registered" ]]
sync
echo BOOT-AUTOMATIC-UPGRADE-UEFI-PASS
GUEST
chmod 0755 "$guest/init" "$guest/qualify"
(cd "$guest" && find . -print0 | cpio --null -o -H newc --owner=0:0 | gzip -1) > "$work/initrd.gz"

truncate -s 80M "$work/disk.img"
sgdisk --new=1:2048:+64M --typecode=1:EF00 "$work/disk.img"
truncate -s 64M "$work/esp.img"
mkfs.fat -F 32 "$work/esp.img"
dd if="$work/esp.img" of="$work/disk.img" bs=1M seek=1 conv=notrunc status=none
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$work/vars.fd"
timeout 150 qemu-system-x86_64 -machine q35,accel=tcg,smm=on -cpu max -m 1024 \
    -display none -monitor none -serial "file:$work/serial.log" -no-reboot \
    -global driver=cfi.pflash01,property=secure,value=on \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.secboot.fd \
    -drive "if=pflash,format=raw,file=$work/vars.fd" \
    -drive "if=none,format=raw,id=disk,file=$work/disk.img" -device virtio-blk-pci,drive=disk \
    -kernel "$kernel" -initrd "$work/initrd.gz" -append 'console=ttyS0 rdinit=/init panic=1' || :
tail -60 "$work/serial.log"
grep -q '^BOOT-AUTOMATIC-UPGRADE-UEFI-PASS' "$work/serial.log"
