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

`/System/Resources/Fonts` is the canonical Linux machine cache for the three MB Corpo faces used by Infiltrator applications: `mb_corpo_a_cond_regular.ttf`, `mb_corpo_s_bold.ttf` and `mb_corpo_s_regular.ttf`. InfiltratorOS and standalone application installers may establish or repair that cache from the same immutable MBLink source and hashes. The TTFs and `/etc/fonts/conf.d/90-infiltrator-native-fonts.conf` are persistent machine resources rather than files owned by any one application package. Individual uninstallers must never remove them.

`/Users` is the root of user-owned spaces, but the InfiltratorOS package creates no named user directory; account/setup machinery owns user lifecycle. `/Volumes` is the native mount/volume presentation root. `/Temporary` is the native temporary-storage concept.

## Executable discovery

The native command search path is `/System/Commands:/Programs` ahead of the inherited host PATH. InfiltratorOS installs that policy through `/etc/profile.d/90-infiltratoros-native-path.sh`. Standalone Infiltrator application packages may establish the same two native entries independently so installation order does not matter. GUI desktop launchers use absolute `/Programs/...` paths and therefore do not depend on a new login session.

No native Infiltrator application is mirrored into `/usr/bin` merely for compatibility. The Linux base can keep its historical paths while the Infiltrator namespace grows independently.

## Transitional status

The active Linux kernel remains in the distribution's existing boot/module/firmware locations until a future migration is explicitly designed and qualified. The current `/usr/lib/infiltrator/temporal` experiment is retained in parallel until temporal resources are migrated deliberately; it is not silently removed by establishing this hierarchy.

The hierarchy should stay small. New top-level directories require a distinct user-visible role rather than mirroring implementation detail.
