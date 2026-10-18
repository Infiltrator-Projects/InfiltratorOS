# Changelog

## 1.0.22 — Mercedes native icon resources

- Add a curated, checksum-verified portable Mercedes icon collection extracted from the six supplied Android application archives: 1,233 named icons and 816 distinct binaries.
- Install persistent application/variant-namespaced icons under /System/Resources/Icons/mercedes via a package seed, without modifying the desktop's default icon theme.
- Make icon installation idempotent, validate all image digests and fail closed for unsafe resource paths; qualify archive and staging behavior in CI.
- Exclude compiled Android XML vectors, nine-patches, animations, photographs and parking-sequence diagrams, which are not directly portable desktop icons.


## 1.0.21

- Document all 23 installed MB/Daimler faces by their internal font family names and allocate restrained, task-specific typography across InfiltratorOS, System Monitor, Calculator and Files.
- Update the native GTK installation-progress window to use verified S Text, S Title, S Text Cond and A Screen typography when available, with existing Common Title families as an independent-install fallback.
- Preserve all existing progress/status/installer operations and the immutable 23-font package manifest.

## 1.0.20

- Replace three superseded 1.000 MB Corpo faces with the corresponding 1.010 Title builds, which preserve their glyph outlines and metrics but update corporate font metadata.
- Retain 23 distinct MB Corpo and Daimler font resources, excluding non-Mercedes third-party fonts, with readable filenames and a single verified SHA-256 manifest.
- Package and install all 23 fonts as persistent shared native resources without downloading font payloads at installation time.
- Extend Calculator font-cache qualification and release tests to verify the new named resources and the full 23-font set.

## 1.0.19

- Let native-install progress windows recover cleanly when a failed installation is retried and later succeeds.
- Treat the shared status file as authoritative on every refresh instead of permanently latching the first terminal state seen by the window.
- Cancel any pending success auto-close when a new queued, running or failed state appears, and restore the active optimisation heading during retries.

## 1.0.18

- Make the standard `infiltrator-calendar-resources` capability pack a direct operating-system dependency so calendar implementations remain system resources rather than application-owned payloads.
- Add Files as the third native-provisioned InfiltratorOS application, consuming Files 0.1.34's published self-contained native installer and shared heavyweight-build lock.
- Require Files to install canonically at `/Programs/files` with aggressive native optimisation, LTO, two-pass PGO BUILD-INFO and a desktop launcher targeting the native executable.
- Keep Files additive for now: do not purge Nemo or another distribution file manager, and do not change the desktop's current default file-manager association.
- Extend package construction, progress/status profiles and CI qualification to the Files worker while continuing to reject `/System/Programs`.

## 1.0.17

- Fix Calculator post-install verification to validate the canonical persistent MB Corpo resources under `/System/Resources/Fonts` instead of the retired Calculator-owned `/usr/share/fonts/truetype/calculator` payload.
- Keep Calculator verification aligned with the native font-cache architecture introduced in Calculator 0.2.50 and InfiltratorOS 1.0.16.
- Add a regression gate requiring all three native font resource paths and rejecting any reintroduction of the retired Calculator-specific font directory.

## 1.0.16

- Commit the three canonical MB Corpo TTF files directly under `assets/fonts/` in the InfiltratorOS repository and verify their fixed SHA-256 identities during package construction.
- Remove font acquisition from InfiltratorOS installation: the package seeds the persistent native cache from its repository-owned files and requires no MBLink or other network access for fonts.
- Keep `/System/Resources/Fonts` outside Debian payload ownership so uninstalling InfiltratorOS cannot remove fonts still used by standalone Infiltrator applications.
- Prefer hard links between the package seed and persistent native cache when they share a filesystem, avoiding duplicate font data; retain a verified copy fallback for split-filesystem installations.
- Remove the now-unneeded `xz-utils` runtime dependency and qualify that the built package contains the three exact repository font seeds but no package-owned TTFs in the native cache.

## 1.0.15

- Change the MB Corpo faces from InfiltratorOS-owned Debian payload files into persistent native machine resources under `/System/Resources/Fonts`.
- Use the canonical Libraries 1.19.51 bootstrap contract to verify, download only when needed, atomically repair and Fontconfig-register the three shared faces.
- Keep the font cache and its Fontconfig registration intact when InfiltratorOS or any individual application is removed, so standalone Infiltrator software cannot be broken by another package uninstall.
- Qualify that the DEB owns no TTF files in the native cache and prove the bootstrap can seed a clean staged system and then reuse it without another archive read.

