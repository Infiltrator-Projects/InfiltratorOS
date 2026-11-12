# InfiltratorOS

InfiltratorOS is the operating-system integration project for the Infiltrator Projects family. **The current deliverable is an amd64 Debian/Linux Mint integration package**, not yet a standalone replacement kernel or completed graphical Setup installer. It packages the approved Plymouth theme, establishes native directories and shared resources, and provisions independently released applications and system modules.

The product is **source-first and hardware-native by design**. Qualified native installers compile verified component source for the target machine; a portable Debian package is not silently described as a locally optimised build. The Linux foundation and current boot integration are transitional.

The active source version is defined by [VERSION](VERSION). For published binaries and immutable release history, use [GitHub Releases](https://github.com/Infiltrator-Projects/InfiltratorOS/releases), not old version-specific instructions embedded in this README.

## Bottom-up development checkpoint


## What is installed

| Component | Current orchestration |
| --- | --- |
| BOOT | The independent [BOOT](https://github.com/Infiltrator-Projects/Boot) native installer compiles its EFI application locally and installs its package. On eligible 64-bit UEFI systems with Secure Boot disabled, existing registered BOOT installations on the mounted GPT EFI System Partition are updated in place. New entries are appended when GRUB is the default on that partition. The upgrade preserves the relative order of existing firmware entries and any one-time boot selection. |
| Applications | Five independent installers: [System Monitor](https://github.com/Infiltrator-Projects/System-Monitor), [Calculator](https://github.com/Infiltrator-Projects/Calculator), [Calendar](https://github.com/Infiltrator-Projects/Calendar), [Files](https://github.com/Infiltrator-Projects/Files) and [Defragmenter](https://github.com/Infiltrator-Projects/Defragmenter) |
| Time capabilities | 31 independently released calendars and 23 clocks, specified by [the temporal catalogue](packaging/temporal-resources.json) |
| Filesystems | Five established providers (FAT12, FAT16, FAT32, OFS and exFAT) plus 18 independently discovered providers, specified by [the filesystem catalogues](packaging/filesystem-resources.json) and [additional catalogue](packaging/filesystem-additional.json) |

The established 64 installation jobs consist of five applications, 54 temporal providers and five filesystem providers. Calendar is selected with the other applications and resolves its native installer from its own release. The 18 additional filesystem installers are admitted only when their own published release assets pass the worker's integrity and identity checks. The graphical progress total includes these eligible jobs and the BOOT deployment result when attempted.

InfiltratorOS verifies each native installer before execution and checks installed identity, version, ABI, package ownership, expected files and build provenance as applicable. A filesystem provider's portable `.deb` installs its `.so`, illustrated PDF and stored `.ko` when the provider has a Linux kernel adapter. Each provider has its own source-bearing `.run`: running it manually compiles the `.so`, kernel adapter and PDF locally and installs over the existing version. InfiltratorOS alone decides whether the `.so` needs compiling; it preserves current or newer verified copies and repairs the PDF and stored kernel module independently. The installed Debian package owns all three files and records their checksums and kernel version. SWAP uses Linux's built-in support; ZFS has no qualified Linux adapter yet. The actual installed state is checked separately. A GitHub Actions artifact, a published release or a successful build on someone else's computer is **not** proof of a verified local installation.

Five filesystem releases are resolved through the [central 63-component release snapshot](https://infiltrator-projects.github.io/Infiltrator-Repository/catalogue/native-installers-v3.json); the additional 18 are independently discovered and checked against their own release metadata. Unavailable or unqualified optional providers are reported in `/Temporary/InfiltratorOS/filesystems-pending.list` rather than counted as installed.

## Current system layout

- `/Programs` is the native location of user-launchable applications.
- `/System/Commands` contains operating-system command helpers; `/System/Filesystems` is the canonical location for separately installed filesystem `.so` modules.
- InfiltratorOS does not create a filesystem-specific kernel-module directory. Older independent provider packages may still create `/System/Kernel/Filesystems` until they are updated. See the [filesystem build outputs](docs/FILESYSTEM.md#filesystem-build-outputs).
- `/System/Resources/Calendars` and `/System/Resources/Clocks` contain independent temporal providers.
- `/System/Resources/Fonts` contains 23 verified MB Corpo/Daimler fonts; `/System/Resources/Icons` contains 1,233 verified named icons. InfiltratorOS owns these shared files, which are placed directly in the native directories without permanent `Seed` folders or duplicate installed archives.
- `/Temporary/InfiltratorOS` holds run-scoped workers, downloads, progress, logs and locks. Successful provisioning cleans up temporary installers; failure diagnostics are retained. Installation does not clear `/Volumes`.

The Debian package unpacks the native directories, fonts and icons before configuration. Configuration verifies those resources, registers the fonts and opens one aggregate GTK progress window before Plymouth and release discovery when a desktop session is available. The window uses pinned Common design code and requires Pango to select the packaged font file for each UI role. Jobs retain separate status records and logs. The [native build queue](packaging/native-build-queue.h) includes selected filesystem, app, calendar and clock repositories. It compiles up to all 16 selected repositories at once when available CPU and memory permit. It honours CPU affinity and cgroup v2 limits, reserves memory for each active aggressive native build, and rechecks the budget before every launch. The resulting packages install serially. Optional filesystems join only after their independent releases pass discovery and verification. Missing build prerequisites trigger an individual component-owned serial fallback. Package-manager operations use a shared build lock and wait for existing APT transactions. Debian package configuration starts this detached work and **does not certify its eventual success**. The separate, atomically updated `/Temporary/InfiltratorOS/last-provision.status` records whether the eligible run ultimately completed, failed or timed out. Failed-run diagnostics remain available in the run stage. Completed runs retain a compressed diagnostic archive at `/Temporary/InfiltratorOS/last-provision-logs.tar.gz`, readable with sudo, while temporary build directories are removed. Application packages and format-specific providers remain independently owned.

The stock Cinnamon Calendar panel applet is changed only through reversible per-user integration, not by removing Cinnamon. Files remains alongside Nemo and does not replace the system default file manager. System Monitor and Calculator have their own post-verification replacement policies.

See the [native filesystem hierarchy](docs/FILESYSTEM.md), [implementation architecture](docs/ARCHITECTURE.md) and [installation qualification](docs/VALIDATION.md) for the precise boundaries.

## Build and verification

On a supported Linux build host, obtain this repository with its pinned first-party Common submodule (`git clone --recurse-submodules`, or `git submodule update --init` after an existing clone). The published source ZIP includes the exact pinned Common sources. Install the C toolchain, GTK 3 development headers, `pkg-config` and Debian packaging tools, then run:

```sh
sh tests/check.sh
sh scripts/build-deb.sh
```

The resulting package is under `dist/`. Repository CI additionally checks reproducible package bytes, the sealed temporary worker bundle, package layout and installed-provider integration on disposable runners. These tests do not themselves demonstrate successful installation on every physical Debian/Mint machine, filesystem media recovery, UEFI boot or kernel mounting.

## Product direction and limits

[Boot](https://github.com/Infiltrator-Projects/Boot) owns OS boot selection; [Filesystems](https://github.com/Infiltrator-Projects/Filesystems) owns shared filesystem capability management; **Setup** is the reserved name for the future graphical OS installer; [InfiltratorFS](https://github.com/Infiltrator-Projects/InfiltratorFS) names a distinct filesystem format.

The [target filesystem execution contract](docs/FILESYSTEM_SO_RUNTIME_CONTRACT.md) is **one trusted format implementation per filesystem**, consumed by BOOT, the kernel and applications through properly qualified generic adapters. The present `.so` packages do not make unmodified Linux kernels or UEFI firmware load those modules directly. Separately qualified `.ko` and `.efi` adapters are transitional; the unified kernel/firmware bridge and bootable-root qualification remain unfinished.

Routine installation, management and recovery are intended to have complete graphical workflows; the current transitional product does not yet meet that final goal.

## Documentation

[Documentation index](docs/README.md) · [Engineering philosophy](docs/PHILOSOPHY.md) · [Product-family contract](docs/PRODUCT-FAMILY.md) · [Architecture](docs/ARCHITECTURE.md) · [Validation](docs/VALIDATION.md) · [Changelog](CHANGELOG.md)

Historical version-specific implementation notes belong in the changelog and immutable Git history, not in this current-state overview.
