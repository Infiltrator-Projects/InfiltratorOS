# Native filesystem skeleton

InfiltratorOS is defining a small, human-readable native filesystem model alongside the conventional Debian/Linux hierarchy during the transition period. The native hierarchy is additive: it does not currently replace `/bin`, `/usr`, `/etc`, `/var` or other paths required by the Linux base and third-party packages.

The first installed skeleton is:

```text
/
├── System/
│   ├── Programs/
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

`/System` contains operating-system capabilities and implementation components. `/System/Programs` is for programs belonging to the operating system; `/System/Commands` is for command-line commands; `/System/Libraries`, `/System/Devices` and `/System/Filesystems` identify their corresponding system roles.

`/System/Kernel` is the native namespace for kernel-owned material. `Images` is intended for kernel images and closely associated boot artefacts, `Modules` for loadable kernel modules, and `Firmware` for firmware consumed by kernel-managed hardware support. These directories are currently empty structural placeholders: establishing them does not move, copy, link, mount over, or otherwise alter the Linux kernel, `/boot`, the distribution module trees or existing firmware locations.

`/System/Resources` contains discoverable capabilities and shared presentation resources. Calendars and clocks therefore belong under `Resources`, not under `Preferences`: a calendar or clock implementation is a capability, while selecting one is a preference.

`/System/Preferences` is reserved for system-wide policy and configuration rather than the resources being configured.

`/Programs` is the native location concept for installed non-system applications. `/Users` is the root of user-owned spaces, but the InfiltratorOS package creates no named user directory; user lifecycle belongs to account/setup machinery. `/Volumes` is the native mount/volume presentation root. `/Temporary` is the native temporary-storage concept.

## Transitional status

These names are an early native InfiltratorOS filesystem contract and are being exercised before deeper migration. Existing Linux compatibility paths remain authoritative where the Linux base requires them. In particular, the active kernel remains in the distribution's existing boot/module/firmware locations until a future migration is explicitly designed and qualified. The current `/usr/lib/infiltrator/temporal` experiment is retained in parallel until temporal resources are migrated deliberately; it is not silently removed by establishing this hierarchy.

The hierarchy should stay small. New top-level directories require a distinct user-visible role rather than mirroring implementation detail.
