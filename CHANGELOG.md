## 1.0.138 — Correct child diagnostic writes

- Handle interrupted, partial and failed diagnostic writes in the fork child while preserving strict fortified compiler checks.

## 1.0.137 — Correct concurrent native installer execution

- Prevent writable installer descriptors leaking into concurrent subprocesses, including atomic close-on-exec downloads and explicit child descriptor cleanup.
- Report installer execution errors and worker exit codes instead of empty build logs; retry only transient ETXTBSY.
- Add regression coverage for child descriptor inheritance and failed-exec diagnostics.

## 1.0.136 — Simplify build and installation checks

- Remove fixed resource and bundle counts, repeated staging checks, duplicate release downloads and separately uploaded source archives. Retain required bundle members, archive and executable download integrity, icon collision checks and package-manager coordination.
- Delegate component-specific payload, version, source identity, optimisation, filesystem descriptor and firmware verification to independent installers. Preserve the existing parallel build-only and serial Debian transaction contracts.
- Accept existing installation directory ownership on removal, and run legacy icon cleanup only when needed.

## 1.0.135 — Correct installer bundle verification

- Account for removal of the release resolver in the embedded runtime file count. Report bundle mismatches and package assembly failure stages explicitly.

## 1.0.134 — Remove obsolete dispatcher helpers

- Remove the unused release-log reader and obsolete BOOT refresh environment switch left behind by independent installer dispatch. Keep strict compiler warnings enabled.

## 1.0.133 — Remove central release management

- Remove the central release catalogue client, shared release preflight, cached release metadata, component version thresholds and already-installed version comparisons.
- Resolve the latest independent native installer directly from GitHub without REST quota or credentials; verify its published SHA-256 and tagged source identity where required.
- Preserve the shared resource-aware scheduler, independent BOOT initialization, serial Debian installation and per-component validation.

## 1.0.132 — Independent release discovery

- Resolve changed or missing cached release metadata directly from each component's GitHub release, without waiting for central publication. Report unavailable components individually and continue installing independent components. Existing asset digest checks remain unchanged.

## 1.0.131 — Reuse unchanged compilation units

- Compile common installer objects once per package instead of once per product. Cache unchanged C/C++ compilation units across GitHub builds with standard ccache; link and assemble the versioned package normally. No installer or runtime changes.

## 1.0.130 — Direct automatic package publication

- Build the Debian package once and automatically publish it from main. Remove separate regression, GUI, simulated-upgrade and duplicate-build jobs from the release path. Existing test sources remain available for explicit execution.

## 1.0.129 — Accept existing installation directory ownership

- Use the existing /Temporary/InfiltratorOS directory without requiring root ownership, renaming it, or changing permissions. Remove the automatic migration introduced in 1.0.128.
- Verify that preparation preserves the original directory inode, ownership, mode and existing status data.

## 1.0.128 — Automatic legacy installation directory migration

- Replace the silent root-ownership failure in preinst and postinst with automatic migration of legacy user-owned or writable status directories; retain old diagnostics in a protected sibling directory rather than trusting legacy state as root.
- Preserve existing valid status directories and lock handling; reject unsafe symlinks with explicit diagnostics. Exercise both ordinary and root-owned migration fixtures in CI.

## 1.0.127 — Initialise BOOT before the component queue

- Perform one clean independent BOOT deployment after release preflight and before the shared component queue, rather than clearing its directory after providers run.
- Refresh BOOT during OS orchestration while retaining standalone verified-install reuse. Preserve BOOT-owned Secure Boot and ESP checks.
- Keep filesystem modules hosted-only until exact BOOT firmware compatibility is qualified; exercise the real dispatch order and child environment in the supervisor regression.

## 1.0.126 — Independent OS and BOOT release contracts

- Remove live external product release checks from OS publication; retain local worker, snapshot, package and integrity regression tests.
- Keep cross-repository native installation stress tests available for manual dispatch only.
- Remove the filesystem-worker success gate on BOOT installation and exclude independently installed shared objects from BOOT EFI attestation. The selected-release preflight still validates requested installers before dispatch.

