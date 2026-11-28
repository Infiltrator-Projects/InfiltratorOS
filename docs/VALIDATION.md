# Validation and evidence boundaries

This document distinguishes **implemented automated checks** from **system behaviour still awaiting independent qualification**. The final one-`.so`-for-BOOT/kernel/apps design is specified in [the filesystem runtime contract](FILESYSTEM_SO_RUNTIME_CONTRACT.md); its generic kernel bridge and real UEFI/root integration have not been demonstrated by the OS package.

## Run the local checks

```sh
sh tests/check.sh
sh scripts/build-deb.sh
```

`tests/check.sh` compiles and exercises production C launcher and status logic with warnings treated as errors, checks exact worker/package contracts, and runs temporal, filesystem, release, icon and native-progress regression tests. Native release regressions additionally validate the published installer filename/version pairing, exact installed System Monitor/Files package versions, and verification before removing stock System Monitor packages. The exact job inventory rejects duplicate or overlapping entries, incomplete fixed catalogues, oversized optional lists and malformed provider identifiers. Process cases include failed or detached exec, closed standard streams and high-numbered file descriptors. Status cases include malformed records, overflows, duplicate known fields and incomplete writes. The parser tolerates unknown extension fields. The shared final-status test checks that contradictory state fields cannot produce a successful outcome.

## Package and published release

The main GitHub Actions build verifies the exact approved Plymouth artwork and resource integrity, builds the Debian package twice and compares the resulting bytes. It checks Debian metadata and maintainer scripts, an exhaustive permanent-file inventory, required native executable profiles and the checksum-sealed temporary worker runtime.

Publication consumes the qualified archive/DEB/commit identity and verifies remote bytes before and after making a release public. Previously published tags and assets are immutable. `VERSION` is the package identity; changes to documentation after a published tag do not retroactively rewrite that release.

## Provider qualification exercised in CI

- **Temporal:** `tests/native-component-test.cpp` checks the release asset and immutable source identity contract for the 54 providers. The full native installation requires local or separate integration qualification.
- **Established filesystems:** `tests/native-component-test.cpp` checks the five fixed identities, release integrity and Common V2 symbol export, identity and callback-table validation against the released Common ABI, in a bounded privilege-dropped process.
- **Additional filesystems:** the native component and progress tests cover catalogue and discovery contracts for the additional 38 filesystem-project components (29 runtime providers and 9 documentation-only packages). The full 43-component filesystem-project deployment must be qualified on the target.
- **Aggregate progress:** `tests/native-progress-count-test.c` verifies the 103-job manifest-derived total, including Calendar and BOOT, and rejection of missing or malformed manifests.

The native worker tests direct release asset identity, matching GitHub-published SHA-256, and tagged source identity. Debian payload and Common V2 descriptor validation remain independent of release publication.

## Installed-state acceptance rules

Each independently installed product must match its published native version,
package ownership and build profile. The worker checks the installed files
with Debian verification. For filesystem providers it also checks for a
defined Common V2 function in the x86-64 ELF dynamic symbol table without
executing the module. The worker test covers a valid export, an obsolete
entry point, a data symbol and an undefined import.

Calendar/clock and filesystem two-file packages have the `.so` and one illustrated PDF as their expected installed provider files, with native source, optimisation and SHA-256 attestations held in Debian metadata. Obsolete persistent `BUILD-INFO` files are not required for these providers. A verified cached build may skip recompilation only if it still satisfies the exact release and installed-state checks. Pre-existing unowned legacy documents are preserved: a familiar filename and missing dpkg ownership cannot prove origin. New packages still own only one illustrated PDF. Removing older files requires independently established provenance.

Calculator likewise requires its one PDF manual, exact package version, executable and desktop integration, but no retired `BUILD-INFO`. Files retains its separate recorded two-pass PGO contract. Defragmenter's OS worker currently claims CPU-native optimisation and LTO, **not** demonstrated two-pass PGO. Fresh calendar/clock installs also require their recorded source revision to match the exact published release commit, not just a correctly formatted SHA; the fresh path re-runs the same package-ownership and immutable-source checks used for cached modules. The established filesystem ABI regression explicitly requires the FAT Boot/Defragmenter and exFAT Defragmenter exports. The System Monitor version regression preserves its unsuffixed Debian package policy.

