#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the shipped helper with actual efibootmgr and efivarfs in a disposable VM.
set -euo pipefail
helper=$(realpath "$1")
work=$(realpath -m "$2")
guest="$work/guest"
mkdir -p "$guest" "$work/kernel"

# Compile BOOT locally from its independently released source installer.
version=0.1.49
run="boot-${version}-native-installer.run"
url="https://github.com/Infiltrator-Projects/Boot/releases/download/v$version"
curl -fsSL --retry 2 "$url/$run" -o "$work/$run"
printf '%s  %s\n' 2d090b5be1cea3053b67c7c128010c992eb1d8f6f00ddecdc4569ef974b58173 "$work/$run" | sha256sum -c -
chmod 0755 "$work/$run"
mkdir "$work/native"
"$work/$run" --build-only --output-dir "$work/native"
boot_deb="$work/native/infiltrator-boot_${version}+native1_amd64.deb"
dpkg-deb -x "$boot_deb" "$guest"
install -m 0755 "$helper" "$guest/install-boot-option"

# Use the exact independent provider package qualified for Common V2. Only
# its shared object goes into the guest's system module directory.
fat16=infiltrator-filesystem-fat16_0.1.18_amd64.deb
curl -fsSL --retry 2 "https://github.com/Infiltrator-Projects/Filesystem-FAT16/releases/download/v0.1.18/$fat16" -o "$work/$fat16"
printf '%s  %s\n' 905e94f3fc563c6d07cdf90601a38237c0637f213e790c64c9b54bd85ec9df6b "$work/$fat16" | sha256sum -c -
dpkg-deb -x "$work/$fat16" "$work/provider"
module=/System/Filesystems/FAT16.so
install -D -m 0644 "$work/provider$module" "$guest$module"
"$guest/usr/lib/infiltrator-boot/validate-module" "$guest$module"

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

for program in bash efibootmgr findmnt lsblk blkid sha256sum cmp od id mktemp mv rm sync cat tail \
               dirname find cp mkdir grep rmdir sed modprobe mount env stat \
               "$guest/usr/lib/infiltrator-boot/validate-module"; do
    path=$(command -v "$program")
    if [[ $path != "$guest/"* ]]; then cp -L --parents "$path" "$guest"; fi
    while IFS= read -r library; do
        cp -L --parents "$library" "$guest"
    done < <(ldd "$path" | awk '$2 == "=>" && $3 ~ /^\// {print $3} $1 ~ /^\// {print $1}')
done
cp "$(command -v busybox)" "$guest/bin/busybox"
ln -s busybox "$guest/bin/sh"
# The BOOT deployment wrapper invokes /bin/bash on the target system.
if [[ ! -e "$guest/bin/bash" ]]; then
    ln -s /usr/bin/bash "$guest/bin/bash"
fi

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
/bin/busybox ln -s /proc/self/fd /dev/fd
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
mount -t vfat -o iocharset=iso8859-1 /dev/vda1 "$esp"
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
obsolete="$esp/EFI/InfiltratorBootTrial"
mkdir "$obsolete"
cat > "$obsolete/boot.conf" <<'CONF'
scanfor manual
menuentry "InfiltratorOS Kernel 0.1.5" {
    filesystem_module ext4
}
CONF
efibootmgr --create-only --disk /dev/vda --part 1 --label 'InfiltratorOS 0.1.5 Trial' \
    --loader '\EFI\InfiltratorBootTrial\BOOTX64.EFI'

module=/System/Filesystems/FAT16.so
/install-boot-option "$module"
registered=$(efibootmgr -v)
printf '%s\n' "$registered"
option=$(sed -nE 's/^Boot([0-9A-F]{4})\*[[:space:]]+Infiltrator BOOT.*/\1/p' <<< "$registered")
[[ $option =~ ^[0-9A-F]{4}$ ]]
grep -Fxq "BootOrder: $option,$grub,$other" <<< "$registered"
grep -Fxq "BootNext: $other" <<< "$registered"
! grep -Fq 'InfiltratorOS 0.1.5 Trial' <<< "$registered"
[[ ! -e $obsolete ]]
installed="$esp/EFI/InfiltratorBoot"
payload=/usr/lib/infiltrator-boot/EFI/BOOT/BOOTX64.EFI
cmp "$payload" "$installed/BOOTX64.EFI"
cmp "$module" "$installed/filesystems_x64/FAT16.so"
/install-boot-option "$module"
[[ $(efibootmgr -v) == "$registered" ]]
rm "$installed/filesystems_x64/FAT16.so"
/install-boot-option "$module"
cmp "$module" "$installed/filesystems_x64/FAT16.so"
[[ $(efibootmgr -v) == "$registered" ]]

# Exercise an existing firmware-registered BOOT loader that predates the
# upgrader's ownership marker, retaining user configuration and artwork.
# The existing BOOT entry may itself be the selected default.
efibootmgr --bootorder "$option,$grub,$other"
registered=$(efibootmgr -v)
grep -Fxq "BootOrder: $option,$grub,$other" <<< "$registered"
grep -Fxq "BootNext: $other" <<< "$registered"
printf 'old BOOT loader\n' > "$installed/BOOTX64.EFI"
rm "$installed/.infiltratoros-loader.sha256"
printf 'user boot configuration\n' > "$installed/boot.conf"
printf 'custom artwork\n' > "$installed/icons/custom.png"
/install-boot-option "$module"
cmp "$payload" "$installed/BOOTX64.EFI"
grep -Fxq 'user boot configuration' "$installed/boot.conf"
grep -Fxq 'custom artwork' "$installed/icons/custom.png"
grep -Fxq 'GRUB unchanged' "$esp/EFI/ubuntu/shimx64.efi"
grep -Fxq 'Other loader unchanged' "$esp/EFI/Other/BOOTX64.EFI"
[[ $(efibootmgr -v) == "$registered" ]]

# The BOOT deployer can retain an existing working EFI filesystem path.
# The upgrader must propagate the skip without rewriting its marker or NVRAM.
mkdir -p "$installed/drivers_x64"
printf 'independent EFI reader\n' > "$installed/drivers_x64/user.efi"
printf 'BOOT using its existing EFI reader\n' > "$installed/BOOTX64.EFI"
retained=$(sha256sum < "$installed/BOOTX64.EFI")
printf '%s\n' "${retained%% *}" > "$installed/.infiltratoros-loader.sha256"
result=0
/install-boot-option "$module" || result=$?
[[ $result == 77 ]]
[[ $(sha256sum < "$installed/BOOTX64.EFI") == "$retained" ]]
[[ $(cat "$installed/.infiltratoros-loader.sha256") == "${retained%% *}" ]]
grep -Fxq 'independent EFI reader' "$installed/drivers_x64/user.efi"
[[ $(efibootmgr -v) == "$registered" ]]
mv "$installed/drivers_x64/user.efi" "$esp/EFI/Other/"

# Later releases must also refresh a BOOT installation with a valid marker.
printf 'previous managed BOOT loader\n' > "$installed/BOOTX64.EFI"
old=$(sha256sum < "$installed/BOOTX64.EFI")
printf '%s\n' "${old%% *}" > "$installed/.infiltratoros-loader.sha256"
/install-boot-option "$module"
cmp "$payload" "$installed/BOOTX64.EFI"
grep -Fxq 'user boot configuration' "$installed/boot.conf"
grep -Fxq 'custom artwork' "$installed/icons/custom.png"
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
