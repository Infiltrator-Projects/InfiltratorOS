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

Install a C compiler, pkg-config, GTK 3 development headers and Debian packaging tools, then run:

```sh
sh tests/check.sh
sh scripts/build-deb.sh
```

The package appears in `dist/`. Main pushes qualify the package and publish an unpublished VERSION using those exact bytes. Public release tags and assets are immutable.

## Installed behaviour

The package selects the canonical Plymouth image, configures Debian/Mint compatibility paths, and rebuilds GRUB and initramfs. The approved PNG is checked against its fixed SHA-256 before packaging.

The package also establishes the first native InfiltratorOS filesystem skeleton under `/System`, `/Programs`, `/Users`, `/Volumes` and `/Temporary`. `/System` contains Programs, Commands, Libraries, Devices, Filesystems, Preferences and shared Resources; Resources currently includes Calendars, Clocks, Fonts, Locales, Icons and Themes. This hierarchy is additive over the Linux base while the native model is being proven; the conventional Linux hierarchy remains available for compatibility. The complete current contract is documented in [Filesystem](docs/FILESYSTEM.md).

The earlier provisional temporal discovery roots at `/usr/lib/infiltrator/temporal/calendars` and `/usr/lib/infiltrator/temporal/clocks` remain installed in parallel for now. They are not treated as a frozen external ABI and will not be removed implicitly while the new native hierarchy is being evaluated.

After the package transaction, separate native workers resolve the latest published System Monitor and Calculator releases. System Monitor uses its aggressive native installer. Calculator builds its published source with CPU tuning, LTO and measured two-pass PGO. Each replacement is verified before competing distribution applications are removed. A shared native-build lock serialises heavyweight compilation and package operations.

A GTK window follows each worker when a graphical session is available. Headless installation uses the same workers and status files. Execution failure is reported before the launcher returns success; completion belongs to the worker.

| Application | Status | Log |
| --- | --- | --- |
| System Monitor | `/run/infiltratoros/system-monitor-install.status` | `/var/log/infiltratoros-system-monitor.log` |
| Calculator | `/run/infiltratoros/calculator-install.status` | `/var/log/infiltratoros-calculator.log` |

Application versions are resolved at installation time. The OS package does not maintain a second application release catalogue.