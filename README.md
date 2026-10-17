# InfiltratorOS

The operating-system integration project for Infiltrator Projects. The current deliverable is an amd64 Debian/Linux Mint package containing the approved Plymouth theme and native provisioning for System Monitor and Calculator.

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

Install a C compiler, pkg-config, GTK 3 development headers, Debian packaging tools, `wget` and `xz-utils`, then run:

```sh
sh tests/check.sh
sh scripts/build-deb.sh
```

The package appears in `dist/`. Main pushes qualify the package and publish an unpublished VERSION using those exact bytes. Public release tags and assets are immutable.

## Installed behaviour

The package selects the canonical Plymouth image, configures Debian/Mint compatibility paths, and rebuilds GRUB and initramfs. The approved PNG is checked against its fixed SHA-256 before packaging.

The package establishes the native InfiltratorOS filesystem skeleton under `/System`, `/Programs`, `/Users`, `/Volumes` and `/Temporary`. `/Programs` is the sole native home for user-launchable applications. `/System` contains operating-system infrastructure such as Commands, Libraries, Devices, Filesystems, Kernel, Preferences and shared Resources; there is deliberately no `/System/Programs`. The hierarchy is additive over the Linux base while the native model is being proven, and the complete current contract is documented in [Filesystem](docs/FILESYSTEM.md).

The package also populates `/System/Resources/Fonts` with the three canonical MB Corpo faces. Package construction retrieves the immutable MBLink font archive and verifies both the archive and individual font hashes before the DEB is built; font installation therefore consists only of installing already-qualified package payload.

InfiltratorOS adds `/System/Commands:/Programs` ahead of the inherited PATH for future login shells. Native applications are not mirrored into `/usr/bin` merely for compatibility.

The earlier provisional temporal discovery roots at `/usr/lib/infiltrator/temporal/calendars` and `/usr/lib/infiltrator/temporal/clocks` remain installed in parallel for now. They are not treated as a frozen external ABI and will not be removed implicitly while the new native hierarchy is being evaluated.

After the package transaction, separate native workers resolve the latest published System Monitor and Calculator releases. Each worker downloads the application's own published native installer and invokes its system-package mode, so the application repository remains the authority for compiler policy, PGO/LTO behaviour, package layout and native build metadata. InfiltratorOS verifies the installed replacement and only then removes competing distribution applications. A shared native-build lock serialises heavyweight compilation and package operations.

Calculator verification includes its canonical package, `/Programs/calculator`, BUILD-INFO, visible desktop launcher and icon, AppStream metadata, native PATH bridge and bundled MB Corpo fonts. This prevents a runnable binary from being mistaken for a complete desktop installation.

A GTK window follows each worker when a graphical session is available. Headless installation uses the same workers and status files. Execution failure is reported before the launcher returns success; completion belongs to the worker.

| Application | Status | Log |
| --- | --- | --- |
| System Monitor | `/run/infiltratoros/system-monitor-install.status` | `/var/log/infiltratoros-system-monitor.log` |
| Calculator | `/run/infiltratoros/calculator-install.status` | `/var/log/infiltratoros-calculator.log` |

Application versions are resolved at installation time. The OS package does not maintain a second application release catalogue.
