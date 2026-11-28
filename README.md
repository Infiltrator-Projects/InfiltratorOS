# InfiltratorOS

InfiltratorOS is the operating-system integration project for the Infiltrator Projects family. **The current deliverable is an amd64 Debian/Linux Mint integration package**, not yet a standalone replacement kernel or completed graphical Setup installer. It packages the approved Plymouth theme, establishes native directories and shared resources, and provisions independently released applications and system modules.

**ELEGANCE is the InfiltratorOS design approach:** artistic, visually sophisticated craftsmanship in interfaces, themes, documentation and source-code presentation. It does not mean minimalism, shortened comments or reduced technical depth. See [Engineering Philosophy](docs/PHILOSOPHY.md#elegance--the-artistic-design-authority).

The product is **source-first and hardware-native by design**. Qualified native installers compile verified component source for the target machine; a portable Debian package is not silently described as a locally optimised build. The Linux foundation and current boot integration are transitional.

The active source version is defined by [VERSION](VERSION). For published binaries and immutable release history, use [GitHub Releases](https://github.com/Infiltrator-Projects/InfiltratorOS/releases), not old version-specific instructions embedded in this README.

## Bottom-up development checkpoint


## What is installed

| Component | Current orchestration |
| --- | --- |
| BOOT | InfiltratorOS downloads BOOT's latest published `boot-<version>-native-installer.run` from [BOOT](https://github.com/Infiltrator-Projects/Boot), checks the GitHub release asset's SHA-256 and size, then runs it with **no arguments**. BOOT itself builds locally, installs only under `/EFI/InfiltratorOS/BOOT` on the EFI System Partition and manages its own UEFI firmware entry. InfiltratorOS verifies the installed EFI image, EFI driver inventory and first-priority firmware entry, including the Secure Boot Shim path; it does not install a second BOOT package or run a separate firmware installer. BOOT releases are published as stable GitHub releases; separate physical hardware boot qualification remains outstanding. |
| Kernel | The independent InfiltratorOS Kernel is a separate project. BOOT installation is not contingent on compiling or installing that kernel; the distribution kernel and existing GRUB installation remain independent. Kernel installation and physical boot qualification require their own workflow. |
| Applications | Five independent installers: [System Monitor](https://github.com/Infiltrator-Projects/System-Monitor), [Calculator](https://github.com/Infiltrator-Projects/Calculator), [Calendar](https://github.com/Infiltrator-Projects/Calendar), [Files](https://github.com/Infiltrator-Projects/Files) and [Defragmenter](https://github.com/Infiltrator-Projects/Defragmenter) |
| Time capabilities | 31 independently released calendars and 23 clocks, specified by [the temporal catalogue](packaging/temporal-resources.json) |
| Filesystems | Five established providers (FAT12, FAT16, FAT32, OFS and exFAT) plus 37 additional runtime providers and 2 documentation-only components, specified by [the filesystem catalogues](packaging/filesystem-resources.json) and [additional catalogue](packaging/filesystem-additional.json) |

The established installation jobs consist of five applications, 54 temporal providers, five filesystem providers and BOOT. The 39 additional filesystem-project installers include 2 documentation-only components. Each uses its own published native RUN; documentation-only packages install their reference PDF and no shared object. The full selection contains 104 components, including 42 filesystem runtime providers. Unsupported operations are determined by the installed Common V2 implementation, not by the number of documentation packages. BOOT is invoked as its own native installer; InfiltratorOS does not stage Common V2 `.so` files into the EFI System Partition or add a second firmware-registration step. BOOT's stable-release designation does not establish physical hardware boot qualification.

InfiltratorOS verifies each native installer before execution and verifies package identity, exact release source revision, native build metadata, installed module SHA-256, package ownership and Common V2 filesystem exports as applicable. Native filesystem provider packages own the hosted `.so` and illustrated PDF. Separate transitional kernel or firmware adapters are not installed by the OS filesystem package pathway. Current verified native copies may be reused; stale or invalid packages are rebuilt from verified source. Installation success is not evidence of filesystem operation or physical firmware qualification.

InfiltratorOS keeps only a local component inventory. Each selected worker resolves its repository's current published native RUN directly from GitHub, verifies the published SHA-256, and invokes it. No central release cache, API credentials, hard-coded release versions or already-installed version comparison is used. Failed components are reported independently.

## Current system layout

- `/Programs` is the native location of user-launchable applications.
- `/System/Commands` contains operating-system command helpers; `/System/Filesystems` is the canonical location for separately installed filesystem `.so` modules.
- InfiltratorOS does not create a filesystem-specific kernel-module directory. Older independent provider packages may still create `/System/Kernel/Filesystems` until they are updated. See the [filesystem build outputs](docs/FILESYSTEM.md#filesystem-build-outputs).
- `/System/Resources/Calendars` and `/System/Resources/Clocks` contain independent temporal providers.
- `/System/Resources/Fonts` contains 23 verified MB Corpo/Daimler fonts; `/System/Resources/Icons` contains 1,233 verified named icons. InfiltratorOS owns these shared files, which are placed directly in the native directories without permanent `Seed` folders or duplicate installed archives.
- `/Temporary/InfiltratorOS` holds run-scoped workers, downloads, progress, logs and locks. Successful provisioning cleans up temporary installers; failure diagnostics are retained. Installation does not clear `/Volumes`.

Before Debian unpacks the complete resource payload, preinst verifies a minimal bootstrap containing the new release's GTK window, launcher and five required font sources. It installs or verifies those five faces directly in `/System/Resources/Fonts`, removes the temporary source copies, and launches GTK using only the canonical font directory. When a desktop session is available, the launcher waits for the first drawn frame (bounded; headless installations continue without a GUI). Debian then unpacks its package-owned resources, including the remaining 18 fonts and the 1,233 icons. Postinst adopts the same run, discards the early font rollback journal after successful unpack, verifies the full collections, configures Plymouth and starts the component queue. An aborted unpack can restore changed early fonts. No alternative font directory, permanent seeds or additional component jobs are created. The window uses pinned Common design code and requires Pango to select the packaged font file for each UI role. Jobs retain separate status records and logs. The [native build queue](packaging/native-build-queue.h) includes selected filesystem, app, calendar and clock repositories. All five selected applications and all selected filesystem and temporal providers are eligible for parallel admission, with no fixed job-count ceiling; CPU availability, memory reservations, storage I/O pressure and pending work determine admission. Each admitted job resolves its own latest RUN, downloads it and compiles it within the same reservation. The queue does not wait for other repository lookups. Missing build prerequisites use exclusive APT access without changing headers or libraries underneath active builds. It honours CPU affinity and cgroup v2 limits, reserves peak memory and temporary storage, measures interval CPU utilisation, and rechecks admission before each launch. Finished builds release their reservations immediately for the next pending component. Resulting packages install serially after the build queue drains. Only selected repositories are consulted. An unavailable installer fails its own job without blocking unrelated components. Unresolved prerequisites fail the affected job rather than bypassing resource admission. After component selection, BOOT performs its clean, independent EFI deployment before the parallel component queue starts. The dispatcher waits for and reaps each worker. Filesystem providers install hosted modules only; firmware deployment is not enabled without explicit BOOT qualification. Package-manager operations use a shared build lock and wait for existing APT transactions. Debian package configuration starts this detached work and **does not certify its eventual success**. The separate, atomically updated `/Temporary/InfiltratorOS/last-provision.status` records whether the eligible run ultimately completed or failed. Failed-run diagnostics remain available in the run stage; five recent fully supervised failed stages are retained, and unresolved or legacy stages are not automatically deleted. Completed runs retain a compressed diagnostic archive at `/Temporary/InfiltratorOS/last-provision-logs.tar.gz`, readable with sudo, while temporary build directories are removed. Application packages and format-specific providers remain independently owned.

The stock Cinnamon Calendar panel applet is changed only through reversible per-user integration, not by removing Cinnamon. Files remains alongside Nemo and does not replace the system default file manager. System Monitor and Calculator have their own post-verification replacement policies.

See the [native filesystem hierarchy](docs/FILESYSTEM.md), [implementation architecture](docs/ARCHITECTURE.md) and [installation qualification](docs/VALIDATION.md) for the precise boundaries.

## Build and verification

On a supported Linux build host, obtain this repository with its pinned first-party Common submodule (`git clone --recurse-submodules`, or `git submodule update --init` after an existing clone). The published source ZIP includes the exact pinned Common sources. Install the C toolchain, GTK 3 development headers, `pkg-config` and Debian packaging tools, then run:

```sh
sh tests/check.sh
sh scripts/build-deb.sh
```

The resulting package is under `dist/`. For local installation without an APT sandbox warning, use:

```sh
sh scripts/install-local-deb.sh "dist/infiltratoros_$(cat VERSION)_amd64.deb"
```

The helper stages a readable package without changing APT sandbox security or hiding real package-manager warnings. Fault-injection tests print a concise pass result; their full diagnostics are printed on failure or when `INFILTRATOROS_TEST_VERBOSE=1` is set.

Publication builds the Debian package once and publishes the resulting assets automatically. The regression tests remain available for explicit execution; they are not release prerequisites. Actual installation testing is performed on the target machines.

## Product direction and limits

[Boot](https://github.com/Infiltrator-Projects/Boot) owns OS boot selection; [Filesystems](https://github.com/Infiltrator-Projects/Filesystems) owns shared filesystem capability management; **Setup** is the reserved name for the future graphical OS installer; [InfiltratorFS](https://github.com/Infiltrator-Projects/InfiltratorFS) names a distinct filesystem format.

The [target filesystem execution contract](docs/FILESYSTEM_SO_RUNTIME_CONTRACT.md) is **one trusted format implementation per filesystem**, consumed by BOOT, the kernel and applications through properly qualified generic adapters. The present `.so` packages do not make unmodified Linux kernels or UEFI firmware load those modules directly. Separately qualified `.ko` and `.efi` adapters are transitional; the unified kernel/firmware bridge and bootable-root qualification remain unfinished.

Routine installation, management and recovery are intended to have complete graphical workflows; the current transitional product does not yet meet that final goal.

## Documentation

[Documentation index](docs/README.md) · [Engineering philosophy](docs/PHILOSOPHY.md) · [Product-family contract](docs/PRODUCT-FAMILY.md) · [Architecture](docs/ARCHITECTURE.md) · [Validation](docs/VALIDATION.md) · [Changelog](CHANGELOG.md)

Historical version-specific implementation notes belong in the changelog and immutable Git history, not in this current-state overview.

InfiltratorOS's original source is licensed GPL-3.0-or-later; see [LICENSE](LICENSE). Third-party assets retain their own terms.