For native-performance claims, record exact source and toolchain, target CPU, measured trained PGO when asserted, build flags, package hashes, functional parity and a repeatable benchmark workload. Flags or a `+native` package suffix alone are not evidence of performance improvement.

## Defragmenter private-helper path verification

The InfiltratorOS worker in `packaging/install-defragmenter.c` requires `/System/Commands/Defragmenter/defragmenter-operation-engine`, `defragmenter-mapper` and the generic private engine resources. This is **consistent** with Defragmenter's `defragger/cmake/project.cmake`, `defragger/cmake/ext_metadata_map.cmake` and `defragger/packaging/build-deb.sh`. The package builder actively rejects the historical `/System/Filesystems/Defragmenter` private-executable layout. An earlier InfiltratorOS document incorrectly described that old directory; the defect was documentation-only, not a confirmed integration fault. Source/path consistency does not by itself prove that a particular installed host has all required executables.

The OS's Debian package configuration starts the detached native jobs; it does not itself establish their success because builds must wait for APT transactions. The final run outcome is atomically recorded at `/Temporary/InfiltratorOS/last-provision.status`. The supervised dispatcher records missing terminal statuses and abnormal worker exits as failures. The monitor reports `complete` or `failed` after the dispatcher exits. Recovery from a killed supervisor or an uninterruptible kernel I/O stall remains unqualified.

The GTK installer is built with pinned Common design source. Package CI runs the production Pango font check against an isolated copy of the unpacked fonts, then verifies that removing the condensed or bold face fails. The aggregate window waits for the monitor's final status before claiming success. An isolated Plymouth test verifies activation ordering, a retry after `update-grub` fails, and successful configuration when the GRUB defaults or updater are absent. Physical font rendering, Day/Night accessibility and boot behaviour still require system testing.

## Evidence still required on real systems

Native BOOT integration tests validate release asset identity, the exact no-argument installer handoff, EFI driver inventory and detection of a single active first-priority UEFI entry including Secure Boot Shim; they do not exercise the BOOT `.run` on a physical EFI system. BOOT releases are stable GitHub publications but must not be described as physically hardware-qualified without separate evidence. Hosted package construction and disposable CI installation do **not** run the OS's complete privileged Debian package configuration on a physical user's Mint installation or prove drive safety. Separately test package configuration and removal, GRUB/initramfs/Plymouth at boot and shutdown, graphical and headless operation, concurrent APT/dpkg locks, failed/interrupted downloads, native toolchains and PGO training, retries, package ownership and rollback.

Filesystem claims additionally require independent media read/write/format/defragmentation and crash recovery, identity and ABI tests, relevant kernel/driver qualification, and real BOOT/UEFI and early-root testing where advertised. Record host distribution, CPU, kernel, file/module hashes, exact release, run logs and observed results; do not turn a simulated or CI pass into a physical-hardware completion claim.

Audit regressions exercise bounded subprocess capture and reaping, canonical font rollback and abandoned supervisor status recovery. The release gate does not simulate every dpkg interruption window or prove Secure Boot trust on physical firmware.

Build isolation is exercised on a disposable privileged CI host: writes outside the selected build folder must fail, and private `/tmp` files must not appear in the host namespace. This protects trusted build-only installers against accidental shared-host mutation; it is not an adversarial root-code sandbox. The runtime requires Linux mount namespace and recursive mount-attribute support.

A disposable privileged CI test installs a synthetic Debian package, replaces it, deliberately fails post-install verification, then verifies that the actual prior package version and payload were restored. No real product package is altered by this fixture.

The native worker regression suite compiles the exact production source with its entry point disabled, avoiding a duplicate public test API. Existing resource, package, isolation, release and firmware fixtures remain mandatory.

### Real native component integration

The manually dispatched `Real native provider integration` workflow exercises the published OFS and Binary Clock installers directly on a disposable host. BOOT is not invoked. It is not a publication gate or whole-system qualification.