## 1.0.125 — Native installer descriptor and checksum corrections

- Preserve checksum subprocess output with closed standard descriptors and verify filenames containing backslashes without GNU filename escaping.
- Retry interrupted checksum child reaping; close inherited descriptors across the complete Linux descriptor table and create the notification pipe atomically with close-on-exec.
- Extend regression coverage for closed standard descriptors, unusual filenames and reduced descriptor limits. Retain the existing release qualification gates.

## 1.0.124 — Consolidate native worker and verification contracts

- Replace the 23-file worker implementation with one structured C++ source while retaining the unified scheduler, isolation and recovery.
- Share staged and installed provider metadata, manual and descriptor checks, and Debian file verification.
- Restrict hosted CI package sources to signed Ubuntu feeds without relaxing release freshness checks.

## 1.0.123 — Live independent native component qualification

- Add an independent, disposable hosted-runner integration workflow that uses the actual verified OFS and Binary Clock release installers, the production resource scheduler, Common V2 descriptor validation, Debian installation and a second idempotent verification pass.
- Capture real timing and status evidence without executing BOOT or installing the OS integration package. Keep physical-machine qualification distinct from hosted-runner qualification.

## 1.0.122 — Single application registry and bounded status records

- Define application package identities and queue eligibility once, consumed by the release verifier, dispatcher and worker.
- Bound written status diagnostics to parser capacities, and initialise and clean up curl explicitly.
- Correct test include dependencies exposed by separate compilation; add registry and status-bound regressions.

## 1.0.121 — One installation lifecycle and unfiltered inventory

- Remove duplicate standalone compilation and installation paths: every non-BOOT job uses the same resource-admitted isolated build and verified package transaction.
- Separate dependency preparation from scheduling, and installation coordination from package transactions; make lock and temporary-directory ownership non-copyable.
- Enumerate the complete packaged inventory independently of environment selection, then apply selection once; exercise reduced selections in CI.

## 1.0.120 — Native worker responsibility boundaries

- Split the monolithic worker into separately compiled runtime, release, filesystem descriptor, BOOT, verification, package transaction, isolation, build and dispatch units with narrow headers.
- Keep the installed command entry point minimal and link the same modules in production and regression tests.
- Preserve independent component contracts, status semantics and release qualification.

## 1.0.119 — Recover failed Debian package replacements

- Repack and verify the existing installed package before replacement; restore it on failed APT or installed-state verification, retaining recovery bytes on failed restoration.
- Test real Debian installation, injected verification failure and restoration on a disposable CI host. No whole-system or BOOT rollback claim.
- Keep XDG runtime and GUI-session environment private during builds.

## 1.0.118 — Protect host mounts during native builds

- Execute actual build-only RUNs inside a private mount namespace with read-only host mounts and writable private scratch/temp paths. Drop mount capability before starting the RUN and refuse an unrestricted fallback.
- Validate the first authenticated GitHub release redirect directly instead of fetching unnecessary release HTML; retain exact repository/tag checks and test rejected redirects.
- Exercise root mount isolation and private temporary files on disposable CI hosts.

## 1.0.117 — Canonical Common V2 descriptor validation

- Pin released Libraries 1.19.89; the shared design implementation is unchanged.
- Validate actual module ABI, format identity, advertised groups and callback tables using the canonical Common V2 validator.
- Load a sealed module copy only in a re-executed, privilege-dropped and resource-bounded child; reject crashes, timeouts and malformed descriptors before APT.
- Test valid, wrong-format, incomplete-group and symbol-only fixtures.

## 1.0.116 — Verify staged packages and BOOT partition identity

