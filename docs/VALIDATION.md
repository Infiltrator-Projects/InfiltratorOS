# Validation and evidence boundaries

This document distinguishes **implemented automated checks** from **system behaviour still awaiting independent qualification**. The final one-`.so`-for-BOOT/kernel/apps design is specified in [the filesystem runtime contract](FILESYSTEM_SO_RUNTIME_CONTRACT.md); its generic kernel bridge and real UEFI/root integration have not been demonstrated by the OS package.

## Run the local checks

```sh
sh tests/check.sh
sh scripts/build-deb.sh
```

`tests/check.sh` compiles and exercises production C launcher and status logic with warnings treated as errors, checks exact worker/package contracts, and runs temporal, filesystem, release, icon and native-progress regression tests. The exact job inventory now rejects duplicate or overlapping entries, incomplete fixed catalogues, oversized optional lists and malformed provider identifiers. Process cases include failed or detached exec, closed standard streams and high-numbered file descriptors. Status cases include malformed records, overflows, duplicate required fields and incomplete writes. Unknown extension fields are intentionally ignored for forward compatibility once all required fields are valid. Invalid input must not create a false success state.

## Package and published release

The main GitHub Actions build verifies the exact approved Plymouth artwork and resource integrity, builds the Debian package twice and compares the resulting bytes. It checks Debian metadata and maintainer scripts, an exhaustive permanent-file inventory, required native executable profiles and the checksum-sealed temporary worker runtime.

Publication consumes the qualified archive/DEB/commit identity and verifies remote bytes before and after making a release public. Previously published tags and assets are immutable. `VERSION` is the package identity; changes to documentation after a published tag do not retroactively rewrite that release.

## Provider qualification exercised in CI

- **Temporal:** `tests/temporal-install-test.py` and `tests/temporal-bundle-e2e.py` exercise the 31 calendar and 23 clock providers, their independent releases, sealed installer workflow, per-provider package ownership, one `.so` and illustrated PDF, and source/build metadata.
- **Established filesystems:** `tests/filesystem-install-test.py` checks the five fixed identities, SHA-256/asset rules, package/PDF/ABI validation, runtime dependencies and safe legacy-file cleanup. `tests/filesystem-bundle-e2e.py` installs the sealed actual workers on disposable GitHub Actions hosts.
- **Additional filesystems:** `tests/filesystem-additional-test.py` verifies the 18 other identities and discovery/qualification rules. The filesystem bundle end-to-end test exercises **all 23** independent native `.so` and PDF packages using published release assets. This is evidence on ephemeral CI machines, not a claim they have been installed or tested on the user's PC.
- **Aggregate progress:** `tests/native-progress-count-test.c` verifies the real 81-job manifest-derived total and rejection of missing/malformed manifests, avoiding a progress display capped at a historical 63/79 count.

The first 63 jobs (four applications, 54 temporal providers, five established filesystems) use one central checksum-checked native release snapshot; up to 18 optional filesystem jobs are checked against their own published releases. An unqualified optional provider is pending, not successful. Test failures and real installation failures are distinct; neither should be suppressed by documentation.

## Installed-state acceptance rules

Each independently installed product must match its exact published native release and package ownership; required executable or `.so` paths, companion PDF where specified, release/source identity, build provenance, checksum and ABI must be checked by the owning worker. Filesystem verification now requires the exact published source commit in Debian metadata and checks exact defined ELF dynamic exports rather than permissive text-substring matches.

Calendar/clock and filesystem two-file packages have the `.so` and one illustrated PDF as their expected installed provider files, with native source, optimisation and SHA-256 attestations held in Debian metadata. Obsolete persistent `BUILD-INFO` files are not required for these providers. A verified cached build may skip recompilation only if it still satisfies the exact release and installed-state checks. Pre-existing unowned legacy documents are preserved: a familiar filename and missing dpkg ownership cannot prove origin. New packages still own only one illustrated PDF. Removing older files requires independently established provenance.

Calculator likewise requires its one PDF manual, exact package version, executable and desktop integration, but no retired `BUILD-INFO`. Files retains its separate recorded two-pass PGO contract. Defragmenter's OS worker currently claims CPU-native optimisation and LTO, **not** demonstrated two-pass PGO.

For native-performance claims, record exact source and toolchain, target CPU, measured trained PGO when asserted, build flags, package hashes, functional parity and a repeatable benchmark workload. Flags or a `+native` package suffix alone are not evidence of performance improvement.

## Defragmenter private-helper path verification

The InfiltratorOS worker in `packaging/install-defragmenter.c` requires `/System/Commands/Defragmenter/defragmenter-operation-engine`, `defragmenter-mapper`, the EXT metadata worker and privileged helper. This is **consistent** with Defragmenter's `defragger/cmake/project.cmake`, `defragger/cmake/ext_metadata_map.cmake` and `defragger/packaging/build-deb.sh`. The package builder actively rejects the historical `/System/Filesystems/Defragmenter` private-executable layout. An earlier InfiltratorOS document incorrectly described that old directory; the defect was documentation-only, not a confirmed integration fault. Source/path consistency does not by itself prove that a particular installed host has all required executables.

The OS's Debian package configuration starts the detached native jobs; it does not itself establish their success because builds must wait for APT transactions. The final run outcome is atomically recorded at `/Temporary/InfiltratorOS/last-provision.status`. The status monitor reports `complete`, `failed` or `timeout`; failed-launch records terminate immediately, but physical-machine recovery and automatic retries are not established by CI.

## Evidence still required on real systems

Hosted package construction and disposable CI installation do **not** run the OS's complete privileged Debian package configuration on a physical user's Mint installation or prove drive safety. Separately test package configuration and removal, GRUB/initramfs/Plymouth at boot and shutdown, graphical and headless operation, concurrent APT/`aptk` locks, failed/interrupted downloads, native toolchains and PGO training, retries, package ownership and rollback.

Filesystem claims additionally require independent media read/write/format/defragmentation and crash recovery, identity and ABI tests, relevant kernel/driver qualification, and real BOOT/UEFI and early-root testing where advertised. Record host distribution, CPU, kernel, file/module hashes, exact release, run logs and observed results; do not turn a simulated or CI pass into a physical-hardware completion claim.
