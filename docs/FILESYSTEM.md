# Native filesystem skeleton

**Filesystem driver target:** one `.so` per format under `/System/Filesystems`, shared by BOOT, kernel, Filesystems and apps via generic adapters. See [the OS runtime contract](FILESYSTEM_SO_RUNTIME_CONTRACT.md). The legacy `.ko`/per-format `.efi` deployment below is an **interim description**, not the final filesystem installation design.

InfiltratorOS defines a small, human-readable native filesystem model alongside the conventional Debian/Linux hierarchy during the transition period. The Linux base may continue to use `/bin`, `/usr`, `/etc`, `/var` and other compatibility paths, but new Infiltrator executables do not extend the old `/usr/bin` application namespace.

The native skeleton is:

```text
/
├── System/
│   ├── Commands/
│   ├── Libraries/
│   ├── Devices/
│   ├── Filesystems/
│   ├── Kernel/
│   │   ├── Images/
│   │   ├── Modules/
│   │   ├── Filesystems/
│   │   └── Firmware/
│   ├── Resources/
│   │   ├── Calendars/
│   │   ├── Documentation/
│   │   ├── Clocks/
│   │   ├── Fonts/
│   │   ├── Locales/
│   │   ├── Icons/
│   │   └── Themes/
│   └── Preferences/
├── Programs/
├── Users/
├── Volumes/
└── Temporary/
```

## Meaning

`/Programs` is the one authoritative native namespace for user-launchable applications. The physical location does not change according to whether an application arrived with InfiltratorOS, from the Infiltrator repository, from a standalone Debian package, or from a local native build. Calculator, System Monitor and System Settings therefore all belong in `/Programs`.

`/System` is reserved for operating-system capabilities and implementation components. `/System/Commands` is for genuine command helpers rather than GUI applications; `/System/Libraries`, `/System/Devices` and `/System/Filesystems` identify their corresponding infrastructure roles. There is deliberately no `/System/Programs`.

`/System/Kernel` is the native namespace for kernel-owned material. `Images` is reserved for kernel images and associated boot artefacts, `Firmware` for kernel-managed hardware firmware, and `Modules` for general kernel modules. Filesystem `.ko` files go directly in `Filesystems` under a single filename per provider.

`/System/Resources` contains discoverable capabilities and shared presentation resources. Calendars and clocks belong under `Resources`, not under `Preferences`: a calendar or clock implementation is a capability, while selecting one is a preference. `/System/Preferences` is reserved for system-wide policy and configuration.

`/System/Resources/Fonts` is the persistent native Linux font cache. InfiltratorOS currently installs **23** verified MB Corpo/Daimler faces there from `assets/fonts/SHA256SUMS`. The three preferred UI roles are `mb_corpo_a_title_cond_regular.ttf` (brand), `mb_corpo_s_title_bold.ttf` (emphasis), and `mb_corpo_s_title_regular.ttf` (body). They are the committed newer, named font assets, not the older three-file archive identities still referenced by some pinned Libraries/Common revisions. InfiltratorOS owns the canonical repository copies under `assets/fonts/` and verifies them during package construction. The Debian package installs all TTFs directly into the native folder and installs the checksum inventory under `/System/Resources/Documentation/InfiltratorOS/fonts.sha256`, with no intermediate `Seed` directory. The helper verifies the installed font resources and registers Fontconfig. The fonts are owned by the InfiltratorOS package; individual application uninstallers must never remove them.

`/Users` is the root of user-owned spaces, but the InfiltratorOS package creates no named user directory; account/setup machinery owns user lifecycle. `/Volumes` is the native mount/volume presentation root and InfiltratorOS installation never clears it. `/Temporary` is the native temporary-storage root: InfiltratorOS installation creates `/Temporary/InfiltratorOS` for ephemeral worker binaries, downloads, status records, logs and shared installation locks, without creating OS-owned temporary state under Linux `/run`, `/var/lib`, `/var/log` or `/tmp`. The installer removes the successful run's temporary work area after its launched provisioning jobs complete, retaining diagnostics if any installation fails.

## Executable discovery

The native command search path is `/System/Commands:/Programs` ahead of the inherited host PATH. InfiltratorOS installs that policy through `/etc/profile.d/90-infiltratoros-native-path.sh`. Standalone Infiltrator application packages may establish the same two native entries independently so installation order does not matter. GUI desktop launchers use absolute `/Programs/...` paths and therefore do not depend on a new login session.

