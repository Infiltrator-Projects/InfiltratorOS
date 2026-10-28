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

The 23 MB Corpo/Daimler fonts are SHA-256 verified during package construction
and installed **directly** under `/System/Resources/Fonts/`. Likewise, all 1,233
PNG/WebP icons are validated against their canonical source checksums and
packaged **directly** under `/System/Resources/Icons/`. Their verification
manifests belong under `/System/Resources/Documentation/InfiltratorOS/`.
No installed `Seed` folders, archives or duplicate resources exist.
Post-install helpers verify the direct resources and register Fontconfig.
The InfiltratorOS package owns these files, so reinstalling it restores them;
individual application removal does not remove shared resources.

InfiltratorOS depends on Calendar and uses a per-user Cinnamon login migration to replace only the stock Calendar panel applet while preserving its panel position and a reversible backup; it does not purge Cinnamon or alter Nemo. `/System/Commands/InfiltratorOS/activate-calendar --restore` reverses the change and stops automatic reactivation; `--activate` explicitly applies it again.

InfiltratorOS adds `/System/Commands:/Programs` ahead of the inherited PATH for future login shells. Native applications are not mirrored into `/usr/bin` merely for compatibility.

The earlier provisional temporal discovery roots at `/usr/lib/infiltrator/temporal/calendars` and `/usr/lib/infiltrator/temporal/clocks` remain installed in parallel for now. They are not treated as a frozen external ABI and will not be removed implicitly while the new native hierarchy is being evaluated.

From 1.0.35, InfiltratorOS launches 54 separate calendar/clock installer jobs (originally with separate progress windows; 1.0.36 consolidates them into one installation window) using one integrity-checked shared worker implementation. Each job resolves each latest published release, requires exactly its generic `.deb`, native source `.run` and complete source ZIP, verifies the `.run` against GitHub’s SHA-256 asset digest, and invokes `--profile aggressive --system-package-mode`. Each source installer performs local `-O3` CPU-native compilation, LTO, measured two-pass PGO and provider checks before installing its own native Debian package. Modules live under `/System/Resources/Calendars/` and `/System/Resources/Clocks/`; passive build records live under `/System/Resources/Documentation/<repository>/`. InfiltratorOS verifies package version, optimization flags, installed module hash and ABI identity for every module. The exact 54-provider catalogue is `packaging/temporal-resources.json`. The OS no longer depends on the combined Common resource package; individual packages transfer their module’s ownership through dpkg `Replaces`, while Common source remains intact. Removing Calendar, Files or another consumer leaves these providers installed.

During package configuration, InfiltratorOS unpacks a SHA-256-verified temporary native worker bundle under `/Temporary/InfiltratorOS`, launches four application jobs plus 54 separately tracked calendar/clock provider jobs. It removes only the current run’s temporary executables after all 58 installations succeed, and preserves that run’s diagnostics on failure or timeout. A new installation cannot delete another run’s temporary workers. No application-installer executables remain permanently under `/System/Commands`. After the package transaction, separate temporary native workers resolve the latest published System Monitor, Calculator, Files, Defragmenter and calendar/clock releases. Each worker downloads the application's own published native installer and invokes its system-package mode, so the application repository remains the authority for compiler policy, PGO/LTO behaviour, package layout and native build metadata. InfiltratorOS verifies each installed native application and a shared native-build lock serialises heavyweight compilation and package operations.

Defragmenter is additive: its provisioning worker downloads the product-owned published compiler, verifies its SHA-256 release manifest, applies -O3/CPU-native/LTO flags and verifies the native Debian package, Test Media utility and launchers. It does not purge existing filesystem utilities. The present Defragmenter local compiler does not implement trained two-pass PGO, unlike the Files and System Monitor installers; no PGO qualification is claimed. Defragmenter follows the same native executable policy as the other products: `/Programs/defragmenter` launches the GUI and `/Programs/defragmenter-test-media` launches Test Media. Filesystem workers now reside under `/System/Filesystems/Defragmenter`, while the internal font helper and product resources/documentation are installed under `/System/Commands` and `/System/Resources`. The old `/usr/lib/defragmenter` and `/usr/share/doc/defragmenter` package paths are retired.

System Monitor and Calculator retain replacement policy: competing distribution applications are removed only after the corresponding Infiltrator replacement passes verification. Files is intentionally different at this stage: it is installed alongside the existing file manager, its launcher targets `/Programs/files`, and InfiltratorOS does not change the desktop's current default file-manager association.

Calculator verification includes its canonical package, `/Programs/calculator`, BUILD-INFO, visible desktop launcher and icon, AppStream metadata and native PATH bridge. Shared MB Corpo resources are verified through the native font cache rather than by requiring Calculator-owned duplicate font files.

