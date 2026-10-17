# InfiltratorOS

The operating-system integration project for Infiltrator Projects. The current deliverable is an amd64 Debian/Linux Mint package containing the approved Plymouth theme and native provisioning for System Monitor, Calculator and Files.

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

The three canonical MB Corpo TTFs are committed directly under [`assets/fonts`](assets/fonts) in this repository. Package construction verifies those exact files and stages them under `/usr/lib/infiltratoros/font-seed`; configuration then establishes or repairs the persistent `/System/Resources/Fonts` cache from that local seed with no font download. The bootstrap prefers hard links when the seed and native cache share a filesystem, avoiding duplicate font data, and falls back to verified copies only when a split filesystem requires it. The native cache and Fontconfig registration are deliberately not package-owned, so removing InfiltratorOS cannot break independently installed Infiltrator applications.

InfiltratorOS adds `/System/Commands:/Programs` ahead of the inherited PATH for future login shells. Native applications are not mirrored into `/usr/bin` merely for compatibility.

The earlier provisional temporal discovery roots at `/usr/lib/infiltrator/temporal/calendars` and `/usr/lib/infiltrator/temporal/clocks` remain installed in parallel for now. They are not treated as a frozen external ABI and will not be removed implicitly while the new native hierarchy is being evaluated.

The operating-system package depends directly on the standard `infiltrator-calendar-resources` capability pack. Calendar applications and System Settings consume those system resources rather than owning them, so removing either application does not remove the installed calendar implementations.

After the package transaction, separate native workers resolve the latest published System Monitor, Calculator and Files releases. Each worker downloads the application's own published native installer and invokes its system-package mode, so the application repository remains the authority for compiler policy, PGO/LTO behaviour, package layout and native build metadata. InfiltratorOS verifies each installed native application and a shared native-build lock serialises heavyweight compilation and package operations.

System Monitor and Calculator retain replacement policy: competing distribution applications are removed only after the corresponding Infiltrator replacement passes verification. Files is intentionally different at this stage: it is installed alongside the existing file manager, its launcher targets `/Programs/files`, and InfiltratorOS does not change the desktop's current default file-manager association.

Calculator verification includes its canonical package, `/Programs/calculator`, BUILD-INFO, visible desktop launcher and icon, AppStream metadata and native PATH bridge. Shared MB Corpo resources are verified through the native font cache rather than by requiring Calculator-owned duplicate font files.

Files verification requires the canonical `files` package, `/Programs/files`, an aggressive two-pass PGO BUILD-INFO record, no obsolete `/usr/bin/files` executable, and a desktop launcher whose Exec target is `/Programs/files`.

A GTK window follows each worker when a graphical session is available. Headless installation uses the same workers and status files. Execution failure is reported before the launcher returns success; completion belongs to the worker.

| Application | Status | Log |
| --- | --- | --- |
| System Monitor | `/run/infiltratoros/system-monitor-install.status` | `/var/log/infiltratoros-system-monitor.log` |
| Calculator | `/run/infiltratoros/calculator-install.status` | `/var/log/infiltratoros-calculator.log` |
| Files | `/run/infiltratoros/files-install.status` | `/var/log/infiltratoros-files.log` |

Application versions are resolved at installation time. The OS package does not maintain a second application release catalogue.
