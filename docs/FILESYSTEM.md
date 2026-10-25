# Native filesystem skeleton

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

`/System/Kernel` is the native namespace for kernel-owned material. `Images` is intended for kernel images and closely associated boot artefacts, `Modules` for loadable kernel modules, and `Firmware` for firmware consumed by kernel-managed hardware support. These directories remain structural placeholders: establishing them does not move, copy, link, mount over or otherwise alter the Linux kernel, `/boot`, distribution module trees or existing firmware locations.

`/System/Resources` contains discoverable capabilities and shared presentation resources. Calendars and clocks belong under `Resources`, not under `Preferences`: a calendar or clock implementation is a capability, while selecting one is a preference. `/System/Preferences` is reserved for system-wide policy and configuration.

`/System/Resources/Fonts` is the persistent native Linux font cache. InfiltratorOS currently installs **23** verified MB Corpo/Daimler faces there from `assets/fonts/SHA256SUMS`. The three preferred UI roles are `mb_corpo_a_title_cond_regular.ttf` (brand), `mb_corpo_s_title_bold.ttf` (emphasis), and `mb_corpo_s_title_regular.ttf` (body). They are the committed newer, named font assets, not the older three-file archive identities still referenced by some pinned Libraries/Common revisions. InfiltratorOS owns the canonical repository copies under `assets/fonts/` and verifies them during package construction. The Debian package installs all TTFs directly into the native folder and installs the checksum inventory under `/System/Resources/Documentation/InfiltratorOS/fonts.sha256`, with no intermediate `Seed` directory. The helper verifies the installed font resources and registers Fontconfig. The fonts are owned by the InfiltratorOS package; individual application uninstallers must never remove them.

`/Users` is the root of user-owned spaces, but the InfiltratorOS package creates no named user directory; account/setup machinery owns user lifecycle. `/Volumes` is the native mount/volume presentation root and InfiltratorOS installation never clears it. `/Temporary` is the native temporary-storage root: InfiltratorOS installation creates `/Temporary/InfiltratorOS` for ephemeral worker binaries, downloads, status records, logs and shared installation locks, without creating OS-owned temporary state under Linux `/run`, `/var/lib`, `/var/log` or `/tmp`.

## Executable discovery

The native command search path is `/System/Commands:/Programs` ahead of the inherited host PATH. InfiltratorOS installs that policy through `/etc/profile.d/90-infiltratoros-native-path.sh`. Standalone Infiltrator application packages may establish the same two native entries independently so installation order does not matter. GUI desktop launchers use absolute `/Programs/...` paths and therefore do not depend on a new login session.

No native Infiltrator application is mirrored into `/usr/bin` merely for compatibility. The Linux base can keep its historical paths while the Infiltrator namespace grows independently.

## Planned boot and modular filesystem integration

The current native namespace reserves `/System/Kernel/Images` and `/System/Kernel/Modules` as a future OS-owned layout. During the intermediate Linux stage, kernel and initramfs images may remain in `/boot` and Linux `.ko` filesystem modules remain in their qualified Linux module paths; no premature migration is implied.

The intended future Setup installer will select an on-disk format based on qualified capabilities, install Boot's corresponding read-only `.efi` reader, put the kernel and initramfs on a filesystem Boot can read, and install the corresponding operating-system-side filesystem modules. The boot-image filesystem and the runtime root filesystem may differ. A future native InfiltratorOS kernel may use a non-Linux module ABI, but the same separation remains.

Filesystem limitations must be represented honestly: Setup may adapt the layout to avoid missing symbolic links when safe, but it cannot treat duplicated files as hard links or claim unsupported namespace, permission, mapping or durability semantics. A format that can hold a boot image does not automatically qualify as a system root. For the detailed, phased cross-project contract, see [Bootable Filesystem Integration Contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/BOOTABLE_FILESYSTEM_CONTRACT.md).

Defragmenter's independently released package installs `/Programs/defragmenter`, `/Programs/defragmenter-desktop` and `/Programs/defragmenter-test-media`. Its private filesystem workers and privileged helper reside under `/System/Filesystems/Defragmenter`, with the font bootstrap in `/System/Commands`. Artwork is under `/System/Resources/Icons` and `/System/Resources/Themes/Defragmenter`, while documentation is under `/System/Resources/Documentation/Defragmenter`. The former `/usr/lib/defragmenter` and `/usr/share/doc/defragmenter` are not native filesystem locations and must not be shipped by a current Defragmenter package. Linux desktop entries and icon-cache entries remain under standard host paths for desktop integration. InfiltratorOS creates no duplicate executables or compatibility symlinks.

## Transitional status

The active Linux kernel remains in the distribution's existing boot/module/firmware locations until a future migration is explicitly designed and qualified. The current `/usr/lib/infiltrator/temporal` experiment is retained in parallel until temporal resources are migrated deliberately; it is not silently removed by establishing this hierarchy.

The hierarchy should stay small. New top-level directories require a distinct user-visible role rather than mirroring implementation detail.

InfiltratorOS installs native helpers under `/System/Commands/InfiltratorOS/`. The 23 font files and 1,233 icon files are package-owned, installed directly in their final native resource directories; checksum inventories live under `/System/Resources/Documentation/InfiltratorOS/`. There are no `Seed` directories or installed resource archive. The stock Mint Calendar is replaced **only in the user's Cinnamon panel configuration**, after installing the independently packaged Infiltrator Calendar. This is a reversible per-user operation and does not purge Cinnamon. Files stays alongside Nemo until Files is production-complete; InfiltratorOS does not replace the default file manager.