- Verify staged provider SHA-256, source/PGO metadata, Common V2 exports, PDF and application payloads before APT.
- Correlate BOOT firmware registration with the mounted ESP partition UUID; test GPT/MBR identity.
- Remove the obsolete EXT-specific Defragmenter helper requirement; preserve generic engines and manual.
- Preserve selected order and admit fitting smaller jobs; isolate build HOME, TMPDIR and Calculator font staging, and deny PATH-resolved APT during build-only execution.

## 1.0.115 — Selection-first release verification and bounded downloads

- Resolve selection before release discovery, verifying only selected releases while retaining the sealed inventory authority.
- Remove runtime catalogue cardinalities; keep packaged inventories authoritative.
- Fetch native RUNs after resource admission; bound aggregate peak reservations by initial available memory.
- Test reduced selection with unrelated unavailable releases.

## 1.0.114 — Dynamic progress and terminal-state integrity

- Allocate progress inventory dynamically; reject empty manifests, malformed statuses and nonterminal completion percentages.
- Use the same full status parser for the BOOT prerequisite and supervisor checks.
- Refuse package removal while active or deferred provisioning owns its lock, without blocking inside dpkg.
- Exercise 160-job manifests and malformed BOOT prerequisite status records.

## 1.0.113 — Resource admission hardening

- Measure interval CPU utilisation and allocate spare cores to remaining builds.
- Separate live available memory from estimated aggregate peak reservations; check temporary storage capacity and I/O pressure.
- Remove resource-starved and serial prerequisite fallback paths; all selected non-BOOT jobs use the scheduler.
- Add CPU load, storage exhaustion and strict memory admission tests.

## 1.0.112 — Resource-determined build concurrency

- Remove the fixed 16-build ceiling; admission is bounded by pending jobs, CPU affinity and quotas, memory reservations and storage I/O pressure.
- Dynamically size the Linux CPU-affinity mask for high-core-count hosts.
- Test 64 simultaneous reservations, a 24-job RAM limit, quota and I/O constraints, and release of resources after completion. Keep serial package installation and the existing BOOT dependency gate.

## 1.0.111 — Unified queue execution qualification

- Prepare declared build dependencies for selected applications and independent modules before parallel compilation, instead of unnecessarily deferring missing provider prerequisites to serial builds.
- Exercise all five actual scheduler subprocess paths with real disposable DEB metadata, reject stale package versions, and qualify an application-led mixed queue through the production supervisor.

## 1.0.110 — Forward-compatible release catalogue and application preflight

- Validate the OS-owned provider subset of an expanded central catalogue, without admitting or staging unselected filesystem releases.
- Reject missing required entries, duplicate identities and unsafe extra repository names; retain live latest-release validation for every authorized published entry.
- Correct Calendar dependency probing outside a source checkout and preserve the C++ dispatcher boundary while allowing component-owned build dependencies.

## 1.0.109 — Complete application admission to the native parallel queue

- Admit all five applications and independent modules; preserve BOOT's final no-argument handoff.
- Use each application's released build-only argument and Debian version contract, prepare missing application prerequisites serially, and preserve Calendar's version transition.
- Bind legacy getconf-based builders to their assigned CPU count and retain serial package transactions and the 16-build cap.

## 1.0.106 — Source licensing and installer audit finalisation

- Add the verbatim GPL version 3 licence and consistent SPDX identifiers to installer sources and public helper headers.
- Document the lifecycle contract and enable the required POSIX/GNU file flags for the GTK recovery implementation.

## 1.0.105 — Graphical startup and interrupted installation recovery

- Prefer active graphical sessions and distinguish a drawn early GTK frame from a failed or headless launch; retry failed launches during postinst.
- Publish the early font recovery pointer before replacing permanent faces, and restore abandoned journals on the next install.
- Detect abandoned supervisor stages under the installation lock, preserve logs and mark the old run failed. Add recovery regression coverage.

## 1.0.104 — InfiltratorOS audit corrections

- Reject incomplete requested filesystem discovery before starting native builds; preserve BOOT.
- Reap child processes after interrupted or oversized output capture; qualify filesystem build prerequisites more carefully.
- Require readable Secure Boot state and parseable loader signatures when enabled; firmware trust still requires separate qualification.

