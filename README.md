# InfiltratorOS

**InfiltratorOS product family:** [InfiltratorOS](https://github.com/Infiltrator-Projects/InfiltratorOS) · [System Monitor](https://github.com/Infiltrator-Projects/System-Monitor) · [Calculator](https://github.com/Infiltrator-Projects/Calculator) · [Files](https://github.com/Infiltrator-Projects/Files) · [Defragmenter](https://github.com/Infiltrator-Projects/Defragmenter). Independently installable components, one [product-family and UI contract](docs/PRODUCT-FAMILY.md).

The operating-system integration project for Infiltrator Projects. The current deliverable is an amd64 Debian/Linux Mint package containing the approved Plymouth theme and native provisioning for System Monitor, Calculator, Files and Defragmenter.

[Philosophy](docs/PHILOSOPHY.md) · [Architecture](docs/ARCHITECTURE.md) · [Filesystem](docs/FILESYSTEM.md) · [Validation](docs/VALIDATION.md) · [Changes](CHANGELOG.md). [VERSION](VERSION) owns package identity.

## InfiltratorOS components

The product and this repository are named **InfiltratorOS**. The related components
use short names within the operating system:

- [Boot](https://github.com/Infiltrator-Projects/Boot) starts the selected operating system.
- [Filesystems](https://github.com/Infiltrator-Projects/Filesystems) owns filesystem capabilities and support.
- **Setup** is the name reserved for the future installer. It will use qualified
  filesystem choices to prepare a target, install the system and configure early boot.
- [InfiltratorFS](https://github.com/Infiltrator-Projects/InfiltratorFS) remains the
  name of the filesystem format.

This repository currently supplies branding and native application provisioning;
it does not yet implement the planned Setup installer.

The shared [Bootable Filesystem Integration Contract](https://github.com/Infiltrator-Projects/Filesystems/blob/main/docs/BOOTABLE_FILESYSTEM_CONTRACT.md) defines the long-term goal: Setup installs Boot plus the selected filesystem's UEFI reader, boot payload, runtime modules and early-boot support, while preserving independent filesystem choices. The first AmigaDOS milestone uses an OFS disk **only to store a test Linux kernel**, not as an already-qualified operating-system root.

The source package version is recorded in [`VERSION`](VERSION).

## Product direction

InfiltratorOS is one operating-system product. Boot, Filesystems, Setup and the
desktop applications share its visual language, interaction conventions and
quality standard. Open-source implementations provide a starting point;
product-owned rewrites establish the intended behaviour and architecture.

Normal installation, configuration, maintenance and recovery must have complete
graphical workflows. A hidden terminal is reserved for absolute emergencies.
Users must not need shell commands to complete ordinary tasks.

The product is currently alpha software. A renamed component uses its current
name consistently in its application identity, packages, installed paths,
publishing and documentation. Historical names do not become additional public
APT packages or compatibility aliases.

## Build and verify

Install a C compiler, pkg-config, GTK 3 development headers and Debian packaging tools, then run:

```sh
sh tests/check.sh
sh scripts/build-deb.sh
```

The package appears in `dist/`. Main pushes qualify the package and publish an unpublished VERSION using those exact bytes. Public release tags and assets are immutable.

## Installed behaviour

The package selects the canonical Plymouth image, configures Debian/Mint compatibility paths, and rebuilds GRUB and initramfs. The approved PNG is checked against its fixed SHA-256 before packaging.

The package establishes the native InfiltratorOS filesystem skeleton under `/System`, `/Programs`, `/Users`, `/Volumes` and `/Temporary`. `/Programs` is the sole native home for user-launchable applications. `/System` contains operating-system infrastructure such as Commands, Libraries, Devices, Filesystems, Kernel, Preferences and shared Resources; there is deliberately no `/System/Programs`. The hierarchy is additive over the Linux base while the native model is being proven, and the complete current contract is documented in [Filesystem](docs/FILESYSTEM.md).

The 23 MB Corpo and Daimler TTFs are committed under [`assets/fonts`](assets/fonts) with a single SHA-256 manifest. Package construction verifies every file and stages the fonts under `/usr/lib/infiltratoros/font-seed`; configuration then establishes or repairs the persistent `/System/Resources/Fonts` cache from that local seed with no font download. The bootstrap prefers hard links when the seed and native cache share a filesystem, avoiding duplicate font data, and falls back to verified copies only when a split filesystem requires it. The native cache and Fontconfig registration are deliberately not package-owned, so removing InfiltratorOS cannot break independently installed Infiltrator applications.

InfiltratorOS adds `/System/Commands:/Programs` ahead of the inherited PATH for future login shells. Native applications are not mirrored into `/usr/bin` merely for compatibility.

The earlier provisional temporal discovery roots at `/usr/lib/infiltrator/temporal/calendars` and `/usr/lib/infiltrator/temporal/clocks` remain installed in parallel for now. They are not treated as a frozen external ABI and will not be removed implicitly while the new native hierarchy is being evaluated.

From 1.0.25, the operating-system package depends directly on `infiltrator-calendar-resources (>= 1.19.67)`, built, released and published by Common (`Infiltrator-Projects/Libraries`). APT installs all 30 calendar and 23 clock modules into `/System/Resources/Calendars/` and `/System/Resources/Clocks/`. Calendar, Files and System Settings consume these independent resources; removing any consumer leaves the modules installed. Without optional modules, the applications use normal host calendars and timezones.

After the package transaction, separate native workers resolve the latest published System Monitor, Calculator, Files and Defragmenter releases. Each worker downloads the application's own published native installer and invokes its system-package mode, so the application repository remains the authority for compiler policy, PGO/LTO behaviour, package layout and native build metadata. InfiltratorOS verifies each installed native application and a shared native-build lock serialises heavyweight compilation and package operations.

Defragmenter is additive: its provisioning worker downloads the product-owned published compiler, verifies its SHA-256 release manifest, applies -O3/CPU-native/LTO flags and verifies the native Debian package, Test Media utility and launchers. It does not purge existing filesystem utilities. The present Defragmenter local compiler does not implement trained two-pass PGO, unlike the Files and System Monitor installers; no PGO qualification is claimed. Defragmenter follows the same native executable policy as the other products: `/Programs/defragmenter` launches the GUI and `/Programs/defragmenter-test-media` launches Test Media. Filesystem workers now reside under `/System/Filesystems/Defragmenter`, while the internal font helper and product resources/documentation are installed under `/System/Commands` and `/System/Resources`. The old `/usr/lib/defragmenter` and `/usr/share/doc/defragmenter` package paths are retired.

System Monitor and Calculator retain replacement policy: competing distribution applications are removed only after the corresponding Infiltrator replacement passes verification. Files is intentionally different at this stage: it is installed alongside the existing file manager, its launcher targets `/Programs/files`, and InfiltratorOS does not change the desktop's current default file-manager association.

Calculator verification includes its canonical package, `/Programs/calculator`, BUILD-INFO, visible desktop launcher and icon, AppStream metadata and native PATH bridge. Shared MB Corpo resources are verified through the native font cache rather than by requiring Calculator-owned duplicate font files.

Files verification requires the canonical `files` package, `/Programs/files`, an aggressive two-pass PGO BUILD-INFO record, no obsolete `/usr/bin/files` executable, and a desktop launcher whose Exec target is `/Programs/files`.

A GTK window follows each worker when a graphical session is available. Headless installation uses the same workers and status files. Execution failure is reported before the launcher returns success; completion belongs to the worker.

| Application | Status | Log |
| --- | --- | --- |
| System Monitor | `/run/infiltratoros/system-monitor-install.status` | `/var/log/infiltratoros-system-monitor.log` |
| Calculator | `/run/infiltratoros/calculator-install.status` | `/var/log/infiltratoros-calculator.log` |
| Files | `/run/infiltratoros/files-install.status` | `/var/log/infiltratoros-files.log` |
| Defragmenter | `/run/infiltratoros/defragmenter-install.status` | `/var/log/infiltratoros-defragmenter.log` |

Application versions are resolved at installation time. The OS package does not maintain a second application release catalogue.

## Shared icon resources

InfiltratorOS 1.0.22 and newer seed a deduplicated catalogue of 1,233
portable PNG/WebP icon identities (816 distinct image binaries), extracted from
six Mercedes Android application archives supplied for this project. The archive
is checksum-verified as part of the package build and again before installation.
From 1.0.23, all icons are installed directly in `/System/Resources/Icons/`.
Filenames use `<app>-<variant>-<resource>.png` (or `.webp`) to retain every
application and normal/night variant without filename clashes. Upgrades migrate
the old subdirectory by removing only verified, unchanged copies after the
replacement icons have been installed; locally changed or added files survive.
They do not overwrite the desktop's existing icon theme and survive removal of
individual Infiltrator applications. See [icon inventory](assets/icons/README.md).
