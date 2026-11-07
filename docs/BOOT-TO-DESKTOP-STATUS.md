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

## Checkpoint 2 — Kernel 0.1.0: BOTH FULL BUILDS VERIFIED; STAGING BLOCKED

- Repository: [Infiltrator-Projects/Kernel](https://github.com/Infiltrator-Projects/Kernel).
- Baseline: Ubuntu HWE Linux source with upstream `7.0.14` and transitional
  `7.0.14-infiltrator` kernel/module ABI; InfiltratorOS Kernel component
  version `0.1.0`. An inherited Linux ABI is not a qualified native module
  loader.
- **2026-11-07 — First full kernel build succeeded** on the Dell Latitude 5550
  running Linux Mint 22.3. Three `linux-*` Debian packages were generated
  and the initial `~/Kernel/dist/native/` tree was staged successfully.
- **2026-11-07 20:19 Melbourne time — Second full kernel build also succeeded.**
  The user has confirmed all three *genuine product-named* packages on disk
  in `/home/ssmith/Kernel/dist/`:
  - `infiltratoros-kernel-headers-0.1.0_0.1.0-1_amd64.deb` — approximately 11 MiB;
  - `infiltratoros-kernel-image-0.1.0_0.1.0-1_amd64.deb` — approximately 120 MiB;
  - `infiltratoros-kernel-image-0.1.0-dbg_0.1.0-1_amd64.deb` — approximately 1.5 GiB.
- All three original Linux-named packages are also still present in that same
  directory (their file timestamps are 19:27). No kernel build was running
  when the user checked with `pgrep`.
- The directory `/Temporary/InfiltratorOS/Kernel/Packages/` **does not
  exist on that host** at this checkpoint. This does **not** invalidate
  either successful build: both were produced before the new source code
  redirected future uninstalled outputs to the native `/Temporary` tree.
  Do not repeat the full compilation merely because that directory is absent.
- The Kernel source has since changed to use
  `/Temporary/InfiltratorOS/Kernel/{Build,Packages,Staging,Logs}` for *new*
  work while retaining access to the successfully compiled historical
  `~/Kernel/dist/` input. The old native stage `~/Kernel/dist/native/`
  remains evidence, not the destination for new staging.
- A prior staging attempt from `Kernel/main` passed local `make check`
  but stopped at `mkdir: cannot create directory
  '/Temporary/InfiltratorOS/Kernel': Permission denied`. This is a
  **workspace permissions issue**, not a build or package failure. No live
  kernel was installed or booted.

**Next concrete gate: stage the existing 0.1.0 image without recompilation.**
On the Dell, provision **only** the Kernel child workspace and stage from
the already-built `dist/` packages:

```bash
sudo install -d -m 0750 -o "$(id -un)" -g "$(id -gn)" /Temporary/InfiltratorOS/Kernel
cd ~/Kernel && git pull --ff-only && bash scripts/infiltrator/stage-native.sh --packages "$PWD/dist"
```

Successful uninstalled output should appear at:
`/Temporary/InfiltratorOS/Kernel/Staging/InfiltratorOS-0.1.0/System/Kernel/`.
Verify `STAGING-STATUS.txt`, `SHA256SUMS.txt`, the image and module
hierarchy before beginning any BOOT-to-native-kernel experiment.
The staging script does **not** modify a live `/System/Kernel`, the ESP,
GRUB or NVRAM. It refuses to replace an existing stage without explicit
`--replace` and should be run as the normal user, not root.

**Preserve all completed local work:** `~/Kernel/dist/`,
`~/Kernel/dist/native/`, `~/Kernel/.native-build/`, source-tree compiled
objects, `~/Kernel-build-next.log` and
`~/Kernel-build-native-0.1.0.log` (where present).
Do **not** run `make clean`, `mrproper`, `git clean`,
`git reset --hard`, delete the previous artifacts or attempt to install
the new image during staging. GitHub preserves source and status records,
not the laptop's compiled binaries.

Kernel-owned detailed history and the most recent staging instructions:
[Kernel/docs/BUILD-STATUS.md](https://github.com/Infiltrator-Projects/Kernel/blob/main/docs/BUILD-STATUS.md)
and
[Kernel/docs/NATIVE-LAYOUT.md](https://github.com/Infiltrator-Projects/Kernel/blob/main/docs/NATIVE-LAYOUT.md).

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

## 2026-11-08 kernel module-discovery progress

The Kernel repository now has the first-stage native kmod helper at
`/System/Kernel/Runtime/InfiltratorOS-0.1.0/modprobe`, selected by
`CONFIG_MODPROBE_PATH` in the first-party kernel build. It creates an
indexed view under `/run/infiltratoros-kernel` for native kernel
`.ko` drivers and only version-matched transitional filesystem
`.ko` adapters. The compiled firmware lookup prefers
`/System/Kernel/Firmware`. GitHub Actions passed the synthetic module
resolution, incompatible-ABI rejection, packaging-path and host-isolation
tests, and republished the small Kernel `.run`.

This is **not** a verified physical native kernel boot or an early-initramfs
loader. The Boot source and Filesystem-*.so repositories retain their own
interfaces. In particular, ordinary userspace `.so` modules under
`/System/Filesystems` still require the separately qualified generic
kernel filesystem bridge; they cannot be directly passed to Linux
`modprobe`. Actual root mounting, signed modules, hardware and boot-to-
desktop verification remain outstanding.