## 1.0.103 — Install interface fonts in their final location before GTK

- Install or verify the five required interface faces directly in `/System/Resources/Fonts` before launching GTK. Remove temporary source copies; never use a second runtime font directory.
- Preserve normal Debian ownership of all 23 fonts and 1,233 icons. Unpack the remaining resources after the first GUI frame, then verify and register the complete collection.
- Journal early font changes for aborted installation recovery, preserve unrelated resources and test fresh installation, upgrade rollback and symlink rejection. Exercise the packaged early window using only the five canonical font faces.

## 1.0.102 — Show the installer before unpacking system resources

- Start the real GTK interface from a separately verified, temporary preinst bootstrap using exactly its five font faces, before dpkg unpacks the full package-owned resource payload.
- Wait for the first drawn GUI frame on desktop systems with a bounded acknowledgement. Keep headless and overlapping upgrade paths functional.
- Adopt the same run in postinst, verify the full fonts and icons, and report aborted unpack through postrm. No persistent bootstrap resources or additional component jobs.
- Qualify the early bootstrap and unpacking state under the packaged Day/Night GTK tests.

## 1.0.101 — Refine the native GTK installation window

- Implement the approved progress-card design using Common Day/Night colours and canonical typography, explicit inner padding, refined statistics and progress bar, and a current-activity section.
- Publish real font, icon, host boot preparation, release discovery and component-installation phases. Phase indicators derive from atomic run-scoped records; no simulated progress or unverified completion claims.
- Show actual queued component activity and validate phase parsing and the packaged Day/Night GTK indicators with regression tests.

## 1.0.100 — Clean local test and installation output

- Capture expected fault-injection diagnostics and report concise successful test results; retain full output and exit codes on genuine regression failures, with optional verbose diagnostics.
- Provide a local Debian package installation helper that stages readable bytes for APT without disabling its sandbox or suppressing real warnings.

## 1.0.99 — Test worker supervision and retain final GUI counts

- Exercise actual worker success, premature exit, forced termination, smart-queue failure propagation and BOOT prerequisite decisions without touching the host package manager or ESP.
- Publish the selected component total with successful run summaries so the aggregate GTK window can show the correct completed count after staging cleanup.
- Strengthen hashing error propagation and add missing SPDX headers to core launcher, status, progress and maintainer sources.

## 1.0.98 — Verify EFI driver deployment and reconcile installation documentation

- Validate and attest deployed BOOT EFI executable and driver inventory without treating mutable configuration as executable integrity evidence.
- Retain recent supervised failed-run diagnostics and leave unresolved and legacy stages untouched.
- Document actual 83-job scheduling, Secure Boot, worker supervision, and the unfinished Common V2 firmware/kernel transition. Check additional BTRFS/NTFS build prerequisites before parallel admission.

## 1.0.97 — Expand scheduling and verify source identity

- Admit selected filesystem, temporal and Files packages to the smart queue, with a hard 16-build limit and I/O pressure-aware admission.
- Verify provider source revision and installed module SHA-256 against the selected release and package metadata.
- Provision missing temporal build dependencies using the lock-aware package transaction before serial build-only installation.

## 1.0.96 — Supervise native installation and enforce BOOT prerequisites

- Run the dispatcher outside the Debian transaction, reap each worker, and convert abnormal exits or missing terminal records into explicit failures.
- Install BOOT only after all selected filesystem providers have passed; preserve the existing EFI installation if a prerequisite fails.
- Apply lock-aware APT package transactions to independent temporal providers as well as filesystems. Check application dpkg verification output, and make worker cleanup non-throwing.
- Report actual component identities instead of labelling every filesystem as FAT.

## 1.0.95 — Read Secure Boot variables without seeking

- Read the efivarfs attributes and Secure Boot byte sequentially; firmware variable files do not implement seek operations.
- Exercise enabled, disabled and truncated non-seekable variables, legacy data and the actual Hyper-V Shim entry format.

