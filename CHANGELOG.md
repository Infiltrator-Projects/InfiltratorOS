## 1.0.67 — Verified native installer download recovery

- Recover transient GitHub release CDN failures through the GitHub asset API, resolving the exact same published RUN and requiring matching release identity, SHA-256 digest and byte length before execution.
- Preserve each independent calendar, clock and filesystem repository and its individually owned native package, module and PDF.
- Exclude unused 9P filesystem code from the isolated InfiltratorFS installed-root VM kernel qualification build; keep the real native PID1 two-boot check as an independent release-readiness test.
- The Debian/Linux Mint integration package is not represented as a fully qualified standalone operating system.

## 1.0.66 — Native boot readiness and independent module compatibility

- Preserve the 0.1.0-4 native Kernel integration source changes and the single-source filesystem repository boundaries.
- Admit exactly one independently published, versioned Linux kernel adapter per additional filesystem release, including the native 0.1.1 ABI, without confusing a stored driver with a loadable host module. Continue to reject unexpected files and require exact GitHub SHA-256 asset metadata.
- Add automated exact-commit Boot / Kernel / InfiltratorFS / InfiltratorOS release-readiness evidence, regression tests and an optional strict manual gate. A green evidence-report workflow does not certify a standalone operating system.
- Record the successful native UEFI three-way VM test and the still-unfinished native init, installed root, desktop, Setup and physical hardware gates. See docs/BOOT-TO-DESKTOP-STATUS.md.
- This release remains a **Debian/Linux Mint integration package**, not a standalone bootable ISO or a completed Setup installer.

## 1.0.65 — Bounded four-provider native build queue

- Add FAT32 and exFAT to the FAT12/FAT16 pilot, with up to two simultaneous builds instead of unrestricted parallel compilation.
- Use serial compilation on constrained hosts and serialize every dpkg transaction; fall back individually if build prerequisites are missing.
- Preserve four-provider per-component timing and build logs; archive successful installation diagnostics before temporary cleanup.
- Retain the original opt-out and add INFILTRATOROS_SMART_BUILD_PILOT=0.

## 1.0.64 — FAT12/FAT16 parallel native build pilot

- Launch two source-bearing filesystem RUNs with --build-only --output concurrently, capped at two builds.
- Keep the existing global native lock across both builds and their serial Debian package installations.
- Preserve individual FAT12 and FAT16 status records and post-install ownership, native SO and PDF checks.
- Use original serial component installers when the build prerequisites are unavailable, or set INFILTRATOROS_PARALLEL_FAT_PILOT=0 to opt out.
- All remaining applications, calendars, clocks and filesystem installers retain the previous path.

# Changelog