No native Infiltrator application is mirrored into `/usr/bin` merely for compatibility. The Linux base can keep its historical paths while the Infiltrator namespace grows independently.

## Filesystem build outputs

These destinations apply to every provider in the [established](../packaging/filesystem-resources.json) and [additional](../packaging/filesystem-additional.json) catalogues.

| Release output | Installed destination | Example |
| --- | --- | --- |
| PDF manual | `/System/Resources/Documentation/<provider-repository>/<manual>.pdf` | `/System/Resources/Documentation/Filesystem-OFS/OFS.pdf` |
| Format shared object | `/System/Filesystems/<catalogue filename>.so` | `/System/Filesystems/OFS.so` |
| Linux kernel module | `/System/Kernel/Filesystems/<provider-id>.ko` | `/System/Kernel/Filesystems/ofs.ko` |

Build the `.so` and the Linux adapter from the same provider release. The release job builds and qualifies one amd64 `.ko` against the project Linux `6.12.107` target and publishes it as `<provider-id>-linux-6.12.107.ko` with the release SHA-256. InfiltratorOS calls the native build when the installed `.so` is missing or older than the selected release. It verifies current or newer copies in place, including portable packages. It checks the package-owned PDF independently and repairs a missing or damaged copy using the matching portable DEB.

The worker keeps a manually built kernel module from the selected provider release when the installed package owns it and its SHA-256 and kernel ABI match the package metadata. Otherwise, it stages the matching release asset after verifying its SHA-256, ELF type, architecture and kernel release. The stored filename stays the same across provider upgrades. The module is available for manual use from this directory, outside Linux's active module tree.

FAT12, FAT16, FAT32, OFS and exFAT already publish the kernel asset. Other providers can use the same contract as their kernel builds qualify. Once a provider stages a `.ko`, later upgrades require its matching asset. SWAP uses the Linux kernel's built-in swap support.

## Planned boot and modular filesystem integration

The intended Setup installer will select an on-disk format based on qualified capabilities, install its **single authoritative `.so`** and supply the verified exact same module to BOOT's generic loader and the kernel's generic filesystem bridge, along with the required early-boot infrastructure. Until those loaders work, Setup may temporarily use existing separately qualified `.efi` readers and Linux `.ko` providers. The boot-image filesystem and the runtime root filesystem may differ. A future native InfiltratorOS kernel may use a non-Linux module ABI, but the same separation remains.

Filesystem limitations must be represented honestly: Setup may adapt the layout to avoid missing symbolic links when safe, but it cannot treat duplicated files as hard links or claim unsupported namespace, permission, mapping or durability semantics. A format that can hold a boot image does not automatically qualify as a system root. For the detailed, phased cross-project contract, see [Bootable Filesystem Integration Contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/BOOTABLE_FILESYSTEM_CONTRACT.md).

Defragmenter's OS worker verifies the two native launchables `/Programs/defragmenter` and `/Programs/defragmenter-test-media`, desktop entries, and product documentation/resources under `/System/Resources`. Defragmenter's CMake installation and Debian package builder place its private executables under `/System/Commands/Defragmenter`, exactly where InfiltratorOS checks them. The package explicitly rejects the older `/System/Filesystems/Defragmenter` private-executable layout; that directory is not a home for independent filesystem `.so` providers. See [validation of the helper paths](VALIDATION.md#defragmenter-private-helper-path-verification). Linux desktop entries and icon caches remain in their conventional integration locations. InfiltratorOS does not create duplicate executables or compatibility symlinks.

## Transitional status

The active Linux kernel remains in the distribution's existing boot/module/firmware locations until a future migration is explicitly designed and qualified. The current `/usr/lib/infiltrator/temporal` experiment is retained in parallel until temporal resources are migrated deliberately; it is not silently removed by establishing this hierarchy.

The hierarchy should stay small. New top-level directories require a distinct user-visible role rather than mirroring implementation detail.

InfiltratorOS installs native helpers under `/System/Commands/InfiltratorOS/`. The 23 font files and 1,233 icon files are package-owned, installed directly in their final native resource directories; checksum inventories live under `/System/Resources/Documentation/InfiltratorOS/`. There are no `Seed` directories or installed resource archive. The stock Mint Calendar is replaced **only in the user's Cinnamon panel configuration**, after installing the independently packaged Infiltrator Calendar. This is a reversible per-user operation and does not purge Cinnamon. Files stays alongside Nemo until Files is production-complete; InfiltratorOS does not replace the default file manager.