## 1.0.14

- Populate `/System/Resources/Fonts` with the three canonical MB Corpo faces used by Infiltrator applications.
- Source the font archive from immutable MBLink commit `aa161e7342112beab8feb7669f072c870f742765` during package construction and verify both the archive and each extracted TTF by SHA-256 before staging.
- Keep font acquisition out of `postinst`: the released DEB already contains the verified fonts, and CI qualifies their exact bytes in the installed native filesystem payload.

## 1.0.13

- Retire the one-shot native filesystem migration workflow and script so normal CI never edits, commits or pushes source back to `main`.
- Make `/Programs` the sole native namespace for user-launchable applications and reserve `/System` for operating-system infrastructure, with `/System/Commands` for genuine command helpers.
- Stop packaging `/System/Programs` and make package qualification fail if that directory is ever reintroduced; do not add upgrade-time cleanup logic for a path that should not exist.
- Add `/System/Commands:/Programs` to future login-shell PATHs without creating `/usr/bin` compatibility aliases.
- Require System Monitor provisioning to verify the canonical `/Programs/system-monitor` executable as well as the installed package.

## 1.0.12

- Make Calculator provisioning consume Calculator's own published native installer instead of maintaining a second source-download, PGO and Debian-build implementation inside InfiltratorOS.
- Verify the canonical `/Programs/calculator` executable rather than the retired `/usr/bin/calculator` path that caused a successful package installation to be reported as failed.
- Treat menu integration as part of successful installation: require the visible desktop launcher, Calculator icon, AppStream metadata, native PATH bridge, native BUILD-INFO and packaged MB Corpo fonts before declaring completion.
- Remove obsolete `infiltrator-calculator` and `infiltrator-calc` package identities, and distribution calculators, only after the canonical Calculator installation has passed verification.
- Read authoritative dpkg status instead of treating any `dpkg-query` database entry as an installed package.
- Add regression checks preventing the OS worker from reintroducing its own `dpkg-buildpackage`/source-bundle pipeline or the obsolete `/usr/bin/calculator` assumption.

## 1.0.11

- Let the published System Monitor native installer own its canonical optimisation profile in system-package mode instead of selecting that profile again in InfiltratorOS.

## 1.0.10

- Add the native `/System/Kernel` namespace with `Images`, `Modules` and `Firmware` directories.
- Keep the new kernel hierarchy structural only; do not alter `/boot`, Linux module trees, firmware locations or the running kernel.
- Qualify the kernel namespace in the built Debian package.

## 1.0.9

- Establish the first native InfiltratorOS filesystem skeleton at `/System`, `/Programs`, `/Users`, `/Volumes` and `/Temporary`.
- Add system role directories for Programs, Commands, Libraries, Devices, Filesystems, Preferences and shared Resources.
- Add resource directories for Calendars, Clocks, Fonts, Locales, Icons and Themes.
- Keep the native hierarchy additive over the Debian/Linux base and retain the earlier temporal discovery roots in parallel.
- Qualify every native directory in the built Debian package.

## 1.0.8

- Establish provisional system-wide temporal resource directories for discovered calendars and clocks.
- Qualify the built Debian package by verifying both temporal resource roots are present.
- Keep the installed path provisional while the resource representation and final hierarchy are still being proven.

## 1.0.7

- Resolve Calculator and System Monitor installers using their canonical package and command names.
- Derive installer filename prefix lengths from the canonical prefix.

## 1.0.6

- Give package identity one VERSION file and preserve immutable qualified release bytes.
- Share native launch and progress mechanics while retaining each product's installation policy.
- Report setup and exec failures before claiming background startup.
- Reject malformed or incomplete status records.
- Qualify process handoff and status failures with production-code fixtures; verify reproducible package bytes and approved artwork in CI.

## 1.0.5

- Use InfiltratorOS consistently in documentation, Plymouth metadata, package descriptions and provisioning windows.
- Document Boot, Filesystems, the planned Setup installer and the unchanged InfiltratorFS format name.
- Publish the infiltratoros Debian package and use infiltratoros consistently for installed paths and Plymouth files.
- Read the package version from VERSION and preserve immutable qualified release tags and assets.
