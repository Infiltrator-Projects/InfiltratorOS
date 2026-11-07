# InfiltratorOS — bottom-up boot-to-desktop development handoff

**Last verified:** 2026-11-07 (Australia/Melbourne).  
**Purpose:** A durable, cross-repository checkpoint so development can resume
from the earliest unqualified boot-to-desktop stage without repeating completed
work, mistaking a build for a successful boot, or losing locally compiled
artifacts. This is an engineering progress record, **not** a production-ready
installation guide or permission to modify the host firmware.

This is the cross-project sequence:

1. Firmware / EFI execution → **Boot**
2. EFI filesystem discovery, kernel and initramfs loading → **Boot** and **Filesystem-***
3. Native kernel build and package identity → **Kernel**
4. Native kernel launch, early drivers and root mounting → **Kernel**, **Boot**,
   **Filesystem-*** and the eventual **Setup**
5. Initramfs → root transition, native system paths and core services → **Kernel**,
   **InfiltratorOS** and the eventual **Setup**
6. Desktop/session startup, applications and system resources → **InfiltratorOS**,
   **Files**, **System-Settings**, **System-Monitor**, calendars, clocks and other
   independent components

## Checkpoint 1 — BOOT source, tests and release: VERIFIED

- Repository: [Infiltrator-Projects/Boot](https://github.com/Infiltrator-Projects/Boot)
- Release: [v0.1.39](https://github.com/Infiltrator-Projects/Boot/releases/tag/v0.1.39)
- CI evidence: [GitHub Actions run 42356420685](https://github.com/Infiltrator-Projects/Boot/actions/runs/42356420685) — **completed successfully**, including GNU-EFI compilation,
  full-resource deployment regression tests, package integrity, FAT16 shared
  object bridging and emulated Linux EFI-stub kernel/initramfs handoff.
- The release is **published**, non-draft, with exactly
  `infiltrator-boot_0.1.39_amd64.deb` and `SHA256SUMS.txt`.
- The BOOT package now ships an explicit ESP deployment wrapper and a private
  deployment engine. Separate, confirmed commands support a fresh installation,
  complete managed-resource upgrade and rollback. The installer preserves
  existing user configuration and independently installed filesystem drivers
  on upgrade; it never modifies NVRAM, BootOrder or unrelated bootloaders.
- These files are committed to `Boot/main`: `packaging/infiltrator-boot-deploy`,
  `packaging/infiltrator-boot-deploy-core`, `tests/check-boot-deploy.sh`, and
  matching package, CI and documentation updates.

**Still unqualified:** Physical EFI installation and launch on the Dell,
Secure Boot/module signing, sudden-power-loss behaviour of the ESP directory
swap, and firmware boot-entry selection. BOOT's full-resource deployment is
**not automatically performed** by installing the Debian package. Retain a
known-good existing boot method during hardware trials. Do not equate a
successful OVMF test with a production-bootable installation.

**BOOT status for sequencing:** Code/release fix is complete; physical
qualification is an open gate. No need to redo the completed source fix.

## Checkpoint 2 — Kernel first native build: VERIFIED BASELINE; NEW PACKAGING OPEN

- Repository: [Infiltrator-Projects/Kernel](https://github.com/Infiltrator-Projects/Kernel)
- Source baseline: full Ubuntu HWE Linux source imported at repository root;
  upstream source `7.0.14`, current test ABI `7.0.14-infiltrator`.
- The Dell Latitude 5550 (Linux Mint 22.3) **successfully completed a full
  native kernel build**, producing **three Debian packages**, and then
  **successfully staged** its initial kernel image and module tree on
  2026-11-07. This proves *compilation, packaging and staging*, not installation
  or execution.
- The original verified build artifacts and logs on the laptop are important
  and must be retained: `~/Kernel/dist/`, `~/Kernel/dist/native/`,
  `~/Kernel/.native-build/`, existing source-tree object files, and
  `~/Kernel-build-next.log`.
- Subsequent work introduced a distinct product version `Kernel/VERSION =
  0.1.0`, new Debian package identities `infiltratoros-kernel-image-0.1.0`,
  `infiltratoros-kernel-headers-0.1.0` and
  `infiltratoros-kernel-image-0.1.0-dbg`, plus a transitional module ABI
  recorded separately. The product identity and source-level tests passed;
  **there is not yet verified evidence of a completed on-machine build of
  these newly named packages**.
- The current build wrapper uses `/Temporary/InfiltratorOS/Kernel/Build` and
  `/Temporary/InfiltratorOS/Kernel/Packages` for **new** output, and
  `/Temporary/InfiltratorOS/Kernel/Staging/InfiltratorOS-<VERSION>/` for new
  uninstalled staging. Preserve the earlier `~/Kernel/dist` baseline.
- The native module-tree layout and native module loader remain transitional;
  writing an unfamiliar directory name over Linux's module ABI does not create
  a working independent kernel/module interface.

**Next concrete engineering gate:** Confirm whether the currently running
or most recent `Kernel 0.1.0` native package build has *actually finished*
on the Mint laptop; inspect its final terminal status and Debian package
metadata in the new `/Temporary/InfiltratorOS/Kernel/Packages` area. If
not complete, diagnose the recorded error without cleaning/restarting from
scratch. Do **not** install or boot the generated kernel yet. The authoritative
detail and any newer local evidence belong in
[Kernel/docs/BUILD-STATUS.md](https://github.com/Infiltrator-Projects/Kernel/blob/main/docs/BUILD-STATUS.md)
and
[Kernel/docs/NATIVE-LAYOUT.md](https://github.com/Infiltrator-Projects/Kernel/blob/main/docs/NATIVE-LAYOUT.md).

**Do not** run `make clean`, `mrproper`, `git clean`, delete
`.native-build` or `dist`, replace the existing GRUB boot path, modify the
ESP, install a test kernel or reboot as part of resolving a build-status
question.

## Checkpoints 3–6 — NOT YET END-TO-END QUALIFIED

- BOOT-to-**native InfiltratorOS Kernel** handoff, verified initramfs and
  transition to an installed root: **pending**. A BOOT-to-stock Linux
  EFI-stub handoff is already tested and is different evidence.
- A generic kernel consumer of the same authoritative filesystem `.so`
  modules used by applications and BOOT: **unfinished**. Per-format `.ko`
  adapters remain transitional, and the presence of a `.so` does not prove
  Linux root-mount or writable suitability.
- Native system services, installation/rollback of the operating system,
  and first boot without relying on an already configured Linux Mint
  installation: **not yet end-to-end qualified**. **Setup** is the reserved
  future installer product.
- Native GUI/session and the portfolio's independent applications: present
  as separately evolving products, but **not proof** of a new native
  InfiltratorOS-from-firmware-to-desktop boot.

## Working and release rules

- Keep each implementation in its owning repository, on existing `main`
  only. Do not silently merge filesystem engines into a central implementation.
- Preserve exact source, tests and published release evidence. A GitHub tag,
  successful CI run, local package and physical boot are **distinct** results.
- Do not place disposable build objects in product runtime directories.
  Current native uninstalled work belongs under
  `/Temporary/InfiltratorOS/Kernel` where configured.
- Never erase previous successful local artifacts merely to repair layout,
  naming, versioning or CI status.
- Prefer a single bottom-up blocker at a time. Complete and qualify one
  handoff before claiming the next milestone.
- After every real milestone, update *this checkpoint* and the relevant
  component-owned status document; keep links to exact commits/tests/releases.

## How to resume in a new ChatGPT conversation

Ask: **"HAL, continue the bottom-up InfiltratorOS boot-to-desktop
development from `InfiltratorOS/docs/BOOT-TO-DESKTOP-STATUS.md`. Read the
current BOOT and Kernel status, check the latest real evidence, and work on
the earliest outstanding gate. Preserve existing local kernel build
artifacts; do not install a kernel or change the ESP without my explicit
approval."**

Read the linked owner documents and current GitHub state before taking
new actions. Never infer the latest local Mint terminal result from GitHub
source or CI alone.