Files verification requires the canonical `files` package, `/Programs/files`, an aggressive two-pass PGO BUILD-INFO record, no obsolete `/usr/bin/files` executable, and a desktop launcher whose Exec target is `/Programs/files`.

From 1.0.36, one GTK window reports the aggregate progress of all 58 jobs when a graphical session is available, rather than spawning 58 overlapping dialogs. All jobs retain independent status files and logs; headless installation uses the same workers. A run-wide lock prevents duplicate concurrent provisioning, and workers wait for Mint's APT frontend transaction to release its lock. Execution failure is reported before the launcher returns success; completion belongs to the worker.

| Application | Status | Log |
| --- | --- | --- |
| System Monitor | `/Temporary/InfiltratorOS/native-worker.<run>/status/system-monitor-install.status` | `/Temporary/InfiltratorOS/native-worker.<run>/status/system-monitor-install.log` |
| Calculator | `/Temporary/InfiltratorOS/native-worker.<run>/status/calculator-install.status` | `/Temporary/InfiltratorOS/native-worker.<run>/status/calculator-install.log` |
| Files | `/Temporary/InfiltratorOS/native-worker.<run>/status/files-install.status` | `/Temporary/InfiltratorOS/native-worker.<run>/status/files-install.log` |
| Defragmenter | `/Temporary/InfiltratorOS/native-worker.<run>/status/defragmenter-install.status` | `/Temporary/InfiltratorOS/native-worker.<run>/status/defragmenter-install.log` |
| Calendars and clocks (54 modules) | `/Temporary/InfiltratorOS/native-worker.<run>/status/<provider>-install.status` | `/Temporary/InfiltratorOS/native-worker.<run>/status/<provider>-install.log` |

Application versions are resolved at installation time. The OS package does not maintain a second application release catalogue.

## Shared icon resources

InfiltratorOS provides 1,233 named portable PNG/WebP icon identities
(816 distinct image binaries) from six Mercedes Android application sources.
The source archive is SHA-256 checked and unpacked **during package build only**.
The resulting individual icons are package-owned files directly under
`/System/Resources/Icons/`; no archive or `Seed` directory is installed.
Filenames use `<app>-<variant>-<resource>.png` (or `.webp`) to retain every
application and normal/night variant without filename clashes. Upgrades migrate
the old subdirectory by removing only verified, unchanged copies after the
replacement icons have been installed; locally changed or added files survive.
They do not overwrite the desktop's existing icon theme. Their owner is
InfiltratorOS, not any individual application. See [icon inventory](assets/icons/README.md).

## Native executable icons in Cinnamon

InfiltratorOS installs a user-session-only Cinnamon autostart integration, `/System/Commands/InfiltratorOS/activate-program-icons`, that automatically applies Nemo's per-user custom file icons to installed native executables under `/Programs`. It uses each app's `/System/Resources/Icons` artwork where available and the existing desktop theme otherwise. It preserves explicitly assigned user icons. This is required because package `postinst` executes as root, while Nemo's GVfs executable-icon metadata belongs to the logged-in user. It does not create extra launchers, require terminal `gio` commands or modify `/Volumes`. New installations are picked up at the next Cinnamon login.

## Independent FAT12 and FAT16 modules

InfiltratorOS 1.0.37 installs [Filesystem-FAT12](https://github.com/Infiltrator-Projects/Filesystem-FAT12) and [Filesystem-FAT16](https://github.com/Infiltrator-Projects/Filesystem-FAT16) through two independent checksum-verified, CPU-native source installers. Each module has its own Debian package and source GitHub repository. Each installs exactly one shared object under `/System/Filesystems/FAT12.so` or `/System/Filesystems/FAT16.so` and publishes a generic `.deb`, native `.run` and GitHub's built-in tag source ZIP. The two new jobs extend the run to 60 independently tracked statuses while preserving **one aggregate GTK progress window** and the existing APT lock coordination. InfiltratorOS checks each package's version, owner, installed source hash, ABI exports, FAT-width identity and CPU-native BUILD-INFO; these packages do not install .ko or .efi drivers or replace the Filesystems manager.

## Complete FAT filesystem installation contract

Since 1.0.38, the intended FAT12 and FAT16 components are each **three driver forms**: Linux shared-object filesystem/Defragmenter engines (`FAT12.so`, `FAT16.so`), kernel VFS modules (`fat12.ko`, `fat16.ko`) compiled against the actual installed running kernel, and independent OVMF-tested UEFI drivers (`FAT12.efi`, `FAT16.efi`). Shared-object-only releases 0.1.0 and 0.1.1 are rejected. The exact published release binaries and SHA-256 values must be verified, with a matching per-kernel module installed and discoverable via Linux `modprobe`. The EFI binary is provided in `/System/Kernel/Firmware/EFI`; no silent changes to ESP or NVRAM. Physical firmware and cross-kernel coverage remain distinct qualification requirements.