## 1.0.94 — Wait for Debian package-manager locks

- Install already-built native DEBs through APT with indefinite lock waiting and no package removals. Preserve same-version repair and local source compilation.
- Apply the same transaction to serial filesystem providers, retaining their built DEB while APT waits instead of restarting compilation.
- Record individual package transaction logs and retain package identity checks. BOOT remains an independent native RUN in the aggregate installation.

## 1.0.93 — Recognise the Secure Boot firmware entry

- Release the corrected BOOT verifier: recognise the fixed-directory Shim entry when Secure Boot is enabled and the corresponding Shim file is present, while retaining uniqueness, active-entry and first-priority checks.
- Preserve the existing native BOOT installation and release attestation contract; BOOT remains one mandatory orchestrated component.
- Include Secure Boot firmware-entry regression fixtures in the package qualification suite.

## 1.0.89 (unreleased) — Qualify the real BOOT installation contract

- Refuse staging-only BOOT preview assets as firmware-ready installers. Require a stable independent native source RUN, its generic Debian package and existing Common V2/native-deployment evidence.
- Preserve the installed BOOT, GRUB and firmware entries when the required native BOOT release does not exist, and report the exact missing qualification rather than silently treating a staged EFI file as installed.
- Make the UEFI integration test resolve the production minimum BOOT version and verify a stable release's exact SHA-256 digest and size. Remove the obsolete, hardcoded BOOT test version.
- Fail the OS release dependency gate immediately with an actionable error rather than occupying GitHub Actions runners for nearly two hours waiting for external promotions. Keep independent Kernel qualification mandatory.

## 1.0.86 — Require BOOT's Common V2 filesystem loader

- Accept BOOT 0.1.46 or newer for the mandatory firmware component.

## 1.0.85 — Preserve BOOT deployments that need an EFI filesystem driver

- Keep the active EFI executable and firmware entries when the BOOT helper reports an incompatible existing deployment.

## 1.0.83 — Verify the Common V2 filesystem export

- Require a defined Common V2 function in installed filesystem shared objects before accepting the provider.
- Remove obsolete filesystem ABI fields from the installer catalogue.

## 1.0.82 — Deploy Common V2 BOOT with its installed filesystem modules

- Wait for the selected filesystem workers before configuring BOOT.
- Pass verified firmware-compatible filesystem shared objects to the independent BOOT deployment helper and refresh their EFI copies on provider upgrades.

## 1.0.81 — Upgrade existing firmware-registered BOOT installations

- Update a previous BOOT installation that predates the upgrader's ownership marker when its active firmware entry identifies the same EFI partition and loader.

## 1.0.80 — Compile BOOT locally during the upgrade

- Resolve BOOT through its independent verified source installer and compile it for the local processor after APT releases its package lock.
- Deploy the compiled BOOT payload alongside GRUB on eligible UEFI systems.

## 1.0.79 — Add BOOT automatically alongside GRUB

- Install the independent BOOT package through APT. On eligible UEFI systems, deploy it to its own EFI directory and append it to the firmware boot order. Keep the original default and any one-time boot selection.
- Refresh upgrader-managed BOOT copies on later upgrades and preserve their configuration and independent drivers.

## 1.0.78 — Refresh the upgrader window and migrate the legacy Calendar pin

- Group progress, the current component and job counts in a compact GTK panel. Give Close its own high-contrast label and button colours.
- Migrate the recognised legacy Calendar priority-1001 file to the current APT policy so a repository refresh preserves an installed native build.

## 1.0.77 — Identify the upgrader in its progress window

- Show the Debian package version in the GTK window title and describe InfiltratorOS as an upgrader for Debian-based systems.

## 1.0.76 — Adapt native build concurrency to the machine

- Admit as many of the 16 aggressive native builds as available CPU slots and RAM permit, including cgroup limits. Reserve more RAM for Files and exFAT and check resources again before starting each build. Install the resulting packages serially.