This is a concise record of **published changes**, not an alternative copy of the current architecture or a replay of every development commit. The [README](README.md) and [technical documentation](docs/README.md) describe current behaviour. [Immutable release tags](https://github.com/Infiltrator-Projects/InfiltratorOS/releases) and [Git commits](https://github.com/Infiltrator-Projects/InfiltratorOS/commits/main/) preserve detailed historical provenance; the full older changelog is retained at [v1.0.52](https://github.com/Infiltrator-Projects/InfiltratorOS/blob/v1.0.52/CHANGELOG.md).

## 1.0.63 — Native dispatcher consolidation and no-op reinstalls

- Replace four application-specific C installers with a single checksum-verified launcher that invokes each component's own published RUN.
- Skip current native application, calendar, clock and filesystem installations only after checking Debian version, installed files, integrity and native-build evidence.
- Retain separate component status records, release identities and independent installer ownership.

## 1.0.62 — Independent native installation dispatch

- Delegate calendar, clock and filesystem source compilation, package building and documentation to independently published, checksum-verified native RUN installers.
- Track exactly the selected installation jobs, rather than reporting unlaunched components as pending for 24 hours.
- Align integration coverage with the component-owned system-package mode, which installs the native SO and PDF without automatically staging optional kernel modules.
- Preserve real end-to-end package, ownership and documentation qualification before publication.

## 1.0.61 — Support flat documentation upgrades

- Install the OS font and icon checksum manifests directly in the documentation directory. An upgrade removes the old OS-owned directory when empty.
- Accept both current nested manuals and the new flat provider manuals during the transition, while retaining package ownership checks and independent filesystem PDF repair.
- Preserve unverified legacy files; each independently released provider or application must be upgraded before its own old package-owned folder is removed.

## 1.0.60 — Flat filesystem kernel storage and independent repairs

- Store each qualified filesystem kernel module directly under `/System/Kernel/Filesystems/<provider-id>.ko`, alongside the independently installed `.so` and provider PDF. Keep kernel modules inactive and retain one stored version.
- Compile the `.so` only when it is missing or older than the selected release; repair a missing or damaged PDF or stored kernel module independently.
- Preserve a verified manually compiled kernel module even when the `.so` is missing. Keep the kernel checksum, kernel version and ownership in the installed three-file Debian package, including after a repair or native rebuild.
- Cover missing-SO preservation, kernel-only repair, repeated verification and Debian package ownership with regression tests and isolated real-dpkg qualification.

## 1.0.59 — Repair installer bootstrap and verification

- Verify and register the packaged fonts and icons, then open the aggregate progress window before Plymouth and release discovery. Check the exact font files selected by Pango, including the MB Corpo condensed alias and bold title face.
- Require the exact source commit in the managed catalogue before launching provider jobs. Keep additional filesystems with unpinned releases pending and qualify the managed catalogue handoff in CI.
- Use the same manifest and status parsers for the GUI and final monitor. Preserve the pending filesystem list even if an eligible job fails.
- Keep GRUB integration conditional on the Debian/Mint host having both its defaults file and updater. Retry regeneration after a failed attempt, even if the settings were already saved.

## 1.0.58 — Repair Ubuntu Plymouth configuration

- Fix a physical-host installation failure: Ubuntu does not ship the Debian-only Plymouth theme selector. Use the supported `update-alternatives` interface on both Ubuntu-derived and Debian systems.
- Continue to require successful theme registration/selection, GRUB modification when necessary and initramfs rebuild; report actual command exit codes and execution errors.
- Add an isolated host-safe integration test for the Ubuntu command set, correct activation ordering, and failure propagation without mutating boot files.

## 1.0.57 — Clean single-main qualified release

- Archive and remove the four superseded development branches, preserving all reviewed branch-tip commits in immutable archive tags.
- Close superseded pull request #21 after its relevant native ABI and verification changes were integrated in main.
- Remove the one-shot branch reconciliation workflow. Product code retains all qualified 1.0.56 native installer, Common visual, filesystem ABI, package version and fault-handling fixes.
- Rebuild and publish exact 1.0.57 source/DEB bytes from the single main branch; previous release tags remain immutable.

## 1.0.56 — Reconcile native contracts and stale branches

- Correct System Monitor's installed native Debian identity to its actual unsuffixed release version; keep Files' `+native1` policy.
- Require all established FAT Boot/Defragmenter entry points, FAT16 V5 and exFAT Defragmenter ABI; retain EXT2's dual-ABI requirement.
- Requalify freshly installed calendar/clock modules for package ownership, safe installed paths and the selected source revision; add failure regressions.
- Verify actual Fontconfig matches for all required UI families, not just the visible Pango family list.
- Preserve removable Debian package cleanup when Plymouth has already gone, while retaining strict configuration failure reporting.
- Integrate reviewed work from stale branches into main, archive historical branch tips and close superseded PRs before pruning branches.

## 1.0.55 — Native verification and Common installer integration

- Verify System Monitor and Files against the exact selected native package version; remove stock System Monitor tools only after replacement qualification.
- Require fresh temporal provider installs to match their published source commit.
- Show aggregate installation success only after the authoritative run summary, and bound detached native workers ahead of the 24-hour watchdog.
- Fail visibly on Plymouth, GRUB and initramfs command errors and failed font-cache registration.
- Build the graphical installer against pinned Libraries/Common semantic colours, typography identity and metrics, with an explicit missing-font failure; release source archives include pinned Common files.
- Add regressions for native installer version parsing and verification ordering. Physical-machine, boot, recovery and UI accessibility qualification remain separate.

## 1.0.54 — Bootstrap failure safety and historical-document preservation

- Publish a fresh run status before fallible installation bootstrap, so failures in fonts, icons, sealed worker verification and release preparation cannot leave an earlier success record visible.
- Preserve the failed run stage and diagnostics; only the detached monitor owns terminal status after worker launch.
- Retain pre-existing unowned documents unless their actual source can be independently proven; a clean module package still provides one illustrated PDF.
- Add pre-launch failure-injection tests and update filesystem/calendar/clock preservation tests. Real filesystem and performance qualification remain independent acceptance gates.

## 1.0.53 — Native provisioning integrity

- Reject duplicated, incomplete or overlapping native job manifests and ignore unexpected status files when reporting aggregate installation progress.
- Record terminal failures for failed worker launches, retain a separate final run outcome after Debian package configuration, and display success/failure/timeout after staging cleanup.
- Require exact published source commit identity and exported ELF ABI symbols for filesystem packages.
- Correct Debian package descriptions and strengthen regression tests.

## 1.0.52 — Actual native-job progress totals

- Compute aggregate GUI progress from the current run's provider manifests instead of hard-coded additional filesystem counts.
- Regress-test the 81-job case and refuse malformed or incomplete job manifests. This resolves the incorrect 79-job denominator when all 23 filesystem providers are present.

## 1.0.51 — EXT2 dual-ABI qualification

- Require both EXT2's standard format interface and the Test Media ABI before qualifying its native installation.
- Add verification regression coverage for the two exports.

## 1.0.50 — Twenty-three filesystem providers

- Extend independent filesystem catalogue and installer qualification from 21 to 23 providers, including SFS2 and UFS1.
- Verify distinct per-format Debian ownership and the complete sealed bundle installation flow.

## 1.0.49 — Exact filesystem PDF and native package identity

- Match released module/PDF names, case and versions for the independently packaged filesystems.
- Requalify actual native `.so` and illustrated-PDF installations on disposable CI hosts.

## 1.0.48 — Independent native filesystem coverage

- Extend the OS to 21 independently selected filesystem providers with per-format published `.run`, portable `.deb`, native `.so` and PDF verification.
- Add release qualification, dynamic progress and real sealed-worker installation tests.

## 1.0.47 — Fifth established filesystem

- Add aggressive source-native exFAT provisioning alongside FAT12, FAT16, FAT32 and OFS, retaining the 63-component central snapshot.
- Check module/PDF identity, ABI, Debian ownership, LTO/PGO metadata and separate kernel asset boundaries.

## 1.0.46–1.0.45 — Two-file provider documentation contracts

- Move filesystem and 54 calendar/clock provider provenance to Debian metadata and require an illustrated PDF with the native `.so`.
- Remove only known unowned legacy engineering documents after verification; preserve unrelated or package-owned files.

## 1.0.44–1.0.41 — Native-install verification repairs

- Repair OFS worker dispatch and clock optimisation flag parsing; introduce OFS as the fourth established filesystem in 1.0.43.
- Accept calendar two-file documentation and fix Calculator installation checks for its one PDF manual instead of an obsolete persistent build report.

## Earlier development milestones

- **1.0.38–1.0.34:** central authenticated-HTTPS release catalogue, independent FAT modules, single aggregate progress window, native-build reuse and separately released temporal providers.
- **1.0.33–1.0.28:** native Cinnamon icon integration, verified direct font/icon layout, `/Temporary` installer staging, fail-closed embedded bundle and removal of permanent provisioning executables.
- **1.0.26–1.0.20:** Defragmenter provisioning and audited direct MB/Daimler font/icon collections.
- **1.0.19–1.0.5:** Files and Calculator installation integration, native directory hierarchy, canonical naming, verified release bytes, graphical progress and defensive launcher/status contracts.

For exact fixes, transitional arrangements and abandoned layouts in these older releases, inspect their [immutable tags](https://github.com/Infiltrator-Projects/InfiltratorOS/tags) or the [complete v1.0.52 historical changelog](https://github.com/Infiltrator-Projects/InfiltratorOS/blob/v1.0.52/CHANGELOG.md). Historical installation paths and job counts do not define current behaviour.
