# InfiltratorOS architecture

This document describes **current implementation and ownership boundaries**. The [filesystem runtime contract](FILESYSTEM_SO_RUNTIME_CONTRACT.md) describes the separate, unfinished target of using one canonical filesystem engine from BOOT, the kernel and applications.

## Package and resource ownership

`VERSION` supplies package identity. `scripts/build-deb.sh` validates the target host, approved artwork and package membership, and builds a deterministic amd64 Debian package. The approved Plymouth theme is installed and selected through the package's maintainer scripts. Both Ubuntu-derived and Debian systems use Plymouth's `default.plymouth` registration and selection through `update-alternatives`. Configuration requires successful theme selection and an initramfs rebuild. GRUB is a host integration and runs only if both its defaults file and updater are present. On those hosts regeneration runs on retry even if a previous attempt already saved the settings. Native BOOT owns boot selection independently.

The InfiltratorOS package creates the native [filesystem skeleton](FILESYSTEM.md) and owns its OS resources: 23 verified TTFs directly under `/System/Resources/Fonts`, 1,233 verified icons directly under `/System/Resources/Icons`, and two integrity manifests in `/System/Resources/Documentation`. The source icon archive is reassembled and checked **during packaging**, not left on the installed system. No permanent resource seeds, archive copies or native application binaries are installed by this OS package.

Independent application, calendar, clock and filesystem Debian packages own their own executables or modules. Uninstalling one consumer must not delete shared system resources.

## Provisioning and release acquisition

The root `debian/postinst` embeds a SHA-256-sealed, installation-only runtime. It unpacks that runtime under `/Temporary/InfiltratorOS/native-worker.*`, checks the worker executables and delegates work to separate application and provider launchers. These build helpers are **not** permanent OS commands.

The work inventory is sourced from the checked catalogues:

- Five product-specific native application workers: System Monitor, Calculator, Calendar, Files and Defragmenter.
- 31 calendar and 23 clock workers defined by `packaging/temporal-resources.json`.
- Five established filesystem workers defined by `packaging/filesystem-resources.json`.
- Up to 18 additional filesystem workers defined by `packaging/filesystem-additional.json`, admitted only after discovery and published-release qualification.

`packaging/native-release-snapshot.cpp` fetches and validates the central `native-installers-v3.json` snapshot for 63 jobs, including the exact published source commit for each calendar, clock and filesystem provider. The Calendar app worker checks its own GitHub release when selected. The filesystem worker independently discovers the additional 18 releases and records unqualified entries, including unpinned releases, as pending before jobs start. The pending list stays available if an eligible job fails.

The default run has 64 established jobs, with up to 82 when all additional providers qualify. The GTK window starts after verification and registration of the packaged resources and extraction of the sealed runtime, before Plymouth and release discovery. The window and final monitor share the same manifest and status parsers. Jobs keep separate statuses and logs in the run stage. Headless installations still run the same workers.

A provision lock prevents concurrent OS batches; native build locking serialises heavyweight work. Workers wait for APT's existing frontend transaction without deleting dpkg locks. Each worker checks execution and independently reports its final outcome. The post-install monitor retains failed-run diagnostics and clears successful run staging without removing another run's state. Since the workers require APT, Debian's `postinst` must not wait for their completion while holding the package-manager transaction; its successful return confirms only the synchronous bootstrap. The UI waits for the monitor's authoritative outcome instead of announcing completion merely because every job wrote a terminal status. Each worker runs under a bounded GNU timeout, with a termination/kill grace period before the 24-hour run watchdog. An atomically updated `/Temporary/InfiltratorOS/last-provision.status` records the separately qualified outcome (`running`, `complete`, `failed` or `timeout`). Launch failures receive terminal per-worker records and the GTK window reads only expected run-manifest identities, so stray status files cannot prematurely complete the display.

## Native component and filesystem verification