## 1.0.75 — Add eight repositories to the native build queue

- Add Files, Clock-Chinese-Ke, Calendar-Byzantine, Clock-Babylonian-Hours, Calendar-Indian, Calendar-Swedish-Historical, Calendar-Gregorian and Calendar-Islamic-Umalqura to the two-slot queue. Retain serial package installation and component-owned fallback for missing build prerequisites.

## 1.0.74 — Stop creating the filesystem kernel-module directory

- Keep `/System/Filesystems` for filesystem libraries without creating `/System/Kernel/Filesystems` in the InfiltratorOS package.

## 1.0.73 — Provision Calendar through its native installer

- Remove Calendar from the InfiltratorOS package dependencies. Run its verified hardware-native installer as a selectable application job and report its result in aggregate progress.
- Require Calendar 1.0.131 or newer for native provisioning to avoid releases that run the Python scanner.
- Keep the Cinnamon panel migration at login, once the applet is available.

## 1.0.72 — Retain provisioning lock until all native workers finish

- Keep the run-wide installation lock until every selected component reports a terminal state; crossing the 24-hour diagnostic threshold never unlocks unfinished work.
- Record a long-running installation diagnostic without publishing a false terminal result or discarding worker logs.
- Qualify mixed failed/running worker status, and correct FAT12 test fixtures to enforce the existing 0.1.24 minimum rather than weakening the forbidden-FUSE safeguard.

## 1.0.71 — Eight-provider resource-aware native compilation queue

- Add OFS, NTFS, FFS and HFS to the existing FAT12/FAT16/FAT32/exFAT queue without changing independent filesystem ownership.
- Share one queue catalogue between the orchestrator and worker to prevent mismatched candidate sets; preserve optional-release admission.
- Refill build slots when individual compilations finish, with resource-aware 1–2 build capacity, serialized Debian installations and existing per-provider fallback and verification.
- Extend native C++ regression coverage for queue membership.

## 1.0.69 — Native C/C++ installer and Python source removal

- Install applications, calendars, clocks and selected filesystem providers using compiled C++ workers, preserving independent repository ownership and installer handoff.
- Use native C++ orchestration and release snapshot verification and a C Cinnamon calendar integration helper; the packaged installer no longer requires Python.
- Remove the retired Python installation code and its superseded Python-only tests. Replace package file allowlist validation with a strict C++17 test and retain native C/C++ installer and resource tests.
- Keep provider build qualification outside the InfiltratorOS release gate. Kernel, Boot and InfiltratorFS are not built or installed by this package.

## 1.0.67 — Verified native installer download recovery

- Recover transient GitHub release CDN failures through the GitHub asset API, resolving the exact same published RUN and requiring matching release identity, SHA-256 digest and byte length before execution.
- Preserve each independent calendar, clock and filesystem repository and its individually owned native package, module and PDF.
- Exclude unused 9P filesystem code from the isolated InfiltratorFS installed-root VM kernel qualification build; keep the real native PID1 two-boot check as an independent release-readiness test.
- The Debian/Linux Mint integration package is not represented as a fully qualified standalone operating system.

## 1.0.66 — Native boot readiness and independent module compatibility

- Preserve the 0.1.0-4 native Kernel integration source changes and the single-source filesystem repository boundaries.
- Admit exactly one independently published, versioned Linux kernel adapter per additional filesystem release, including the native 0.1.1 ABI, without confusing a stored driver with a loadable host module. Continue to reject unexpected files and require exact GitHub SHA-256 asset metadata.
- Add automated exact-commit Boot / Kernel / InfiltratorFS / InfiltratorOS release-readiness evidence, regression tests and an optional strict manual gate. A green evidence-report workflow does not certify a standalone operating system.
- Historical boot-to-desktop experiments were recorded here; those independent Boot, Kernel and InfiltratorFS qualifications are not part of the InfiltratorOS installer release gate.
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
