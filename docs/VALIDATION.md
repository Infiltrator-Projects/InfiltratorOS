# Validation

**Unimplemented final gate:** [one `.so` per filesystem for firmware, kernel and applications](FILESYSTEM_SO_RUNTIME_CONTRACT.md). Existing installed shared objects and separately tested platform drivers do not prove the kernel can mount or BOOT can read a volume through the same `.so`. Require a real UEFI load test, kernel VFS bridge read/write and early-root qualification before a completion claim.

## Local contracts

Run `sh tests/check.sh` on Linux. Optimised builds with NDEBUG exercise the production process and status modules using disposable fixtures. Checks remain active in release builds.

Process fixtures cover missing executables, failed setup, successful exec, an executable that subsequently fails, asynchronous worker handoff, closure of a parent descriptor above 4096, and startup with closed stdin. Status fixtures cover complete records, indeterminate progress, CRLF, unknown fields, invalid numbers, overflow, bounds, duplicate fields, embedded NUL, oversized lines and truncated records. Rejected records must leave output unchanged.

Defragmenter's integration worker is compiled with warnings-as-errors and checked for its published-release checksum validation, native -O3/LTO compiler policy, native package identity, Test Media and launcher contract. Built-package qualification requires the fourth worker, launcher and progress binary. Actual native build quality and integration on physical Linux/Mint hardware remain distinct from a hosted package-construction test.

## Source-first and local optimisation evidence

The [source-first native-build contract](PHILOSOPHY.md#source-first-hardware-native-execution) is qualified per component and per supported installation path. Provider repositories remain responsible for their compiler and runtime tests; the InfiltratorOS integration tests verify that their declared results were obtained and installed. Evidence for each native build should distinguish:

1. **Source identity:** exact release revision, verified source/installer bytes, toolchain and dependency identities, with no unnoticed substitution of a generic precompiled binary.
2. **Target identity:** actual target CPU architecture and supported features, recorded compiler choices and the absence of unsupported instruction assumptions.
3. **Optimisation evidence:** applied `-O3`, CPU tuning and LTO where claimed, plus recorded PGO training workload, profile use and final trained build when PGO is claimed. Untrained, unsupported, failed or skipped PGO must not be described as completed.
4. **Installed identity:** verified installed artifact hashes, package ownership, expected paths, ABI versions, provider identity and declared capabilities.
5. **Behaviour and performance:** parity with the qualified functional and failure-path tests. If a performance gain is claimed, compare against an appropriate baseline with a documented workload; optimisation flags alone do not prove a gain.
6. **Failure and recovery:** interrupted downloads, source-build failures, incompatible hardware, unsupported compiler features, failed verification and recovery without accepting a partially qualified native installation.

These are system-wide acceptance criteria, not an assertion that the existing automated CI exercises every condition on physical hardware. Individual component records and the integration tests below identify the evidence currently available. A generic package can be a valid documented bootstrap or compatibility path, but must be identified separately from a successful locally optimised build.

## Package and release qualification

GitHub Actions builds all installed C programs with warnings as errors, builds the DEB twice and compares bytes, inspects control metadata and maintainer-script syntax, extracts the payload, verifies installed executable membership, and compares the installed PNG with approved source artwork. Source archive and commit identity accompany SHA-256 sealed artifacts.

Publication consumes only the successful build's sealed artifact. It verifies remote bytes while the release is a draft and again after publication. A draft can be repaired; public tags and assets cannot be replaced.

## Integration scope

These automated checks do not run the package's root maintainer scripts on the CI host. Real Debian/Mint qualification must inspect Plymouth during boot, reboot and shutdown; GRUB and initramfs updates; graphical/headless worker startup; APT lock contention; interrupted downloads/builds; local PGO; and replacement verification on supported hardware. Record distribution, kernel, desktop, package version and logs with that evidence.

The calendar/clock contract checks all 54 catalogue identities, validates stable three-asset release selection and digest rejection, verifies independent per-provider and per-run statuses, and tests the extracted native package record and module identity. Package construction embeds a fifteenth temporary worker and launcher/progress group while preserving the exhaustive permanent-payload allowlist. Provider repositories qualify the exact generic package and aggressive native installer before publishing.

Release qualification also runs `tests/temporal-bundle-e2e.py` on an ephemeral GitHub Actions host. The test extracts the exact integrity-checked worker from the built Debian maintainer script, installs a legacy ownership fixture at all 54 canonical module paths, invokes the real worker against the published releases, and verifies aggressive native package installation, provider metadata and all 54 dpkg ownership transfers. The OS release is published only after this full worker test passes. A distinct process installs each provider and verifies its own status and package ownership. The maintainer-script monitor checks all 58 statuses from the current run and only removes that run’s stage, never the shared /Temporary/InfiltratorOS directory.

## Native installed-build reuse

The calendar, clock and FAT resource workers now distinguish a verified current native build from a missing/stale build. Provider tests exercise the no-download/no-APT fast path and the fallback that still invokes the checksum-checked native installer and post-install verification. A previously installed version must not bypass newer release discovery. The verification checks use the exact installed package version, BUILD-INFO provenance, module checksum, native optimisation profile and ABI/identity, with source revision comparison for temporal resources. These tests prove control flow with fixtures; they do not assert performance gains, current-host CPU fingerprint equivalence or production filesystem media write safety.

The FAT 0.1.2+ installed-module contract additionally requires `LTO: enabled` and `PGO: trained two-pass profile (V1 and V3/V4 fixtures)`. The standalone FAT12/FAT16 source build uses disposable images for training and must still produce a freestanding library without runtime dependencies. These checks verify the declared training path and installed module identity; they do not assert performance uplift or full defragmentation crash-recovery coverage.

## Independently released FAT modules

`tests/filesystem-install-test.py` validates the two-item catalogue, exact two-asset stable release contract, digest/URL/size fail-closed rules and independent per-run status tracking. InfiltratorOS must not claim success unless the matching module is installed under `/System/Filesystems`, owned by the format-specific Debian package, built aggressively with local CPU/LTO flags and authenticated by BUILD-INFO hash plus format-specific V1/V3/V4/defrag exports. The individual FAT repositories separately qualify actual native package installation and filesystem fixtures before publication. Full Defragmenter integration and physical-media and firmware qualification remain separate evidence gates.

### Three independent FAT installation workers (1.0.40)

The SHA-256 sealed native bundle contains Filesystem-FAT12, Filesystem-FAT16 and Filesystem-FAT32. The three .so builds, native optimisations, package ownership and ABI signatures are verified separately. FAT32 does not claim the FAT12/FAT16 defrag ABI. Later releases require independently qualified .efi and kernel-specific .ko release assets. The OS worker does not load kernel modules or stage firmware drivers on arbitrary installations; host-specific deployment and physical-media/boot qualification remain separately gated.
