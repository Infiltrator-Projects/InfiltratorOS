# InfiltratorOS architecture

This document describes **current implementation and ownership boundaries**. The [filesystem runtime contract](FILESYSTEM_SO_RUNTIME_CONTRACT.md) describes the separate, unfinished target of using one canonical filesystem engine from BOOT, the kernel and applications.

## Package and resource ownership

`VERSION` supplies package identity. `scripts/build-deb.sh` validates the target host, approved artwork and package membership, and builds a deterministic amd64 Debian package. The approved Plymouth theme is installed and selected through the package's maintainer scripts.

The InfiltratorOS package creates the native [filesystem skeleton](FILESYSTEM.md) and owns its OS resources: 23 verified TTFs directly under `/System/Resources/Fonts`, 1,233 verified icons directly under `/System/Resources/Icons`, and two integrity manifests in `/System/Resources/Documentation/InfiltratorOS`. The source icon archive is reassembled and checked **during packaging**, not left on the installed system. No permanent resource seeds, archive copies or native application binaries are installed by this OS package.

Independent application, calendar, clock and filesystem Debian packages own their own executables or modules. Uninstalling one consumer must not delete shared system resources.

## Provisioning and release acquisition

The root `debian/postinst` embeds a SHA-256-sealed, installation-only runtime. It unpacks that runtime under `/Temporary/InfiltratorOS/native-worker.*`, checks the worker executables and delegates work to separate application and provider launchers. These build helpers are **not** permanent OS commands.

The work inventory is sourced from the checked catalogues:

- Four product-specific native application workers: System Monitor, Calculator, Files and Defragmenter.
- 31 calendar and 23 clock workers defined by `packaging/temporal-resources.json`.
- Five established filesystem workers defined by `packaging/filesystem-resources.json`.
- Up to 18 additional filesystem workers defined by `packaging/filesystem-additional.json`, admitted only after discovery and published-release qualification.

`packaging/prepare-native-releases.py` fetches and validates the central `native-installers-v3.json` snapshot for the **63 established** jobs. The additional 18 filesystem releases are discovered independently by `install-filesystem-resources.py`; unavailable entries are explicitly marked pending. An ordinary CI artifact cannot substitute for a published, checksum-qualified native installer.

Thus 63 jobs form the established run; up to 81 may be launched if all additional providers qualify. `packaging/native-progress-count.c` counts the actual manifests to determine the progress denominator. A single GTK progress window monitors the run when a desktop session is available, with separate per-job status/log files in the run's temporary directory. Headless installations still run the same workers.

A provision lock prevents concurrent OS batches; native build locking serialises heavyweight work. Workers wait for APT's existing frontend transaction without deleting dpkg locks. Each worker checks execution and independently reports its final outcome. The post-install monitor retains failed-run diagnostics and clears successful run staging without removing another run's state.

## Native component and filesystem verification

The source-first requirement is specified in [Engineering philosophy](PHILOSOPHY.md#source-first-hardware-native-execution). Each independent component repository owns its compilation, PGO training where supported, ABI and package layout. The OS verifies exact release and installed-state contracts; it does not duplicate a component's compiler implementation. Generic package installation and verified locally compiled installation are different states.

Calendar and clock modules must have their expected `.so`, single illustrated PDF and Debian package metadata attesting to source revision, build profile, LTO/PGO, module hash and exact identity. Legacy unowned engineering files can be cleaned only after verification; unrelated or package-owned files are preserved.

Filesystem providers likewise install independent `.so` modules under `/System/Filesystems` and one illustrated PDF under their provider documentation directory. Module/PDF hashes, Debian package ownership and source/build metadata, expected ELF dependencies and format-specific ABI exports are verified. The five established providers have individual V1/V2/V3/V4/V5/Boot/Defragmenter obligations as applicable. The additional 18 have their own sealed format identities and exports; EXT2 requires both its drop-in and Test Media ABI entry points. Neither an installed `.so` nor an available `.ko`/`.efi` proves a volume is bootable or safely writable by the kernel.

A verified existing current native package may be reused under the provider's cache contract; a missing, stale or invalid package must take the checksum-checked native build path. Current provider verification relies on Debian source/build metadata and does not require obsolete persistent `BUILD-INFO` documents for calendar, clock and filesystem two-file packages. Other products retain their own independent build-record rules.

## Application policy

System Monitor, Calculator, Files and Defragmenter are installed using independently published product-owned native installers. Only verified replacements may remove competing distribution applications where that product permits it. **Files is additive**: it must not purge Nemo or change the system's default file manager. Calculator's worker checks its exact Debian version, `/Programs/calculator`, one PDF manual, menu/icon/AppStream integration, PATH bridge and verified shared fonts rather than an obsolete persistent `BUILD-INFO` file. Files still requires its product-specific native PGO build evidence.

The InfiltratorOS Defragmenter worker currently checks for private helpers under `/System/Commands/Defragmenter` and the two launchables `/Programs/defragmenter` and `/Programs/defragmenter-test-media`. Its source build is not yet asserted to have trained two-pass PGO. The exact private helper path must be reconciled with Defragmenter's own installed layout; see [Validation](VALIDATION.md#known-integration-discrepancy).

## Native namespace and desktop integration

`/Programs` is the single native executable namespace for user applications. `/System` holds operating-system capabilities, `/System/Commands` holds command helpers, and the login path prioritises `/System/Commands:/Programs` without creating duplicate `/usr/bin` application wrappers.

Cinnamon login integration may assign native executable icons in the user's GVfs metadata and migrate the Calendar applet reversibly. It must preserve users' explicit icon choices, existing panel location and default file-manager policy. The source of truth for directories is [FILESYSTEM.md](FILESYSTEM.md), not historical package-version notes.

## Release and failure boundaries

`tests/check.sh` exercises the compiled launcher/status contracts, provider identity rules and verification logic. CI rebuilds and compares Debian package bytes and exercises real provider installers inside disposable GitHub Actions hosts. Publishing uses exactly the qualified bytes, with immutable public release tags/assets. These checks are not equivalent to physical-machine, filesystem crash-recovery, native BOOT or Linux-kernel VFS qualification.

The current system remains an integration layer on Debian/Linux Mint. `/System/Kernel` directories are placeholders; they do not automatically replace Linux kernel, firmware or initramfs files. See [VALIDATION.md](VALIDATION.md) for explicit remaining evidence gates.
