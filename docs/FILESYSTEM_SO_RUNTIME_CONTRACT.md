# InfiltratorOS filesystem execution model — one `.so`, all consumers

**Target architecture (not current Linux kernel functionality):** [the single-filesystem-module contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/UNIFIED_FILESYSTEM_MODULE_ARCHITECTURE.md) is authoritative. Each format's independently distributed driver is exactly one trusted `.so` under `/System/Filesystems/`. BOOT, the operating-system kernel, Filesystems and applications consume the same canonical format engine. User-visible filesystem-specific `.ko` and `.efi` installers are **transitional**, not the intended final operating-system layout.

## Generic kernel integration is owned by the OS

An InfiltratorOS-controlled kernel can implement a generic loader, VFS/filesystem bridge or verified execution profile compatible with the canonical module ABI. The exact mechanism is not yet chosen and must be proven. It may support a freestanding binary representation shared by BOOT and userspace, or use a generic platform-independent translation/bridge; a kernel cannot safely call untrusted, ordinary Linux userspace `.so` code at kernel privilege merely by changing the extension.

Until the new kernel bridge exists, stock Linux requires a qualified mounted-filesystem provider (built-in driver, version-matched `.ko`, or suitable user-space service). Preserve functioning drivers, module signing and supported initramfs paths. Do not claim that installing an `.so` already makes it mountable as a root filesystem.

## Boot/root preparation and ownership

- Setup/installer selects one real filesystem identity, verifies format/root capabilities and installs that format's one `.so` without installing unrelated formats.
- BOOT may require the **same signed module bytes** to be staged on firmware-readable media to discover a volume otherwise inaccessible to firmware. A staging copy is not a second engine. Boot's EFI executable and adapters are platform machinery, not per-format drivers.
- If the chosen filesystem holds the root, early kernel/initramfs needs its generic loader and the verified exact module before mounting that root. Boot-loading a kernel from a filesystem does not prove Linux or InfiltratorOS can mount it as root.
- Applications including Filesystems, Disks, Files, Defragmenter and Test Media load the same module and use its versioned capabilities; per-format format/mutation engines must not be duplicated.
- Common/Libraries owns reusable contract, ABI and validation mechanics when integrated. Format semantics and released source are owned by each independent filesystem repository.
- A package may own documentation, trust metadata and generic OS tooling, but the final **per-format driver artifact** is the `.so`, not three independently distributed driver binaries.

## Required release evidence

Qualify module format/identity and complete read/write/format/recovery/defrag on independent media; BOOT loading from this same module under real OVMF/UEFI testing; a generic kernel bridge actually mounting and writing the format (and root bootstrap when advertised); staged trust/rollback and supported host ABIs. Publish capability-specific status instead of claiming a driver complete because its source has `.ko`/`.efi` or its `.so` was successfully installed. Until then legacy per-format adapters are interim interoperability code.