The source-first requirement is specified in [Engineering philosophy](PHILOSOPHY.md#source-first-hardware-native-execution). Each independent component repository owns its compilation, PGO training where supported, ABI and package layout. The OS verifies exact release and installed-state contracts; it does not duplicate a component's compiler implementation. Generic package installation and verified locally compiled installation are different states.

Calendar and clock modules must have their expected `.so`, single illustrated PDF and Debian package metadata attesting to source revision, build profile, LTO/PGO, module hash and exact identity. A provider package installs one PDF. Pre-existing unowned documents are preserved because their names and dpkg ownership alone cannot establish that an obsolete installer created them; destructive cleanup requires independently verified provenance.

Filesystem providers install independent `.so` modules under `/System/Filesystems`. Their flat-layout releases put the illustrated PDF under `/System/Resources/Documentation/<provider-repository>.pdf`; older nested releases remain supported while each provider is upgraded. Module and PDF hashes are checked separately, along with Debian package ownership, source/build metadata, expected ELF dependencies and format-specific ABI exports. The five established providers have individual V1/V2/V3/V4/V5/Boot/Defragmenter obligations as applicable. The additional 18 have their own sealed format identities and exports; EXT2 requires both its drop-in and Test Media ABI entry points. The established FAT providers require their Boot and Defragmenter interfaces, FAT16 its V5 entry point, and exFAT its defragmentation ABI before the installer accepts an installed module. Neither an installed `.so` nor an available `.ko`/`.efi` proves a volume is bootable or safely writable by the kernel.

A verified existing current native package may be reused under the provider's cache contract; a missing, stale or invalid package must take the checksum-checked native build path. Current provider verification relies on Debian source/build metadata and does not require obsolete persistent `BUILD-INFO` documents for calendar, clock and filesystem two-file packages. Other products retain their own independent build-record rules.

## Application policy

System Monitor, Calculator, Calendar, Files and Defragmenter are installed using independently published product-owned native installers. Calendar installs its `+nativepgo` Debian build through its local-folder installer. The worker verifies its applet, native library, typelib, PDF and PGO build record. The per-user Cinnamon login hook can migrate the panel after Calendar is installed. Only verified replacements may remove competing distribution applications where that product permits it. System Monitor and Calculator verify the exact installed native Debian release before removing competing distribution packages. System Monitor uses the unsuffixed Debian release version; Files and the independent native providers use the product-specific `+native1` policy. Files verifies its exact selected native package version without replacing Nemo. **Files is additive**: it must not purge Nemo or change the system's default file manager. Calculator's worker checks its exact Debian version, `/Programs/calculator`, one PDF manual, menu/icon/AppStream integration, PATH bridge and verified shared fonts rather than an obsolete persistent `BUILD-INFO` file. Files still requires its product-specific native PGO build evidence.

Defragmenter's CMake install rules and native Debian package builder place its private operation engine, mapper, EXT metadata worker and privileged helper under `/System/Commands/Defragmenter`. InfiltratorOS verifies those same paths, plus the two user launchables `/Programs/defragmenter` and `/Programs/defragmenter-test-media`. The Defragmenter package deliberately rejects `/System/Filesystems/Defragmenter` because private executables are not separately distributed format `.so` drivers. Its source build is not yet asserted to have trained two-pass PGO.

The GTK progress UI consumes Libraries/Common's host-following Day/Night semantic palette, typography identity and metrics through a pinned first-party source gitlink. The package verifies its full 23-font manifest and fails configuration if Fontconfig cannot register the fonts. The UI uses Pango to check the actual file selected for each required role and weight, including the condensed family alias.

## Native namespace and desktop integration

`/Programs` is the single native executable namespace for user applications. `/System` holds operating-system capabilities, `/System/Commands` holds command helpers, and the login path prioritises `/System/Commands:/Programs` without creating duplicate `/usr/bin` application wrappers.

Cinnamon login integration may assign native executable icons in the user's GVfs metadata and migrate the Calendar applet reversibly. It must preserve users' explicit icon choices, existing panel location and default file-manager policy. The source of truth for directories is [FILESYSTEM.md](FILESYSTEM.md), not historical package-version notes.

## Release and failure boundaries

`tests/check.sh` exercises the compiled launcher/status contracts, provider identity rules and verification logic. CI rebuilds and compares Debian package bytes and exercises real provider installers inside disposable GitHub Actions hosts. Publishing uses exactly the qualified bytes, with immutable public release tags/assets. These checks are not equivalent to physical-machine, filesystem crash-recovery, native BOOT or Linux-kernel VFS qualification.

The current system remains an integration layer on Debian/Linux Mint. `/System/Kernel/Filesystems` stores verified filesystem kernel modules for manual use, with one filename per provider. Linux's active module, firmware and initramfs paths remain distribution-owned. See [VALIDATION.md](VALIDATION.md) for explicit remaining evidence gates.
